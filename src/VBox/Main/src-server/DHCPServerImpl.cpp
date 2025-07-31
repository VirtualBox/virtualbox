/* $Id: DHCPServerImpl.cpp 110486 2025-07-30 17:39:08Z alexander.eichner@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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
#define LOG_GROUP LOG_GROUP_MAIN_DHCPSERVER
#include "DHCPServerImpl.h"
#include "LoggingNew.h"

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/net.h>
#include <iprt/path.h>
#include <iprt/cpp/path.h>
#include <iprt/cpp/utils.h>
#include <iprt/cpp/xml.h>

#include <VBox/com/array.h>
#include <VBox/settings.h>

#include "AutoCaller.h"
#include "DHCPConfigImpl.h"
#include "MachineImpl.h"
#include "NetworkServiceRunner.h"
#include "VirtualBoxImpl.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# define DHCP_EXECUTABLE_NAME "VBoxNetDHCP.exe"
#else
# define DHCP_EXECUTABLE_NAME "VBoxNetDHCP"
#endif


/**
 * DHCP server specialization of NetworkServiceRunner.
 *
 * Just defines the executable name and adds option constants.
 */
class DHCPServerRunner : public NetworkServiceRunner
{
public:
    DHCPServerRunner() : NetworkServiceRunner(DHCP_EXECUTABLE_NAME)
    {}
    virtual ~DHCPServerRunner()
    {}
};


/**
 * Hidden private data of the DHCPServer class.
 */
struct DHCPServer::Data
{
    Data()
        : pVirtualBox(NULL)
        , strName()
        , enabled(FALSE)
        , uIndividualMACAddressVersion(1)
    {
    }

    /** weak VirtualBox parent */
    VirtualBox * const  pVirtualBox;
    /** The DHCP server name (network). */
    Utf8Str const       strName;

    Utf8Str IPAddress;
    Utf8Str lowerIP;
    Utf8Str upperIP;

    BOOL enabled;
    DHCPServerRunner dhcp;

    com::Utf8Str strLeasesFilename;
    com::Utf8Str strConfigFilename;
    com::Utf8Str strLogFilename;

    com::Utf8Str trunkName;
    com::Utf8Str trunkType;

    /** Global configuration. */
    ComObjPtr<DHCPGlobalConfig> globalConfig;

    /** Group configuration indexed by name. */
    std::map<com::Utf8Str, ComObjPtr<DHCPGroupConfig> > groupConfigs;
    /** Iterator for groupConfigs. */
    typedef std::map<com::Utf8Str, ComObjPtr<DHCPGroupConfig> >::iterator GroupConfigIterator;

    /** Individual (host) configuration indexed by MAC address or VM UUID. */
    std::map<com::Utf8Str, ComObjPtr<DHCPIndividualConfig> > individualConfigs;
    /** Iterator for individualConfigs. */
    typedef std::map<com::Utf8Str, ComObjPtr<DHCPIndividualConfig> >::iterator IndividualConfigIterator;

    /** Part of a lock-avoidance hack to resolve the VM ID + slot into MAC
     *  addresses before writing out the Dhcpd configuration file. */
    uint32_t uIndividualMACAddressVersion;
};


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////


DHCPServer::DHCPServer()
    : m(NULL)
{
    m = new DHCPServer::Data();
}


DHCPServer::~DHCPServer()
{
    if (m)
    {
        delete m;
        m = NULL;
    }
}


HRESULT DHCPServer::FinalConstruct()
{
    return BaseFinalConstruct();
}


void DHCPServer::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


void DHCPServer::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    if (m->dhcp.isRunning())
        stop();

    unconst(m->pVirtualBox) = NULL;
}


HRESULT DHCPServer::init(VirtualBox *aVirtualBox, const Utf8Str &aName)
{
    AssertReturn(!aName.isEmpty(), E_INVALIDARG);

    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* share VirtualBox weakly (parent remains NULL so far) */
    unconst(m->pVirtualBox) = aVirtualBox;

    unconst(m->strName) = aName;
    m->IPAddress = "0.0.0.0";
    m->lowerIP   = "0.0.0.0";
    m->upperIP   = "0.0.0.0";
    m->enabled   = FALSE;

    /* Global configuration: */
    HRESULT hrc = m->globalConfig.createObject();
    if (SUCCEEDED(hrc))
        hrc = m->globalConfig->initWithDefaults(aVirtualBox, this);

    Assert(m->groupConfigs.size() == 0);
    Assert(m->individualConfigs.size() == 0);

    /* Confirm a successful initialization or not: */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed(hrc);
    return hrc;
}


