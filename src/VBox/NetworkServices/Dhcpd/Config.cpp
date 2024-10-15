/* $Id: Config.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - server configuration
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include "DhcpdInternal.h"

#include <iprt/ctype.h>
#include <iprt/net.h>           /* NB: must come before getopt.h */
#include <iprt/getopt.h>
#include <iprt/path.h>
#include <iprt/message.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/cpp/path.h>

#include <VBox/com/utils.h>     /* For log initialization. */

#include "Config.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/*static*/ bool         Config::g_fInitializedLog = false;
/*static*/ uint32_t     GroupConfig::s_uGroupNo   = 0;


/**
 * Configuration file exception.
 */
class ConfigFileError
    : public RTCError
{
public:
#if 0 /* This just confuses the compiler. */
    ConfigFileError(const char *a_pszMessage)
        : RTCError(a_pszMessage)
    {}
#endif

    explicit ConfigFileError(xml::Node const *pNode, const char *a_pszMsgFmt, ...)
        : RTCError((char *)NULL)
    {

        i_buildPath(pNode);
        m_strMsg.append(": ");

        va_list va;
        va_start(va, a_pszMsgFmt);
        m_strMsg.appendPrintfV(a_pszMsgFmt, va);
        va_end(va);
    }


    ConfigFileError(const char *a_pszMsgFmt, ...)
        : RTCError((char *)NULL)
    {
        va_list va;
        va_start(va, a_pszMsgFmt);
        m_strMsg.printfV(a_pszMsgFmt, va);
        va_end(va);
    }

    ConfigFileError(const RTCString &a_rstrMessage)
        : RTCError(a_rstrMessage)
    {}

private:
    void i_buildPath(xml::Node const *pNode)
    {
        if (pNode)
        {
            i_buildPath(pNode->getParent());
            m_strMsg.append('/');
            m_strMsg.append(pNode->getName());
            if (pNode->isElement() && pNode->getParent())
            {
                xml::ElementNode const *pElm = (xml::ElementNode const *)pNode;
                for (xml::Node const *pAttrib = pElm->getFirstAttribute(); pAttrib != NULL;
                      pAttrib = pAttrib->getNextSibiling())
                    if (pAttrib->isAttribute())
                    {
                        m_strMsg.append("[@");
                        m_strMsg.append(pAttrib->getName());
                        m_strMsg.append('=');
                        m_strMsg.append(pAttrib->getValue());
                        m_strMsg.append(']');
                    }
            }
        }
    }

};


/**
 * Private default constructor, external users use factor methods.
 */
Config::Config()
    : m_strHome()
    , m_strNetwork()
    , m_strTrunk()
    , m_enmTrunkType(kIntNetTrunkType_Invalid)
    , m_MacAddress()
    , m_IPv4Address()
    , m_IPv4Netmask()
    , m_IPv4PoolFirst()
    , m_IPv4PoolLast()
    , m_GlobalConfig()
    , m_GroupConfigs()
    , m_HostConfigs()
{
}


/**
 * Initializes the object.
 *
 * @returns IPRT status code.
 */
int Config::i_init() RT_NOEXCEPT
{
    return i_homeInit();
}


/**
 * Initializes the m_strHome member with the path to ~/.VirtualBox or equivalent.
 *
 * @returns IPRT status code.
 * @todo Too many init functions?
 */
int Config::i_homeInit() RT_NOEXCEPT
{
    char szHome[RTPATH_MAX];
    int rc = com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome), false);
    if (RT_SUCCESS(rc))
        rc = m_strHome.assignNoThrow(szHome);
    else
        DHCP_LOG_MSG_ERROR(("unable to locate the VirtualBox home directory: %Rrc\n", rc));
    return rc;
}


/**
 * Internal worker for the public factory methods that creates an instance and
 * calls i_init() on it.
 *
 * @returns Config instance on success, NULL on failure.
 */
/*static*/ Config *Config::i_createInstanceAndCallInit() RT_NOEXCEPT
{
    Config *pConfig;
    try
    {
        pConfig = new Config();
    }
    catch (std::bad_alloc &)
    {
        return NULL;
    }

    int rc = pConfig->i_init();
    if (RT_SUCCESS(rc))
        return pConfig;
    delete pConfig;
    return NULL;
}


/**
 * Worker for i_complete() that initializes the release log of the process.
 *
 * Requires network name to be known as the log file name depends on
 * it.  Alternatively, consider passing the log file name via the
 * command line?
 *
 * @note This is only used when no --log parameter was given.
 */
int Config::i_logInit() RT_NOEXCEPT
{
    if (g_fInitializedLog)
        return VINF_SUCCESS;

    if (m_strHome.isEmpty() || m_strNetwork.isEmpty())
        return VERR_PATH_ZERO_LENGTH;

    /* default log file name */
    char szLogFile[RTPATH_MAX];
    ssize_t cch = RTStrPrintf2(szLogFile, sizeof(szLogFile),
                               "%s%c%s-Dhcpd.log",
                               m_strHome.c_str(), RTPATH_DELIMITER, m_strNetwork.c_str());
    if (cch > 0)
    {
        RTPathPurgeFilename(RTPathFilename(szLogFile), RTPATH_STR_F_STYLE_HOST);
        return i_logInitWithFilename(szLogFile);
    }
    return VERR_BUFFER_OVERFLOW;
}


/**
 * Worker for i_logInit and for handling --log on the command line.
 *
 * @returns IPRT status code.
 * @param   pszFilename         The log filename.
 */
/*static*/ int Config::i_logInitWithFilename(const char *pszFilename) RT_NOEXCEPT
{
    AssertReturn(!g_fInitializedLog, VERR_WRONG_ORDER);

    int rc = com::VBoxLogRelCreate("DHCP Server",
                                   pszFilename,
                                   RTLOGFLAGS_PREFIX_TIME_PROG,
                                   "all net_dhcpd.e.l.f.l3.l4.l5.l6",
                                   "VBOXDHCP_RELEASE_LOG",
                                   RTLOGDEST_FILE
#ifdef DEBUG
                                   | RTLOGDEST_STDERR
#endif
                                   ,
                                   32768 /* cMaxEntriesPerGroup */,
                                   5 /* cHistory */,
                                   RT_SEC_1DAY /* uHistoryFileTime */,
                                   _32M /* uHistoryFileSize */,
                                   NULL /* pErrInfo */);
    if (RT_SUCCESS(rc))
        g_fInitializedLog = true;
    else
        RTMsgError("Log initialization failed: %Rrc, log file '%s'", rc, pszFilename);
    return rc;

}


