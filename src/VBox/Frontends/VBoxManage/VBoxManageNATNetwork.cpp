/* $Id: VBoxManageNATNetwork.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - Implementation of NAT Network command command.
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

#ifndef RT_OS_WINDOWS
# include <netinet/in.h>
#else
/* from  <ws2ipdef.h> */
# define INET6_ADDRSTRLEN 65
#endif

#define IPv6

#include <iprt/cdefs.h>
#include <iprt/cidr.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/net.h>
#include <iprt/getopt.h>
#include <iprt/ctype.h>

#include <VBox/log.h>

#include <algorithm>
#include <vector>
#include <iprt/sanitized/string>

#include "VBoxManage.h"
#include "VBoxPortForwardString.h"


DECLARE_TRANSLATION_CONTEXT(Nat);

using namespace com;

typedef enum
{
    OP_ADD = 1000,
    OP_REMOVE,
    OP_MODIFY,
    OP_START,
    OP_STOP
} OPCODE;

typedef struct PFNAME2DELETE
{
    char szName[PF_NAMELEN];
    bool fIPv6;
} PFNAME2DELETE, *PPFNAME2DELETE;

typedef std::vector<PFNAME2DELETE> VPF2DELETE;
typedef VPF2DELETE::const_iterator VPF2DELETEITERATOR;

typedef std::vector<PORTFORWARDRULE> VPF2ADD;
typedef VPF2ADD::const_iterator VPF2ADDITERATOR;

typedef std::vector<std::string>  LOOPBACK2DELETEADD;
typedef LOOPBACK2DELETEADD::iterator LOOPBACK2DELETEADDITERATOR;

static HRESULT printNATNetwork(const ComPtr<INATNetwork> &pNATNet,
                               bool fLong = true)
{
    HRESULT hrc;

    do
    {
        Bstr strVal;
        BOOL fVal;

        CHECK_ERROR_BREAK(pNATNet, COMGETTER(NetworkName)(strVal.asOutParam()));
        RTPrintf(Nat::tr("Name:         %ls\n"), strVal.raw());

        if (fLong)
        {
            /*
             * What does it even mean for a natnet to be disabled?
             * (rhetorical question).  Anyway, don't print it unless
             * asked for a complete dump.
             */
            CHECK_ERROR_BREAK(pNATNet, COMGETTER(Enabled)(&fVal));
            RTPrintf(Nat::tr("Enabled:      %s\n"),  fVal ? Nat::tr("Yes") : Nat::tr("No"));
        }

        CHECK_ERROR_BREAK(pNATNet, COMGETTER(Network)(strVal.asOutParam()));
        RTPrintf(Nat::tr("Network:      %ls\n"), strVal.raw());

        CHECK_ERROR_BREAK(pNATNet, COMGETTER(Gateway)(strVal.asOutParam()));
        RTPrintf(Nat::tr("Gateway:      %ls\n"), strVal.raw());

        CHECK_ERROR_BREAK(pNATNet, COMGETTER(NeedDhcpServer)(&fVal));
        RTPrintf(Nat::tr("DHCP Server:  %s\n"),  fVal ? Nat::tr("Yes") : Nat::tr("No"));

        CHECK_ERROR_BREAK(pNATNet, COMGETTER(IPv6Enabled)(&fVal));
        RTPrintf("IPv6:         %s\n",  fVal ? Nat::tr("Yes") : Nat::tr("No"));

        CHECK_ERROR_BREAK(pNATNet, COMGETTER(IPv6Prefix)(strVal.asOutParam()));
        RTPrintf(Nat::tr("IPv6 Prefix:  %ls\n"), strVal.raw());

        CHECK_ERROR_BREAK(pNATNet, COMGETTER(AdvertiseDefaultIPv6RouteEnabled)(&fVal));
        RTPrintf(Nat::tr("IPv6 Default: %s\n"),  fVal ? Nat::tr("Yes") : Nat::tr("No"));


        if (fLong)
        {
            com::SafeArray<BSTR> strs;

#define PRINT_STRING_ARRAY(title) do {                                  \
                if (strs.size() > 0)                                    \
                {                                                       \
                    RTPrintf(title);                                    \
                    for (size_t j = 0; j < strs.size(); ++j)            \
                        RTPrintf("        %s\n", Utf8Str(strs[j]).c_str()); \
                }                                                       \
            } while (0)

        CHECK_ERROR_BREAK(pNATNet, COMGETTER(PortForwardRules4)(ComSafeArrayAsOutParam(strs)));
        PRINT_STRING_ARRAY(Nat::tr("Port-forwarding (ipv4)\n"));
        strs.setNull();

        CHECK_ERROR(pNATNet, COMGETTER(PortForwardRules6)(ComSafeArrayAsOutParam(strs)));
        PRINT_STRING_ARRAY(Nat::tr("Port-forwarding (ipv6)\n"));
        strs.setNull();

        CHECK_ERROR(pNATNet, COMGETTER(LocalMappings)(ComSafeArrayAsOutParam(strs)));
        PRINT_STRING_ARRAY(Nat::tr("loopback mappings (ipv4)\n"));
        strs.setNull();

#undef PRINT_STRING_ARRAY
        }

        RTPrintf("\n");
    } while (0);

    return hrc;
}

