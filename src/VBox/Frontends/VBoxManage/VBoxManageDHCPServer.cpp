/* $Id: VBoxManageDHCPServer.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxManage - Implementation of dhcpserver command.
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

#include <vector>
#include <map>

using namespace com;

DECLARE_TRANSLATION_CONTEXT(DHCPServer);


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define DHCPD_CMD_COMMON_OPT_NETWORK          999 /**< The --network / --netname option number. */
#define DHCPD_CMD_COMMON_OPT_INTERFACE        998 /**< The --interface / --ifname option number. */
/** Common option definitions. */
#define DHCPD_CMD_COMMON_OPTION_DEFS() \
        { "--network",              DHCPD_CMD_COMMON_OPT_NETWORK,       RTGETOPT_REQ_STRING  }, \
        { "--netname",              DHCPD_CMD_COMMON_OPT_NETWORK,       RTGETOPT_REQ_STRING  }, /* legacy */ \
        { "--interface",            DHCPD_CMD_COMMON_OPT_INTERFACE,     RTGETOPT_REQ_STRING  }, \
        { "--ifname",               DHCPD_CMD_COMMON_OPT_INTERFACE,     RTGETOPT_REQ_STRING  }  /* legacy */

/** Handles common options in the typical option parsing switch. */
#define DHCPD_CMD_COMMON_OPTION_CASES(a_pCtx, a_ch, a_pValueUnion) \
        case DHCPD_CMD_COMMON_OPT_NETWORK: \
            if ((a_pCtx)->pszInterface != NULL) \
                return errorSyntax(DHCPServer::tr("Either --network or --interface, not both")); \
            (a_pCtx)->pszNetwork = ValueUnion.psz; \
            break; \
        case DHCPD_CMD_COMMON_OPT_INTERFACE: \
            if ((a_pCtx)->pszNetwork != NULL) \
                return errorSyntax(DHCPServer::tr("Either --interface or --network, not both")); \
            (a_pCtx)->pszInterface = ValueUnion.psz; \
            break


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a dhcpserver command context. */
typedef struct DHCPDCMDCTX *PDHCPDCMDCTX;

/**
 * Definition of a dhcpserver command, with handler and various flags.
 */
typedef struct DHCPDCMDDEF
{
    /** The command name. */
    const char *pszName;

    /**
     * Actual command handler callback.
     *
     * @param   pCtx            Pointer to command context to use.
     */
    DECLR3CALLBACKMEMBER(RTEXITCODE, pfnHandler, (PDHCPDCMDCTX pCtx, int argc, char **argv));

    /** The sub-command scope flags. */
    uint64_t    fSubcommandScope;
} DHCPDCMDDEF;
/** Pointer to a const dhcpserver command definition. */
typedef DHCPDCMDDEF const *PCDHCPDCMDDEF;

/**
 * dhcpserver command context (mainly for carrying common options and such).
 */
typedef struct DHCPDCMDCTX
{
    /** The handler arguments from the main() function. */
    HandlerArg     *pArg;
    /** Pointer to the command definition. */
    PCDHCPDCMDDEF   pCmdDef;
    /** The network name. */
    const char     *pszNetwork;
    /** The (trunk) interface name. */
    const char     *pszInterface;
} DHCPDCMDCTX;

typedef std::pair<DHCPOption_T, Utf8Str> DhcpOptSpec;
typedef std::vector<DhcpOptSpec> DhcpOpts;
typedef DhcpOpts::iterator DhcpOptIterator;

typedef std::vector<DHCPOption_T> DhcpOptIds;
typedef DhcpOptIds::iterator DhcpOptIdIterator;

struct VmNameSlotKey
{
    const Utf8Str VmName;
    uint8_t u8Slot;

    VmNameSlotKey(const Utf8Str &aVmName, uint8_t aSlot)
        : VmName(aVmName)
        , u8Slot(aSlot)
    {}

    bool operator<(const VmNameSlotKey& that) const
    {
        if (VmName == that.VmName)
            return u8Slot < that.u8Slot;
        return VmName < that.VmName;
    }
};

typedef std::map<VmNameSlotKey, DhcpOpts> VmSlot2OptionsM;
typedef VmSlot2OptionsM::iterator VmSlot2OptionsIterator;
typedef VmSlot2OptionsM::value_type VmSlot2OptionsPair;

typedef std::map<VmNameSlotKey, DhcpOptIds> VmSlot2OptionIdsM;
typedef VmSlot2OptionIdsM::iterator VmSlot2OptionIdsIterator;



/**
 * Helper that find the DHCP server instance.
 *
 * @returns The DHCP server instance. NULL if failed (complaining done).
 * @param   pCtx                The DHCP server command context.
 */
static ComPtr<IDHCPServer> dhcpdFindServer(PDHCPDCMDCTX pCtx)
{
    ComPtr<IDHCPServer> ptrRet;
    if (pCtx->pszNetwork || pCtx->pszInterface)
    {
        Assert(pCtx->pszNetwork == NULL || pCtx->pszInterface == NULL);

        /*
         * We need a network name to find the DHCP server.  So, if interface is
         * given we have to look it up.
         */
        HRESULT hrc;
        Bstr bstrNetName(pCtx->pszNetwork);
        if (!pCtx->pszNetwork)
        {
            ComPtr<IHost> ptrIHost;
            CHECK_ERROR2_RET(hrc, pCtx->pArg->virtualBox, COMGETTER(Host)(ptrIHost.asOutParam()), ptrRet);

            Bstr bstrInterface(pCtx->pszInterface);
            ComPtr<IHostNetworkInterface> ptrIHostIf;
            CHECK_ERROR2(hrc, ptrIHost, FindHostNetworkInterfaceByName(bstrInterface.raw(), ptrIHostIf.asOutParam()));
            if (FAILED(hrc))
            {
                errorArgument(DHCPServer::tr("Failed to locate host-only interface '%s'"), pCtx->pszInterface);
                return ptrRet;
            }

            CHECK_ERROR2_RET(hrc, ptrIHostIf, COMGETTER(NetworkName)(bstrNetName.asOutParam()), ptrRet);
        }

        /*
         * Now, try locate the server
         */
        hrc = pCtx->pArg->virtualBox->FindDHCPServerByNetworkName(bstrNetName.raw(), ptrRet.asOutParam());
        if (SUCCEEDED(hrc))
            return ptrRet;
        if (pCtx->pszNetwork)
            errorArgument(DHCPServer::tr("Failed to find DHCP server for network '%s'"), pCtx->pszNetwork);
        else
            errorArgument(DHCPServer::tr("Failed to find DHCP server for host-only interface '%s' (network '%ls')"),
                          pCtx->pszInterface, bstrNetName.raw());
    }
    else
        errorSyntax(DHCPServer::tr("You need to specify either --network or --interface to identify the DHCP server"));
    return ptrRet;
}


