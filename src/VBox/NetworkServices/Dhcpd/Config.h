/* $Id: Config.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef VBOX_INCLUDED_SRC_Dhcpd_Config_h
#define VBOX_INCLUDED_SRC_Dhcpd_Config_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "DhcpdInternal.h"
#include <iprt/types.h>
#include <iprt/net.h>
#include <iprt/cpp/xml.h>
#include <iprt/cpp/ministring.h>

#include <VBox/intnet.h>

#include "DhcpOptions.h"
#include "ClientId.h"


class Config;

/**
 * Base configuration
 *
 * @author bird (2019-07-15)
 */
class ConfigLevelBase
{
private:
    /** DHCP options. */
    optmap_t        m_Options;
protected:
    /** Minimum lease time, zero means try next level up. */
    uint32_t        m_secMinLeaseTime;
    /** Default lease time, zero means try next level up. */
    uint32_t        m_secDefaultLeaseTime;
    /** Maximum lease time, zero means try next level up. */
    uint32_t        m_secMaxLeaseTime;

    /** Options forced unsolicited upon the client. */
    octets_t        m_vecForcedOptions;
    /** Options (typcially from higher up) that should be hidden from the client. */
    octets_t        m_vecSuppressedOptions;

public:
    ConfigLevelBase()
        : m_Options()
        , m_secMinLeaseTime(0)
        , m_secDefaultLeaseTime(0)
        , m_secMaxLeaseTime(0)
        , m_vecForcedOptions()
        , m_vecSuppressedOptions()
    { }

    virtual ~ConfigLevelBase()
    { }

    virtual void        initFromXml(xml::ElementNode const *pElmConfig, bool fStrict, Config const *pConfig);
    virtual const char *getType() const RT_NOEXCEPT = 0;
    virtual const char *getName() const RT_NOEXCEPT = 0;

    /**
     * Tries to find DHCP option @a bOpt, returning an success indicator and
     * iterator to the result.
     */
    bool            findOption(uint8_t bOpt, optmap_t::const_iterator &a_rItRet) const RT_NOEXCEPT
    {
        a_rItRet = m_Options.find(bOpt);
        return a_rItRet != m_Options.end();
    }

    /** Checks if @a bOpt is suppressed or not. */
    bool            isOptionSuppressed(uint8_t bOpt) const RT_NOEXCEPT
    {
        return m_vecSuppressedOptions.size() > 0
            && memchr(&m_vecSuppressedOptions.front(), bOpt, m_vecSuppressedOptions.size()) != NULL;
    }

    /** @name Accessors
     * @{ */
    uint32_t        getMinLeaseTime()       const RT_NOEXCEPT { return m_secMinLeaseTime; }
    uint32_t        getDefaultLeaseTime()   const RT_NOEXCEPT { return m_secDefaultLeaseTime; }
    uint32_t        getMaxLeaseTime()       const RT_NOEXCEPT { return m_secMaxLeaseTime; }
    octets_t const &getForcedOptions()      const RT_NOEXCEPT { return m_vecForcedOptions; }
    octets_t const &getSuppressedOptions()  const RT_NOEXCEPT { return m_vecSuppressedOptions; }
    optmap_t const &getOptions()            const RT_NOEXCEPT { return m_Options; }
    /** @} */

protected:
    void            i_parseOption(const xml::ElementNode *pElmOption);
    void            i_parseForcedOrSuppressedOption(const xml::ElementNode *pElmOption, bool fForced);
    virtual void    i_parseChild(const xml::ElementNode *pElmChild, bool fStrict, Config const *pConfig);
};


/**
 * Global config
 */
class GlobalConfig : public ConfigLevelBase
{
public:
    GlobalConfig()
        : ConfigLevelBase()
    { }
    void initFromXml(xml::ElementNode const *pElmOptions, bool fStrict, Config const *pConfig) RT_OVERRIDE;
    const char *getType() const RT_NOEXCEPT RT_OVERRIDE { return "global"; }
    const char *getName() const RT_NOEXCEPT RT_OVERRIDE { return "GlobalConfig"; }
};


/**
 * Group membership condition.
 */
class GroupCondition
{
protected:
    /** The value. */
    RTCString   m_strValue;
    /** Inclusive (true) or exclusive (false), latter takes precedency. */
    bool        m_fInclusive;

public:
    virtual ~GroupCondition()
    {}

    virtual int  initCondition(const char *a_pszValue, bool a_fInclusive);
    virtual bool match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
                       const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT = 0;

    /** @name accessors
     * @{  */
    RTCString const    &getValue()     const RT_NOEXCEPT { return m_strValue; }
    bool                getInclusive() const RT_NOEXCEPT { return m_fInclusive; }
    /** @} */

protected:
    bool matchClassId(bool a_fPresent, std::vector<uint8_t> const &a_rBytes, bool fWildcard = false) const RT_NOEXCEPT;
};

