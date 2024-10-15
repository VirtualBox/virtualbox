/* $Id: CryptoUtils.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - Cryptographic utility functions used by both VBoxSVC and VBoxC.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_CryptoUtils_h
#define MAIN_INCLUDED_CryptoUtils_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/vfs.h>

#include <VBox/VBoxCryptoIf.h>
#include <VBox/com/string.h>

#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/vmm/vmapi.h>

#include "SecretKeyStore.h"
#ifdef VBOX_COM_INPROC
# include "ConsoleImpl.h"
#else
# include "MachineImpl.h"
# include "VirtualBoxImpl.h"
#endif


/**
 * Class handling encrypted and non encrypted SSM files.
 */
class SsmStream
{
    public:
#ifdef VBOX_COM_INPROC
        SsmStream(Console *pParent, PCVMMR3VTABLE pVMM, SecretKeyStore *pKeyStore, const Utf8Str &strKeyId, const Utf8Str &strKeyStore);
#else
        SsmStream(VirtualBox *pParent, SecretKeyStore *pKeyStore, const Utf8Str &strKeyId, const Utf8Str &strKeyStore);
#endif
        ~SsmStream();

        /**
         * Actually opens the stream for either reading or writing.
         *
         * @returns VBox status code.
         * @param   strFilename The filename of the saved state to open or create.
         * @param   fWrite      Flag whether the stream should be opened for writing (true) or readonly (false).
         * @param   ppSsmHandle Where to store the SSM handle on success, don't call SSMR3Close() but the provided close() method.
         */
        int open(const Utf8Str &strFilename, bool fWrite, PSSMHANDLE *ppSsmHandle);

        /**
         * Opens the saved state file for reading, doesn't call SSMR3Open().
         *
         * @returns VBox status code.
         * @param   strFilename The filename of the saved state to open.
         */
        int open(const Utf8Str &strFilename);

        /**
         * Creates a new saved state file under the given path.
         *
         * @returns VBox status code.
         * @param   strFilename The filename of the saved state to create.
         */
        int create(const Utf8Str &strFilename);

        /**
         * Returns the pointer to the stream operations table after a succesful opening/creation.
         *
         * @return VBox status code.
         * @param  ppStrmOps      Where to store the pointer to the stream operations table on success.
         * @param  ppvStrmOpsUser Where to store the pointer to the opaque user data on success.
         */
        int querySsmStrmOps(PCSSMSTRMOPS *ppStrmOps, void **ppvStrmOpsUser);

        /**
         * Closes an previously opened stream.
         *
         * @returns VBox status code.
         */
        int close(void);

    private:

        static DECLCALLBACK(int) i_ssmCryptoWrite(void *pvUser, uint64_t offStream, const void *pvBuf, size_t cbToWrite);
        static DECLCALLBACK(int) i_ssmCryptoRead(void *pvUser, uint64_t offStream, void *pvBuf, size_t cbToRead, size_t *pcbRead);
        static DECLCALLBACK(int) i_ssmCryptoSeek(void *pvUser, int64_t offSeek, unsigned uMethod, uint64_t *poffActual);
        static DECLCALLBACK(uint64_t) i_ssmCryptoTell(void *pvUser);
        static DECLCALLBACK(int) i_ssmCryptoSize(void *pvUser, uint64_t *pcb);
        static DECLCALLBACK(int) i_ssmCryptoIsOk(void *pvUser);
        static DECLCALLBACK(int) i_ssmCryptoClose(void *pvUser, bool fCancelled);

#ifdef VBOX_COM_INPROC
        Console        *m_pParent;
        PCVMMR3VTABLE  m_pVMM;
#else
        VirtualBox     *m_pParent;
#endif
        /** The key store for getting at passwords. */
        SecretKeyStore *m_pKeyStore;
        /** The key ID holding the password, empty if the saved state is not encrypted. */
        Utf8Str        m_strKeyId;
        /** The keystore holding the encrypted DEK. */
        Utf8Str        m_strKeyStore;
        /** The VFS file handle. */
        RTVFSFILE      m_hVfsFile;
        /** The SSM handle when opened. */
        PSSMHANDLE     m_pSsm;
        /** The SSM stream callbacks table. */
        SSMSTRMOPS     m_StrmOps;
        /** The cryptographic interfacer. */
        PCVBOXCRYPTOIF m_pCryptoIf;
};

#endif /* !MAIN_INCLUDED_CryptoUtils_h */