/**
 * Helper class for dhcpdHandleAddAndModify
 */
class DHCPCmdScope
{
    DHCPConfigScope_T               m_enmScope;
    const char                     *m_pszName;
    uint8_t                         m_uSlot;
    ComPtr<IDHCPConfig>             m_ptrConfig;
    ComPtr<IDHCPGlobalConfig>       m_ptrGlobalConfig;
    ComPtr<IDHCPGroupConfig>        m_ptrGroupConfig;
    ComPtr<IDHCPIndividualConfig>   m_ptrIndividualConfig;

public:
    DHCPCmdScope()
        : m_enmScope(DHCPConfigScope_Global)
        , m_pszName(NULL)
        , m_uSlot(0)
    {
    }

    void setGlobal()
    {
        m_enmScope = DHCPConfigScope_Global;
        m_pszName  = NULL;
        m_uSlot    = 0;
        resetPointers();
    }

    void setGroup(const char *pszGroup)
    {
        m_enmScope = DHCPConfigScope_Group;
        m_pszName  = pszGroup;
        m_uSlot    = 0;
        resetPointers();
    }

    void setMachineNIC(const char *pszMachine)
    {
        m_enmScope = DHCPConfigScope_MachineNIC;
        m_pszName  = pszMachine;
        m_uSlot    = 0;
        resetPointers();
    }

    void setMachineSlot(uint8_t uSlot)
    {
        Assert(m_enmScope == DHCPConfigScope_MachineNIC);
        m_uSlot    = uSlot;
        resetPointers();
    }

    void setMACAddress(const char *pszMACAddress)
    {
        m_enmScope = DHCPConfigScope_MAC;
        m_pszName  = pszMACAddress;
        m_uSlot    = 0;
        resetPointers();
    }

    ComPtr<IDHCPConfig> &getConfig(ComPtr<IDHCPServer> const &ptrDHCPServer)
    {
        if (m_ptrConfig.isNull())
        {
            CHECK_ERROR2I_STMT(ptrDHCPServer, GetConfig(m_enmScope, Bstr(m_pszName).raw(), m_uSlot, TRUE /*mayAdd*/,
                                                        m_ptrConfig.asOutParam()), m_ptrConfig.setNull());
        }
        return m_ptrConfig;
    }

    ComPtr<IDHCPIndividualConfig> &getIndividual(ComPtr<IDHCPServer> const &ptrDHCPServer)
    {
        getConfig(ptrDHCPServer);
        if (m_ptrIndividualConfig.isNull() && m_ptrConfig.isNotNull())
        {
            HRESULT hrc = m_ptrConfig.queryInterfaceTo(m_ptrIndividualConfig.asOutParam());
            if (FAILED(hrc))
            {
                com::GlueHandleComError(m_ptrConfig, "queryInterface", hrc, __FILE__, __LINE__);
                m_ptrIndividualConfig.setNull();
            }
        }
        return m_ptrIndividualConfig;
    }

    ComPtr<IDHCPGroupConfig> &getGroup(ComPtr<IDHCPServer> const &ptrDHCPServer)
    {
        getConfig(ptrDHCPServer);
        if (m_ptrGroupConfig.isNull() && m_ptrConfig.isNotNull())
        {
            HRESULT hrc = m_ptrConfig.queryInterfaceTo(m_ptrGroupConfig.asOutParam());
            if (FAILED(hrc))
            {
                com::GlueHandleComError(m_ptrConfig, "queryInterface", hrc, __FILE__, __LINE__);
                m_ptrGroupConfig.setNull();
            }
        }
        return m_ptrGroupConfig;
    }

    DHCPConfigScope_T getScope() const { return m_enmScope; }

private:
    void resetPointers()
    {
        m_ptrConfig.setNull();
        m_ptrGlobalConfig.setNull();
        m_ptrIndividualConfig.setNull();
        m_ptrGroupConfig.setNull();
    }
};

enum
{
    DHCP_ADDMOD = 1000,
    DHCP_ADDMOD_FORCE_OPTION,
    DHCP_ADDMOD_UNFORCE_OPTION,
    DHCP_ADDMOD_SUPPRESS_OPTION,
    DHCP_ADDMOD_UNSUPPRESS_OPTION,
    DHCP_ADDMOD_ZAP_OPTIONS,
    DHCP_ADDMOD_INCL_MAC,
    DHCP_ADDMOD_EXCL_MAC,
    DHCP_ADDMOD_DEL_MAC,
    DHCP_ADDMOD_INCL_MAC_WILD,
    DHCP_ADDMOD_EXCL_MAC_WILD,
    DHCP_ADDMOD_DEL_MAC_WILD,
    DHCP_ADDMOD_INCL_VENDOR,
    DHCP_ADDMOD_EXCL_VENDOR,
    DHCP_ADDMOD_DEL_VENDOR,
    DHCP_ADDMOD_INCL_VENDOR_WILD,
    DHCP_ADDMOD_EXCL_VENDOR_WILD,
    DHCP_ADDMOD_DEL_VENDOR_WILD,
    DHCP_ADDMOD_INCL_USER,
    DHCP_ADDMOD_EXCL_USER,
    DHCP_ADDMOD_DEL_USER,
    DHCP_ADDMOD_INCL_USER_WILD,
    DHCP_ADDMOD_EXCL_USER_WILD,
    DHCP_ADDMOD_DEL_USER_WILD,
    DHCP_ADDMOD_ZAP_CONDITIONS
};

/**
 * Handles the 'add' and 'modify' subcommands.
 */
