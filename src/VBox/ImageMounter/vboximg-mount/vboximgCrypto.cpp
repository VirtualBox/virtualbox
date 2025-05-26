/* $Id: vboximgCrypto.cpp 169001 2025-05-26 19:52:08Z klaus.espenlaub@oracle.com $ */
/** @file
 * vboximgCypto.cpp - Disk Image Flattening FUSE Program.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#include <iprt/cdefs.h>
#include <VBox/err.h>
#include <VBox/settings.h>
#include <VBox/vd.h>
#include "vboximgCrypto.h"
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/memsafer.h>

/*
 * Apparently there is a more COM:: oriented (but less efficient?) approach to dealing
 * with the keystore and disk encryption, which will need to be investigated. Keeping
 * all this duplicated code in a separate file until the ideal approach is determined.
 */
SecretKey::SecretKey(const uint8_t *pbKey, size_t cbKey, bool fKeyBufNonPageable)
{
    m_cRefs            = 0;
    m_fRemoveOnSuspend = false;
    m_cUsers           = 0;
    m_cbKey            = cbKey;

    int rc = RTMemSaferAllocZEx((void **)&this->m_pbKey, cbKey,
                                fKeyBufNonPageable ? RTMEMSAFER_F_REQUIRE_NOT_PAGABLE : 0);
    if (RT_SUCCESS(rc))
    {
        memcpy(this->m_pbKey, pbKey, cbKey);

        /* Scramble content to make retrieving the key more difficult. */
        rc = RTMemSaferScramble(this->m_pbKey, cbKey);
    }

    if (RT_FAILURE(rc))
        throw rc;
}

SecretKey::~SecretKey()
{
    Assert(!m_cRefs);

    RTMemSaferFree(m_pbKey, m_cbKey);
    m_cRefs = 0;
    m_pbKey = NULL;
    m_cbKey = 0;
    m_fRemoveOnSuspend = false;
    m_cUsers = 0;
}

uint32_t SecretKey::retain()
{
    uint32_t cRefs = ASMAtomicIncU32(&m_cRefs);
    if (cRefs == 1)
    {
        int rc = RTMemSaferUnscramble(m_pbKey, m_cbKey);
        AssertRC(rc);
    }

    return cRefs;
}

uint32_t SecretKey::release()
{
    uint32_t cRefs = ASMAtomicDecU32(&m_cRefs);
    if (!cRefs)
    {
        int rc = RTMemSaferScramble(m_pbKey, m_cbKey);
        AssertRC(rc);
    }

    return cRefs;
}

uint32_t SecretKey::refCount()
{
    return m_cRefs;
}

int SecretKey::setUsers(uint32_t cUsers)
{
    m_cUsers = cUsers;
    return VINF_SUCCESS;
}

uint32_t SecretKey::getUsers()
{
    return m_cUsers;
}

int SecretKey::setRemoveOnSuspend(bool fRemoveOnSuspend)
{
    m_fRemoveOnSuspend = fRemoveOnSuspend;
    return VINF_SUCCESS;
}

bool SecretKey::getRemoveOnSuspend()
{
    return m_fRemoveOnSuspend;
}

const void *SecretKey::getKeyBuffer()
{
    AssertReturn(m_cRefs > 0, NULL);
    return m_pbKey;
}

size_t SecretKey::getKeySize()
{
    return m_cbKey;
}

SecretKeyStore::SecretKeyStore(bool fKeyBufNonPageable)
{
    m_fKeyBufNonPageable = fKeyBufNonPageable;
}

SecretKeyStore::~SecretKeyStore()
{
    int rc = deleteAllSecretKeys(false /* fSuspend */, true /* fForce */);
    AssertRC(rc);
}

int SecretKeyStore::addSecretKey(const com::Utf8Str &strKeyId, const uint8_t *pbKey, size_t cbKey)
{
    /* Check that the ID is not existing already. */
    SecretKeyMap::const_iterator it = m_mapSecretKeys.find(strKeyId);
    if (it != m_mapSecretKeys.end())
        return VERR_ALREADY_EXISTS;

    SecretKey *pKey = NULL;
    try
    {
        pKey = new SecretKey(pbKey, cbKey, m_fKeyBufNonPageable);

        m_mapSecretKeys.insert(std::make_pair(strKeyId, pKey));
    }
    catch (int rc)
    {
        return rc;
    }
    catch (std::bad_alloc &)
    {
        if (pKey)
            delete pKey;
        return VERR_NO_MEMORY;
    }

    return VINF_SUCCESS;
}

int SecretKeyStore::deleteSecretKey(const com::Utf8Str &strKeyId)
{
    SecretKeyMap::iterator it = m_mapSecretKeys.find(strKeyId);
    if (it == m_mapSecretKeys.end())
        return VERR_NOT_FOUND;

    SecretKey *pKey = it->second;
    if (pKey->refCount() != 0)
        return VERR_RESOURCE_IN_USE;

    m_mapSecretKeys.erase(it);
    delete pKey;

    return VINF_SUCCESS;
}

