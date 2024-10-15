/* $Id: DhcpOptions.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - DHCP options
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

#ifndef VBOX_INCLUDED_SRC_Dhcpd_DhcpOptions_h
#define VBOX_INCLUDED_SRC_Dhcpd_DhcpOptions_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "DhcpdInternal.h"

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/net.h>
#include <iprt/string.h>
#include <iprt/cpp/ministring.h>


class DhcpClientMessage;

typedef struct DhcpIpv4AddrAndMask
{
    RTNETADDRIPV4   Ipv4;
    RTNETADDRIPV4   Mask;
} DhcpIpv4AddrAndMask;


class DhcpOption
{
protected:
    uint8_t m_OptCode;
    bool    m_fPresent;

public:
    explicit DhcpOption(uint8_t aOptCode)
        : m_OptCode(aOptCode), m_fPresent(true)
    {}

    DhcpOption(uint8_t aOptCode, bool fPresent)
        : m_OptCode(aOptCode), m_fPresent(fPresent)
    {}

    virtual DhcpOption *clone() const = 0;

    virtual ~DhcpOption()
    {}

public:
    static DhcpOption *parse(uint8_t aOptCode, int aEnc, const char *pcszValue, int *prc = NULL);
    static const char *name(uint8_t bOptcode);

public:
    uint8_t optcode() const RT_NOEXCEPT { return m_OptCode; }
    bool    present() const RT_NOEXCEPT { return m_fPresent; }

public:
    int encode(octets_t &dst) const;

    int decode(const rawopts_t &map);
    int decode(const DhcpClientMessage &req);

protected:
    virtual ssize_t encodeValue(octets_t &dst) const = 0;
    virtual int decodeValue(const octets_t &src, size_t cb) = 0;

protected:
    static const octets_t *findOption(const rawopts_t &aOptMap, uint8_t aOptCode);

protected:
    /** @name Serialization
     * @{ */
    static void append(octets_t &aDst, bool aValue)
    {
        uint8_t b = aValue ? 1 : 0;
        aDst.push_back(b);
    }

    static void append(octets_t &aDst, uint8_t aValue)
    {
        aDst.push_back(aValue);
    }

    static void append(octets_t &aDst, uint16_t aValue)
    {
        RTUINT16U u16 = { RT_H2N_U16(aValue) };
        aDst.insert(aDst.end(), u16.au8, u16.au8 + sizeof(aValue));
    }

    static void append(octets_t &aDst, uint32_t aValue)
    {
        RTUINT32U u32 = { RT_H2N_U32(aValue) };
        aDst.insert(aDst.end(), u32.au8, u32.au8 + sizeof(aValue));
    }

    static void append(octets_t &aDst, RTNETADDRIPV4 aIPv4)
    {
        aDst.insert(aDst.end(), aIPv4.au8, aIPv4.au8 + sizeof(aIPv4));
    }

    static void append(octets_t &aDst, DhcpIpv4AddrAndMask aIPv4)
    {
        aDst.insert(aDst.end(), (uint8_t *)&aIPv4, (uint8_t *)&aIPv4 + sizeof(aIPv4));
    }

    static void append(octets_t &aDst, const char *pszString, size_t cb)
    {
        aDst.insert(aDst.end(), pszString, pszString + cb);
    }

    static void append(octets_t &aDst, const RTCString &str)
    {
        append(aDst, str.c_str(), str.length());
    }

    /* non-overloaded name to avoid ambiguity */
    static void appendLength(octets_t &aDst, size_t cb)
    {
        append(aDst, static_cast<uint8_t>(cb));
    }

    /** @} */


    /** @name Deserialization
     * @{  */
    static void extract(bool &aValue, octets_t::const_iterator &pos)
    {
        aValue = *pos != 0;
        pos += sizeof(uint8_t);
    }

    static void extract(uint8_t &aValue, octets_t::const_iterator &pos)
    {
        aValue = *pos;
        pos += sizeof(uint8_t);
    }

    static void extract(uint16_t &aValue, octets_t::const_iterator &pos)
    {
        RTUINT16U u16;
        memcpy(u16.au8, &pos[0], sizeof(uint16_t));
        aValue = RT_N2H_U16(u16.u);
        pos += sizeof(uint16_t);
    }

    static void extract(uint32_t &aValue, octets_t::const_iterator &pos)
    {
        RTUINT32U u32;
        memcpy(u32.au8, &pos[0], sizeof(uint32_t));
        aValue = RT_N2H_U32(u32.u);
        pos += sizeof(uint32_t);
    }

    static void extract(RTNETADDRIPV4 &aValue, octets_t::const_iterator &pos)
    {
        memcpy(aValue.au8, &pos[0], sizeof(RTNETADDRIPV4));
        pos += sizeof(RTNETADDRIPV4);
    }

    static void extract(DhcpIpv4AddrAndMask &aValue, octets_t::const_iterator &pos)
    {
        memcpy(&aValue, &pos[0], sizeof(aValue));
        pos += sizeof(aValue);
    }