static RTEXITCODE handleNATList(HandlerArg *a)
{
    HRESULT hrc;

    RTPrintf(Nat::tr("NAT Networks:\n\n"));

    const char *pszFilter = NULL;
    if (a->argc > 1)
        pszFilter = a->argv[1];

    size_t cFound = 0;

    com::SafeIfaceArray<INATNetwork> arrNetNets;
    CHECK_ERROR(a->virtualBox, COMGETTER(NATNetworks)(ComSafeArrayAsOutParam(arrNetNets)));
    for (size_t i = 0; i < arrNetNets.size(); ++i)
    {
        ComPtr<INATNetwork> pNATNet = arrNetNets[i];

        if (pszFilter)
        {
            Bstr strVal;
            CHECK_ERROR_BREAK(pNATNet, COMGETTER(NetworkName)(strVal.asOutParam()));

            Utf8Str strValUTF8(strVal);
            if (!RTStrSimplePatternMatch(pszFilter,  strValUTF8.c_str()))
                continue;
        }

        hrc = printNATNetwork(pNATNet);
        if (FAILED(hrc))
            break;

        cFound++;
    }

    if (SUCCEEDED(hrc))
        RTPrintf(Nat::tr("%zu %s found\n"), cFound, cFound == 1 ? Nat::tr("network") : Nat::tr("networks", "", cFound));

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE handleOp(HandlerArg *a, OPCODE enmCode)
{
    if (a->argc - 1 <= 1)
        return errorSyntax(Nat::tr("Not enough parameters"));

    const char *pNetName = NULL;
    const char *pPrefixIPv4 = NULL;
    const char *pPrefixIPv6 = NULL;
    int enable = -1;
    int dhcp = -1;
    int ipv6 = -1;
    int ipv6_default = -1;

    VPF2DELETE vPfName2Delete;
    VPF2ADD vPf2Add;

    LOOPBACK2DELETEADD vLoopback2Delete;
    LOOPBACK2DELETEADD vLoopback2Add;

    LONG loopback6Offset = 0; /* ignore me */

    enum
    {
        kNATNetworkIota = 1000,
        kNATNetwork_IPv6Default,
        kNATNetwork_IPv6Prefix,
    };

    static const RTGETOPTDEF g_aNATNetworkIPOptions[] =
    {
        { "--netname",          't',                            RTGETOPT_REQ_STRING  },
        { "--network",          'n',                            RTGETOPT_REQ_STRING  }, /* old name */
        { "--ipv4-prefix",      'n',                            RTGETOPT_REQ_STRING  }, /* new name */
        { "--dhcp",             'h',                            RTGETOPT_REQ_BOOL    },
        { "--ipv6",             '6',                            RTGETOPT_REQ_BOOL    }, /* old name */
        { "--ipv6-default",     kNATNetwork_IPv6Default,        RTGETOPT_REQ_BOOL    },
        { "--ipv6-enable",      '6',                            RTGETOPT_REQ_BOOL    }, /* new name */
        { "--ipv6-prefix",      kNATNetwork_IPv6Prefix,         RTGETOPT_REQ_STRING  },
        { "--enable",           'e',                            RTGETOPT_REQ_NOTHING },
        { "--disable",          'd',                            RTGETOPT_REQ_NOTHING },
        { "--port-forward-4",   'p',                            RTGETOPT_REQ_STRING  },
        { "--port-forward-6",   'P',                            RTGETOPT_REQ_STRING  },
        { "--loopback-4",       'l',                            RTGETOPT_REQ_STRING  },
        { "--loopback-6",       'L',                            RTGETOPT_REQ_STRING  },
    };

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, g_aNATNetworkIPOptions,
                 enmCode != OP_REMOVE ? RT_ELEMENTS(g_aNATNetworkIPOptions) : 4, /* we use only --netname and --ifname for remove*/
                 1, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 't':   // --netname
                if (pNetName)
                    return errorSyntax(Nat::tr("You can only specify --netname only once."));
                pNetName = ValueUnion.psz;
                break;

            case 'n':   // --network
                if (pPrefixIPv4)
                    return errorSyntax(Nat::tr("You can only specify --network only once."));
                pPrefixIPv4 = ValueUnion.psz;
                break;

            case 'e':   // --enable
                if (enable >= 0)
                    return errorSyntax(Nat::tr("You can specify either --enable or --disable once."));
                enable = 1;
                break;

            case 'd':   // --disable
                if (enable >= 0)
                    return errorSyntax(Nat::tr("You can specify either --enable or --disable once."));
                enable = 0;
                break;

            case 'h':
                if (dhcp != -1)
                    return errorSyntax(Nat::tr("You can specify --dhcp only once."));
                dhcp = ValueUnion.f;
                break;

            case '6':
                if (ipv6 != -1)
                    return errorSyntax(Nat::tr("You can specify --ipv6 only once."));
                ipv6 = ValueUnion.f;
                break;

            case kNATNetwork_IPv6Prefix:
                if (pPrefixIPv6)
                    return errorSyntax(Nat::tr("You can specify --ipv6-prefix only once."));
                pPrefixIPv6 = ValueUnion.psz;
                break;

            case kNATNetwork_IPv6Default: // XXX: uwe
                if (ipv6_default != -1)
                    return errorSyntax(Nat::tr("You can specify --ipv6-default only once."));
                ipv6_default = ValueUnion.f;
                break;

            case 'L': /* ipv6 loopback */
            case 'l': /* ipv4 loopback */
                if (RTStrCmp(ValueUnion.psz, "delete") == 0)
                {
                    /* deletion */
                    if (enmCode != OP_MODIFY)
                      errorSyntax(Nat::tr("loopback couldn't be deleted on modified\n"));
                    if (c == 'L')
                        loopback6Offset = -1;
                    else
                    {
                        int vrc;
                        RTGETOPTUNION Addr2Delete;
                        vrc = RTGetOptFetchValue(&GetState,
                                                 &Addr2Delete,
                                                 RTGETOPT_REQ_STRING);
                        if (RT_FAILURE(vrc))
                          return errorSyntax(Nat::tr("Not enough parаmeters\n"));

                        vLoopback2Delete.push_back(std::string(Addr2Delete.psz));
                    }
                }
                else
                {
                    /* addition */
                    if (c == 'L')
                        loopback6Offset = ValueUnion.u32;
                    else
                        vLoopback2Add.push_back(std::string(ValueUnion.psz));
                }
                break;

            case 'P': /* ipv6 portforwarding*/
            case 'p': /* ipv4 portforwarding */
            {
                if (RTStrCmp(ValueUnion.psz, "delete") != 0)
                {
                    /* addition */
                    /* netPfStrToPf will clean up the Pfr */
                    PORTFORWARDRULE Pfr;
                    int irc = netPfStrToPf(ValueUnion.psz, (c == 'P'), &Pfr);
                    if (RT_FAILURE(irc))
                        return errorSyntax(Nat::tr("Invalid port-forward rule %s\n"), ValueUnion.psz);

                    vPf2Add.push_back(Pfr);
                }
                else
                {
                    /* deletion */
                    if (enmCode != OP_MODIFY)
                        return errorSyntax(Nat::tr("Port-forward could be deleted on modify\n"));

                    RTGETOPTUNION NamePf2DeleteUnion;
                    int vrc = RTGetOptFetchValue(&GetState, &NamePf2DeleteUnion, RTGETOPT_REQ_STRING);
                    if (RT_FAILURE(vrc))
                        return errorSyntax(Nat::tr("Not enough parаmeters\n"));

                    if (strlen(NamePf2DeleteUnion.psz) > PF_NAMELEN)
                        return errorSyntax(Nat::tr("Port-forward rule name is too long\n"));

                    PFNAME2DELETE Name2Delete;
                    RT_ZERO(Name2Delete);
                    RTStrCopy(Name2Delete.szName, PF_NAMELEN, NamePf2DeleteUnion.psz);
                    Name2Delete.fIPv6 = (c == 'P');
                    vPfName2Delete.push_back(Name2Delete);
                }
                break;
            }

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!pNetName)
        return errorSyntax(Nat::tr("You need to specify the --netname option"));
    /* verification */
    switch (enmCode)
    {
        case OP_ADD:
            if (!pPrefixIPv4)
                return errorSyntax(Nat::tr("You need to specify the --network option"));
            break;
        case OP_MODIFY:
        case OP_REMOVE:
        case OP_START:
        case OP_STOP:
            break;
        default:
            AssertMsgFailedReturn((Nat::tr("Unknown operation (:%d)"), enmCode), RTEXITCODE_FAILURE);
    }

    HRESULT hrc;
    Bstr NetName;
    NetName = Bstr(pNetName);

    ComPtr<INATNetwork> net;
    hrc = a->virtualBox->FindNATNetworkByName(NetName.mutableRaw(), net.asOutParam());
    if (enmCode == OP_ADD)
    {
        if (SUCCEEDED(hrc))
            return errorArgument(Nat::tr("NATNetwork server already exists"));

        CHECK_ERROR(a->virtualBox, CreateNATNetwork(NetName.raw(), net.asOutParam()));
        if (FAILED(hrc))
            return errorArgument(Nat::tr("Failed to create the NAT network service"));
    }
    else if (FAILED(hrc))
        return errorArgument(Nat::tr("NATNetwork server does not exist"));

    switch (enmCode)
    {
        case OP_ADD:
        case OP_MODIFY:
        {
            if (pPrefixIPv4)
            {
                CHECK_ERROR(net, COMSETTER(Network)(Bstr(pPrefixIPv4).raw()));
                if (FAILED(hrc))
                    return errorArgument(Nat::tr("Failed to set configuration"));
            }
            if (dhcp >= 0)
            {
                CHECK_ERROR(net, COMSETTER(NeedDhcpServer) ((BOOL)dhcp));
                if (FAILED(hrc))
                    return errorArgument(Nat::tr("Failed to set configuration"));
            }

            /*
             * If we are asked to disable IPv6, do it early so that
             * the same command can also set IPv6 prefix to empty if
             * it so wishes.
             */
            if (ipv6 == 0)
            {
                CHECK_ERROR(net, COMSETTER(IPv6Enabled)(FALSE));
                if (FAILED(hrc))
                    return errorArgument(Nat::tr("Failed to set configuration"));
            }

            if (pPrefixIPv6)
            {
                CHECK_ERROR(net, COMSETTER(IPv6Prefix)(Bstr(pPrefixIPv6).raw()));
                if (FAILED(hrc))
                    return errorArgument(Nat::tr("Failed to set configuration"));
            }

            /*
             * If we are asked to enable IPv6, do it late, so that the
             * same command can also set IPv6 prefix.
             */
            if (ipv6 > 0)
            {
                CHECK_ERROR(net, COMSETTER(IPv6Enabled)(TRUE));
                if (FAILED(hrc))
                    return errorArgument(Nat::tr("Failed to set configuration"));
            }

            if (ipv6_default != -1)
            {
                BOOL fIPv6Default = RT_BOOL(ipv6_default);
                CHECK_ERROR(net, COMSETTER(AdvertiseDefaultIPv6RouteEnabled)(fIPv6Default));
                if (FAILED(hrc))
                    return errorArgument(Nat::tr("Failed to set configuration"));
            }

            if (!vPfName2Delete.empty())
            {
                VPF2DELETEITERATOR it;
                for (it = vPfName2Delete.begin(); it != vPfName2Delete.end(); ++it)
                {
                    CHECK_ERROR(net, RemovePortForwardRule((BOOL)(*it).fIPv6,
                                                           Bstr((*it).szName).raw()));
                    if (FAILED(hrc))
                        return errorArgument(Nat::tr("Failed to delete pf"));
                }
            }

            if (!vPf2Add.empty())
            {
                VPF2ADDITERATOR it;
                for (it = vPf2Add.begin(); it != vPf2Add.end(); ++it)
                {
                    NATProtocol_T proto = NATProtocol_TCP;
                    if ((*it).iPfrProto == IPPROTO_TCP)
                        proto = NATProtocol_TCP;
                    else if ((*it).iPfrProto == IPPROTO_UDP)
                        proto = NATProtocol_UDP;
                    else
                        continue; /* XXX: warning here. */

                    CHECK_ERROR(net, AddPortForwardRule((BOOL)(*it).fPfrIPv6,
                                                        Bstr((*it).szPfrName).raw(),
                                                        proto,
                                                        Bstr((*it).szPfrHostAddr).raw(),
                                                        (*it).u16PfrHostPort,
                                                        Bstr((*it).szPfrGuestAddr).raw(),
                                                        (*it).u16PfrGuestPort));
                    if (FAILED(hrc))
                        return errorArgument(Nat::tr("Failed to add pf"));
                }
            }

            if (loopback6Offset)
            {
                if (loopback6Offset == -1)
                    loopback6Offset = 0; /* deletion */

                CHECK_ERROR_RET(net, COMSETTER(LoopbackIp6)(loopback6Offset), RTEXITCODE_FAILURE);
            }

            /* addLocalMapping (hostid, offset) */
            if (!vLoopback2Add.empty())
            {
                /* we're expecting stings 127.0.0.1=5 */
                LOOPBACK2DELETEADDITERATOR it;
                for (it = vLoopback2Add.begin();
                     it != vLoopback2Add.end();
                     ++it)
                {
                    std::string address, strOffset;
                    size_t pos = it->find('=');
                    LONG lOffset = 0;
                    Bstr bstrAddress;

                    AssertReturn(pos != std::string::npos, errorArgument(Nat::tr("invalid loopback string")));

                    address = it->substr(0, pos);
                    strOffset = it->substr(pos + 1);

                    lOffset = RTStrToUInt32(strOffset.c_str());
                    AssertReturn(lOffset > 0, errorArgument(Nat::tr("invalid loopback string")));

                    bstrAddress = Bstr(address.c_str());

                    CHECK_ERROR_RET(net, AddLocalMapping(bstrAddress.raw(), lOffset), RTEXITCODE_FAILURE);
                }
            }

            if (!vLoopback2Delete.empty())
            {
                /* we're expecting stings 127.0.0.1 */
                LOOPBACK2DELETEADDITERATOR it;
                for (it = vLoopback2Add.begin();
                     it != vLoopback2Add.end();
                     ++it)
                {
                    Bstr bstrAddress;
                    bstrAddress = Bstr(it->c_str());

                    CHECK_ERROR_RET(net, AddLocalMapping(bstrAddress.raw(), 0), RTEXITCODE_FAILURE);
                }
            }

            if (enable >= 0)
            {
                CHECK_ERROR(net, COMSETTER(Enabled) ((BOOL)enable));
                if (FAILED(hrc))
                    return errorArgument(Nat::tr("Failed to set configuration"));
            }
            break;
        }
        case OP_REMOVE:
        {
            CHECK_ERROR(a->virtualBox, RemoveNATNetwork(net));
            if (FAILED(hrc))
                return errorArgument(Nat::tr("Failed to remove nat network"));
            break;
        }
        case OP_START:
        {
            CHECK_ERROR(net, Start());
            if (FAILED(hrc))
                return errorArgument(Nat::tr("Failed to start network"));
            break;
        }
        case OP_STOP:
        {
            CHECK_ERROR(net, Stop());
            if (FAILED(hrc))
                return errorArgument(Nat::tr("Failed to stop network"));
            break;
        }
        default:;
    }
    return RTEXITCODE_SUCCESS;
}