int SecretKeyStore::retainSecretKey(const com::Utf8Str &strKeyId, SecretKey **ppKey)
{
    SecretKeyMap::const_iterator it = m_mapSecretKeys.find(strKeyId);
    if (it == m_mapSecretKeys.end())
        return VERR_NOT_FOUND;

    SecretKey *pKey = it->second;
    pKey->retain();

    *ppKey = pKey;

    return VINF_SUCCESS;
}

int SecretKeyStore::releaseSecretKey(const com::Utf8Str &strKeyId)
{
    SecretKeyMap::const_iterator it = m_mapSecretKeys.find(strKeyId);
    if (it == m_mapSecretKeys.end())
        return VERR_NOT_FOUND;

    SecretKey *pKey = it->second;
    pKey->release();
    return VINF_SUCCESS;
}

int SecretKeyStore::deleteAllSecretKeys(bool fSuspend, bool fForce)
{
    /* First check whether a key is still in use. */
    if (!fForce)
    {
        for (SecretKeyMap::iterator it = m_mapSecretKeys.begin();
             it != m_mapSecretKeys.end();
             ++it)
        {
            SecretKey *pKey = it->second;
            if (   pKey->refCount()
                && (   (   pKey->getRemoveOnSuspend()
                        && fSuspend)
                    || !fSuspend))
                return VERR_RESOURCE_IN_USE;
        }
    }

    SecretKeyMap::iterator it = m_mapSecretKeys.begin();
    while (it != m_mapSecretKeys.end())
    {
        SecretKey *pKey = it->second;
        if (   pKey->getRemoveOnSuspend()
            || !fSuspend)
        {
            AssertMsg(!pKey->refCount(), ("No one should access the stored key at this point anymore!\n"));
            delete pKey;
            SecretKeyMap::iterator itNext = it;
            ++itNext;
            m_mapSecretKeys.erase(it);
            it = itNext;
        }
        else
            ++it;
    }

    return VINF_SUCCESS;
}

void vboxImageCryptoSetup(VDISKCRYPTOSETTINGS *pSettings, const char *pszCipher,
                                        const char *pszKeyStore, const char *pszPassword,
                                        bool fCreateKeyStore)
{
    pSettings->pszCipher       = pszCipher;
    pSettings->pszPassword     = pszPassword;
    pSettings->pszKeyStoreLoad = pszKeyStore;
    pSettings->fCreateKeyStore = fCreateKeyStore;
    pSettings->pbDek           = NULL;
    pSettings->cbDek           = 0;
    pSettings->vdFilterIfaces  = NULL;

    pSettings->vdIfCfg.pfnAreKeysValid = vboximgVdCryptoConfigAreKeysValid;
    pSettings->vdIfCfg.pfnQuerySize    = vboximgVdCryptoConfigQuerySize;
    pSettings->vdIfCfg.pfnQuery        = vboximgVdCryptoConfigQuery;
    pSettings->vdIfCfg.pfnQueryBytes   = NULL;

    pSettings->vdIfCrypto.pfnKeyRetain                = vboximgVdCryptoKeyRetain;
    pSettings->vdIfCrypto.pfnKeyRelease               = vboximgVdCryptoKeyRelease;
    pSettings->vdIfCrypto.pfnKeyStorePasswordRetain   = vboximgVdCryptoKeyStorePasswordRetain;
    pSettings->vdIfCrypto.pfnKeyStorePasswordRelease  = vboximgVdCryptoKeyStorePasswordRelease;
    pSettings->vdIfCrypto.pfnKeyStoreSave             = vboximgVdCryptoKeyStoreSave;
    pSettings->vdIfCrypto.pfnKeyStoreReturnParameters = vboximgVdCryptoKeyStoreReturnParameters;

    int rc = VDInterfaceAdd(&pSettings->vdIfCfg.Core,
                             "vboximgVdInterfaceCfgCrypto",
                             VDINTERFACETYPE_CONFIG, pSettings,
                             sizeof(VDINTERFACECONFIG), &pSettings->vdFilterIfaces);
    AssertRC(rc);

    rc = VDInterfaceAdd(&pSettings->vdIfCrypto.Core,
                         "vboximgVdInterfaceCrypto",
                         VDINTERFACETYPE_CRYPTO, pSettings,
                         sizeof(VDINTERFACECRYPTO), &pSettings->vdFilterIfaces);
    AssertRC(rc);
}

DECLCALLBACK(bool) vboximgVdCryptoConfigAreKeysValid(void *pvUser, const char *pszzValid)
{
    /* Just return always true here. */
    NOREF(pvUser);
    NOREF(pszzValid);
    return true;
}