HRESULT DHCPServer::init(VirtualBox *aVirtualBox, const settings::DHCPServer &rData)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* share VirtualBox weakly (parent remains NULL so far) */
    unconst(m->pVirtualBox) = aVirtualBox;

    unconst(m->strName) = rData.strNetworkName;
    m->IPAddress        = rData.strIPAddress;
    m->enabled          = rData.fEnabled;
    m->lowerIP          = rData.strIPLower;
    m->upperIP          = rData.strIPUpper;

    /*
     * Global configuration:
     */
    HRESULT hrc = m->globalConfig.createObject();
    if (SUCCEEDED(hrc))
        hrc = m->globalConfig->initWithSettings(aVirtualBox, this, rData.globalConfig);

    /*
     * Group configurations:
     */
    Assert(m->groupConfigs.size() == 0);
    for (settings::DHCPGroupConfigVec::const_iterator it = rData.vecGroupConfigs.begin();
         it != rData.vecGroupConfigs.end() && SUCCEEDED(hrc); ++it)
    {
        ComObjPtr<DHCPGroupConfig> ptrGroupConfig;
        hrc = ptrGroupConfig.createObject();
        if (SUCCEEDED(hrc))
            hrc = ptrGroupConfig->initWithSettings(aVirtualBox, this, *it);
        if (SUCCEEDED(hrc))
        {
            try
            {
                m->groupConfigs[it->strName] = ptrGroupConfig;
            }
            catch (std::bad_alloc &)
            {
                return E_OUTOFMEMORY;
            }
        }
    }

    /*
     * Individual configuration:
     */
    Assert(m->individualConfigs.size() == 0);
    for (settings::DHCPIndividualConfigMap::const_iterator it = rData.mapIndividualConfigs.begin();
         it != rData.mapIndividualConfigs.end() && SUCCEEDED(hrc); ++it)
    {
        ComObjPtr<DHCPIndividualConfig> ptrIndiCfg;
        com::Utf8Str                    strKey;
        if (it->second.strVMName.isEmpty())
        {
            RTMAC MACAddress;
            int vrc = RTNetStrToMacAddr(it->second.strMACAddress.c_str(), &MACAddress);
            if (RT_FAILURE(vrc))
            {
                LogRel(("Ignoring invalid MAC address for individual DHCP config: '%s' - %Rrc\n", it->second.strMACAddress.c_str(), vrc));
                continue;
            }

            vrc = strKey.printfNoThrow("%RTmac", &MACAddress);
            AssertRCReturn(vrc, E_OUTOFMEMORY);

            hrc = ptrIndiCfg.createObject();
            if (SUCCEEDED(hrc))
                hrc = ptrIndiCfg->initWithSettingsAndMACAddress(aVirtualBox, this, it->second, &MACAddress);
        }
        else
        {
            /* This ASSUMES that we're being called after the machines have been
               loaded so we can resolve VM names into UUID for old settings. */
            com::Guid idMachine;
            hrc = i_vmNameToIdAndValidateSlot(it->second.strVMName, it->second.uSlot, idMachine);
            if (SUCCEEDED(hrc))
            {
                int vrc = strKey.printfNoThrow("%RTuuid/%u", idMachine.raw(), it->second.uSlot);
                AssertRCReturn(vrc, E_OUTOFMEMORY);

                hrc = ptrIndiCfg.createObject();
                if (SUCCEEDED(hrc))
                    hrc = ptrIndiCfg->initWithSettingsAndMachineIdAndSlot(aVirtualBox, this, it->second,
                                                                          idMachine, it->second.uSlot,
                                                                          m->uIndividualMACAddressVersion - UINT32_MAX / 4);
            }
        }
        if (SUCCEEDED(hrc))
        {
            try
            {
                m->individualConfigs[strKey] = ptrIndiCfg;
            }
            catch (std::bad_alloc &)
            {
                return E_OUTOFMEMORY;
            }
        }
    }

    /* Confirm a successful initialization or not: */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed(hrc);
    return hrc;
}


/**
 * Called by VirtualBox to save our settings.
 */
