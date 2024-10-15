/* $Id: CloudGateway.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Implementation of local and cloud gateway management.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_CONSOLE

/* Make sure all the stdint.h macros are included - must come first! */
#ifndef __STDC_LIMIT_MACROS
# define __STDC_LIMIT_MACROS
#endif
#ifndef __STDC_CONSTANT_MACROS
# define __STDC_CONSTANT_MACROS
#endif

#include "LoggingNew.h"
#include "ApplianceImpl.h"
#include "CloudNetworkImpl.h"
#include "CloudGateway.h"

#include <iprt/http.h>
#include <iprt/inifile.h>
#include <iprt/net.h>
#include <iprt/path.h>
#include <iprt/vfs.h>
#include <iprt/uri.h>
#ifdef DEBUG
#include <iprt/file.h>
#include <VBox/com/utils.h>
#endif

#ifdef VBOX_WITH_LIBSSH
/* Prevent inclusion of Winsock2.h */
#define _WINSOCK2API_
#include <libssh/libssh.h>
#endif /* VBOX_WITH_LIBSSH */


static HRESULT setMacAddress(const Utf8Str& str, RTMAC& mac)
{
    int vrc = RTNetStrToMacAddr(str.c_str(), &mac);
    if (RT_FAILURE(vrc))
    {
        LogRel(("CLOUD-NET: Invalid MAC address '%s'\n", str.c_str()));
        return E_INVALIDARG;
    }
    return S_OK;
}


HRESULT GatewayInfo::setCloudMacAddress(const Utf8Str& mac)
{
    return setMacAddress(mac, mCloudMacAddress);
}


HRESULT GatewayInfo::setLocalMacAddress(const Utf8Str& mac)
{
    return setMacAddress(mac, mLocalMacAddress);
}


class CloudError
{
public:
    CloudError(HRESULT hrc, const Utf8Str& strText) : mHrc(hrc), mText(strText) {};
    HRESULT getRc() { return mHrc; };
    Utf8Str getText() { return mText; };

private:
    HRESULT mHrc;
    Utf8Str mText;
};


static void handleErrors(HRESULT hrc, const char *pszFormat, ...)
{
    if (FAILED(hrc))
    {
        va_list va;
        va_start(va, pszFormat);
        Utf8Str strError(pszFormat, va);
        va_end(va);
        LogRel(("CLOUD-NET: %s (hrc=%x)\n", strError.c_str(), hrc));
        throw CloudError(hrc, strError);
    }

}


class CloudClient
{
public:
    CloudClient(ComPtr<IVirtualBox> virtualBox, const Bstr& strProvider, const Bstr& strProfile);
    ~CloudClient() {};

    void startCloudGateway(const ComPtr<ICloudNetwork> &network, GatewayInfo& gateway);
    void stopCloudGateway(const GatewayInfo& gateway);

private:
    ComPtr<ICloudProviderManager> mManager;
    ComPtr<ICloudProvider>        mProvider;
    ComPtr<ICloudProfile>         mProfile;
    ComPtr<ICloudClient>          mClient;
};


CloudClient::CloudClient(ComPtr<IVirtualBox> virtualBox, const Bstr& strProvider, const Bstr& strProfile)
{
    HRESULT hrc = virtualBox->COMGETTER(CloudProviderManager)(mManager.asOutParam());
    handleErrors(hrc, "Failed to obtain cloud provider manager object");
    hrc = mManager->GetProviderByShortName(strProvider.raw(), mProvider.asOutParam());
    handleErrors(hrc, "Failed to obtain cloud provider '%ls'", strProvider.raw());
    hrc = mProvider->GetProfileByName(strProfile.raw(), mProfile.asOutParam());
    handleErrors(hrc, "Failed to obtain cloud profile '%ls'", strProfile.raw());
    hrc = mProfile->CreateCloudClient(mClient.asOutParam());
    handleErrors(hrc, "Failed to create cloud client");
}


void CloudClient::startCloudGateway(const ComPtr<ICloudNetwork> &network, GatewayInfo& gateway)
{
    ComPtr<IProgress> progress;
    ComPtr<ICloudNetworkGatewayInfo> gatewayInfo;
    HRESULT hrc = mClient->StartCloudNetworkGateway(network, Bstr(gateway.mPublicSshKey).raw(),
                                                    gatewayInfo.asOutParam(), progress.asOutParam());
    handleErrors(hrc, "Failed to launch compute instance");
    hrc = progress->WaitForCompletion(-1);
    handleErrors(hrc, "Failed to launch compute instance (wait)");

    Bstr instanceId;
    hrc = gatewayInfo->COMGETTER(InstanceId)(instanceId.asOutParam());
    handleErrors(hrc, "Failed to get launched compute instance id");
    gateway.mGatewayInstanceId = instanceId;

    Bstr publicIP;
    hrc = gatewayInfo->COMGETTER(PublicIP)(publicIP.asOutParam());
    handleErrors(hrc, "Failed to get cloud gateway public IP address");
    gateway.mCloudPublicIp = publicIP;

    Bstr secondaryPublicIP;
    hrc = gatewayInfo->COMGETTER(SecondaryPublicIP)(secondaryPublicIP.asOutParam());
    handleErrors(hrc, "Failed to get cloud gateway secondary public IP address");
    gateway.mCloudSecondaryPublicIp = secondaryPublicIP;

    Bstr macAddress;
    hrc = gatewayInfo->COMGETTER(MacAddress)(macAddress.asOutParam());
    handleErrors(hrc, "Failed to get cloud gateway public IP address");
    gateway.setCloudMacAddress(macAddress);
}