static DECLCALLBACK(RTEXITCODE) dhcpdHandleAddAndModify(PDHCPDCMDCTX pCtx, int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        DHCPD_CMD_COMMON_OPTION_DEFS(),
        { "--server-ip",        'a',                            RTGETOPT_REQ_STRING  },
        { "--ip",               'a',                            RTGETOPT_REQ_STRING  },    // deprecated
        { "-ip",                'a',                            RTGETOPT_REQ_STRING  },    // deprecated
        { "--netmask",          'm',                            RTGETOPT_REQ_STRING  },
        { "-netmask",           'm',                            RTGETOPT_REQ_STRING  },    // deprecated
        { "--lower-ip",         'l',                            RTGETOPT_REQ_STRING  },
        { "--lowerip",          'l',                            RTGETOPT_REQ_STRING  },
        { "-lowerip",           'l',                            RTGETOPT_REQ_STRING  },    // deprecated
        { "--upper-ip",         'u',                            RTGETOPT_REQ_STRING  },
        { "--upperip",          'u',                            RTGETOPT_REQ_STRING  },
        { "-upperip",           'u',                            RTGETOPT_REQ_STRING  },    // deprecated
        { "--enable",           'e',                            RTGETOPT_REQ_NOTHING },
        { "-enable",            'e',                            RTGETOPT_REQ_NOTHING },    // deprecated
        { "--disable",          'd',                            RTGETOPT_REQ_NOTHING },
        { "-disable",           'd',                            RTGETOPT_REQ_NOTHING },    // deprecated
        { "--global",           'g',                            RTGETOPT_REQ_NOTHING },
        { "--group",            'G',                            RTGETOPT_REQ_STRING  },
        { "--mac-address",      'E',                            RTGETOPT_REQ_MACADDR },
        { "--vm",               'M',                            RTGETOPT_REQ_STRING  },
        { "--nic",              'n',                            RTGETOPT_REQ_UINT8   },
        { "--set-opt",          's',                            RTGETOPT_REQ_UINT8   },
        { "--set-opt-hex",      'x',                            RTGETOPT_REQ_UINT8   },
        { "--del-opt",          'D',                            RTGETOPT_REQ_UINT8   },
        { "--force-opt",        DHCP_ADDMOD_FORCE_OPTION,       RTGETOPT_REQ_UINT8   },
        { "--unforce-opt",      DHCP_ADDMOD_UNFORCE_OPTION,     RTGETOPT_REQ_UINT8   },
        { "--suppress-opt",     DHCP_ADDMOD_SUPPRESS_OPTION,    RTGETOPT_REQ_UINT8   },
        { "--unsuppress-opt",   DHCP_ADDMOD_UNSUPPRESS_OPTION,  RTGETOPT_REQ_UINT8   },
        { "--zap-options",      DHCP_ADDMOD_ZAP_OPTIONS,        RTGETOPT_REQ_NOTHING },
        { "--min-lease-time",   'q' ,                           RTGETOPT_REQ_UINT32  },
        { "--default-lease-time", 'L' ,                         RTGETOPT_REQ_UINT32  },
        { "--max-lease-time",   'Q' ,                           RTGETOPT_REQ_UINT32  },
        { "--remove-config",    'R',                            RTGETOPT_REQ_NOTHING },
        { "--fixed-address",    'f',                            RTGETOPT_REQ_STRING  },
        /* group conditions: */
        { "--incl-mac",         DHCP_ADDMOD_INCL_MAC,           RTGETOPT_REQ_STRING  },
        { "--excl-mac",         DHCP_ADDMOD_EXCL_MAC,           RTGETOPT_REQ_STRING  },
        { "--del-mac",          DHCP_ADDMOD_DEL_MAC,            RTGETOPT_REQ_STRING  },
        { "--incl-mac-wild",    DHCP_ADDMOD_INCL_MAC_WILD,      RTGETOPT_REQ_STRING  },
        { "--excl-mac-wild",    DHCP_ADDMOD_EXCL_MAC_WILD,      RTGETOPT_REQ_STRING  },
        { "--del-mac-wild",     DHCP_ADDMOD_DEL_MAC_WILD,       RTGETOPT_REQ_STRING  },
        { "--incl-vendor",      DHCP_ADDMOD_INCL_VENDOR,        RTGETOPT_REQ_STRING  },
        { "--excl-vendor",      DHCP_ADDMOD_EXCL_VENDOR,        RTGETOPT_REQ_STRING  },
        { "--del-vendor",       DHCP_ADDMOD_DEL_VENDOR,         RTGETOPT_REQ_STRING  },
        { "--incl-vendor-wild", DHCP_ADDMOD_INCL_VENDOR_WILD,   RTGETOPT_REQ_STRING  },
        { "--excl-vendor-wild", DHCP_ADDMOD_EXCL_VENDOR_WILD,   RTGETOPT_REQ_STRING  },
        { "--del-vendor-wild",  DHCP_ADDMOD_DEL_VENDOR_WILD,    RTGETOPT_REQ_STRING  },
        { "--incl-user",        DHCP_ADDMOD_INCL_USER,          RTGETOPT_REQ_STRING  },
        { "--excl-user",        DHCP_ADDMOD_EXCL_USER,          RTGETOPT_REQ_STRING  },
        { "--del-user",         DHCP_ADDMOD_DEL_USER,           RTGETOPT_REQ_STRING  },
        { "--incl-user-wild",   DHCP_ADDMOD_INCL_USER_WILD,     RTGETOPT_REQ_STRING  },
        { "--excl-user-wild",   DHCP_ADDMOD_EXCL_USER_WILD,     RTGETOPT_REQ_STRING  },
        { "--del-user-wild",    DHCP_ADDMOD_DEL_USER_WILD,      RTGETOPT_REQ_STRING  },
        { "--zap-conditions",   DHCP_ADDMOD_ZAP_CONDITIONS,     RTGETOPT_REQ_NOTHING },
        /* obsolete, to be removed: */
        { "--id",               'i', RTGETOPT_REQ_UINT8   },    // obsolete, backwards compatibility only.
        { "--value",            'p', RTGETOPT_REQ_STRING  },    // obsolete, backwards compatibility only.
        { "--remove",           'r', RTGETOPT_REQ_NOTHING },    // obsolete, backwards compatibility only.
        { "--options",          'o', RTGETOPT_REQ_NOTHING },    // obsolete legacy, ignored

    };

    /*
     * Parse the arguments in two passes:
     *
     *  1. Validate the command line and establish the IDHCPServer settings.
     *  2. Execute the various IDHCPConfig settings changes.
     *
     * This is considered simpler than duplicating the command line instructions
     * into elaborate structures and executing these.
     */
    RTEXITCODE          rcExit = RTEXITCODE_SUCCESS;
    ComPtr<IDHCPServer> ptrDHCPServer;
    for (size_t iPass = 0; iPass < 2; iPass++)
    {
        const char         *pszServerIp         = NULL;
        const char         *pszNetmask          = NULL;
        const char         *pszLowerIp          = NULL;
        const char         *pszUpperIp          = NULL;
        int                 fEnabled            = -1;

        DHCPCmdScope        Scope;
        char                szMACAddress[32];

        bool                fNeedValueOrRemove  = false; /* Only used with --id; remove in 6.1+ */
        uint8_t             u8OptId             = 0;     /* Only used too keep --id for following --value/--remove. remove in 6.1+ */

        RTGETOPTSTATE GetState;
        int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
        AssertRCReturn(vrc, RTEXITCODE_FAILURE);

        RTGETOPTUNION ValueUnion;
        while ((vrc = RTGetOpt(&GetState, &ValueUnion)))
        {
            switch (vrc)
            {
                DHCPD_CMD_COMMON_OPTION_CASES(pCtx, vrc, &ValueUnion);
                case 'a':   // --server-ip
                    pszServerIp = ValueUnion.psz;
                    break;
                case 'm':   // --netmask
                    pszNetmask = ValueUnion.psz;
                    break;
                case 'l':   // --lower-ip
                    pszLowerIp = ValueUnion.psz;
                    break;
                case 'u':   // --upper-ip
                    pszUpperIp = ValueUnion.psz;
                    break;
                case 'e':   // --enable
                    fEnabled = 1;
                    break;
                case 'd':   // --disable
                    fEnabled = 0;
                    break;

                /*
                 * Configuration selection:
                 */
                case 'g':   // --global     Sets the option scope to 'global'.
                    if (fNeedValueOrRemove)
                        return errorSyntax(DHCPServer::tr("Incomplete option sequence preseeding '--global'"));
                    Scope.setGlobal();
                    break;

                case 'G':   // --group
                    if (fNeedValueOrRemove)
                        return errorSyntax(DHCPServer::tr("Incomplete option sequence preseeding '--group'"));
                    if (!*ValueUnion.psz)
                        return errorSyntax(DHCPServer::tr("Group name cannot be empty"));
                    Scope.setGroup(ValueUnion.psz);
                    break;

                case 'E':   // --mac-address
                    if (fNeedValueOrRemove)
                        return errorSyntax(DHCPServer::tr("Incomplete option sequence preseeding '--mac-address'"));
                    RTStrPrintf(szMACAddress, sizeof(szMACAddress), "%RTmac", &ValueUnion.MacAddr);
                    Scope.setMACAddress(szMACAddress);
                    break;

                case 'M':   // --vm         Sets the option scope to ValueUnion.psz + 0.
                    if (fNeedValueOrRemove)
                        return errorSyntax(DHCPServer::tr("Incomplete option sequence preseeding '--vm'"));
                    Scope.setMachineNIC(ValueUnion.psz);
                    break;

                case 'n':   // --nic        Sets the option scope to pszVmName + (ValueUnion.u8 - 1).
                    if (Scope.getScope() != DHCPConfigScope_MachineNIC)
                        return errorSyntax(DHCPServer::tr("--nic option requires a --vm preceding selecting the VM it should apply to"));
                    if (fNeedValueOrRemove)
                        return errorSyntax(DHCPServer::tr("Incomplete option sequence preseeding '--nic=%u"), ValueUnion.u8);
                    if (ValueUnion.u8 < 1)
                        return errorSyntax(DHCPServer::tr("invalid NIC number: %u"), ValueUnion.u8);
                    Scope.setMachineSlot(ValueUnion.u8 - 1);
                    break;

                /*
                 * Modify configuration:
                 */
                case 's':   // --set-opt num stringvalue
                {
                    uint8_t const idAddOpt = ValueUnion.u8;
                    vrc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_FAILURE(vrc))
                        return errorFetchValue(1, "--set-opt", vrc, &ValueUnion);
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, SetOption((DHCPOption_T)idAddOpt, DHCPOptionEncoding_Normal,
                                                                Bstr(ValueUnion.psz).raw()), rcExit = RTEXITCODE_FAILURE);
                    }
                    break;
                }

                case 'x':   // --set-opt-hex num hex-string
                {
                    uint8_t const idAddOpt = ValueUnion.u8;
                    vrc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_FAILURE(vrc))
                        return errorFetchValue(1, "--set-opt-hex", vrc, &ValueUnion);
                    uint8_t abBuf[256];
                    size_t cbRet;
                    vrc = RTStrConvertHexBytesEx(ValueUnion.psz, abBuf, sizeof(abBuf), RTSTRCONVERTHEXBYTES_F_SEP_COLON,
                                                 NULL, &cbRet);
                    if (RT_FAILURE(vrc))
                        return errorArgument(DHCPServer::tr("Malformed hex string given to --set-opt-hex %u: %s\n"),
                                             idAddOpt, ValueUnion.psz);
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, SetOption((DHCPOption_T)idAddOpt, DHCPOptionEncoding_Hex,
                                                                Bstr(ValueUnion.psz).raw()), rcExit = RTEXITCODE_FAILURE);
                    }
                    break;
                }

                case 'D':   // --del-opt num
                    if (pCtx->pCmdDef->fSubcommandScope == HELP_SCOPE_DHCPSERVER_ADD)
                        return errorSyntax(DHCPServer::tr("--del-opt does not apply to the 'add' subcommand"));
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, RemoveOption((DHCPOption_T)ValueUnion.u8), rcExit = RTEXITCODE_FAILURE);
                    }
                    break;

                case DHCP_ADDMOD_UNFORCE_OPTION:    // --unforce-opt
                    if (pCtx->pCmdDef->fSubcommandScope == HELP_SCOPE_DHCPSERVER_ADD)
                        return errorSyntax(DHCPServer::tr("--unforce-opt does not apply to the 'add' subcommand"));
                    RT_FALL_THROUGH();
                case DHCP_ADDMOD_UNSUPPRESS_OPTION: // --unsuppress-opt
                    if (pCtx->pCmdDef->fSubcommandScope == HELP_SCOPE_DHCPSERVER_ADD)
                        return errorSyntax(DHCPServer::tr("--unsuppress-opt does not apply to the 'add' subcommand"));
                    RT_FALL_THROUGH();
                case DHCP_ADDMOD_FORCE_OPTION:      // --force-opt
                case DHCP_ADDMOD_SUPPRESS_OPTION:   // --suppress-opt
                    if (iPass == 1)
                    {
                        DHCPOption_T const enmOption = (DHCPOption_T)ValueUnion.u8;
                        bool const fForced = vrc == DHCP_ADDMOD_FORCE_OPTION || vrc == DHCP_ADDMOD_UNFORCE_OPTION;

                        /* Get the current option list: */
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        com::SafeArray<DHCPOption_T> Options;
                        if (fForced)
                            CHECK_ERROR2I_STMT(ptrConfig, COMGETTER(ForcedOptions)(ComSafeArrayAsOutParam(Options)),
                                               rcExit = RTEXITCODE_FAILURE; break);
                        else
                            CHECK_ERROR2I_STMT(ptrConfig, COMGETTER(SuppressedOptions)(ComSafeArrayAsOutParam(Options)),
                                               rcExit = RTEXITCODE_FAILURE; break);
                        if (vrc == DHCP_ADDMOD_FORCE_OPTION || vrc == DHCP_ADDMOD_SUPPRESS_OPTION)
                        {
                            /* Add if not present. */
                            size_t iSrc;
                            for (iSrc = 0; iSrc < Options.size(); iSrc++)
                                if (Options[iSrc] == enmOption)
                                    break;
                            if (iSrc < Options.size())
                                break; /* already present */
                            Options.push_back(enmOption);
                        }
                        else
                        {
                            /* Remove */
                            size_t iDst = 0;
                            for (size_t iSrc = 0; iSrc < Options.size(); iSrc++)
                            {
                                DHCPOption_T enmCurOpt = Options[iSrc];
                                if (enmCurOpt != enmOption)
                                    Options[iDst++] = enmCurOpt;
                            }
                            if (iDst == Options.size())
                                break; /* Not found. */
                            Options.resize(iDst);
                        }

                        /* Update the option list: */
                        if (fForced)
                            CHECK_ERROR2I_STMT(ptrConfig, COMSETTER(ForcedOptions)(ComSafeArrayAsInParam(Options)),
                                               rcExit = RTEXITCODE_FAILURE);
                        else
                            CHECK_ERROR2I_STMT(ptrConfig, COMSETTER(SuppressedOptions)(ComSafeArrayAsInParam(Options)),
                                               rcExit = RTEXITCODE_FAILURE);
                    }
                    break;

                case DHCP_ADDMOD_ZAP_OPTIONS:
                    if (pCtx->pCmdDef->fSubcommandScope == HELP_SCOPE_DHCPSERVER_ADD)
                        return errorSyntax(DHCPServer::tr("--zap-options does not apply to the 'add' subcommand"));
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, RemoveAllOptions(), rcExit = RTEXITCODE_FAILURE);
                    }
                    break;

                case 'q':   // --min-lease-time
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, COMSETTER(MinLeaseTime)(ValueUnion.u32), rcExit = RTEXITCODE_FAILURE);
                    }
                    break;

                case 'L':   // --default-lease-time
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, COMSETTER(DefaultLeaseTime)(ValueUnion.u32), rcExit = RTEXITCODE_FAILURE);
                    }
                    break;

                case 'Q':   // --max-lease-time
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, COMSETTER(MaxLeaseTime)(ValueUnion.u32), rcExit = RTEXITCODE_FAILURE);
                    }
                    break;

                case 'R':   // --remove-config
                    if (pCtx->pCmdDef->fSubcommandScope == HELP_SCOPE_DHCPSERVER_ADD)
                        return errorSyntax(DHCPServer::tr("--remove-config does not apply to the 'add' subcommand"));
                    if (Scope.getScope() == DHCPConfigScope_Global)
                        return errorSyntax(DHCPServer::tr("--remove-config cannot be applied to the global config"));
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, Remove(), rcExit = RTEXITCODE_FAILURE);
                    }
                    Scope.setGlobal();
                    break;

                case 'f':   // --fixed-address
                    if (Scope.getScope() != DHCPConfigScope_MachineNIC && Scope.getScope() != DHCPConfigScope_MAC)
                        return errorSyntax(DHCPServer::tr("--fixed-address can only be applied to a VM NIC or an MAC address"));
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPIndividualConfig> &ptrIndividualConfig = Scope.getIndividual(ptrDHCPServer);
                        if (ptrIndividualConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrIndividualConfig, COMSETTER(FixedAddress)(Bstr(ValueUnion.psz).raw()),
                                           rcExit = RTEXITCODE_FAILURE);
                    }
                    break;

                /*
                 * Group conditions:
                 */
                case DHCP_ADDMOD_INCL_MAC:
                case DHCP_ADDMOD_EXCL_MAC:
                case DHCP_ADDMOD_DEL_MAC:
                case DHCP_ADDMOD_INCL_MAC_WILD:
                case DHCP_ADDMOD_EXCL_MAC_WILD:
                case DHCP_ADDMOD_DEL_MAC_WILD:
                case DHCP_ADDMOD_INCL_VENDOR:
                case DHCP_ADDMOD_EXCL_VENDOR:
                case DHCP_ADDMOD_DEL_VENDOR:
                case DHCP_ADDMOD_INCL_VENDOR_WILD:
                case DHCP_ADDMOD_EXCL_VENDOR_WILD:
                case DHCP_ADDMOD_DEL_VENDOR_WILD:
                case DHCP_ADDMOD_INCL_USER:
                case DHCP_ADDMOD_EXCL_USER:
                case DHCP_ADDMOD_DEL_USER:
                case DHCP_ADDMOD_INCL_USER_WILD:
                case DHCP_ADDMOD_EXCL_USER_WILD:
                case DHCP_ADDMOD_DEL_USER_WILD:
                {
                    if (Scope.getScope() != DHCPConfigScope_Group)
                        return errorSyntax(DHCPServer::tr("A group must be selected to perform condition alterations."));
                    if (!*ValueUnion.psz)
                        return errorSyntax(DHCPServer::tr("Condition value cannot be empty")); /* or can it? */
                    if (iPass != 1)
                        break;

                    DHCPGroupConditionType_T enmType;
                    switch (vrc)
                    {
                        case DHCP_ADDMOD_INCL_MAC: case DHCP_ADDMOD_EXCL_MAC: case DHCP_ADDMOD_DEL_MAC:
                            enmType = DHCPGroupConditionType_MAC;
                            break;
                        case DHCP_ADDMOD_INCL_MAC_WILD: case DHCP_ADDMOD_EXCL_MAC_WILD: case DHCP_ADDMOD_DEL_MAC_WILD:
                            enmType = DHCPGroupConditionType_MACWildcard;
                            break;
                        case DHCP_ADDMOD_INCL_VENDOR: case DHCP_ADDMOD_EXCL_VENDOR: case DHCP_ADDMOD_DEL_VENDOR:
                            enmType = DHCPGroupConditionType_vendorClassID;
                            break;
                        case DHCP_ADDMOD_INCL_VENDOR_WILD: case DHCP_ADDMOD_EXCL_VENDOR_WILD: case DHCP_ADDMOD_DEL_VENDOR_WILD:
                            enmType = DHCPGroupConditionType_vendorClassIDWildcard;
                            break;
                        case DHCP_ADDMOD_INCL_USER: case DHCP_ADDMOD_EXCL_USER: case DHCP_ADDMOD_DEL_USER:
                            enmType = DHCPGroupConditionType_userClassID;
                            break;
                        case DHCP_ADDMOD_INCL_USER_WILD: case DHCP_ADDMOD_EXCL_USER_WILD: case DHCP_ADDMOD_DEL_USER_WILD:
                            enmType = DHCPGroupConditionType_userClassIDWildcard;
                            break;
                        default:
                            AssertFailedReturn(RTEXITCODE_FAILURE);
                    }

                    int fInclusive;
                    switch (vrc)
                    {
                        case DHCP_ADDMOD_DEL_MAC:
                        case DHCP_ADDMOD_DEL_MAC_WILD:
                        case DHCP_ADDMOD_DEL_USER:
                        case DHCP_ADDMOD_DEL_USER_WILD:
                        case DHCP_ADDMOD_DEL_VENDOR:
                        case DHCP_ADDMOD_DEL_VENDOR_WILD:
                            fInclusive = -1;
                            break;
                        case DHCP_ADDMOD_EXCL_MAC:
                        case DHCP_ADDMOD_EXCL_MAC_WILD:
                        case DHCP_ADDMOD_EXCL_USER:
                        case DHCP_ADDMOD_EXCL_USER_WILD:
                        case DHCP_ADDMOD_EXCL_VENDOR:
                        case DHCP_ADDMOD_EXCL_VENDOR_WILD:
                            fInclusive = 0;
                            break;
                        case DHCP_ADDMOD_INCL_MAC:
                        case DHCP_ADDMOD_INCL_MAC_WILD:
                        case DHCP_ADDMOD_INCL_USER:
                        case DHCP_ADDMOD_INCL_USER_WILD:
                        case DHCP_ADDMOD_INCL_VENDOR:
                        case DHCP_ADDMOD_INCL_VENDOR_WILD:
                            fInclusive = 1;
                            break;
                        default:
                            AssertFailedReturn(RTEXITCODE_FAILURE);
                    }

                    ComPtr<IDHCPGroupConfig> &ptrGroupConfig = Scope.getGroup(ptrDHCPServer);
                    if (ptrGroupConfig.isNull())
                        return RTEXITCODE_FAILURE;
                    if (fInclusive >= 0)
                    {
                        ComPtr<IDHCPGroupCondition> ptrCondition;
                        CHECK_ERROR2I_STMT(ptrGroupConfig, AddCondition((BOOL)fInclusive, enmType, Bstr(ValueUnion.psz).raw(),
                                                                        ptrCondition.asOutParam()), rcExit = RTEXITCODE_FAILURE);
                    }
                    else
                    {
                        com::SafeIfaceArray<IDHCPGroupCondition> Conditions;
                        CHECK_ERROR2I_STMT(ptrGroupConfig, COMGETTER(Conditions)(ComSafeArrayAsOutParam(Conditions)),
                                           rcExit = RTEXITCODE_FAILURE; break);
                        bool fFound = false;
                        for (size_t iCond = 0; iCond < Conditions.size(); iCond++)
                            {
                                DHCPGroupConditionType_T enmCurType = DHCPGroupConditionType_MAC;
                                CHECK_ERROR2I_STMT(Conditions[iCond], COMGETTER(Type)(&enmCurType),
                                                   rcExit = RTEXITCODE_FAILURE; continue);
                                if (enmCurType == enmType)
                                {
                                    Bstr bstrValue;
                                    CHECK_ERROR2I_STMT(Conditions[iCond], COMGETTER(Value)(bstrValue.asOutParam()),
                                                      rcExit = RTEXITCODE_FAILURE; continue);
                                    if (RTUtf16CmpUtf8(bstrValue.raw(), ValueUnion.psz) == 0)
                                    {
                                        CHECK_ERROR2I_STMT(Conditions[iCond], Remove(), rcExit = RTEXITCODE_FAILURE);
                                        fFound = true;
                                    }
                                }
                            }
                        if (!fFound)
                            rcExit = RTMsgErrorExitFailure(DHCPServer::tr("Could not find any condition of type %d with value '%s' to delete"),
                                                           enmType, ValueUnion.psz);
                    }
                    break;
                }

                case DHCP_ADDMOD_ZAP_CONDITIONS:
                    if (Scope.getScope() != DHCPConfigScope_Group)
                        return errorSyntax(DHCPServer::tr("--zap-conditions can only be with a group selected"));
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPGroupConfig> &ptrGroupConfig = Scope.getGroup(ptrDHCPServer);
                        if (ptrGroupConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrGroupConfig, RemoveAllConditions(), rcExit = RTEXITCODE_FAILURE);
                    }
                    break;

                /*
                 * For backwards compatibility. Remove in 6.1 or later.
                 */

                case 'o':   // --options - obsolete, ignored.
                    break;

                case 'i':   // --id
                    if (fNeedValueOrRemove)
                        return errorSyntax(DHCPServer::tr("Incomplete option sequence preseeding '--id=%u"), ValueUnion.u8);
                    u8OptId = ValueUnion.u8;
                    fNeedValueOrRemove = true;
                    break;

                case 'p':   // --value
                    if (!fNeedValueOrRemove)
                        return errorSyntax(DHCPServer::tr("--value without --id=dhcp-opt-no"));
                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, SetOption((DHCPOption_T)u8OptId, DHCPOptionEncoding_Normal,
                                                                Bstr(ValueUnion.psz).raw()), rcExit = RTEXITCODE_FAILURE);
                    }
                    fNeedValueOrRemove = false;
                    break;

                case 'r':   // --remove
                    if (pCtx->pCmdDef->fSubcommandScope == HELP_SCOPE_DHCPSERVER_ADD)
                        return errorSyntax(DHCPServer::tr("--remove does not apply to the 'add' subcommand"));
                    if (!fNeedValueOrRemove)
                        return errorSyntax(DHCPServer::tr("--remove without --id=dhcp-opt-no"));

                    if (iPass == 1)
                    {
                        ComPtr<IDHCPConfig> &ptrConfig = Scope.getConfig(ptrDHCPServer);
                        if (ptrConfig.isNull())
                            return RTEXITCODE_FAILURE;
                        CHECK_ERROR2I_STMT(ptrConfig, RemoveOption((DHCPOption_T)u8OptId), rcExit = RTEXITCODE_FAILURE);
                    }
                    fNeedValueOrRemove = false;
                    break;

                default:
                    return errorGetOpt(vrc, &ValueUnion);
            }
        }

        if (iPass != 0)
            break;

        /*
         * Ensure we've got mandatory options and supply defaults
         * where needed (modify case)
         */
        if (!pCtx->pszNetwork && !pCtx->pszInterface)
            return errorSyntax(DHCPServer::tr("You need to specify either --network or --interface to identify the DHCP server"));

        if (pCtx->pCmdDef->fSubcommandScope == HELP_SCOPE_DHCPSERVER_ADD)
        {
            if (!pszServerIp)
                rcExit = errorSyntax(DHCPServer::tr("Missing required option: --ip"));
            if (!pszNetmask)
                rcExit = errorSyntax(DHCPServer::tr("Missing required option: --netmask"));
            if (!pszLowerIp)
                rcExit = errorSyntax(DHCPServer::tr("Missing required option: --lowerip"));
            if (!pszUpperIp)
                rcExit = errorSyntax(DHCPServer::tr("Missing required option: --upperip"));
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
        }

        /*
         * Find or create the server.
         */
        HRESULT hrc;
        Bstr NetName;
        if (!pCtx->pszNetwork)
        {
            ComPtr<IHost> host;
            CHECK_ERROR(pCtx->pArg->virtualBox, COMGETTER(Host)(host.asOutParam()));

            ComPtr<IHostNetworkInterface> hif;
            CHECK_ERROR(host, FindHostNetworkInterfaceByName(Bstr(pCtx->pszInterface).mutableRaw(), hif.asOutParam()));
            if (FAILED(hrc))
                return errorArgument(DHCPServer::tr("Could not find interface '%s'"), pCtx->pszInterface);

            CHECK_ERROR(hif, COMGETTER(NetworkName) (NetName.asOutParam()));
            if (FAILED(hrc))
                return errorArgument(DHCPServer::tr("Could not get network name for the interface '%s'"), pCtx->pszInterface);
        }
        else
        {
            NetName = Bstr(pCtx->pszNetwork);
        }

        hrc = pCtx->pArg->virtualBox->FindDHCPServerByNetworkName(NetName.mutableRaw(), ptrDHCPServer.asOutParam());
        if (pCtx->pCmdDef->fSubcommandScope == HELP_SCOPE_DHCPSERVER_ADD)
        {
            if (SUCCEEDED(hrc))
                return errorArgument(DHCPServer::tr("DHCP server already exists"));

            CHECK_ERROR(pCtx->pArg->virtualBox, CreateDHCPServer(NetName.mutableRaw(), ptrDHCPServer.asOutParam()));
            if (FAILED(hrc))
                return errorArgument(DHCPServer::tr("Failed to create the DHCP server"));
        }
        else if (FAILED(hrc))
            return errorArgument(DHCPServer::tr("DHCP server does not exist"));

        /*
         * Apply IDHCPServer settings:
         */
        if (pszServerIp || pszNetmask || pszLowerIp || pszUpperIp)
        {
            Bstr bstrServerIp(pszServerIp);
            Bstr bstrNetmask(pszNetmask);
            Bstr bstrLowerIp(pszLowerIp);
            Bstr bstrUpperIp(pszUpperIp);

            if (!pszServerIp)
            {
                CHECK_ERROR2_RET(hrc, ptrDHCPServer, COMGETTER(IPAddress)(bstrServerIp.asOutParam()), RTEXITCODE_FAILURE);
            }
            if (!pszNetmask)
            {
                CHECK_ERROR2_RET(hrc, ptrDHCPServer, COMGETTER(NetworkMask)(bstrNetmask.asOutParam()), RTEXITCODE_FAILURE);
            }
            if (!pszLowerIp)
            {
                CHECK_ERROR2_RET(hrc, ptrDHCPServer, COMGETTER(LowerIP)(bstrLowerIp.asOutParam()), RTEXITCODE_FAILURE);
            }
            if (!pszUpperIp)
            {
                CHECK_ERROR2_RET(hrc, ptrDHCPServer, COMGETTER(UpperIP)(bstrUpperIp.asOutParam()), RTEXITCODE_FAILURE);
            }

            CHECK_ERROR2_STMT(hrc, ptrDHCPServer, SetConfiguration(bstrServerIp.raw(), bstrNetmask.raw(),
                                                                   bstrLowerIp.raw(), bstrUpperIp.raw()),
                              rcExit = errorArgument(DHCPServer::tr("Failed to set configuration (%ls, %ls, %ls, %ls)"), bstrServerIp.raw(),
                                                     bstrNetmask.raw(), bstrLowerIp.raw(), bstrUpperIp.raw()));
        }

        if (fEnabled >= 0)
        {
            CHECK_ERROR2_STMT(hrc, ptrDHCPServer, COMSETTER(Enabled)((BOOL)fEnabled), rcExit = RTEXITCODE_FAILURE);
        }
    }

    return rcExit;
}