#if 0 /** @todo fix me */
    static void extract(RTCString &aString, octets_t::const_iterator &pos, size_t cb)
    {
        aString.replace(aString.begin(), aString.end(), &pos[0], &pos[cb]);
        pos += cb;
    }
#endif

    /** @} */

    /** @name Parse textual representation (e.g. in config file)
     * @{  */
    static int parse1(bool &aValue, const char *pcszValue);
    static int parse1(uint8_t &aValue, const char *pcszValue);
    static int parse1(uint16_t &aValue, const char *pcszValue);
    static int parse1(uint32_t &aValue, const char *pcszValue);
    static int parse1(RTNETADDRIPV4 &aValue, const char *pcszValue);
    static int parse1(DhcpIpv4AddrAndMask &aValue, const char *pcszValue);

    template <typename a_Type> static int parseList(std::vector<a_Type> &aList, const char *pcszValue);

    static int parseHex(octets_t &aRawValue, const char *pcszValue);

    /** @} */
};


inline octets_t &operator<<(octets_t &dst, const DhcpOption &option)
{
    option.encode(dst);
    return dst;
}


#ifndef IN_VBOXSVC
optmap_t &operator<<(optmap_t &optmap, DhcpOption *option);
optmap_t &operator<<(optmap_t &optmap, const std::shared_ptr<DhcpOption> &option);
#endif



/**
 * Only for << OptEnd() syntactic sugar...
 */
struct OptEnd {};
inline octets_t &operator<<(octets_t &dst, const OptEnd &end)
{
    RT_NOREF(end);

    dst.push_back(RTNET_DHCP_OPT_END);
    return dst;
}



/**
 * Option that has no value
 */
class OptNoValueBase
    : public DhcpOption
{
public:
    explicit OptNoValueBase(uint8_t aOptCode)
        : DhcpOption(aOptCode, false)
    {}

    OptNoValueBase(uint8_t aOptCode, bool fPresent)
        : DhcpOption(aOptCode, fPresent)
    {}

    OptNoValueBase(uint8_t aOptCode, const DhcpClientMessage &req)
        : DhcpOption(aOptCode, false)
    {
        decode(req);
    }

    virtual OptNoValueBase *clone() const
    {
        return new OptNoValueBase(*this);
    }

protected:
    virtual ssize_t encodeValue(octets_t &dst) const
    {
        RT_NOREF(dst);
        return 0;
    }

public:
    static bool isLengthValid(size_t cb)
    {
        return cb == 0;
    }

    virtual int decodeValue(const octets_t &src, size_t cb)
    {
        RT_NOREF(src);

        if (!isLengthValid(cb))
            return VERR_INVALID_PARAMETER;

        m_fPresent = true;
        return VINF_SUCCESS;
    }
};

template <uint8_t _OptCode>
class OptNoValue
    : public OptNoValueBase
{
public:
    static const uint8_t optcode = _OptCode;

    OptNoValue()
        : OptNoValueBase(optcode)
    {}

    explicit OptNoValue(bool fPresent) /* there's no overloaded ctor with value */
        : OptNoValueBase(optcode, fPresent)
    {}

    explicit OptNoValue(const DhcpClientMessage &req)
        : OptNoValueBase(optcode, req)
    {}
};