/*
 * VBoxManage natnetwork ...
 */
RTEXITCODE handleNATNetwork(HandlerArg *a)
{
    if (a->argc < 1)
        return errorSyntax(Nat::tr("Not enough parameters"));

    RTEXITCODE rcExit;
    if (strcmp(a->argv[0], "modify") == 0)
    {
        setCurrentSubcommand(HELP_SCOPE_NATNETWORK_MODIFY);
        rcExit = handleOp(a, OP_MODIFY);
    }
    else if (strcmp(a->argv[0], "add") == 0)
    {
        setCurrentSubcommand(HELP_SCOPE_NATNETWORK_ADD);
        rcExit = handleOp(a, OP_ADD);
    }
    else if (strcmp(a->argv[0], "remove") == 0)
    {
        setCurrentSubcommand(HELP_SCOPE_NATNETWORK_REMOVE);
        rcExit = handleOp(a, OP_REMOVE);
    }
    else if (strcmp(a->argv[0], "start") == 0)
    {
        setCurrentSubcommand(HELP_SCOPE_NATNETWORK_START);
        rcExit = handleOp(a, OP_START);
    }
    else if (strcmp(a->argv[0], "stop") == 0)
    {
        setCurrentSubcommand(HELP_SCOPE_NATNETWORK_STOP);
        rcExit = handleOp(a, OP_STOP);
    }
    else if (strcmp(a->argv[0], "list") == 0)
    {
        setCurrentSubcommand(HELP_SCOPE_NATNETWORK_LIST);
        rcExit = handleNATList(a);
    }
    else
        rcExit = errorSyntax(Nat::tr("Invalid parameter '%s'"), a->argv[0]);
    return rcExit;
}