DECLCALLBACK(int) vboximgVdCryptoConfigQuerySize(void *pvUser, const char *pszName, size_t *pcbValue)
{
    VDISKCRYPTOSETTINGS *pSettings = (VDISKCRYPTOSETTINGS *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    AssertPtrReturn(pcbValue, VERR_INVALID_POINTER);

    size_t cbValue = 0;
    if (!strcmp(pszName, "Algorithm"))
        cbValue = strlen(pSettings->pszCipher) + 1;
    else if (!strcmp(pszName, "KeyId"))
        cbValue = sizeof("irrelevant");
    else if (!strcmp(pszName, "KeyStore"))
    {
        if (!pSettings->pszKeyStoreLoad)
            return VERR_CFGM_VALUE_NOT_FOUND;
        cbValue = strlen(pSettings->pszKeyStoreLoad) + 1;
    }
    else if (!strcmp(pszName, "CreateKeyStore"))
        cbValue = 2; /* Single digit + terminator. */
    else
        return VERR_CFGM_VALUE_NOT_FOUND;

    *pcbValue = cbValue + 1 /* include terminator */;

    return VINF_SUCCESS;
}

DECLCALLBACK(int) vboximgVdCryptoConfigQuery(void *pvUser, const char *pszName,
                                                char *pszValue, size_t cchValue)
{
    VDISKCRYPTOSETTINGS *pSettings = (VDISKCRYPTOSETTINGS *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    AssertPtrReturn(pszValue, VERR_INVALID_POINTER);

    const char *psz = NULL;
    if (!strcmp(pszName, "Algorithm"))
        psz = pSettings->pszCipher;
    else if (!strcmp(pszName, "KeyId"))
        psz = "irrelevant";
    else if (!strcmp(pszName, "KeyStore"))
        psz = pSettings->pszKeyStoreLoad;
    else if (!strcmp(pszName, "CreateKeyStore"))
    {
        if (pSettings->fCreateKeyStore)
            psz = "1";
        else
            psz = "0";
    }
    else
        return VERR_CFGM_VALUE_NOT_FOUND;

    size_t cch = strlen(psz);
    if (cch >= cchValue)
        return VERR_CFGM_NOT_ENOUGH_SPACE;

    memcpy(pszValue, psz, cch + 1);
    return VINF_SUCCESS;
}

DECLCALLBACK(int) vboximgVdCryptoKeyRetain(void *pvUser, const char *pszId,
                                              const uint8_t **ppbKey, size_t *pcbKey)
{
    VDISKCRYPTOSETTINGS *pSettings = (VDISKCRYPTOSETTINGS *)pvUser;
    NOREF(pszId);
    NOREF(ppbKey);
    NOREF(pcbKey);
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    AssertMsgFailedReturn(("This method should not be called here!\n"), VERR_INVALID_STATE);
}

DECLCALLBACK(int) vboximgVdCryptoKeyRelease(void *pvUser, const char *pszId)
{
    VDISKCRYPTOSETTINGS *pSettings = (VDISKCRYPTOSETTINGS *)pvUser;
    NOREF(pszId);
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    AssertMsgFailedReturn(("This method should not be called here!\n"), VERR_INVALID_STATE);
}

DECLCALLBACK(int) vboximgVdCryptoKeyStorePasswordRetain(void *pvUser, const char *pszId, const char **ppszPassword)
{
    VDISKCRYPTOSETTINGS *pSettings = (VDISKCRYPTOSETTINGS *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);

    NOREF(pszId);
    *ppszPassword = pSettings->pszPassword;
    return VINF_SUCCESS;
}

DECLCALLBACK(int) vboximgVdCryptoKeyStorePasswordRelease(void *pvUser, const char *pszId)
{
    VDISKCRYPTOSETTINGS *pSettings = (VDISKCRYPTOSETTINGS *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);
    NOREF(pszId);
    return VINF_SUCCESS;
}

DECLCALLBACK(int) vboximgVdCryptoKeyStoreSave(void *pvUser, const void *pvKeyStore, size_t cbKeyStore)
{
    VDISKCRYPTOSETTINGS *pSettings = (VDISKCRYPTOSETTINGS *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);

    pSettings->pszKeyStore = (char *)RTMemAllocZ(cbKeyStore);
    if (!pSettings->pszKeyStore)
        return VERR_NO_MEMORY;

    memcpy(pSettings->pszKeyStore, pvKeyStore, cbKeyStore);
    return VINF_SUCCESS;
}

DECLCALLBACK(int) vboximgVdCryptoKeyStoreReturnParameters(void *pvUser, const char *pszCipher,
                                                             const uint8_t *pbDek, size_t cbDek)
{
    VDISKCRYPTOSETTINGS *pSettings = (VDISKCRYPTOSETTINGS *)pvUser;
    AssertPtrReturn(pSettings, VERR_GENERAL_FAILURE);

    pSettings->pszCipherReturned = RTStrDup(pszCipher);
    pSettings->pbDek             = pbDek;
    pSettings->cbDek             = cbDek;

    return pSettings->pszCipherReturned ? VINF_SUCCESS : VERR_NO_MEMORY;
}