HRESULT DHCPServer::i_saveSettings(settings::DHCPServer &rData)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    rData.strNetworkName = m->strName;
    rData.strIPAddress   = m->IPAddress;
    rData.fEnabled       = m->enabled != FALSE;
    rData.strIPLower     = m->lowerIP;
    rData.strIPUpper     = m->upperIP;

    /* Global configuration: */
    HRESULT hrc = m->globalConfig->i_saveSettings(rData.globalConfig);

    /* Group configuration: */
    size_t const cGroupConfigs = m->groupConfigs.size();
    try
    {
        rData.vecGroupConfigs.resize(cGroupConfigs);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    size_t i = 0;
    for (Data::GroupConfigIterator it = m->groupConfigs.begin(); it != m->groupConfigs.end() && SUCCEEDED(hrc); ++it, i++)
    {
        try
        {
            rData.vecGroupConfigs[i] = settings::DHCPGroupConfig();
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        hrc = it->second->i_saveSettings(rData.vecGroupConfigs[i]);
    }

    /* Individual configuration: */
    for (Data::IndividualConfigIterator it = m->individualConfigs.begin();
         it != m->individualConfigs.end() && SUCCEEDED(hrc); ++it)
    {
        try
        {
            rData.mapIndividualConfigs[it->first] = settings::DHCPIndividualConfig();
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        hrc = it->second->i_saveSettings(rData.mapIndividualConfigs[it->first]);
    }

    return hrc;
}


HRESULT DHCPServer::i_removeConfig(DHCPConfig *pConfig, DHCPConfigScope_T enmScope)
{
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        bool fFound = false;
        switch (enmScope)
        {
            case DHCPConfigScope_Group:
            {
                for (Data::GroupConfigIterator it = m->groupConfigs.begin(); it != m->groupConfigs.end();)
                {
                    DHCPConfig *pCurConfig = it->second;
                    if (pCurConfig == pConfig)
                    {
                        m->groupConfigs.erase(it++); /* Post increment returns copy of original that is then erased. */
                        fFound = true;
                    }
                    else
                        ++it;
                }
                break;
            }

            case DHCPConfigScope_MAC:
            case DHCPConfigScope_MachineNIC:
            {
                for (Data::IndividualConfigIterator it = m->individualConfigs.begin(); it != m->individualConfigs.end();)
                {
                    DHCPConfig *pCurConfig = it->second;
                    if (pCurConfig == pConfig)
                    {
                        m->individualConfigs.erase(it++); /* Post increment returns copy of original that is then erased. */
                        fFound = true;
                    }
                    else
                        ++it;
                }
                break;
            }

            default:
                AssertFailedReturn(E_FAIL);
        }

        /* Don't complain if already removed, right? */
        if (!fFound)
            return S_OK;
    }

    return i_doSaveSettings();
}


/**
 * Internal worker that saves the settings after a modification was made.
 *
 * @returns COM status code.
 *
 * @note    Caller must not hold any locks!
 */
HRESULT DHCPServer::i_doSaveSettings()
{
    // save the global settings; for that we should hold only the VirtualBox lock
    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    return m->pVirtualBox->i_saveSettings();
}


HRESULT DHCPServer::getNetworkName(com::Utf8Str &aName)
{
    /* The name is const, so no need to for locking. */
    return aName.assignEx(m->strName);
}


HRESULT DHCPServer::getEnabled(BOOL *aEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aEnabled = m->enabled;
    return S_OK;
}


HRESULT DHCPServer::setEnabled(BOOL aEnabled)
{
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        m->enabled = aEnabled;
    }
    return i_doSaveSettings();
}


HRESULT DHCPServer::getIPAddress(com::Utf8Str &aIPAddress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return aIPAddress.assignEx(m->IPAddress);
}


HRESULT DHCPServer::getNetworkMask(com::Utf8Str &aNetworkMask)
{
    return m->globalConfig->i_getNetworkMask(aNetworkMask);
}


HRESULT DHCPServer::getLowerIP(com::Utf8Str &aIPAddress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return aIPAddress.assignEx(m->lowerIP);
}


HRESULT DHCPServer::getUpperIP(com::Utf8Str &aIPAddress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return aIPAddress.assignEx(m->upperIP);
}


HRESULT DHCPServer::setConfiguration(const com::Utf8Str &aIPAddress,
                                     const com::Utf8Str &aNetworkMask,
                                     const com::Utf8Str &aLowerIP,
                                     const com::Utf8Str &aUpperIP)
{
    RTNETADDRIPV4 IPAddress, NetworkMask, LowerIP, UpperIP;

    int vrc = RTNetStrToIPv4Addr(aIPAddress.c_str(), &IPAddress);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid server address: %s"), aIPAddress.c_str());

    vrc = RTNetStrToIPv4Addr(aNetworkMask.c_str(), &NetworkMask);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid netmask: %s"), aNetworkMask.c_str());

    vrc = RTNetStrToIPv4Addr(aLowerIP.c_str(), &LowerIP);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid range lower address: %s"), aLowerIP.c_str());

    vrc = RTNetStrToIPv4Addr(aUpperIP.c_str(), &UpperIP);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid range upper address: %s"), aUpperIP.c_str());

    /*
     * Insist on continuous mask.  May be also accept prefix length
     * here or address/prefix for aIPAddress?
     */
    vrc = RTNetMaskToPrefixIPv4(&NetworkMask, NULL);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid netmask: %s"), aNetworkMask.c_str());

    /* It's more convenient to convert to host order once: */
    IPAddress.u   = RT_N2H_U32(IPAddress.u);
    NetworkMask.u = RT_N2H_U32(NetworkMask.u);
    LowerIP.u     = RT_N2H_U32(LowerIP.u);
    UpperIP.u     = RT_N2H_U32(UpperIP.u);

    /*
     * Addresses must be unicast and from the same network
     */
    if (   (IPAddress.u & UINT32_C(0xe0000000)) == UINT32_C(0xe0000000)
        || (IPAddress.u & ~NetworkMask.u) == 0
        || ((IPAddress.u & ~NetworkMask.u) | NetworkMask.u) == UINT32_C(0xffffffff))
        return setError(E_INVALIDARG, tr("Invalid server address: %s (mask %s)"), aIPAddress.c_str(), aNetworkMask.c_str());

    if (   (LowerIP.u & UINT32_C(0xe0000000)) == UINT32_C(0xe0000000)
        || (LowerIP.u & NetworkMask.u) != (IPAddress.u &NetworkMask.u)
        || (LowerIP.u & ~NetworkMask.u) == 0
        || ((LowerIP.u & ~NetworkMask.u) | NetworkMask.u) == UINT32_C(0xffffffff))
        return setError(E_INVALIDARG, tr("Invalid range lower address: %s (mask %s)"), aLowerIP.c_str(), aNetworkMask.c_str());

    if (   (UpperIP.u & UINT32_C(0xe0000000)) == UINT32_C(0xe0000000)
        || (UpperIP.u & NetworkMask.u) != (IPAddress.u &NetworkMask.u)
        || (UpperIP.u & ~NetworkMask.u) == 0
        || ((UpperIP.u & ~NetworkMask.u) | NetworkMask.u) == UINT32_C(0xffffffff))
        return setError(E_INVALIDARG, tr("Invalid range upper address"), aUpperIP.c_str(), aNetworkMask.c_str());

    /* The range should be valid. (It's okay to overlap the server IP.) */
    if (LowerIP.u > UpperIP.u)
        return setError(E_INVALIDARG, tr("Lower bound must be less or eqaul than the upper: %s vs %s"),
                        aLowerIP.c_str(), aUpperIP.c_str());

    /*
     * Input is valid, effect the changes.
     */
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        m->IPAddress  = aIPAddress;
        m->lowerIP    = aLowerIP;
        m->upperIP    = aUpperIP;
    }
    HRESULT hrc = m->globalConfig->i_setNetworkMask(aNetworkMask);
    if (SUCCEEDED(hrc))
        hrc = i_doSaveSettings();
    return hrc;
}