/*
 * VBoxManage list natnetworks ...
 */
RTEXITCODE listNATNetworks(bool fLong, bool fSorted,
                           const ComPtr<IVirtualBox> &pVirtualBox)
{
    HRESULT hrc;

    com::SafeIfaceArray<INATNetwork> aNets;
    CHECK_ERROR_RET(pVirtualBox,
        COMGETTER(NATNetworks)(ComSafeArrayAsOutParam(aNets)),
            RTEXITCODE_FAILURE);

    const size_t cNets = aNets.size();
    if (cNets == 0)
        return RTEXITCODE_SUCCESS;

    /*
     * Sort the list if necessary.  The sort is indirect via an
     * intermediate array of indexes.
     */
    std::vector<size_t> vIndexes(cNets);
    for (size_t i = 0; i < cNets; ++i)
        vIndexes[i] = i;

    if (fSorted)
    {
        std::vector<com::Bstr> vBstrNames(cNets);
        for (size_t i = 0; i < cNets; ++i)
        {
            CHECK_ERROR_RET(aNets[i],
                COMGETTER(NetworkName)(vBstrNames[i].asOutParam()),
                    RTEXITCODE_FAILURE);
        }

        struct SortBy {
            const std::vector<com::Bstr> &ks;
            SortBy(const std::vector<com::Bstr> &aKeys) : ks(aKeys) {}
            bool operator() (size_t l, size_t r) { return ks[l] < ks[r]; }
        };

        std::sort(vIndexes.begin(), vIndexes.end(),
                  SortBy(vBstrNames));
    }

    for (size_t i = 0; i < cNets; ++i)
    {
        printNATNetwork(aNets[vIndexes[i]], fLong);
    }

    return RTEXITCODE_SUCCESS;
}