/**
 * Post process and validate the configuration after it has been loaded.
 */
int Config::i_complete() RT_NOEXCEPT
{
    if (m_strNetwork.isEmpty())
    {
        LogRel(("network name is not specified\n"));
        return false;
    }

    i_logInit();

    /** @todo the MAC address is always generated, no XML config option for it ... */
    bool fMACGenerated = false;
    if (   m_MacAddress.au16[0] == 0
        && m_MacAddress.au16[1] == 0
        && m_MacAddress.au16[2] == 0)
    {
        RTUUID Uuid;
        int rc = RTUuidCreate(&Uuid);
        AssertRCReturn(rc, rc);

        m_MacAddress.au8[0] = 0x08;
        m_MacAddress.au8[1] = 0x00;
        m_MacAddress.au8[2] = 0x27;
        m_MacAddress.au8[3] = Uuid.Gen.au8Node[3];
        m_MacAddress.au8[4] = Uuid.Gen.au8Node[4];
        m_MacAddress.au8[5] = Uuid.Gen.au8Node[5];

        LogRel(("MAC address is not specified: will use generated MAC %RTmac\n", &m_MacAddress));
        fMACGenerated = true;
    }

    /* unicast MAC address */
    if (m_MacAddress.au8[0] & 0x01)
    {
        LogRel(("MAC address is not unicast: %RTmac\n", &m_MacAddress));
        return VERR_GENERAL_FAILURE;
    }

    if (!fMACGenerated)
        LogRel(("MAC address %RTmac\n", &m_MacAddress));

    return VINF_SUCCESS;
}


/**
 * Parses the command line and loads the configuration.
 *
 * @returns The configuration, NULL if we ran into some fatal problem.
 * @param   argc    The argc from main().
 * @param   argv    The argv from main().
 */
Config *Config::create(int argc, char **argv) RT_NOEXCEPT
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--comment",              '#', RTGETOPT_REQ_STRING },
        { "--config",               'c', RTGETOPT_REQ_STRING },
        { "--log",                  'l', RTGETOPT_REQ_STRING },
        { "--log-destinations",     'd', RTGETOPT_REQ_STRING },
        { "--log-flags",            'f', RTGETOPT_REQ_STRING },
        { "--log-group-settings",   'g', RTGETOPT_REQ_STRING },
        { "--relaxed",              'r', RTGETOPT_REQ_NOTHING },
        { "--strict",               's', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    AssertRCReturn(rc, NULL);

    const char *pszLogFile          = NULL;
    const char *pszLogGroupSettings = NULL;
    const char *pszLogDestinations  = NULL;
    const char *pszLogFlags         = NULL;
    const char *pszConfig           = NULL;
    const char *pszComment          = NULL;
    bool        fStrict             = true;

    for (;;)
    {
        RTGETOPTUNION ValueUnion;
        rc = RTGetOpt(&State, &ValueUnion);
        if (rc == 0)            /* done */
            break;

        switch (rc)
        {
            case 'c': /* --config */
                pszConfig = ValueUnion.psz;
                break;

            case 'l':
                pszLogFile = ValueUnion.psz;
                break;

            case 'd':
                pszLogDestinations = ValueUnion.psz;
                break;

            case 'f':
                pszLogFlags = ValueUnion.psz;
                break;

            case 'g':
                pszLogGroupSettings = ValueUnion.psz;
                break;

            case 'r':
                fStrict = false;
                break;

            case 's':
                fStrict = true;
                break;

            case '#': /* --comment */
                /* The sole purpose of this option is to allow identification of DHCP
                 * server instances in the process list. We ignore the required string
                 * argument of this option. */
                pszComment = ValueUnion.psz;
                break;

            default:
                RTGetOptPrintError(rc, &ValueUnion);
                return NULL;
        }
    }

    if (!pszConfig)
    {
        RTMsgError("No configuration file specified (--config file)!\n");
        return NULL;
    }

    /*
     * Init the log if a log file was specified.
     */
    if (pszLogFile)
    {
        rc = i_logInitWithFilename(pszLogFile);
        if (RT_FAILURE(rc))
            RTMsgError("Failed to initialize log file '%s': %Rrc", pszLogFile, rc);

        if (pszLogDestinations)
            RTLogDestinations(RTLogRelGetDefaultInstance(), pszLogDestinations);
        if (pszLogFlags)
            RTLogFlags(RTLogRelGetDefaultInstance(), pszLogFlags);
        if (pszLogGroupSettings)
            RTLogGroupSettings(RTLogRelGetDefaultInstance(), pszLogGroupSettings);

        LogRel(("--config:  %s\n", pszComment));
        if (pszComment)
            LogRel(("--comment: %s\n", pszComment));
    }

    /*
     * Read the config file.
     */
    RTMsgInfo("reading config from '%s'...\n", pszConfig);
    std::unique_ptr<Config> ptrConfig;
    ptrConfig.reset(Config::i_read(pszConfig, fStrict));
    if (ptrConfig.get() != NULL)
    {
        rc = ptrConfig->i_complete();
        if (RT_SUCCESS(rc))
            return ptrConfig.release();
    }
    return NULL;
}


/**
 *
 * @note The release log is not operational when this method is called.
 */