/**
 * Validates the VM name and slot, returning the machine ID.
 *
 * If a machine ID is given instead of a name, we won't check whether it
 * actually exists...
 *
 * @returns COM status code.
 * @param   aVmName             The VM name or UUID.
 * @param   a_uSlot             The slot.
 * @param   idMachine           Where to return the VM UUID.
 */
HRESULT DHCPServer::i_vmNameToIdAndValidateSlot(const com::Utf8Str &aVmName, ULONG a_uSlot, com::Guid &idMachine)
{
    if (a_uSlot <= 32)
    {
        /* Is it a UUID? */
        idMachine = aVmName;
        if (idMachine.isValid() && !idMachine.isZero())
            return S_OK;

        /* No, find the VM and get it's UUID. */
        ComObjPtr<Machine> ptrMachine;
        HRESULT hrc = m->pVirtualBox->i_findMachineByName(aVmName, true /*aSetError*/, &ptrMachine);
        if (SUCCEEDED(hrc))
            idMachine = ptrMachine->i_getId();
        return hrc;
    }
    return setError(E_INVALIDARG, tr("NIC slot number (%d) is out of range (0..32)"), a_uSlot);
}


/**
 * Translates a VM name/id and slot to an individual configuration object.
 *
 * @returns COM status code.
 * @param   a_strVmName         The VM name or ID.
 * @param   a_uSlot             The NIC slot.
 * @param   a_fCreateIfNeeded   Whether to create a new entry if not found.
 * @param   a_rPtrConfig        Where to return the config object.  It's
 *                              implicitly referenced, so we don't be returning
 *                              with any locks held.
 *
 * @note    Caller must not be holding any locks!
 */
HRESULT DHCPServer::i_vmNameAndSlotToConfig(const com::Utf8Str &a_strVmName, ULONG a_uSlot, bool a_fCreateIfNeeded,
                                            ComObjPtr<DHCPIndividualConfig> &a_rPtrConfig)
{
    /*
     * Validate the slot and normalize the name into a UUID.
     */
    com::Guid idMachine;
    HRESULT hrc = i_vmNameToIdAndValidateSlot(a_strVmName, a_uSlot, idMachine);
    if (SUCCEEDED(hrc))
    {
        Utf8Str strKey;
        int vrc = strKey.printfNoThrow("%RTuuid/%u", idMachine.raw(), a_uSlot);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Look it up.
             */
            {
                AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
                Data::IndividualConfigIterator it = m->individualConfigs.find(strKey);
                if (it != m->individualConfigs.end())
                {
                    a_rPtrConfig = it->second;
                    return S_OK;
                }
            }
            if (a_fCreateIfNeeded)
            {
                /*
                 * Create a new slot.
                 */
                /* Instantiate the object: */
                hrc = a_rPtrConfig.createObject();
                if (SUCCEEDED(hrc))
                    hrc = a_rPtrConfig->initWithMachineIdAndSlot(m->pVirtualBox, this, idMachine, a_uSlot,
                                                                 m->uIndividualMACAddressVersion - UINT32_MAX / 4);
                if (SUCCEEDED(hrc))
                {
                    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

                    /* Check for creation race: */
                    Data::IndividualConfigIterator it = m->individualConfigs.find(strKey);
                    if (it != m->individualConfigs.end())
                    {
                        a_rPtrConfig.setNull();
                        a_rPtrConfig = it->second;
                        return S_OK;
                    }

                    /* Add it. */
                    try
                    {
                        m->individualConfigs[strKey] = a_rPtrConfig;

                        /* Save settings. */
                        alock.release();
                        return i_doSaveSettings();
                    }
                    catch (std::bad_alloc &)
                    {
                        hrc = E_OUTOFMEMORY;
                    }
                    a_rPtrConfig.setNull();
                }
            }
            else
                hrc = VBOX_E_OBJECT_NOT_FOUND;
        }
        else
            hrc = E_OUTOFMEMORY;
    }
    return hrc;
}