/*
 * Option that contains single value of fixed-size type T
 */
template <typename T>
class OptValueBase
    : public DhcpOption
{
public:
    typedef T value_t;

protected:
    T m_Value;

    explicit OptValueBase(uint8_t aOptCode)
        : DhcpOption(aOptCode, false), m_Value()
    {}

    OptValueBase(uint8_t aOptCode, const T &aOptValue)
        : DhcpOption(aOptCode), m_Value(aOptValue)
    {}

    OptValueBase(uint8_t aOptCode, const DhcpClientMessage &req)
        : DhcpOption(aOptCode, false), m_Value()
    {
        decode(req);
    }

public:
    virtual OptValueBase *clone() const
    {
        return new OptValueBase(*this);
    }

public:
    T &value()              { return m_Value; }
    const T &value() const  { return m_Value; }

protected:
    virtual ssize_t encodeValue(octets_t &dst) const
    {
        append(dst, m_Value);
        return sizeof(T);
    }

public:
    static bool isLengthValid(size_t cb)
    {
        return cb == sizeof(T);
    }

    virtual int decodeValue(const octets_t &src, size_t cb)
    {
        if (!isLengthValid(cb))
            return VERR_INVALID_PARAMETER;

        octets_t::const_iterator pos(src.begin());
        extract(m_Value, pos);

        m_fPresent = true;
        return VINF_SUCCESS;
    }
};

template<uint8_t _OptCode, typename T>
class OptValue
    : public OptValueBase<T>
{
public:
    using typename OptValueBase<T>::value_t;

public:
    static const uint8_t optcode = _OptCode;

    OptValue()
        : OptValueBase<T>(optcode)
    {}

    explicit OptValue(const T &aOptValue)
        : OptValueBase<T>(optcode, aOptValue)
    {}

    explicit OptValue(const DhcpClientMessage &req)
        : OptValueBase<T>(optcode, req)
    {}

    static OptValue *parse(const char *pcszValue, int *prc)
    {
        typename OptValueBase<T>::value_t v;
        int rc = DhcpOption::parse1(v, pcszValue);
        *prc = rc;
        if (RT_SUCCESS(rc))
            return new OptValue(v);
        return NULL;
    }
};



/**
 * Option that contains a string.
 */
class OptStringBase
    : public DhcpOption
{
public:
    typedef RTCString value_t;

protected:
    RTCString m_String;

    explicit OptStringBase(uint8_t aOptCode)
        : DhcpOption(aOptCode, false), m_String()
    {}

    OptStringBase(uint8_t aOptCode, const RTCString &aOptString)
        : DhcpOption(aOptCode), m_String(aOptString)
    {}

    OptStringBase(uint8_t aOptCode, const DhcpClientMessage &req)
        : DhcpOption(aOptCode, false), m_String()
    {
        decode(req);
    }

public:
    virtual OptStringBase *clone() const
    {
        return new OptStringBase(*this);
    }

public:
    RTCString &value()              { return m_String; }
    const RTCString &value() const  { return m_String; }

protected:
    virtual ssize_t encodeValue(octets_t &dst) const
    {
        if (!isLengthValid(m_String.length()))
            return -1;

        append(dst, m_String);
        return (ssize_t)m_String.length();
    }

public:
    static bool isLengthValid(size_t cb)
    {
        return cb <= UINT8_MAX;
    }

    virtual int decodeValue(const octets_t &src, size_t cb)
    {
        if (!isLengthValid(cb))
            return VERR_INVALID_PARAMETER;

        int rc = m_String.assignNoThrow((char *)&src.front(), cb); /** @todo encoding. */
        m_fPresent = true;
        return rc;
    }
};

template<uint8_t _OptCode>
class OptString
    : public OptStringBase
{
public:
    static const uint8_t optcode = _OptCode;

    OptString()
        : OptStringBase(optcode)
    {}

    explicit OptString(const RTCString &aOptString)
        : OptStringBase(optcode, aOptString)
    {}

    explicit OptString(const DhcpClientMessage &req)
        : OptStringBase(optcode, req)
    {}

    static OptString *parse(const char *pcszValue, int *prc)
    {
        *prc = VINF_SUCCESS;
        return new OptString(pcszValue);
    }
};