Config *Config::i_read(const char *pszFileName, bool fStrict) RT_NOEXCEPT
{
    if (pszFileName == NULL || pszFileName[0] == '\0')
    {
        DHCP_LOG_MSG_ERROR(("Config::i_read: Empty configuration filename\n"));
        return NULL;
    }

    xml::Document doc;
    try
    {
        xml::XmlFileParser parser;
        parser.read(pszFileName, doc);
    }
    catch (const xml::EIPRTFailure &e)
    {
        DHCP_LOG_MSG_ERROR(("Config::i_read: %s\n", e.what()));
        return NULL;
    }
    catch (const RTCError &e)
    {
        DHCP_LOG_MSG_ERROR(("Config::i_read: %s\n", e.what()));
        return NULL;
    }
    catch (...)
    {
        DHCP_LOG_MSG_ERROR(("Config::i_read: Unknown exception while reading and parsing '%s'\n", pszFileName));
        return NULL;
    }

    std::unique_ptr<Config> config(i_createInstanceAndCallInit());
    AssertReturn(config.get() != NULL, NULL);

    try
    {
        config->i_parseConfig(doc.getRootElement(), fStrict);
    }
    catch (const RTCError &e)
    {
        DHCP_LOG_MSG_ERROR(("Config::i_read: %s\n", e.what()));
        return NULL;
    }
    catch (std::bad_alloc &)
    {
        DHCP_LOG_MSG_ERROR(("Config::i_read: std::bad_alloc\n"));
        return NULL;
    }
    catch (...)
    {
        DHCP_LOG_MSG_ERROR(("Config::i_read: Unexpected exception\n"));
        return NULL;
    }

    return config.release();
}


/**
 * Helper for retrieving a IPv4 attribute.
 *
 * @param   pElm            The element to get the attribute from.
 * @param   pszAttrName     The name of the attribute
 * @param   pAddr           Where to return the address.
 * @throws  ConfigFileError
 */
static void getIPv4AddrAttribute(const xml::ElementNode *pElm, const char *pszAttrName, PRTNETADDRIPV4 pAddr)
{
    const char *pszAttrValue;
    if (pElm->getAttributeValue(pszAttrName, &pszAttrValue))
    {
        int rc = RTNetStrToIPv4Addr(pszAttrValue, pAddr);
        if (RT_SUCCESS(rc))
            return;
        throw ConfigFileError(pElm, "Attribute %s is not a valid IPv4 address: '%s' -> %Rrc", pszAttrName, pszAttrValue, rc);
    }
    throw ConfigFileError(pElm, "Required %s attribute missing", pszAttrName);
}


/**
 * Helper for retrieving a MAC address attribute.
 *
 * @param   pElm            The element to get the attribute from.
 * @param   pszAttrName     The name of the attribute
 * @param   pMacAddr        Where to return the MAC address.
 * @throws  ConfigFileError
 */
static void getMacAddressAttribute(const xml::ElementNode *pElm, const char *pszAttrName, PRTMAC pMacAddr)
{
    const char *pszAttrValue;
    if (pElm->getAttributeValue(pszAttrName, &pszAttrValue))
    {
        int rc = RTNetStrToMacAddr(pszAttrValue, pMacAddr);
        if (RT_SUCCESS(rc) && rc != VWRN_TRAILING_CHARS)
            return;
        throw ConfigFileError(pElm, "attribute %s is not a valid MAC address: '%s' -> %Rrc", pszAttrName, pszAttrValue, rc);
    }
    throw ConfigFileError(pElm, "Required %s attribute missing", pszAttrName);
}


/**
 * Internal worker for i_read() that parses the root element and everything
 * below it.
 *
 * @param   pElmRoot            The root element.
 * @param   fStrict             Set if we're in strict mode, clear if we just
 *                              want to get on with it if we can.
 * @throws  std::bad_alloc, ConfigFileError
 */
void Config::i_parseConfig(const xml::ElementNode *pElmRoot, bool fStrict)
{
    /*
     * Check the root element and call i_parseServer to do real work.
     */
    if (pElmRoot == NULL)
        throw ConfigFileError("Empty config file");

    /** @todo XXX: NAMESPACE API IS COMPLETELY BROKEN, SO IGNORE IT FOR NOW */

    if (!pElmRoot->nameEquals("DHCPServer"))
        throw ConfigFileError("Unexpected root element '%s'", pElmRoot->getName());

    i_parseServer(pElmRoot, fStrict);

#if 0 /** @todo convert to LogRel2 stuff */
    // XXX: debug
    for (optmap_t::const_iterator it = m_GlobalOptions.begin(); it != m_GlobalOptions.end(); ++it) {
        std::shared_ptr<DhcpOption> opt(it->second);

        octets_t data;
        opt->encode(data);

        bool space = false;
        for (octets_t::const_iterator itData = data.begin(); itData != data.end(); ++itData) {
            uint8_t c = *itData;
            if (space)
                std::cout << " ";
            else
                space = true;
            std::cout << (int)c;
        }
        std::cout << std::endl;
    }
#endif
}


/**
 * Internal worker for parsing the elements under /DHCPServer/.
 *
 * @param   pElmServer          The DHCPServer element.
 * @param   fStrict             Set if we're in strict mode, clear if we just
 *                              want to get on with it if we can.
 * @throws  std::bad_alloc, ConfigFileError
 */