HRESULT DHCPServer::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    NOREF(aEventSource);
    ReturnComNotImplemented();
}


HRESULT DHCPServer::getGlobalConfig(ComPtr<IDHCPGlobalConfig> &aGlobalConfig)
{
    /* The global configuration is immutable, so no need to lock anything here. */
    return m->globalConfig.queryInterfaceTo(aGlobalConfig.asOutParam());
}


HRESULT DHCPServer::getGroupConfigs(std::vector<ComPtr<IDHCPGroupConfig> > &aGroupConfigs)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    size_t const cGroupConfigs = m->groupConfigs.size();
    try
    {
        aGroupConfigs.resize(cGroupConfigs);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    size_t i = 0;
    for (Data::GroupConfigIterator it = m->groupConfigs.begin(); it != m->groupConfigs.end(); ++it, i++)
    {
        Assert(i < cGroupConfigs);
        HRESULT hrc = it->second.queryInterfaceTo(aGroupConfigs[i].asOutParam());
        if (FAILED(hrc))
            return hrc;
    }

    return S_OK;
}


HRESULT DHCPServer::getIndividualConfigs(std::vector<ComPtr<IDHCPIndividualConfig> > &aIndividualConfigs)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    size_t const cIndividualConfigs = m->individualConfigs.size();
    try
    {
        aIndividualConfigs.resize(cIndividualConfigs);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    size_t i = 0;
    for (Data::IndividualConfigIterator it = m->individualConfigs.begin(); it != m->individualConfigs.end(); ++it, i++)
    {
        Assert(i < cIndividualConfigs);
        HRESULT hrc = it->second.queryInterfaceTo(aIndividualConfigs[i].asOutParam());
        if (FAILED(hrc))
            return hrc;
    }

    return S_OK;
}


HRESULT DHCPServer::restart()
{
    if (!m->dhcp.isRunning())
        return setErrorBoth(E_FAIL, VERR_PROCESS_NOT_FOUND, tr("not running"));

    /*
     * Disabled servers will be brought down, but won't be restarted.
     * (see DHCPServer::start)
     */
    HRESULT hrc = stop();
    if (SUCCEEDED(hrc))
        hrc = start(m->trunkName, m->trunkType);
    return hrc;
}


/**
 * @throws std::bad_alloc
 */