/*
 * Option that contains a list of values of type T
 */
template <typename T>
class OptListBase
    : public DhcpOption
{
public:
    typedef std::vector<T> value_t;

protected:
    std::vector<T> m_List;

    explicit OptListBase(uint8_t aOptCode)
        : DhcpOption(aOptCode, false), m_List()
    {}

    OptListBase(uint8_t aOptCode, const T &aOptSingle)
        : DhcpOption(aOptCode), m_List(1, aOptSingle)
    {}

    OptListBase(uint8_t aOptCode, const std::vector<T> &aOptList)
        : DhcpOption(aOptCode), m_List(aOptList)
    {}

    OptListBase(uint8_t aOptCode, const DhcpClientMessage &req)
        : DhcpOption(aOptCode, false), m_List()
    {
        decode(req);
    }

public:
    virtual OptListBase *clone() const
    {
        return new OptListBase(*this);
    }

public:
    std::vector<T> &value()             { return m_List; }
    const std::vector<T> &value() const { return m_List; }

protected:
    virtual ssize_t encodeValue(octets_t &dst) const
    {
        const size_t cbItem = sizeof(T);
        size_t cbValue = 0;

        for (size_t i = 0; i < m_List.size(); ++i)
        {
            if (cbValue + cbItem > UINT8_MAX)
                break;

            append(dst, m_List[i]);
            cbValue += cbItem;
        }

        return (ssize_t)cbValue;
    }

public:
    static bool isLengthValid(size_t cb)
    {
        return cb % sizeof(T) == 0;
    }

    virtual int decodeValue(const octets_t &src, size_t cb)
    {
        if (!isLengthValid(cb))
            return VERR_INVALID_PARAMETER;

        m_List.erase(m_List.begin(), m_List.end());

        octets_t::const_iterator pos(src.begin());
        for (size_t i = 0; i < cb / sizeof(T); ++i)
        {
            T item;
            extract(item, pos);
            m_List.push_back(item);
        }
        m_fPresent = true;
        return VINF_SUCCESS;
    }
};

template<uint8_t _OptCode, typename T>
class OptList
    : public OptListBase<T>

{
public:
    using typename OptListBase<T>::value_t;

public:
    static const uint8_t optcode = _OptCode;

    OptList()
        : OptListBase<T>(optcode)
    {}

    explicit OptList(const T &aOptSingle)
        : OptListBase<T>(optcode, aOptSingle)
    {}

    explicit OptList(const std::vector<T> &aOptList)
        : OptListBase<T>(optcode, aOptList)
    {}

    explicit OptList(const DhcpClientMessage &req)
        : OptListBase<T>(optcode, req)
    {}

    static OptList *parse(const char *pcszValue, int *prc)
    {
        typename OptListBase<T>::value_t v;
        int rc = DhcpOption::parseList<T>(v, pcszValue);
        if (RT_SUCCESS(rc))
        {
            if (!v.empty())
            {
                *prc = rc;
                return new OptList(v);
            }
            rc = VERR_NO_DATA;
        }
        *prc = rc;
        return NULL;
    }
};


template<uint8_t _OptCode, typename T>
class OptPairList
    : public OptListBase<T>

{
public:
    using typename OptListBase<T>::value_t;

public:
    static const uint8_t optcode = _OptCode;

    OptPairList()
        : OptListBase<T>(optcode)
    {}

    explicit OptPairList(const T &aOptSingle)
        : OptListBase<T>(optcode, aOptSingle)
    {}

    explicit OptPairList(const std::vector<T> &aOptList)
        : OptListBase<T>(optcode, aOptList)
    {}

    explicit OptPairList(const DhcpClientMessage &req)
        : OptListBase<T>(optcode, req)
    {}

    static OptPairList *parse(const char *pcszValue, int *prc)
    {
        typename OptListBase<T>::value_t v;
        int rc = DhcpOption::parseList<T>(v, pcszValue);
        if (RT_SUCCESS(rc))
        {
            if (!v.empty())
            {
                if ((v.size() & 1) == 0)
                {
                    *prc = rc;
                    return new OptPairList(v);
                }
                rc = VERR_UNEVEN_INPUT;
            }
            else
                rc = VERR_NO_DATA;
        }
        *prc = rc;
        return NULL;
    }
};