void Config::i_parseServer(const xml::ElementNode *pElmServer, bool fStrict)
{
    /*
     * <DHCPServer> attributes
     */
    if (!pElmServer->getAttributeValue("networkName", m_strNetwork))
        throw ConfigFileError("DHCPServer/@networkName missing");
    if (m_strNetwork.isEmpty())
        throw ConfigFileError("DHCPServer/@networkName is empty");

    const char *pszTrunkType;
    if (!pElmServer->getAttributeValue("trunkType", &pszTrunkType))
        throw ConfigFileError("DHCPServer/@trunkType missing");
    if (strcmp(pszTrunkType, "none") == 0)
        m_enmTrunkType = kIntNetTrunkType_None;
    else if (strcmp(pszTrunkType, "whatever") == 0)
        m_enmTrunkType = kIntNetTrunkType_WhateverNone;
    else if (strcmp(pszTrunkType, "netflt") == 0)
        m_enmTrunkType = kIntNetTrunkType_NetFlt;
    else if (strcmp(pszTrunkType, "netadp") == 0)
        m_enmTrunkType = kIntNetTrunkType_NetAdp;
    else
        throw ConfigFileError("Invalid DHCPServer/@trunkType value: %s", pszTrunkType);

    if (   m_enmTrunkType == kIntNetTrunkType_NetFlt
        || m_enmTrunkType == kIntNetTrunkType_NetAdp)
    {
        if (!pElmServer->getAttributeValue("trunkName", &m_strTrunk))
            throw ConfigFileError("DHCPServer/@trunkName missing");
    }
    else
        m_strTrunk = "";

    m_strLeasesFilename = pElmServer->findAttributeValue("leasesFilename"); /* optional */
    if (m_strLeasesFilename.isEmpty())
    {
        int rc = m_strLeasesFilename.assignNoThrow(getHome());
        if (RT_SUCCESS(rc))
            rc = RTPathAppendCxx(m_strLeasesFilename, m_strNetwork);
        if (RT_SUCCESS(rc))
            rc = m_strLeasesFilename.appendNoThrow("-Dhcpd.leases");
        if (RT_FAILURE(rc))
            throw ConfigFileError("Unexpected error constructing default m_strLeasesFilename value: %Rrc", rc);
        RTPathPurgeFilename(RTPathFilename(m_strLeasesFilename.mutableRaw()), RTPATH_STR_F_STYLE_HOST);
        m_strLeasesFilename.jolt();
    }

    /*
     * Addresses and mask.
     */
    ::getIPv4AddrAttribute(pElmServer, "IPAddress", &m_IPv4Address);
    ::getIPv4AddrAttribute(pElmServer, "networkMask", &m_IPv4Netmask);
    ::getIPv4AddrAttribute(pElmServer, "lowerIP", &m_IPv4PoolFirst);
    ::getIPv4AddrAttribute(pElmServer, "upperIP", &m_IPv4PoolLast);

    /* unicast IP address */
    if ((m_IPv4Address.au8[0] & 0xe0) == 0xe0)
        throw ConfigFileError("DHCP server IP address is not unicast: %RTnaipv4", m_IPv4Address.u);

    /* valid netmask */
    int cPrefixBits;
    int rc = RTNetMaskToPrefixIPv4(&m_IPv4Netmask, &cPrefixBits);
    if (RT_FAILURE(rc) || cPrefixBits == 0)
        throw ConfigFileError("IP mask is not valid: %RTnaipv4", m_IPv4Netmask.u);

    /* first IP is from the same network */
    if ((m_IPv4PoolFirst.u & m_IPv4Netmask.u) != (m_IPv4Address.u & m_IPv4Netmask.u))
        throw ConfigFileError("first pool address is outside the network %RTnaipv4/%d: %RTnaipv4",
                              (m_IPv4Address.u & m_IPv4Netmask.u), cPrefixBits, m_IPv4PoolFirst.u);

    /* last IP is from the same network */
    if ((m_IPv4PoolLast.u & m_IPv4Netmask.u) != (m_IPv4Address.u & m_IPv4Netmask.u))
        throw ConfigFileError("last pool address is outside the network %RTnaipv4/%d: %RTnaipv4\n",
                              (m_IPv4Address.u & m_IPv4Netmask.u), cPrefixBits, m_IPv4PoolLast.u);

    /* the pool is valid */
    if (RT_N2H_U32(m_IPv4PoolLast.u) < RT_N2H_U32(m_IPv4PoolFirst.u))
        throw ConfigFileError("pool range is invalid: %RTnaipv4 - %RTnaipv4", m_IPv4PoolFirst.u, m_IPv4PoolLast.u);
    LogRel(("IP address:   %RTnaipv4/%d\n", m_IPv4Address.u, cPrefixBits));
    LogRel(("Address pool: %RTnaipv4 - %RTnaipv4\n", m_IPv4PoolFirst.u, m_IPv4PoolLast.u));

    /*
     * <DHCPServer> children
     */
    xml::NodesLoop it(*pElmServer);
    const xml::ElementNode *pElmChild;
    while ((pElmChild = it.forAllNodes()) != NULL)
    {
        /* Global options: */
        if (pElmChild->nameEquals("Options"))
            m_GlobalConfig.initFromXml(pElmChild, fStrict, this);
        /* Group w/ options: */
        else if (pElmChild->nameEquals("Group"))
        {
            std::unique_ptr<GroupConfig> ptrGroup(new GroupConfig());
            ptrGroup->initFromXml(pElmChild, fStrict, this);
            if (m_GroupConfigs.find(ptrGroup->getGroupName()) == m_GroupConfigs.end())
            {
                m_GroupConfigs[ptrGroup->getGroupName()] = ptrGroup.get();
                ptrGroup.release();
            }
            else if (!fStrict)
                LogRelFunc(("Ignoring duplicate group name: %s", ptrGroup->getGroupName().c_str()));
            else
                throw ConfigFileError("Duplicate group name: %s", ptrGroup->getGroupName().c_str());
        }
        /*
         * MAC address and per VM NIC configurations:
         */
        else if (pElmChild->nameEquals("Config"))
        {
            std::unique_ptr<HostConfig> ptrHost(new HostConfig());
            ptrHost->initFromXml(pElmChild, fStrict, this);
            if (m_HostConfigs.find(ptrHost->getMACAddress()) == m_HostConfigs.end())
            {
                m_HostConfigs[ptrHost->getMACAddress()] = ptrHost.get();
                ptrHost.release();
            }
            else if (!fStrict)
                LogRelFunc(("Ignorining duplicate MAC address (Config): %RTmac", &ptrHost->getMACAddress()));
            else
                throw ConfigFileError("Duplicate MAC address (Config): %RTmac", &ptrHost->getMACAddress());
        }
        else if (!fStrict)
            LogRel(("Ignoring unexpected DHCPServer child: %s\n", pElmChild->getName()));
        else
            throw ConfigFileError("Unexpected DHCPServer child <%s>'", pElmChild->getName());
    }
}


/**
 * Internal worker for parsing \<Option\> elements found under
 * /DHCPServer/Options/, /DHCPServer/Group/ and /DHCPServer/Config/.
 *
 * @param   pElmOption          An \<Option\> element.
 * @throws  std::bad_alloc, ConfigFileError
 */