HRESULT DHCPServer::i_writeDhcpdConfig(const char *pszFilename, uint32_t uMACAddressVersion) RT_NOEXCEPT
{
    /*
     * Produce the DHCP server configuration.
     */
    xml::Document doc;
    try
    {
        xml::ElementNode *pElmRoot = doc.createRootElement("DHCPServer");
        pElmRoot->setAttribute("networkName", m->strName);
        if (m->trunkName.isNotEmpty())
            pElmRoot->setAttribute("trunkName", m->trunkName);
        pElmRoot->setAttribute("trunkType", m->trunkType);
        pElmRoot->setAttribute("IPAddress",  m->IPAddress);
        pElmRoot->setAttribute("lowerIP", m->lowerIP);
        pElmRoot->setAttribute("upperIP", m->upperIP);
        pElmRoot->setAttribute("leasesFilename", m->strLeasesFilename);
        Utf8Str strNetworkMask;
        HRESULT hrc = m->globalConfig->i_getNetworkMask(strNetworkMask);
        if (FAILED(hrc))
            return hrc;
        pElmRoot->setAttribute("networkMask", strNetworkMask);

        /*
         * Process global options
         */
        m->globalConfig->i_writeDhcpdConfig(pElmRoot->createChild("Options"));

        /*
         * Groups.
         */
        for (Data::GroupConfigIterator it = m->groupConfigs.begin(); it != m->groupConfigs.end(); ++it)
            it->second->i_writeDhcpdConfig(pElmRoot->createChild("Group"));

        /*
         * Individual NIC configurations.
         */
        for (Data::IndividualConfigIterator it = m->individualConfigs.begin(); it != m->individualConfigs.end(); ++it)
            if (it->second->i_isMACAddressResolved(uMACAddressVersion))
                it->second->i_writeDhcpdConfig(pElmRoot->createChild("Config"));
            else
                LogRelFunc(("Skipping %RTuuid/%u, no MAC address.\n", it->second->i_getMachineId().raw(), it->second->i_getSlot()));
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Write out the document.
     */
    try
    {
        xml::XmlFileWriter writer(doc);
        writer.write(pszFilename, false);
    }
    catch (...)
    {
        return E_FAIL;
    }

    return S_OK;
}


HRESULT DHCPServer::start(const com::Utf8Str &aTrunkName, const com::Utf8Str &aTrunkType)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Silently ignore attempts to run disabled servers. */
    if (!m->enabled)
        return S_OK;

    /*
     * Resolve the MAC addresses.  This requires us to leave the lock.
     */
    uint32_t uMACAddressVersion = m->uIndividualMACAddressVersion;
    if (m->individualConfigs.size() > 0)
    {
        m->uIndividualMACAddressVersion = uMACAddressVersion + 1;

        /* Retain pointers to all the individual configuration objects so we
           can safely access these after releaseing the lock: */
        std::vector< ComObjPtr<DHCPIndividualConfig> > vecIndividualConfigs;
        try
        {
            vecIndividualConfigs.resize(m->individualConfigs.size());
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        size_t i = 0;
        for (Data::IndividualConfigIterator it = m->individualConfigs.begin(); it != m->individualConfigs.end(); ++it, i++)
            vecIndividualConfigs[i] = it->second;

        /* Drop the lock and resolve the MAC addresses: */
        alock.release();

        i = vecIndividualConfigs.size();
        while (i-- > 0)
            vecIndividualConfigs[i]->i_resolveMACAddress(uMACAddressVersion);

        /* Reacquire the lock  */
        alock.acquire();
        if (!m->enabled)
            return S_OK;
    }

    /*
     * Refuse to start a 2nd DHCP server instance for the same network.
     */
    if (m->dhcp.isRunning())
        return setErrorBoth(VBOX_E_OBJECT_IN_USE, VERR_PROCESS_RUNNING,
                            tr("Cannot start DHCP server because it is already running (pid %RTproc)"), m->dhcp.getPid());

    /*
     * Copy the startup parameters.
     */
    m->trunkName   = aTrunkName;
    m->trunkType   = aTrunkType;
    HRESULT hrc = i_calcLeasesConfigAndLogFilenames(m->strName);
    if (SUCCEEDED(hrc))
    {
        /*
         * Create configuration file path and write out the configuration.
         */
        hrc = i_writeDhcpdConfig(m->strConfigFilename.c_str(), uMACAddressVersion);
        if (SUCCEEDED(hrc))
        {
            /*
             * Setup the arguments and start the DHCP server.
             */
            m->dhcp.resetArguments();
            int vrc = m->dhcp.addArgPair("--comment", m->strName.c_str());
            if (RT_SUCCESS(vrc))
                vrc = m->dhcp.addArgPair("--config", m->strConfigFilename.c_str());
            if (RT_SUCCESS(vrc))
                vrc = m->dhcp.addArgPair("--log", m->strLogFilename.c_str());
            /** @todo Add --log-flags, --log-group-settings, and --log-destinations with
             *        associated IDHCPServer attributes.  (Not doing it now because that'll
             *        exhaust all reserved attribute slot in 6.0.) */
            if (RT_SUCCESS(vrc))
            {
                /* Start it: */
                vrc = m->dhcp.start(true /*aKillProcessOnStop*/);
                if (RT_FAILURE(vrc))
                    hrc = setErrorVrc(vrc, tr("Failed to start DHCP server for '%s': %Rrc"), m->strName.c_str(), vrc);
            }
            else
                hrc = setErrorVrc(vrc, tr("Failed to assemble the command line for DHCP server '%s': %Rrc"),
                                  m->strName.c_str(), vrc);
        }
    }
    return hrc;
}


HRESULT DHCPServer::stop(void)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = m->dhcp.stop();
    if (RT_SUCCESS(vrc))
        return S_OK;
    return setErrorVrc(vrc);
}