/*
 * Options specified by raw binary data that we don't know how to
 * interpret.
 */
class RawOption
    : public DhcpOption
{
protected:
    octets_t m_Data;

public:
    explicit RawOption(uint8_t aOptCode)
        : DhcpOption(aOptCode, false), m_Data()
    {}

    RawOption(uint8_t aOptCode, const octets_t &aSrc)
        : DhcpOption(aOptCode), m_Data(aSrc)
    {}

public:
    virtual RawOption *clone() const
    {
        return new RawOption(*this);
    }


protected:
    virtual ssize_t encodeValue(octets_t &dst) const
    {
        dst.insert(dst.end(), m_Data.begin(), m_Data.end());
        return (ssize_t)m_Data.size();
    }

    virtual int decodeValue(const octets_t &src, size_t cb)
    {
        octets_t::const_iterator beg(src.begin());
        octets_t data(beg, beg + (ssize_t)cb);
        m_Data.swap(data);

        m_fPresent = true;
        return VINF_SUCCESS;
    }

public:
    static RawOption *parse(uint8_t aOptCode, const char *pcszValue, int *prc)
    {
        octets_t data;
        int rc = DhcpOption::parseHex(data, pcszValue);
        *prc = rc;
        if (RT_SUCCESS(rc))
            return new RawOption(aOptCode, data);
        return NULL;
    }
};



/** @name The DHCP options types.
 * @{
 */
