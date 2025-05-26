/* $Id: vboximgCrypto.h 169001 2025-05-26 19:52:08Z klaus.espenlaub@oracle.com $ */
/** @file
 * vboximgCrypto.h
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_vboximg_mount_vboximgCrypto_h
#define VBOX_INCLUDED_SRC_vboximg_mount_vboximgCrypto_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <map>

/*
 * Settings for a crypto filter instance.
 */
typedef struct VDiskCryptoSettings
{
    VDiskCryptoSettings()
        : fCreateKeyStore(false),
          pszPassword(NULL),
          pszKeyStore(NULL),
          pszKeyStoreLoad(NULL),
          pbDek(NULL),
          cbDek(0),
          pszCipher(NULL),
          pszCipherReturned(NULL)
    { }

    bool              fCreateKeyStore;
    const char        *pszPassword;
    char              *pszKeyStore;
    const char        *pszKeyStoreLoad;

    const uint8_t     *pbDek;
    size_t            cbDek;
    const char        *pszCipher;

    /** The cipher returned by the crypto filter. */
    char              *pszCipherReturned;

    PVDINTERFACE      vdFilterIfaces;

    VDINTERFACECONFIG vdIfCfg;
    VDINTERFACECRYPTO vdIfCrypto;
} VDISKCRYPTOSETTINGS;


class SecretKey
{
    public:

        /**
         * Constructor for a secret key.
         *
         * @param pbKey                 The key buffer.
         * @param cbKey                 Size of the key.
         * @param fKeyBufNonPageable    Flag whether the key buffer should be non pageable.
         */
        SecretKey(const uint8_t *pbKey, size_t cbKey, bool fKeyBufNonPageable);

        /**
         * Secret key destructor.
         */
        ~SecretKey();

        /**
         * Increments the reference counter of the key.
         *
         * @returns The new reference count.
         */
        uint32_t retain();

        /**
         * Releases a reference of the key.
         * If the reference counter reaches 0 the key buffer might be protected
         * against further access or the data will become scrambled.
         *
         * @returns The new reference count.
         */
        uint32_t release();

        /**
         * Returns the reference count of the secret key.
         */
        uint32_t refCount();

        /**
         * Sets the possible number of users for this key.
         *
         * @returns VBox status code.
         * @param   cUsers              The possible number of user for this key.
         */
        int setUsers(uint32_t cUsers);

        /**
         * Returns the possible amount of users.
         *
         * @returns Possible amount of users.
         */
        uint32_t getUsers();

        /**
         * Sets the remove on suspend flag.
         *
         * @returns VBox status code.
         * @param   fRemoveOnSuspend    Flag whether to remove the key on host suspend.
         */
        int setRemoveOnSuspend(bool fRemoveOnSuspend);

        /**
         * Returns whether the key should be destroyed on suspend.
         */
        bool getRemoveOnSuspend();

        /**
         * Returns the buffer to the key.
         */
        const void *getKeyBuffer();

        /**
         * Returns the size of the key.
         */
       size_t getKeySize();

    private:
        /** Reference counter of the key. */
        volatile uint32_t m_cRefs;
        /** Key material. */
        uint8_t          *m_pbKey;
        /** Size of the key in bytes. */
        size_t            m_cbKey;
        /** Flag whether to remove the key on suspend. */
        bool              m_fRemoveOnSuspend;
        /** Number of entities which will use this key. */
        uint32_t          m_cUsers;
};

class SecretKeyStore
{
    public:

        /**
         * Constructor for a secret key store.
         *
         * @param fKeyBufNonPageable    Flag whether the key buffer is required to
         *                              be non pageable.
         */
        SecretKeyStore(bool fKeyBufNonPageable);

        /**
         * Destructor of a secret key store. This will free all stored secret keys
         * inluding the key buffers. Make sure there no one accesses one of the keys
         * stored.
         */
        ~SecretKeyStore();

        /**
         * Add a secret key to the store.
         *
         * @returns VBox status code.
         * @param   strKeyId            The key identifier.
         * @param   pbKey               The key to store.
         * @param   cbKey               Size of the key.
         */
        int addSecretKey(const com::Utf8Str &strKeyId, const uint8_t *pbKey, size_t cbKey);

        /**
         * Deletes a key from the key store associated with the given identifier.
         *
         * @returns VBox status code.
         * @param   strKeyId            The key identifier.
         */
        int deleteSecretKey(const com::Utf8Str &strKeyId);

        /**
         * Returns the secret key object associated with the given identifier.
         * This increments the reference counter of the secret key object.
         *
         * @returns VBox status code.
         * @param   strKeyId            The key identifier.
         * @param   ppKey               Where to store the secret key object on success.
         */
        int retainSecretKey(const com::Utf8Str &strKeyId, SecretKey **ppKey);

        /**
         * Releases a reference to the secret key object.
         *
         * @returns VBox status code.
         * @param   strKeyId            The key identifier.
         */
        int releaseSecretKey(const com::Utf8Str &strKeyId);

        /**
         * Deletes all secret keys from the key store.
         *
         * @returns VBox status code.
         * @param   fSuspend           Flag whether to delete only keys which are
         *                             marked for deletion during a suspend.
         * @param   fForce             Flag whether to force deletion if some keys
         *                             are still in use. Otherwise an error is returned.
         */
        int deleteAllSecretKeys(bool fSuspend, bool fForce);

    private:

        typedef std::map<com::Utf8Str, SecretKey *> SecretKeyMap;

        /** The map to map key identifers to secret keys. */
        SecretKeyMap m_mapSecretKeys;
        /** Flag whether key buffers should be non pagable. */
        bool         m_fKeyBufNonPageable;
};

void vboxImageCryptoSetup(VDISKCRYPTOSETTINGS *pSettings, const char *pszCipher,
                                        const char *pszKeyStore, const char *pszPassword,
                                        bool fCreateKeyStore);

DECLCALLBACK(bool) vboximgVdCryptoConfigAreKeysValid(void *pvUser, const char *pszzValid);

DECLCALLBACK(int)  vboximgVdCryptoConfigQuerySize(void *pvUser, const char *pszName, size_t *pcbValue);

DECLCALLBACK(int)  vboximgVdCryptoConfigQuery(void *pvUser, const char *pszName,
                                                char *pszValue, size_t cchValue);

DECLCALLBACK(int)  vboximgVdCryptoKeyRetain(void *pvUser, const char *pszId,
                                              const uint8_t **ppbKey, size_t *pcbKey);

DECLCALLBACK(int)  vboximgVdCryptoKeyRelease(void *pvUser, const char *pszId);

DECLCALLBACK(int)  vboximgVdCryptoKeyStorePasswordRetain(void *pvUser, const char *pszId, const char **ppszPassword);

DECLCALLBACK(int)  vboximgVdCryptoKeyStorePasswordRelease(void *pvUser, const char *pszId);

DECLCALLBACK(int)  vboximgVdCryptoKeyStoreSave(void *pvUser, const void *pvKeyStore, size_t cbKeyStore);

DECLCALLBACK(int)  vboximgVdCryptoKeyStoreReturnParameters(void *pvUser, const char *pszCipher,
                                                             const uint8_t *pbDek, size_t cbDek);


#endif /* !VBOX_INCLUDED_SRC_vboximg_mount_vboximgCrypto_h */