void ConfigLevelBase::i_parseOption(const xml::ElementNode *pElmOption)
{
    /* The 'name' attribute: */
    const char *pszName;
    if (!pElmOption->getAttributeValue("name", &pszName))
        throw ConfigFileError(pElmOption, "missing option name");

    uint8_t u8Opt;
    int rc = RTStrToUInt8Full(pszName, 10, &u8Opt);
    if (rc != VINF_SUCCESS) /* no warnings either */
        throw ConfigFileError(pElmOption, "Bad option name '%s': %Rrc", pszName, rc);

    /* The optional 'encoding' attribute: */
    uint32_t u32Enc = 0;            /* XXX: DHCPOptionEncoding_Normal */
    const char *pszEncoding;
    if (pElmOption->getAttributeValue("encoding", &pszEncoding))
    {
        rc = RTStrToUInt32Full(pszEncoding, 10, &u32Enc);
        if (rc != VINF_SUCCESS) /* no warnings either */
            throw ConfigFileError(pElmOption, "Bad option encoding '%s': %Rrc", pszEncoding, rc);

        switch (u32Enc)
        {
            case 0:                 /* XXX: DHCPOptionEncoding_Normal */
            case 1:                 /* XXX: DHCPOptionEncoding_Hex */
                break;
            default:
                throw ConfigFileError(pElmOption, "Unknown encoding '%s'", pszEncoding);
        }
    }

    /* The 'value' attribute. May be omitted for OptNoValue options like rapid commit. */
    const char *pszValue;
    if (!pElmOption->getAttributeValue("value", &pszValue))
        pszValue = "";

    /** @todo XXX: TODO: encoding, handle hex */
    DhcpOption *opt = DhcpOption::parse(u8Opt, u32Enc, pszValue);
    if (opt == NULL)
        throw ConfigFileError(pElmOption, "Bad option '%s' (encoding %u): '%s' ", pszName, u32Enc, pszValue ? pszValue : "");

    /* Add it to the map: */
    m_Options << opt;
}


/**
 * Internal worker for parsing \<ForcedOption\> and \<SupcressedOption\> elements
 * found under /DHCPServer/Options/, /DHCPServer/Group/ and /DHCPServer/Config/.
 *
 * @param   pElmOption          The element.
 * @param   fForced             Whether it's a ForcedOption (true) or
 *                              SuppressedOption element.
 * @throws  std::bad_alloc, ConfigFileError
 */
void ConfigLevelBase::i_parseForcedOrSuppressedOption(const xml::ElementNode *pElmOption, bool fForced)
{
    /* Only a name attribute: */
    const char *pszName;
    if (!pElmOption->getAttributeValue("name", &pszName))
        throw ConfigFileError(pElmOption, "missing option name");

    uint8_t u8Opt;
    int rc = RTStrToUInt8Full(pszName, 10, &u8Opt);
    if (rc != VINF_SUCCESS) /* no warnings either */
        throw ConfigFileError(pElmOption, "Bad option name '%s': %Rrc", pszName, rc);

    if (fForced)
        m_vecForcedOptions.push_back(u8Opt);
    else
        m_vecSuppressedOptions.push_back(u8Opt);
}


/**
 * Final children parser, handling only \<Option\> and barfing at anything else.
 *
 * @param   pElmChild           The child element to handle.
 * @param   fStrict             Set if we're in strict mode, clear if we just
 *                              want to get on with it if we can.  That said,
 *                              the caller will catch ConfigFileError exceptions
 *                              and ignore them if @a fStrict is @c false.
 * @param   pConfig             The configuration object.
 * @throws  std::bad_alloc, ConfigFileError
 */
void ConfigLevelBase::i_parseChild(const xml::ElementNode *pElmChild, bool fStrict, Config const *pConfig)
{
    /*
     * Options.
     */
    if (pElmChild->nameEquals("Option"))
    {
        i_parseOption(pElmChild);
        return;
    }

    /*
     * Forced and suppressed options.
     */
    bool const fForced = pElmChild->nameEquals("ForcedOption");
    if (fForced || pElmChild->nameEquals("SuppressedOption"))
    {
        i_parseForcedOrSuppressedOption(pElmChild, fForced);
        return;
    }

    /*
     * What's this?
     */
    throw ConfigFileError(pElmChild->getParent(), "Unexpected child '%s'", pElmChild->getName());
    RT_NOREF(fStrict, pConfig);
}


/**
 * Base class initialization taking a /DHCPServer/Options, /DHCPServer/Group or
 * /DHCPServer/Config element as input and handling common attributes as well as
 * any \<Option\> children.
 *
 * @param   pElmConfig          The configuration element to parse.
 * @param   fStrict             Set if we're in strict mode, clear if we just
 *                              want to get on with it if we can.
 * @param   pConfig             The configuration object.
 * @throws  std::bad_alloc, ConfigFileError
 */
void ConfigLevelBase::initFromXml(const xml::ElementNode *pElmConfig, bool fStrict, Config const *pConfig)
{
    /*
     * Common attributes:
     */
    if (!pElmConfig->getAttributeValue("secMinLeaseTime", &m_secMinLeaseTime))
        m_secMinLeaseTime = 0;
    if (!pElmConfig->getAttributeValue("secDefaultLeaseTime", &m_secDefaultLeaseTime))
        m_secDefaultLeaseTime = 0;
    if (!pElmConfig->getAttributeValue("secMaxLeaseTime", &m_secMaxLeaseTime))
        m_secMaxLeaseTime = 0;

    /* Swap min and max if max is smaller: */
    if (m_secMaxLeaseTime < m_secMinLeaseTime && m_secMinLeaseTime && m_secMaxLeaseTime)
    {
        LogRel(("Swapping min/max lease times: %u <-> %u\n", m_secMinLeaseTime, m_secMaxLeaseTime));
        uint32_t uTmp = m_secMaxLeaseTime;
        m_secMaxLeaseTime = m_secMinLeaseTime;
        m_secMinLeaseTime = uTmp;
    }

    /*
     * Parse children.
     */
    xml::NodesLoop it(*pElmConfig);
    const xml::ElementNode *pElmChild;
    while ((pElmChild = it.forAllNodes()) != NULL)
    {
        try
        {
            i_parseChild(pElmChild, fStrict, pConfig);
        }
        catch (ConfigFileError &rXcpt)
        {
            if (fStrict)
                throw rXcpt;
            LogRelFunc(("Ignoring: %s\n", rXcpt.what()));
        }
    }
}