void CloudClient::stopCloudGateway(const GatewayInfo& gateway)
{
    ComPtr<IProgress> progress;
    HRESULT hrc = mClient->TerminateInstance(Bstr(gateway.mGatewayInstanceId).raw(), progress.asOutParam());
    handleErrors(hrc, "Failed to terminate compute instance");
#if 0
    /* Someday we may want to wait until the cloud gateway has terminated. */
    hrc = progress->WaitForCompletion(-1);
    handleErrors(hrc, "Failed to terminate compute instance (wait)");
#endif
}


HRESULT startCloudGateway(ComPtr<IVirtualBox> virtualBox, ComPtr<ICloudNetwork> network, GatewayInfo& gateway)
{
    HRESULT hrc = S_OK;

    try {
        hrc = network->COMGETTER(Provider)(gateway.mCloudProvider.asOutParam());
        hrc = network->COMGETTER(Profile)(gateway.mCloudProfile.asOutParam());
        CloudClient client(virtualBox, gateway.mCloudProvider, gateway.mCloudProfile);
        client.startCloudGateway(network, gateway);
    }
    catch (CloudError e)
    {
        hrc = e.getRc();
    }

    return hrc;
}


HRESULT stopCloudGateway(ComPtr<IVirtualBox> virtualBox, GatewayInfo& gateway)
{
    if (gateway.mGatewayInstanceId.isEmpty())
        return S_OK;

    LogRel(("CLOUD-NET: Terminating cloud gateway instance '%s'...\n", gateway.mGatewayInstanceId.c_str()));

    HRESULT hrc = S_OK;
    try {
        CloudClient client(virtualBox, gateway.mCloudProvider, gateway.mCloudProfile);
        client.stopCloudGateway(gateway);
#if 0
# ifdef DEBUG
        char szKeyPath[RTPATH_MAX];

        int vrc = GetVBoxUserHomeDirectory(szKeyPath, sizeof(szKeyPath), false /* fCreateDir */);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTPathAppend(szKeyPath, sizeof(szKeyPath), "gateway-key.pem");
            AssertRCReturn(vrc, vrc);
            vrc = RTFileDelete(szKeyPath);
            if (RT_FAILURE(vrc))
                LogRel(("WARNING! Failed to delete private key %s with vrc=%d\n", szKeyPath, vrc));
        }
        else
            LogRel(("WARNING! Failed to get VirtualBox user home directory with '%Rrc'\n", vrc));
# endif /* DEBUG */
#endif
    }
    catch (CloudError e)
    {
        hrc = e.getRc();
        LogRel(("CLOUD-NET: Failed to terminate cloud gateway instance (hrc=%x).\n", hrc));
    }
    gateway.mGatewayInstanceId.setNull();
    return hrc;
}


HRESULT generateKeys(GatewayInfo& gateway)
{
#ifndef VBOX_WITH_LIBSSH
    RT_NOREF(gateway);
    return E_NOTIMPL;
#else /* VBOX_WITH_LIBSSH */
    ssh_key single_use_key;
    int iRcSsh = ssh_pki_generate(SSH_KEYTYPE_RSA, 2048, &single_use_key);
    if (iRcSsh != SSH_OK)
    {
        LogRel(("Failed to generate a key pair. iRcSsh = %d\n", iRcSsh));
        return E_FAIL;
    }

    char *pstrKey = NULL;
    iRcSsh = ssh_pki_export_privkey_base64(single_use_key, NULL, NULL, NULL, &pstrKey);
    if (iRcSsh != SSH_OK)
    {
        LogRel(("Failed to export private key. iRcSsh = %d\n", iRcSsh));
        return E_FAIL;
    }
    gateway.mPrivateSshKey = pstrKey;
#if 0
# ifdef DEBUG
    char szConfigPath[RTPATH_MAX];

    vrc = GetVBoxUserHomeDirectory(szConfigPath, sizeof(szConfigPath), false /* fCreateDir */);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTPathAppend(szConfigPath, sizeof(szConfigPath), "gateway-key.pem");
        AssertRCReturn(vrc, E_FAIL);
        iRcSsh = ssh_pki_export_privkey_file(single_use_key, NULL, NULL, NULL, szConfigPath);
        if (iRcSsh != SSH_OK)
        {
            LogRel(("Failed to export private key to %s with iRcSsh=%d\n", szConfigPath, iRcSsh));
            return E_FAIL;
        }
#  ifndef RT_OS_WINDOWS
        vrc = RTPathSetMode(szConfigPath, RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR); /* Satisfy ssh client */
        AssertRCReturn(vrc, E_FAIL);
#  endif
    }
    else
    {
        LogRel(("Failed to get VirtualBox user home directory with '%Rrc'\n", vrc));
        return E_FAIL;
    }
# endif /* DEBUG */
#endif
    ssh_string_free_char(pstrKey);
    pstrKey = NULL;
    iRcSsh = ssh_pki_export_pubkey_base64(single_use_key, &pstrKey);
    if (iRcSsh != SSH_OK)
    {
        LogRel(("Failed to export public key. iRcSsh = %d\n", iRcSsh));
        return E_FAIL;
    }
    gateway.mPublicSshKey = Utf8StrFmt("ssh-rsa %s single-use-key", pstrKey);
    ssh_string_free_char(pstrKey);
    ssh_key_free(single_use_key);

    return S_OK;
#endif /* VBOX_WITH_LIBSSH */
}