/** MAC condition. */
class GroupConditionMAC : public GroupCondition
{
private:
    RTMAC   m_MACAddress;
public:
    int  initCondition(const char *a_pszValue, bool a_fInclusive) RT_OVERRIDE;
    bool match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
               const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT RT_OVERRIDE;
};

/** MAC wildcard condition. */
class GroupConditionMACWildcard : public GroupCondition
{
public:
    bool match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
               const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT RT_OVERRIDE;
};

/** Vendor class ID condition. */
class GroupConditionVendorClassID : public GroupCondition
{
public:
    bool match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
               const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT RT_OVERRIDE;
};

/** Vendor class ID wildcard condition. */
class GroupConditionVendorClassIDWildcard : public GroupCondition
{
public:
    bool match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
               const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT RT_OVERRIDE;
};

/** User class ID condition. */
class GroupConditionUserClassID : public GroupCondition
{
public:
    bool match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
               const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT RT_OVERRIDE;
};

/** User class ID wildcard condition. */
class GroupConditionUserClassIDWildcard : public GroupCondition
{
public:
    bool match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass,
               const OptUserClassId &a_ridUserClass) const RT_NOEXCEPT RT_OVERRIDE;
};


/**
 * Group config
 */
class GroupConfig : public ConfigLevelBase
{
private:
    typedef std::vector<GroupCondition *> GroupConditionVec;

    /** The group name. */
    RTCString           m_strName;
    /** Vector containing the inclusive membership conditions (must match one). */
    GroupConditionVec   m_Inclusive;
    /** Vector containing the exclusive membership conditions (must match none). */
    GroupConditionVec   m_Exclusive;

public:
    GroupConfig()
        : ConfigLevelBase()
    {
    }

    void initFromXml(xml::ElementNode const *pElmGroup, bool fStrict, Config const *pConfig) RT_OVERRIDE;
    bool match(const ClientId &a_ridClient, const OptVendorClassId &a_ridVendorClass, const OptUserClassId &a_ridUserClass) const;

    /** @name Accessors
     * @{ */
    const char         *getType() const RT_NOEXCEPT RT_OVERRIDE { return "group"; }
    const char         *getName() const RT_NOEXCEPT RT_OVERRIDE { return m_strName.c_str(); }
    RTCString const    &getGroupName() const RT_NOEXCEPT        { return m_strName; }
    /** @} */

protected:
    void                i_parseChild(const xml::ElementNode *pElmChild, bool fStrict, Config const *pConfig) RT_OVERRIDE;
    /** Used to name unnamed groups. */
    static uint32_t     s_uGroupNo;
};


/**
 * Host (MAC address) specific configuration.
 */
class HostConfig : public ConfigLevelBase
{
protected:
    /** The MAC address. */
    RTMAC           m_MACAddress;
    /** Name annotating the entry. */
    RTCString       m_strName;
    /** Fixed address assignment when m_fHaveFixedAddress is true. */
    RTNETADDRIPV4   m_FixedAddress;
    /** Set if we have a fixed address asignment. */
    bool            m_fHaveFixedAddress;

public:
    HostConfig()
        : ConfigLevelBase()
        , m_fHaveFixedAddress(false)
    {
        RT_ZERO(m_MACAddress);
        RT_ZERO(m_FixedAddress);
    }

    void initFromXml(xml::ElementNode const *pElmConfig, bool fStrict, Config const *pConfig) RT_OVERRIDE;
    const char *getType() const RT_NOEXCEPT RT_OVERRIDE { return "host"; }
    const char *getName() const RT_NOEXCEPT RT_OVERRIDE { return m_strName.c_str(); }

    /** @name Accessors
     * @{ */
    RTMAC const            &getMACAddress() const RT_NOEXCEPT       { return m_MACAddress; }
    bool                    haveFixedAddress() const RT_NOEXCEPT    { return m_fHaveFixedAddress; }
    RTNETADDRIPV4 const &   getFixedAddress() const RT_NOEXCEPT     { return m_FixedAddress; }
    /** @} */
};


/**
 * DHCP server configuration.
 */
class Config
{
    /** Group configuration map. */
    typedef std::map<RTCString, GroupConfig const * > GroupConfigMap;
    /** Host configuration map. */
    typedef std::map<RTMAC,     HostConfig const * > HostConfigMap;


    RTCString       m_strHome;          /**< path of ~/.VirtualBox or equivalent, */

    RTCString       m_strNetwork;       /**< The name of the internal network the DHCP server is connected to. */
    RTCString       m_strLeasesFilename;/**< The lease DB filename. */