/**
 * Internal worker for parsing the elements under /DHCPServer/Options/.
 *
 * @param   pElmOptions         The \<Options\> element.
 * @param   fStrict             Set if we're in strict mode, clear if we just
 *                              want to get on with it if we can.
 * @param   pConfig             The configuration object.
 * @throws  std::bad_alloc, ConfigFileError
 */
void GlobalConfig::initFromXml(const xml::ElementNode *pElmOptions, bool fStrict, Config const *pConfig)
{
    ConfigLevelBase::initFromXml(pElmOptions, fStrict, pConfig);

    /*
     * Resolve defaults here in the global config so we don't have to do this
     * in Db::allocateBinding() for every lease request.
     */
    if (m_secMaxLeaseTime == 0 && m_secDefaultLeaseTime == 0 && m_secMinLeaseTime == 0)
    {
        m_secMinLeaseTime     = 300;                /*  5 min */
        m_secDefaultLeaseTime = 600;                /* 10 min */
        m_secMaxLeaseTime     = 12 * RT_SEC_1HOUR;  /* 12 hours */
    }
    else
    {
        if (m_secDefaultLeaseTime == 0)
        {
            if (m_secMaxLeaseTime != 0)
                m_secDefaultLeaseTime = RT_MIN(RT_MAX(m_secMinLeaseTime, 600), m_secMaxLeaseTime);
            else
            {
                m_secDefaultLeaseTime = RT_MAX(m_secMinLeaseTime, 600);
                m_secMaxLeaseTime = RT_MAX(m_secDefaultLeaseTime, 12 * RT_SEC_1HOUR);
            }
        }
        if (m_secMaxLeaseTime == 0)
            m_secMaxLeaseTime = RT_MAX(RT_MAX(m_secMinLeaseTime, m_secDefaultLeaseTime), 12 * RT_SEC_1HOUR);
        if (m_secMinLeaseTime == 0)
            m_secMinLeaseTime = RT_MIN(300, m_secDefaultLeaseTime);
    }

}


/**
 * Overrides base class to handle the condition elements under \<Group\>.
 *
 * @param   pElmChild           The child element.
 * @param   fStrict             Set if we're in strict mode, clear if we just
 *                              want to get on with it if we can.
 * @param   pConfig             The configuration object.
 * @throws  std::bad_alloc, ConfigFileError
 */
void GroupConfig::i_parseChild(const xml::ElementNode *pElmChild, bool fStrict, Config const *pConfig)
{
    /*
     * Match the condition
     */
    std::unique_ptr<GroupCondition> ptrCondition;
    if (pElmChild->nameEquals("ConditionMAC"))
        ptrCondition.reset(new GroupConditionMAC());
    else if (pElmChild->nameEquals("ConditionMACWildcard"))
        ptrCondition.reset(new GroupConditionMACWildcard());
    else if (pElmChild->nameEquals("ConditionVendorClassID"))
        ptrCondition.reset(new GroupConditionVendorClassID());
    else if (pElmChild->nameEquals("ConditionVendorClassIDWildcard"))
        ptrCondition.reset(new GroupConditionVendorClassIDWildcard());
    else if (pElmChild->nameEquals("ConditionUserClassID"))
        ptrCondition.reset(new GroupConditionUserClassID());
    else if (pElmChild->nameEquals("ConditionUserClassIDWildcard"))
        ptrCondition.reset(new GroupConditionUserClassIDWildcard());
    else
    {
        /*
         * Not a condition, pass it on to the base class.
         */
        ConfigLevelBase::i_parseChild(pElmChild, fStrict, pConfig);
        return;
    }

    /*
     * Get the attributes and initialize the condition.
     */
    bool fInclusive;
    if (!pElmChild->getAttributeValue("inclusive", fInclusive))
        fInclusive = true;
    const char *pszValue = pElmChild->findAttributeValue("value");
    if (pszValue && *pszValue)
    {
        int rc = ptrCondition->initCondition(pszValue, fInclusive);
        if (RT_SUCCESS(rc))
        {
            /*
             * Add it to the appropriate vector.
             */
            if (fInclusive)
                m_Inclusive.push_back(ptrCondition.release());
            else
                m_Exclusive.push_back(ptrCondition.release());
        }
        else
        {
            ConfigFileError Xcpt(pElmChild, "initCondition failed with %Rrc for '%s' and %RTbool", rc, pszValue, fInclusive);
            if (!fStrict)
                LogRelFunc(("%s, ignoring condition\n", Xcpt.what()));
            else
                throw ConfigFileError(Xcpt);
        }
    }
    else
    {
        ConfigFileError Xcpt(pElmChild, "condition value is empty or missing (inclusive=%RTbool)", fInclusive);
        if (fStrict)
            throw Xcpt;
        LogRelFunc(("%s, ignoring condition\n", Xcpt.what()));
    }
}


/**
 * Internal worker for parsing the elements under /DHCPServer/Group/.
 *
 * @param   pElmGroup           The \<Group\> element.
 * @param   fStrict             Set if we're in strict mode, clear if we just
 *                              want to get on with it if we can.
 * @param   pConfig             The configuration object.
 * @throws  std::bad_alloc, ConfigFileError
 */
void GroupConfig::initFromXml(const xml::ElementNode *pElmGroup, bool fStrict, Config const *pConfig)
{
    /*
     * Attributes:
     */
    if (!pElmGroup->getAttributeValue("name", m_strName) || m_strName.isEmpty())
    {
        if (fStrict)
            throw ConfigFileError(pElmGroup, "Group as no name or the name is empty");
        m_strName.printf("Group#%u", s_uGroupNo++);
    }

    /*
     * Do common initialization (including children).
     */
    ConfigLevelBase::initFromXml(pElmGroup, fStrict, pConfig);
}


/**
 * Internal worker for parsing the elements under /DHCPServer/Config/.
 *
 * VM Config entries are generated automatically from VirtualBox.xml
 * with the MAC fetched from the VM config.  The client id is nowhere
 * in the picture there, so VM config is indexed with plain RTMAC, not
 * ClientId (also see getOptions below).
 *
 * @param   pElmConfig          The \<Config\> element.
 * @param   fStrict             Set if we're in strict mode, clear if we just
 *                              want to get on with it if we can.
 * @param   pConfig             The configuration object (for netmask).
 * @throws  std::bad_alloc, ConfigFileError
 */