typedef OptValue<1, RTNETADDRIPV4>      OptSubnetMask;
typedef OptValue<2, uint32_t>           OptTimeOffset;
typedef OptList<3, RTNETADDRIPV4>       OptRouters;
typedef OptList<4, RTNETADDRIPV4>       OptTimeServers;
typedef OptList<5, RTNETADDRIPV4>       OptNameServers;
typedef OptList<6, RTNETADDRIPV4>       OptDNSes;
typedef OptList<7, RTNETADDRIPV4>       OptLogServers;
typedef OptList<8, RTNETADDRIPV4>       OptCookieServers;
typedef OptList<9, RTNETADDRIPV4>       OptLPRServers;
typedef OptList<10, RTNETADDRIPV4>      OptImpressServers;
typedef OptList<11, RTNETADDRIPV4>      OptResourceLocationServers;
typedef OptString<12>                   OptHostName;
typedef OptValue<13, uint16_t>          OptBootFileSize;
typedef OptString<14>                   OptMeritDumpFile;
typedef OptString<15>                   OptDomainName;
typedef OptValue<16, RTNETADDRIPV4>     OptSwapServer;
typedef OptString<17>                   OptRootPath;
typedef OptString<18>                   OptExtensionPath;
typedef OptValue<19, bool>              OptIPForwarding;
typedef OptValue<20, bool>              OptNonLocalSourceRouting;
typedef OptList<21, DhcpIpv4AddrAndMask> OptPolicyFilter;
typedef OptValue<22, uint16_t>          OptMaxDgramReassemblySize;
typedef OptValue<23, uint16_t>          OptDefaultIPTTL;
typedef OptValue<24, uint32_t>          OptPathMTUAgingTimeout;
typedef OptList<25, uint16_t>           OptPathMTUPlateauTable;
typedef OptValue<26, uint16_t>          OptInterfaceMTU;
typedef OptValue<27, bool>              OptAllSubnetsAreLocal;
typedef OptValue<28, RTNETADDRIPV4>     OptBroadcastAddress;
typedef OptValue<29, bool>              OptPerformMaskDiscovery;
typedef OptValue<30, bool>              OptMaskSupplier;
typedef OptValue<31, bool>              OptPerformRouterDiscovery;
typedef OptValue<32, RTNETADDRIPV4>     OptRouterSolicitationAddress;
typedef OptPairList<33, RTNETADDRIPV4>  OptStaticRoute;
typedef OptValue<34, bool>              OptTrailerEncapsulation;
typedef OptValue<35, uint32_t>          OptARPCacheTimeout;
typedef OptValue<36, bool>              OptEthernetEncapsulation;
typedef OptValue<37, uint8_t>           OptTCPDefaultTTL;
typedef OptValue<38, uint32_t>          OptTCPKeepaliveInterval;
typedef OptValue<39, bool>              OptTCPKeepaliveGarbage;
typedef OptString<40>                   OptNISDomain;
typedef OptList<41, RTNETADDRIPV4>      OptNISServers;
typedef OptList<42, RTNETADDRIPV4>      OptNTPServers;
/* DHCP related options: */
typedef OptList<43, uint8_t>            OptVendorSpecificInfo;
typedef OptList<44, RTNETADDRIPV4>      OptNetBIOSNameServers;
typedef OptList<45, RTNETADDRIPV4>      OptNetBIOSDatagramServers;
typedef OptValue<46, uint8_t>           OptNetBIOSNodeType;
typedef OptList<47, uint8_t>            OptNetBIOSScope;            /**< uint8_t or string? */
typedef OptList<48, RTNETADDRIPV4>      OptXWindowsFontServers;
typedef OptList<49, RTNETADDRIPV4>      OptXWindowsDisplayManager;
typedef OptValue<50, RTNETADDRIPV4>     OptRequestedAddress;
typedef OptValue<51, uint32_t>          OptLeaseTime;
/* 52 - option overload is syntactic and handled internally */
typedef OptValue<53, uint8_t>           OptMessageType;
typedef OptValue<54, RTNETADDRIPV4>     OptServerId;
typedef OptList<55, uint8_t>            OptParameterRequest;
typedef OptString<56>                   OptMessage;
typedef OptValue<57, uint16_t>          OptMaxDHCPMessageSize;
typedef OptValue<58, uint32_t>          OptRenewalTime;
typedef OptValue<59, uint32_t>          OptRebindingTime;
typedef OptList<60, uint8_t>            OptVendorClassId;
typedef OptList<61, uint8_t>            OptClientId;
typedef OptString<62>                   OptNetWareIPDomainName;     /**< RFC2242 */
typedef OptList<63, uint8_t>            OptNetWareIPInformation;    /**< complicated, so just byte list for now. RFC2242 */
typedef OptString<64>                   OptNISPlusDomain;
typedef OptString<65>                   OptNISPlusServers;
typedef OptString<66>                   OptTFTPServerName;          /**< when overloaded */
typedef OptString<67>                   OptBootfileName;            /**< when overloaded */
typedef OptList<68, RTNETADDRIPV4>      OptMobileIPHomeAgents;
typedef OptList<69, RTNETADDRIPV4>      OptSMTPServers;
typedef OptList<70, RTNETADDRIPV4>      OptPOP3Servers;
typedef OptList<71, RTNETADDRIPV4>      OptNNTPServers;
typedef OptList<72, RTNETADDRIPV4>      OptWWWServers;
typedef OptList<73, RTNETADDRIPV4>      OptFingerServers;
typedef OptList<74, RTNETADDRIPV4>      OptIRCServers;
typedef OptList<75, RTNETADDRIPV4>      OptStreetTalkServers;
typedef OptList<76, RTNETADDRIPV4>      OptSTDAServers;
typedef OptList<77, uint8_t>            OptUserClassId;
typedef OptList<78, uint8_t>            OptSLPDirectoryAgent;       /**< complicated, so just byte list for now. RFC2610 */
typedef OptList<79, uint8_t>            OptSLPServiceScope;         /**< complicated, so just byte list for now. RFC2610 */
typedef OptNoValue<80>                  OptRapidCommit;             /**< RFC4039 */
typedef OptList<119, uint8_t>           OptDomainSearch;            /**< RFC3397 */
/** @} */

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_DhcpOptions_h */