/**
 * Handles the 'remove' subcommand.
 */
static DECLCALLBACK(RTEXITCODE) dhcpdHandleRemove(PDHCPDCMDCTX pCtx, int argc, char **argv)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        DHCPD_CMD_COMMON_OPTION_DEFS(),
    };

    RTGETOPTSTATE   GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    RTGETOPTUNION   ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (vrc)
        {
            DHCPD_CMD_COMMON_OPTION_CASES(pCtx, vrc, &ValueUnion);
            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    /*
     * Locate the server and perform the requested operation.
     */
    ComPtr<IDHCPServer> ptrDHCPServer = dhcpdFindServer(pCtx);
    if (ptrDHCPServer.isNotNull())
    {
        HRESULT hrc;
        CHECK_ERROR2(hrc, pCtx->pArg->virtualBox, RemoveDHCPServer(ptrDHCPServer));
        if (SUCCEEDED(hrc))
            return RTEXITCODE_SUCCESS;
        errorArgument(DHCPServer::tr("Failed to remove server"));
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Handles the 'start' subcommand.
 */
static DECLCALLBACK(RTEXITCODE) dhcpdHandleStart(PDHCPDCMDCTX pCtx, int argc, char **argv)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        DHCPD_CMD_COMMON_OPTION_DEFS(),
    };

    RTGETOPTSTATE   GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    RTGETOPTUNION   ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (vrc)
        {
            DHCPD_CMD_COMMON_OPTION_CASES(pCtx, vrc, &ValueUnion);
            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    /*
     * Locate the server.
     */
    ComPtr<IDHCPServer> ptrDHCPServer = dhcpdFindServer(pCtx);
    if (ptrDHCPServer.isNotNull())
    {
        /*
         * We have to figure out the trunk name and type here, which is silly to
         * leave to the API client as it's a pain to get right.  But here we go...
         */
        static char const s_szHostOnlyPrefix[] = "HostInterfaceNetworking-";
        bool fHostOnly = true;
        Bstr strTrunkName;
        if (pCtx->pszInterface)
            strTrunkName = pCtx->pszInterface;
        else if (RTStrStartsWith(pCtx->pszNetwork, s_szHostOnlyPrefix))
            strTrunkName = &pCtx->pszNetwork[sizeof(s_szHostOnlyPrefix) - 1];
        else
            fHostOnly = false;

        Bstr strTrunkType;
        if (fHostOnly)
#if defined(RT_OS_WINDOWS) || defined(RT_OS_DARWIN)
            strTrunkType = "netadp";
#else /* lazy implementations: */
            strTrunkType = "netflt";
#endif
        else
            strTrunkType = "whatever";

        HRESULT hrc = ptrDHCPServer->Start(strTrunkName.raw(), strTrunkType.raw());
        if (SUCCEEDED(hrc))
            return RTEXITCODE_SUCCESS;
        errorArgument(DHCPServer::tr("Failed to start the server"));
        GlueHandleComErrorNoCtx(ptrDHCPServer, hrc);
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Handles the 'restart' subcommand.
 */
static DECLCALLBACK(RTEXITCODE) dhcpdHandleRestart(PDHCPDCMDCTX pCtx, int argc, char **argv)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        DHCPD_CMD_COMMON_OPTION_DEFS(),
    };

    RTGETOPTSTATE   GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    RTGETOPTUNION   ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (vrc)
        {
            DHCPD_CMD_COMMON_OPTION_CASES(pCtx, vrc, &ValueUnion);
            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    /*
     * Locate the server and perform the requested operation.
     */
    ComPtr<IDHCPServer> ptrDHCPServer = dhcpdFindServer(pCtx);
    if (ptrDHCPServer.isNotNull())
    {
        HRESULT hrc = ptrDHCPServer->Restart();
        if (SUCCEEDED(hrc))
            return RTEXITCODE_SUCCESS;
        errorArgument(DHCPServer::tr("Failed to restart the server"));
        GlueHandleComErrorNoCtx(ptrDHCPServer, hrc);
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Handles the 'stop' subcommand.
 */
static DECLCALLBACK(RTEXITCODE) dhcpdHandleStop(PDHCPDCMDCTX pCtx, int argc, char **argv)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        DHCPD_CMD_COMMON_OPTION_DEFS(),
    };

    RTGETOPTSTATE   GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    RTGETOPTUNION   ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (vrc)
        {
            DHCPD_CMD_COMMON_OPTION_CASES(pCtx, vrc, &ValueUnion);
            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    /*
     * Locate the server and perform the requested operation.
     */
    ComPtr<IDHCPServer> ptrDHCPServer = dhcpdFindServer(pCtx);
    if (ptrDHCPServer.isNotNull())
    {
        HRESULT hrc = ptrDHCPServer->Stop();
        if (SUCCEEDED(hrc))
            return RTEXITCODE_SUCCESS;
        errorArgument(DHCPServer::tr("Failed to stop the server"));
        GlueHandleComErrorNoCtx(ptrDHCPServer, hrc);
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Handles the 'findlease' subcommand.
 */
static DECLCALLBACK(RTEXITCODE) dhcpdHandleFindLease(PDHCPDCMDCTX pCtx, int argc, char **argv)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        DHCPD_CMD_COMMON_OPTION_DEFS(),
        { "--mac-address",      'm', RTGETOPT_REQ_MACADDR  },

    };

    bool            fHaveMacAddress   = false;
    RTMAC           MacAddress        = { { 0, 0, 0,  0, 0, 0 } };

    RTGETOPTSTATE   GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    RTGETOPTUNION   ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (vrc)
        {
            DHCPD_CMD_COMMON_OPTION_CASES(pCtx, vrc, &ValueUnion);

            case 'm':   // --mac-address
                fHaveMacAddress = true;
                MacAddress = ValueUnion.MacAddr;
                break;

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    if (!fHaveMacAddress)
        return errorSyntax(DHCPServer::tr("You need to specify a MAC address too look for"));

    /*
     * Locate the server and perform the requested operation.
     */
    ComPtr<IDHCPServer> ptrDHCPServer = dhcpdFindServer(pCtx);
    if (ptrDHCPServer.isNull())
        return RTEXITCODE_FAILURE;

    char    szMac[32];
    RTStrPrintf(szMac, sizeof(szMac), "%RTmac", &MacAddress);
    Bstr    bstrAddress;
    Bstr    bstrState;
    LONG64  secIssued = 0;
    LONG64  secExpire = 0;
    HRESULT hrc;
    CHECK_ERROR2(hrc, ptrDHCPServer, FindLeaseByMAC(Bstr(szMac).raw(), 0 /*type*/,
                                                    bstrAddress.asOutParam(), bstrState.asOutParam(), &secIssued, &secExpire));
    if (SUCCEEDED(hrc))
    {
        RTTIMESPEC  TimeSpec;
        int64_t     cSecLeftToLive = secExpire - RTTimeSpecGetSeconds(RTTimeNow(&TimeSpec));
        RTTIME      Time;
        char        szIssued[RTTIME_STR_LEN];
        RTTimeToStringEx(RTTimeExplode(&Time, RTTimeSpecSetSeconds(&TimeSpec, secIssued)), szIssued, sizeof(szIssued), 0);
        char        szExpire[RTTIME_STR_LEN];
        RTTimeToStringEx(RTTimeExplode(&Time, RTTimeSpecSetSeconds(&TimeSpec, secExpire)), szExpire, sizeof(szExpire), 0);

        RTPrintf(DHCPServer::tr("IP Address:  %ls\n"
                                "MAC Address: %RTmac\n"
                                "State:       %ls\n"
                                "Issued:      %s (%RU64)\n"
                                "Expire:      %s (%RU64)\n"
                                "TTL:         %RU64 sec, currently %RU64 sec left\n"),
                 bstrAddress.raw(),
                 &MacAddress,
                 bstrState.raw(),
                 szIssued, secIssued,
                 szExpire, secExpire,
                 secExpire >= secIssued ? secExpire - secIssued : 0, cSecLeftToLive > 0  ? cSecLeftToLive : 0);
        return RTEXITCODE_SUCCESS;
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Handles the 'dhcpserver' command.
 */
RTEXITCODE handleDHCPServer(HandlerArg *pArg)
{
    /*
     * Command definitions.
     */
    static const DHCPDCMDDEF s_aCmdDefs[] =
    {
        { "add",            dhcpdHandleAddAndModify,    HELP_SCOPE_DHCPSERVER_ADD },
        { "modify",         dhcpdHandleAddAndModify,    HELP_SCOPE_DHCPSERVER_MODIFY },
        { "remove",         dhcpdHandleRemove,          HELP_SCOPE_DHCPSERVER_REMOVE },
        { "start",          dhcpdHandleStart,           HELP_SCOPE_DHCPSERVER_START },
        { "restart",        dhcpdHandleRestart,         HELP_SCOPE_DHCPSERVER_RESTART },
        { "stop",           dhcpdHandleStop,            HELP_SCOPE_DHCPSERVER_STOP },
        { "findlease",      dhcpdHandleFindLease,       HELP_SCOPE_DHCPSERVER_FINDLEASE },
    };

    /*
     * VBoxManage dhcpserver [common-options] subcommand ...
     */
    DHCPDCMDCTX CmdCtx;
    CmdCtx.pArg         = pArg;
    CmdCtx.pCmdDef      = NULL;
    CmdCtx.pszInterface = NULL;
    CmdCtx.pszNetwork   = NULL;

    static const RTGETOPTDEF s_CommonOptions[] = { DHCPD_CMD_COMMON_OPTION_DEFS() };
    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, pArg->argc, pArg->argv, s_CommonOptions, RT_ELEMENTS(s_CommonOptions), 0,
                           0 /* No sorting! */);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    RTGETOPTUNION ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            DHCPD_CMD_COMMON_OPTION_CASES(&CmdCtx, vrc, &ValueUnion);

            case VINF_GETOPT_NOT_OPTION:
            {
                const char *pszCmd = ValueUnion.psz;
                uint32_t    iCmd;
                for (iCmd = 0; iCmd < RT_ELEMENTS(s_aCmdDefs); iCmd++)
                    if (strcmp(s_aCmdDefs[iCmd].pszName, pszCmd) == 0)
                    {
                        CmdCtx.pCmdDef = &s_aCmdDefs[iCmd];
                        setCurrentSubcommand(s_aCmdDefs[iCmd].fSubcommandScope);
                        return s_aCmdDefs[iCmd].pfnHandler(&CmdCtx, pArg->argc - GetState.iNext + 1,
                                                           &pArg->argv[GetState.iNext - 1]);
                    }
                return errorUnknownSubcommand(pszCmd);
            }

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }
    return errorNoSubcommand();
}