void HostConfig::initFromXml(const xml::ElementNode *pElmConfig, bool fStrict, Config const *pConfig)
{
    /*
     * Attributes:
     */
    /* The MAC address: */
    ::getMacAddressAttribute(pElmConfig, "MACAddress", &m_MACAddress);

    /* Name - optional: */
    if (!pElmConfig->getAttributeValue("name", m_strName))
        m_strName.printf("MAC:%RTmac", &m_MACAddress);

    /* Fixed IP address assignment - optional: */
    const char *pszFixedAddress = pElmConfig->findAttributeValue("fixedAddress");
    if (!pszFixedAddress || *RTStrStripL(pszFixedAddress) == '\0')
        m_fHaveFixedAddress = false;
    else
    {
        ::getIPv4AddrAttribute(pElmConfig, "fixedAddress", &m_FixedAddress);
        if (pConfig->isInIPv4Network(m_FixedAddress))
            m_fHaveFixedAddress = true;
        else
        {
            ConfigFileError Xcpt(pElmConfig, "fixedAddress '%s' is not the DHCP network", pszFixedAddress);
            if (fStrict)
                throw Xcpt;
            LogRelFunc(("%s - ignoring the fixed address assignment\n", Xcpt.what()));
            m_fHaveFixedAddress = false;
        }
    }

    /*
     * Do common initialization.
     */
    ConfigLevelBase::initFromXml(pElmConfig, fStrict, pConfig);
}


/**
 * Assembles a list of hosts with fixed address assignments.
 *
 * @returns IPRT status code.
 * @param   a_rRetConfigs       Where to return the configurations.
 */
int Config::getFixedAddressConfigs(HostConfigVec &a_rRetConfigs) const
{
    for (HostConfigMap::const_iterator it = m_HostConfigs.begin(); it != m_HostConfigs.end(); ++it)
    {
        HostConfig const *pHostConfig = it->second;
        if (pHostConfig->haveFixedAddress())
            try
            {
                a_rRetConfigs.push_back(pHostConfig);
            }
            catch (std::bad_alloc &)
            {
                return VERR_NO_MEMORY;
            }
    }
    return VINF_SUCCESS;
}


/**
 * Assembles a priorities vector of configurations for the client.
 *
 * @returns a_rRetConfigs for convenience.
 * @param   a_rRetConfigs       Where to return the configurations.
 * @param   a_ridClient         The client ID.
 * @param   a_ridVendorClass    The vendor class ID if present.
 * @param   a_ridUserClass      The user class ID if present
 */
Config::ConfigVec &Config::getConfigsForClient(Config::ConfigVec &a_rRetConfigs, const ClientId &a_ridClient,
                                               const OptVendorClassId &a_ridVendorClass,
                                               const OptUserClassId &a_ridUserClass) const
{
    /* Host specific config first: */
    HostConfigMap::const_iterator itHost = m_HostConfigs.find(a_ridClient.mac());
    if (itHost != m_HostConfigs.end())
        a_rRetConfigs.push_back(itHost->second);

    /* Groups: */
    for (GroupConfigMap::const_iterator itGrp = m_GroupConfigs.begin(); itGrp != m_GroupConfigs.end(); ++itGrp)
        if (itGrp->second->match(a_ridClient, a_ridVendorClass, a_ridUserClass))
            a_rRetConfigs.push_back(itGrp->second);

    /* Global: */
    a_rRetConfigs.push_back(&m_GlobalConfig);

    return a_rRetConfigs;
}


/**
 * Method used by DHCPD to assemble a list of options for the client.
 *
 * @returns a_rRetOpts for convenience
 * @param   a_rRetOpts      Where to put the requested options.
 * @param   a_rReqOpts      The requested options.
 * @param   a_rConfigs      Relevant configurations returned by
 *                          Config::getConfigsForClient().
 *
 * @throws  std::bad_alloc
 */
optmap_t &Config::getOptionsForClient(optmap_t &a_rRetOpts, const OptParameterRequest &a_rReqOpts, ConfigVec &a_rConfigs) const
{
    /*
     * The client typically requests a list of options.  The list is subject to
     * forced and suppressed lists on each configuration level in a_rConfig.  To
     * efficiently manage it without resorting to maps, the current code
     * assembles a C-style array of options on the stack that should be returned
     * to the client.
     */
    uint8_t abOptions[256];
    size_t  cOptions = 0;
    size_t  iFirstForced = 255;
#define IS_OPTION_PRESENT(a_bOption)         (memchr(abOptions, (a_bOption), cOptions) != NULL)
#define APPEND_NOT_PRESENT_OPTION(a_bOption) do { \
            AssertLogRelMsgBreak(cOptions < sizeof(abOptions), \
                                 ("a_bOption=%#x abOptions=%.*Rhxs\n", (a_bOption), sizeof(abOptions), &abOptions[0])); \
            abOptions[cOptions++] = (a_bOption); \
        } while (0)

    const OptParameterRequest::value_t &reqValue = a_rReqOpts.value();
    if (reqValue.size() != 0)
    {
        /* Copy the requested list and append any forced options from the configs: */
        for (octets_t::const_iterator itOptReq = reqValue.begin(); itOptReq != reqValue.end(); ++itOptReq)
            if (!IS_OPTION_PRESENT(*itOptReq))
                APPEND_NOT_PRESENT_OPTION(*itOptReq);
        iFirstForced = cOptions;
        for (Config::ConfigVec::const_iterator itCfg = a_rConfigs.begin(); itCfg != a_rConfigs.end(); ++itCfg)
        {
            octets_t const &rForced = (*itCfg)->getForcedOptions();
            for (octets_t::const_iterator itOpt = rForced.begin(); itOpt != rForced.end(); ++itOpt)
                if (!IS_OPTION_PRESENT(*itOpt))
                {
                    LogRel3((">>> Forcing option %d (%s)\n", *itOpt, DhcpOption::name(*itOpt)));
                    APPEND_NOT_PRESENT_OPTION(*itOpt);
                }
        }
    }
    else
    {
        /* No options requests, feed the client all available options: */
        for (Config::ConfigVec::const_iterator itCfg = a_rConfigs.begin(); itCfg != a_rConfigs.end(); ++itCfg)
        {
            optmap_t const &rOptions = (*itCfg)->getOptions();
            for (optmap_t::const_iterator itOpt = rOptions.begin(); itOpt != rOptions.end(); ++itOpt)
                if (!IS_OPTION_PRESENT(itOpt->first))
                    APPEND_NOT_PRESENT_OPTION(itOpt->first);

        }
    }

    /*
     * Always supply the subnet:
     */
    a_rRetOpts << new OptSubnetMask(m_IPv4Netmask);

    /*
     * Try provide the options we've decided to return.
     */
    for (size_t iOpt = 0; iOpt < cOptions; iOpt++)
    {
        uint8_t const bOptReq = abOptions[iOpt];
        if (iOpt < iFirstForced)
            LogRel2((">>> requested option %d (%s)\n", bOptReq, DhcpOption::name(bOptReq)));
        else
            LogRel2((">>> forced option %d (%s)\n", bOptReq, DhcpOption::name(bOptReq)));

        if (bOptReq != OptSubnetMask::optcode)
        {
            bool fFound = false;
            for (size_t i = 0; i < a_rConfigs.size(); i++)
            {
                if (!a_rConfigs[i]->isOptionSuppressed(bOptReq))
                {
                    optmap_t::const_iterator itFound;
                    if (a_rConfigs[i]->findOption(bOptReq, itFound)) /* crap interface */
                    {
                        LogRel2(("... found in %s (type %s)\n", a_rConfigs[i]->getName(), a_rConfigs[i]->getType()));
                        a_rRetOpts << itFound->second;
                        fFound = true;
                        break;
                    }
                }
                else
                {
                    LogRel2(("... suppressed by %s (type %s)\n", a_rConfigs[i]->getName(), a_rConfigs[i]->getType()));
                    fFound = true;
                    break;
                }
            }
            if (!fFound)
                LogRel3(("... not found\n"));
        }
        else
            LogRel2(("... always supplied\n"));
    }