    RTCString       m_strTrunk;         /**< The trunk name of the internal network. */
    INTNETTRUNKTYPE m_enmTrunkType;     /**< The trunk type of the internal network. */

    RTMAC           m_MacAddress;       /**< The MAC address for the DHCP server. */

    RTNETADDRIPV4   m_IPv4Address;      /**< The IPv4 address of the DHCP server. */
    RTNETADDRIPV4   m_IPv4Netmask;      /**< The IPv4 netmask for the DHCP server. */

    RTNETADDRIPV4   m_IPv4PoolFirst;    /**< The first IPv4 address in the pool. */
    RTNETADDRIPV4   m_IPv4PoolLast;     /**< The last IPV4 address in the pool (inclusive like all other 'last' variables). */


    /** The global configuration. */
    GlobalConfig    m_GlobalConfig;
    /** The group configurations, indexed by group name. */
    GroupConfigMap  m_GroupConfigs;
    /** The host configurations, indexed by MAC address. */
    HostConfigMap   m_HostConfigs;

    /** Set if we've initialized the log already (via command line). */
    static bool     g_fInitializedLog;

private:
    Config();

    int                 i_init() RT_NOEXCEPT;
    int                 i_homeInit() RT_NOEXCEPT;
    static Config      *i_createInstanceAndCallInit() RT_NOEXCEPT;
    int                 i_logInit() RT_NOEXCEPT;
    static int          i_logInitWithFilename(const char *pszFilename) RT_NOEXCEPT;
    int                 i_complete() RT_NOEXCEPT;

public:
    /** @name Factory methods
     * @{ */
    static Config      *hardcoded() RT_NOEXCEPT;                    /**< For testing. */
    static Config      *create(int argc, char **argv) RT_NOEXCEPT;  /**< --config */
    static Config      *compat(int argc, char **argv);
    /** @} */

    /** @name Accessors
     * @{ */
    const RTCString    &getHome() const RT_NOEXCEPT             { return m_strHome; }

    const RTCString    &getNetwork() const RT_NOEXCEPT          { return m_strNetwork; }
    const RTCString    &getLeasesFilename() const RT_NOEXCEPT   { return m_strLeasesFilename; }

    const RTCString    &getTrunk() const RT_NOEXCEPT            { return m_strTrunk; }
    INTNETTRUNKTYPE     getTrunkType() const RT_NOEXCEPT        { return m_enmTrunkType; }

    const RTMAC        &getMacAddress() const RT_NOEXCEPT       { return m_MacAddress; }

    RTNETADDRIPV4       getIPv4Address() const RT_NOEXCEPT      { return m_IPv4Address; }
    RTNETADDRIPV4       getIPv4Netmask() const RT_NOEXCEPT      { return m_IPv4Netmask; }
    RTNETADDRIPV4       getIPv4PoolFirst() const RT_NOEXCEPT    { return m_IPv4PoolFirst; }
    RTNETADDRIPV4       getIPv4PoolLast() const RT_NOEXCEPT     { return m_IPv4PoolLast; }
    /** @} */

    /** Gets the network (IP masked by network mask). */
    RTNETADDRIPV4       getIPv4Network() const RT_NOEXCEPT
    {
        RTNETADDRIPV4 Network;
        Network.u = m_IPv4Netmask.u & m_IPv4Address.u;
        return Network;
    }
    /** Checks if the given IPv4 address is in the DHCP server network. */
    bool                isInIPv4Network(RTNETADDRIPV4 a_rAddress) const RT_NOEXCEPT
    {
        return (a_rAddress.u & getIPv4Netmask().u) == getIPv4Network().u;
    }

    /** Host configuration vector. */
    typedef std::vector<HostConfig const *> HostConfigVec;
    int                 getFixedAddressConfigs(HostConfigVec &a_rRetConfigs) const;

    /** Configuration vector. */
    typedef std::vector<ConfigLevelBase const *> ConfigVec;
    ConfigVec          &getConfigsForClient(ConfigVec &a_rRetConfigs, const ClientId &a_ridClient,
                                            const OptVendorClassId &a_ridVendorClass,
                                            const OptUserClassId &a_ridUserClass) const;
    optmap_t           &getOptionsForClient(optmap_t &a_rRetOpts, const OptParameterRequest &a_rReqOpts,
                                            ConfigVec &a_rConfigs) const;

private:
    /** @name Configuration file reading and parsing
     * @{ */
    static Config      *i_read(const char *pszFilename, bool fStrict) RT_NOEXCEPT;
    void                i_parseConfig(const xml::ElementNode *pElmRoot, bool fStrict);
    void                i_parseServer(const xml::ElementNode *pElmServer, bool fStrict);
    /** @} */
};

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_Config_h */