HRESULT DHCPServer::findLeaseByMAC(const com::Utf8Str &aMac, LONG aType,
                                    com::Utf8Str &aAddress, com::Utf8Str &aState, LONG64 *aIssued, LONG64 *aExpire)
{
    /* Reset output before we start */
    *aIssued = 0;
    *aExpire = 0;
    aAddress.setNull();
    aState.setNull();

    /*
     * Convert and check input.
     */
    RTMAC MacAddress;
    int vrc = RTStrConvertHexBytes(aMac.c_str(), &MacAddress, sizeof(MacAddress), RTSTRCONVERTHEXBYTES_F_SEP_COLON);
    if (vrc != VINF_SUCCESS)
        return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid MAC address '%s': %Rrc"), aMac.c_str(), vrc);
    if (aType != 0)
        return setError(E_INVALIDARG, tr("flags must be zero (not %#x)"), aType);

    /*
     * Make sure we've got a lease filename to work with.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (m->strLeasesFilename.isEmpty())
    {
        HRESULT hrc = i_calcLeasesConfigAndLogFilenames(m->strName);
        if (FAILED(hrc))
            return hrc;
    }

    /*
     * Try at least twice to read the lease database, more if busy.
     */
    uint64_t const nsStart = RTTimeNanoTS();
    for (uint32_t uReadAttempt = 0; ; uReadAttempt++)
    {
        /*
         * Try read the file.
         */
        xml::Document doc;
        try
        {
            xml::XmlFileParser parser;
            parser.read(m->strLeasesFilename.c_str(), doc);
        }
        catch (const xml::EIPRTFailure &e)
        {
            vrc = e.getStatus();
            LogThisFunc(("caught xml::EIPRTFailure: rc=%Rrc (attempt %u, msg=%s)\n", vrc, uReadAttempt, e.what()));
            if (   (   vrc == VERR_FILE_NOT_FOUND
                    || vrc == VERR_OPEN_FAILED
                    || vrc == VERR_ACCESS_DENIED
                    || vrc == VERR_SHARING_VIOLATION
                    || vrc == VERR_READ_ERROR /*?*/)
                && (   uReadAttempt == 0
                    || (   uReadAttempt < 64
                        && RTTimeNanoTS() - nsStart < RT_NS_1SEC / 4)) )
            {
                alock.release();

                if (uReadAttempt > 0)
                    RTThreadYield();
                RTThreadSleep(8/*ms*/);

                alock.acquire();
                LogThisFunc(("Retrying...\n"));
                continue;
            }
            return setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Reading '%s' failed: %Rrc - %s"),
                                m->strLeasesFilename.c_str(), vrc, e.what());
        }
        catch (const RTCError &e)
        {
            if (e.what())
                return setError(VBOX_E_FILE_ERROR, tr("Reading '%s' failed: %s"), m->strLeasesFilename.c_str(), e.what());
            return setError(VBOX_E_FILE_ERROR, tr("Reading '%s' failed: RTCError"), m->strLeasesFilename.c_str());
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        catch (...)
        {
            AssertFailed();
            return setError(VBOX_E_FILE_ERROR, tr("Reading '%s' failed: Unexpected exception"), m->strLeasesFilename.c_str());
        }

        /*
         * Look for that mac address.
         */
        xml::ElementNode *pElmRoot = doc.getRootElement();
        if (pElmRoot && pElmRoot->nameEquals("Leases"))
        {
            xml::NodesLoop          it(*pElmRoot);
            const xml::ElementNode *pElmLease;
            while ((pElmLease = it.forAllNodes()) != NULL)
                if (pElmLease->nameEquals("Lease"))
                {
                    const char *pszCurMacAddress = pElmLease->findAttributeValue("mac");
                    RTMAC       CurMacAddress;
                    if (   pszCurMacAddress
                        && RT_SUCCESS(RTNetStrToMacAddr(pszCurMacAddress, &CurMacAddress))
                        && memcmp(&CurMacAddress, &MacAddress, sizeof(MacAddress)) == 0)
                    {
                        /*
                         * Found it!
                         */
                        xml::ElementNode const *pElmTime    = pElmLease->findChildElement("Time");
                        int64_t                 secIssued   = 0;
                        uint32_t                cSecsToLive = 0;
                        if (pElmTime)
                        {
                            pElmTime->getAttributeValue("issued", &secIssued);
                            pElmTime->getAttributeValue("expiration", &cSecsToLive);
                            *aIssued = secIssued;
                            *aExpire = secIssued + cSecsToLive;
                        }
                        try
                        {
                            aAddress = pElmLease->findChildElementAttributeValue("Address", "value");
                            aState   = pElmLease->findAttributeValue("state");
                        }
                        catch (std::bad_alloc &)
                        {
                            return E_OUTOFMEMORY;
                        }

                        /* Check if the lease has expired in the mean time. */
                        HRESULT hrc = S_OK;
                        RTTIMESPEC Now;
                        if (   (aState.equals("acked") || aState.equals("offered") || aState.isEmpty())
                            && secIssued + cSecsToLive < RTTimeSpecGetSeconds(RTTimeNow(&Now)))
                            hrc = RT_SUCCESS(aState.assignNoThrow("expired")) ? S_OK : E_OUTOFMEMORY;
                        return hrc;
                    }
                }
        }
        break;
    }

    return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Could not find a lease for %RTmac"), &MacAddress);
}