#undef IS_OPTION_PRESENT
#undef APPEND_NOT_PRESENT_OPTION
    return a_rRetOpts;
}



/*********************************************************************************************************************************
*   Group Condition Matching                                                                                                     *
*********************************************************************************************************************************/

bool GroupConfig::match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
                        const OptUserClassId &a_ridUserClass) const
{
    /*
     * Check the inclusive ones first, only one need to match.
     */
    for (GroupConditionVec::const_iterator itIncl = m_Inclusive.begin(); itIncl != m_Inclusive.end(); ++itIncl)
        if ((*itIncl)->match(a_ridClient, a_ridVendorClass, a_ridUserClass))
        {
            /*
             * Now make sure it isn't excluded by any of the exclusion condition.
             */
            for (GroupConditionVec::const_iterator itExcl = m_Exclusive.begin(); itExcl != m_Exclusive.end(); ++itExcl)
                if ((*itIncl)->match(a_ridClient, a_ridVendorClass, a_ridUserClass))
                    return false;
            return true;
        }

    return false;
}


int GroupCondition::initCondition(const char *a_pszValue, bool a_fInclusive)
{
    m_fInclusive = a_fInclusive;
    return m_strValue.assignNoThrow(a_pszValue);
}


bool GroupCondition::matchClassId(bool a_fPresent, const std::vector<uint8_t> &a_rBytes, bool fWildcard) const RT_NOEXCEPT
{
    if (a_fPresent)
    {
        size_t const cbBytes = a_rBytes.size();
        if (cbBytes > 0)
        {
            if (a_rBytes[cbBytes - 1] == '\0')
            {
                uint8_t const *pb = &a_rBytes.front();
                if (!fWildcard)
                    return m_strValue.equals((const char *)pb);
                return RTStrSimplePatternMatch(m_strValue.c_str(), (const char *)pb);
            }

            if (cbBytes <= 255)
            {
                char szTmp[256];
                memcpy(szTmp, &a_rBytes.front(), cbBytes);
                szTmp[cbBytes] = '\0';
                if (!fWildcard)
                    return m_strValue.equals(szTmp);
                return RTStrSimplePatternMatch(m_strValue.c_str(), szTmp);
            }
        }
    }
    return false;

}


int GroupConditionMAC::initCondition(const char *a_pszValue, bool a_fInclusive)
{
    int vrc = RTNetStrToMacAddr(a_pszValue, &m_MACAddress);
    if (RT_SUCCESS(vrc))
        return GroupCondition::initCondition(a_pszValue, a_fInclusive);
    return vrc;
}


bool GroupConditionMAC::match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
                              const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT
{
    RT_NOREF(a_ridVendorClass, a_ridUserClass);
    return a_ridClient.mac() == m_MACAddress;
}


bool GroupConditionMACWildcard::match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
                                      const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT
{
    RT_NOREF(a_ridVendorClass, a_ridUserClass);
    char szTmp[32];
    RTStrPrintf(szTmp, sizeof(szTmp), "%RTmac", &a_ridClient.mac());
    return RTStrSimplePatternMatch(m_strValue.c_str(), szTmp);
}


bool GroupConditionVendorClassID::match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
                                        const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT
{
    RT_NOREF(a_ridClient, a_ridUserClass);
    return matchClassId(a_ridVendorClass.present(), a_ridVendorClass.value());
}


bool GroupConditionVendorClassIDWildcard::match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
                                                const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT
{
    RT_NOREF(a_ridClient, a_ridUserClass);
    return matchClassId(a_ridVendorClass.present(), a_ridVendorClass.value(), true /*fWildcard*/);
}


bool GroupConditionUserClassID::match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
                                      const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT
{
    RT_NOREF(a_ridClient, a_ridVendorClass);
    return matchClassId(a_ridVendorClass.present(), a_ridUserClass.value());
}


bool GroupConditionUserClassIDWildcard::match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
                                              const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT
{
    RT_NOREF(a_ridClient, a_ridVendorClass);
    return matchClassId(a_ridVendorClass.present(), a_ridUserClass.value(), true /*fWildcard*/);
}