HRESULT DHCPServer::getConfig(DHCPConfigScope_T aScope, const com::Utf8Str &aName, ULONG aSlot, BOOL aMayAdd,
                              ComPtr<IDHCPConfig> &aConfig)
{
    if (aSlot != 0 && aScope != DHCPConfigScope_MachineNIC)
        return setError(E_INVALIDARG, tr("The 'slot' argument must be zero for all but the MachineNIC scope!"));

    switch (aScope)
    {
        case DHCPConfigScope_Global:
            if (aName.isNotEmpty())
                return setError(E_INVALIDARG, tr("The name must be empty or NULL for the Global scope!"));

            /* No locking required here. */
            return m->globalConfig.queryInterfaceTo(aConfig.asOutParam());

        case DHCPConfigScope_Group:
        {
            if (aName.isEmpty())
                return setError(E_INVALIDARG, tr("A group must have a name!"));
            if (aName.length() > _1K)
                return setError(E_INVALIDARG, tr("Name too long! %zu bytes", "", aName.length()), aName.length());

            /* Look up the group: */
            {
                AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
                Data::GroupConfigIterator it = m->groupConfigs.find(aName);
                if (it != m->groupConfigs.end())
                    return it->second.queryInterfaceTo(aConfig.asOutParam());
            }
            /* Create a new group if we can. */
            if (!aMayAdd)
                return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Found no configuration for group %s"), aName.c_str());
            ComObjPtr<DHCPGroupConfig> ptrGroupConfig;
            HRESULT hrc = ptrGroupConfig.createObject();
            if (SUCCEEDED(hrc))
                hrc = ptrGroupConfig->initWithDefaults(m->pVirtualBox, this, aName);
            if (SUCCEEDED(hrc))
            {
                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

                /* Check for insertion race: */
                Data::GroupConfigIterator it = m->groupConfigs.find(aName);
                if (it != m->groupConfigs.end())
                    return it->second.queryInterfaceTo(aConfig.asOutParam()); /* creation race*/

                /* Try insert it: */
                try
                {
                    m->groupConfigs[aName] = ptrGroupConfig;
                }
                catch (std::bad_alloc &)
                {
                    return E_OUTOFMEMORY;
                }
                return ptrGroupConfig.queryInterfaceTo(aConfig.asOutParam());
            }
            return hrc;
        }

        case DHCPConfigScope_MachineNIC:
        {
            ComObjPtr<DHCPIndividualConfig> ptrIndividualConfig;
            HRESULT hrc = i_vmNameAndSlotToConfig(aName, aSlot, aMayAdd != FALSE, ptrIndividualConfig);
            if (SUCCEEDED(hrc))
                hrc = ptrIndividualConfig.queryInterfaceTo(aConfig.asOutParam());
            return hrc;
        }

        case DHCPConfigScope_MAC:
        {
            /* Check and Normalize the MAC address into a key: */
            RTMAC MACAddress;
            int vrc = RTNetStrToMacAddr(aName.c_str(), &MACAddress);
            if (RT_SUCCESS(vrc))
            {
                Utf8Str strKey;
                vrc = strKey.printfNoThrow("%RTmac", &MACAddress);
                if (RT_SUCCESS(vrc))
                {
                    /* Look up the MAC address: */
                    {
                        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
                        Data::IndividualConfigIterator it = m->individualConfigs.find(strKey);
                        if (it != m->individualConfigs.end())
                            return it->second.queryInterfaceTo(aConfig.asOutParam());
                    }
                    if (aMayAdd)
                    {
                        /* Create a new individiual configuration: */
                        ComObjPtr<DHCPIndividualConfig> ptrIndividualConfig;
                        HRESULT hrc = ptrIndividualConfig.createObject();
                        if (SUCCEEDED(hrc))
                            hrc = ptrIndividualConfig->initWithMACAddress(m->pVirtualBox, this, &MACAddress);
                        if (SUCCEEDED(hrc))
                        {
                            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

                            /* Check for insertion race: */
                            Data::IndividualConfigIterator it = m->individualConfigs.find(strKey);
                            if (it != m->individualConfigs.end())
                                return it->second.queryInterfaceTo(aConfig.asOutParam()); /* creation race*/

                            /* Try insert it: */
                            try
                            {
                                m->individualConfigs[strKey] = ptrIndividualConfig;
                            }
                            catch (std::bad_alloc &)
                            {
                                return E_OUTOFMEMORY;
                            }
                            return ptrIndividualConfig.queryInterfaceTo(aConfig.asOutParam());
                        }
                    }
                    else
                        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Found no configuration for MAC address %s"), strKey.c_str());
                }
                return E_OUTOFMEMORY;
            }
            return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid MAC address: %s"), aName.c_str());
        }

        default:
            return E_FAIL;
    }
}


/**
 * Calculates and updates the value of strLeasesFilename given @a aNetwork.
 */
HRESULT DHCPServer::i_calcLeasesConfigAndLogFilenames(const com::Utf8Str &aNetwork) RT_NOEXCEPT
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* The lease file must be the same as we used the last time, so careful when changing this code. */
    int vrc = m->strLeasesFilename.assignNoThrow(m->pVirtualBox->i_homeDir());
    if (RT_SUCCESS(vrc))
        vrc = RTPathAppendCxx(m->strLeasesFilename, aNetwork);
    if (RT_SUCCESS(vrc))
    {
        RTPathPurgeFilename(RTPathFilename(m->strLeasesFilename.mutableRaw()), RTPATH_STR_F_STYLE_HOST);

        /* The configuration file: */
        vrc = m->strConfigFilename.assignNoThrow(m->strLeasesFilename);
        if (RT_SUCCESS(vrc))
            vrc = m->strConfigFilename.appendNoThrow("-Dhcpd.config");


        /* The log file: */
        if (RT_SUCCESS(vrc))
        {
            vrc = m->strLogFilename.assignNoThrow(m->strLeasesFilename);
            if (RT_SUCCESS(vrc))
                vrc = m->strLogFilename.appendNoThrow("-Dhcpd.log");

            /* Finally, complete the leases file: */
            if (RT_SUCCESS(vrc))
            {
                vrc = m->strLeasesFilename.appendNoThrow("-Dhcpd.leases");
                if (RT_SUCCESS(vrc))
                {
                    RTPathPurgeFilename(RTPathFilename(m->strLeasesFilename.mutableRaw()), RTPATH_STR_F_STYLE_HOST);
                    m->strLeasesFilename.jolt();
                    return S_OK;
                }
            }
        }
    }
    return setErrorBoth(E_FAIL, vrc, tr("Failed to construct leases, config and log filenames: %Rrc"), vrc);
}

