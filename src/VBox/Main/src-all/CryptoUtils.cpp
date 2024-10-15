/* $Id: CryptoUtils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/vfs.h>

#include "CryptoUtils.h"


/*static*/
DECLCALLBACK(int) SsmStream::i_ssmCryptoWrite(void *pvUser, uint64_t offStream, const void *pvBuf, size_t cbToWrite)
{
    SsmStream *pThis = static_cast<SsmStream *>(pvUser);

    return RTVfsFileWriteAt(pThis->m_hVfsFile, (RTFOFF)offStream, pvBuf, cbToWrite, NULL /*pcbWritten*/);
}


/*static*/
DECLCALLBACK(int) SsmStream::i_ssmCryptoRead(void *pvUser, uint64_t offStream, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    SsmStream *pThis = static_cast<SsmStream *>(pvUser);

    return RTVfsFileReadAt(pThis->m_hVfsFile, (RTFOFF)offStream, pvBuf, cbToRead, pcbRead);
}


/*static*/
DECLCALLBACK(int) SsmStream::i_ssmCryptoSeek(void *pvUser, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    SsmStream *pThis = static_cast<SsmStream *>(pvUser);

    return RTVfsFileSeek(pThis->m_hVfsFile, (RTFOFF)offSeek, uMethod, poffActual);
}


/*static*/
DECLCALLBACK(uint64_t) SsmStream::i_ssmCryptoTell(void *pvUser)
{
    SsmStream *pThis = static_cast<SsmStream *>(pvUser);

    return (uint64_t)RTVfsFileTell(pThis->m_hVfsFile);
}


/*static*/
DECLCALLBACK(int) SsmStream::i_ssmCryptoSize(void *pvUser, uint64_t *pcb)
{
    SsmStream *pThis = static_cast<SsmStream *>(pvUser);

    return RTVfsFileQuerySize(pThis->m_hVfsFile, pcb);
}


/*static*/
DECLCALLBACK(int) SsmStream::i_ssmCryptoIsOk(void *pvUser)
{
    RT_NOREF(pvUser);

    /** @todo */
    return VINF_SUCCESS;
}


/*static*/
DECLCALLBACK(int) SsmStream::i_ssmCryptoClose(void *pvUser, bool fCancelled)
{
    SsmStream *pThis = static_cast<SsmStream *>(pvUser);

    RT_NOREF(fCancelled); /** @todo */
    RTVfsFileRelease(pThis->m_hVfsFile);
    pThis->m_hVfsFile = NIL_RTVFSFILE;
    return VINF_SUCCESS;
}


#ifdef VBOX_COM_INPROC
SsmStream::SsmStream(Console *pParent, PCVMMR3VTABLE pVMM, SecretKeyStore *pKeyStore, const Utf8Str &strKeyId, const Utf8Str &strKeyStore)
#else
SsmStream::SsmStream(VirtualBox *pParent, SecretKeyStore *pKeyStore, const Utf8Str &strKeyId, const Utf8Str &strKeyStore)
#endif
{
    m_StrmOps.u32Version    = SSMSTRMOPS_VERSION;
    m_StrmOps.pfnWrite      = SsmStream::i_ssmCryptoWrite;
    m_StrmOps.pfnRead       = SsmStream::i_ssmCryptoRead;
    m_StrmOps.pfnSeek       = SsmStream::i_ssmCryptoSeek;
    m_StrmOps.pfnTell       = SsmStream::i_ssmCryptoTell;
    m_StrmOps.pfnSize       = SsmStream::i_ssmCryptoSize;
    m_StrmOps.pfnIsOk       = SsmStream::i_ssmCryptoIsOk;
    m_StrmOps.pfnClose      = SsmStream::i_ssmCryptoClose;
    m_StrmOps.u32EndVersion = SSMSTRMOPS_VERSION;

    m_pKeyStore             = pKeyStore;
    m_strKeyId              = strKeyId;
    m_strKeyStore           = strKeyStore;
    m_pParent               = pParent;
    m_hVfsFile              = NIL_RTVFSFILE;
    m_pSsm                  = NULL;
    m_pCryptoIf             = NULL;
#ifdef VBOX_COM_INPROC
    m_pVMM                  = pVMM;
#endif
}


SsmStream::~SsmStream()
{
    close();

    if (m_pCryptoIf)
        m_pParent->i_releaseCryptoIf(m_pCryptoIf);

    m_pCryptoIf = NULL;
    m_pKeyStore = NULL;
}


int SsmStream::open(const Utf8Str &strFilename, bool fWrite, PSSMHANDLE *ppSsmHandle)
{
    /* Fast path, if the saved state is not encrypted we can skip everything and let SSM handle the file. */
    if (m_strKeyId.isEmpty())
    {
        AssertReturn(!fWrite, VERR_NOT_SUPPORTED);

#ifdef VBOX_COM_INPROC
        int vrc = m_pVMM->pfnSSMR3Open(strFilename.c_str(), NULL /*pStreamOps*/, NULL /*pvStreamOps*/,
                                       0 /*fFlags*/, &m_pSsm);
#else
        int vrc = SSMR3Open(strFilename.c_str(), NULL /*pStreamOps*/, NULL /*pvStreamOps*/,
                            0 /*fFlags*/, &m_pSsm);
#endif
        if (   RT_SUCCESS(vrc)
            && ppSsmHandle)
            *ppSsmHandle = m_pSsm;

        return vrc;
    }

    int vrc = VINF_SUCCESS;
    if (!m_pCryptoIf)
    {
#ifdef VBOX_COM_INPROC
        vrc = m_pParent->i_retainCryptoIf(&m_pCryptoIf);
        if (RT_FAILURE(vrc))
            return vrc;
#else
        HRESULT hrc = m_pParent->i_retainCryptoIf(&m_pCryptoIf);
        if (FAILED(hrc))
            return VERR_COM_IPRT_ERROR;
#endif
    }

    SecretKey *pKey;
    vrc = m_pKeyStore->retainSecretKey(m_strKeyId, &pKey);
    if (RT_SUCCESS(vrc))
    {
        RTVFSFILE hVfsFileSsm = NIL_RTVFSFILE;
        uint32_t fOpen =   fWrite
                         ? RTFILE_O_READWRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_WRITE
                         : RTFILE_O_READ      | RTFILE_O_OPEN           | RTFILE_O_DENY_WRITE;

        vrc = RTVfsFileOpenNormal(strFilename.c_str(), fOpen, &hVfsFileSsm);
        if (RT_SUCCESS(vrc))
        {
            const char *pszPassword = (const char *)pKey->getKeyBuffer();

            vrc = m_pCryptoIf->pfnCryptoFileFromVfsFile(hVfsFileSsm, m_strKeyStore.c_str(), pszPassword, &m_hVfsFile);
            if (RT_SUCCESS(vrc))
            {
#ifdef VBOX_COM_INPROC
                vrc = m_pVMM->pfnSSMR3Open(NULL /*pszFilename*/, &m_StrmOps, this, 0 /*fFlags*/, &m_pSsm);
#else
                vrc = SSMR3Open(NULL /*pszFilename*/, &m_StrmOps, this, 0 /*fFlags*/, &m_pSsm);
#endif
                if (   RT_SUCCESS(vrc)
                    && ppSsmHandle)
                    *ppSsmHandle = m_pSsm;

                if (RT_FAILURE(vrc))
                {
                    RTVfsFileRelease(m_hVfsFile);
                    m_hVfsFile = NIL_RTVFSFILE;
                }
            }

            /* Also release in success case because the encrypted file handle retained a new reference to it. */
            RTVfsFileRelease(hVfsFileSsm);
        }

        pKey->release();
    }

    return vrc;
}


int SsmStream::open(const Utf8Str &strFilename)
{
#ifdef VBOX_COM_INPROC
    RTVFSFILE hVfsFileSsm = NIL_RTVFSFILE;
    uint32_t fOpen = RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE;

    int vrc = RTVfsFileOpenNormal(strFilename.c_str(), fOpen, &hVfsFileSsm);
    if (RT_SUCCESS(vrc))
    {
        if (m_strKeyId.isNotEmpty())
        {
            /* File is encrypted, set up machinery. */
            if (!m_pCryptoIf)
                vrc = m_pParent->i_retainCryptoIf(&m_pCryptoIf);

            if (RT_SUCCESS(vrc))
            {
                SecretKey *pKey;
                vrc = m_pKeyStore->retainSecretKey(m_strKeyId, &pKey);
                if (RT_SUCCESS(vrc))
                {
                    const char *pszPassword = (const char *)pKey->getKeyBuffer();

                    vrc = m_pCryptoIf->pfnCryptoFileFromVfsFile(hVfsFileSsm, m_strKeyStore.c_str(), pszPassword, &m_hVfsFile);
                    pKey->release();
                }

                /* Also release in success case because the encrypted file handle retained a new reference to it. */
                RTVfsFileRelease(hVfsFileSsm);
            }
        }
        else /* File is not encrypted. */
            m_hVfsFile = hVfsFileSsm;
    }

    return vrc;
#else
    RT_NOREF(strFilename);
    return VERR_NOT_SUPPORTED;
#endif
}


int SsmStream::create(const Utf8Str &strFilename)
{
#ifdef VBOX_COM_INPROC
    RTVFSFILE hVfsFileSsm = NIL_RTVFSFILE;
    uint32_t fOpen = RTFILE_O_READWRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_WRITE;

    int vrc = RTVfsFileOpenNormal(strFilename.c_str(), fOpen, &hVfsFileSsm);
    if (RT_SUCCESS(vrc))
    {
        if (m_strKeyId.isNotEmpty())
        {
            /* File is encrypted, set up machinery. */
            if (!m_pCryptoIf)
                vrc = m_pParent->i_retainCryptoIf(&m_pCryptoIf);

            if (RT_SUCCESS(vrc))
            {
                SecretKey *pKey;
                vrc = m_pKeyStore->retainSecretKey(m_strKeyId, &pKey);
                if (RT_SUCCESS(vrc))
                {
                    const char *pszPassword = (const char *)pKey->getKeyBuffer();

                    vrc = m_pCryptoIf->pfnCryptoFileFromVfsFile(hVfsFileSsm, m_strKeyStore.c_str(), pszPassword, &m_hVfsFile);
                    pKey->release();
                }

                /* Also release in success case because the encrypted file handle retained a new reference to it. */
                RTVfsFileRelease(hVfsFileSsm);
                if (RT_FAILURE(vrc))
                    RTFileDelete(strFilename.c_str());
            }
        }
        else /* File doesn't need to be encrypted. */
            m_hVfsFile = hVfsFileSsm;
    }

    return vrc;
#else
    RT_NOREF(strFilename);
    return VERR_NOT_SUPPORTED;
#endif
}


int SsmStream::querySsmStrmOps(PCSSMSTRMOPS *ppStrmOps, void **ppvStrmOpsUser)
{
    AssertReturn(m_hVfsFile != NIL_RTVFSFILE, VERR_INVALID_STATE);

    *ppStrmOps      = &m_StrmOps;
    *ppvStrmOpsUser = this;
    return VINF_SUCCESS;
}


int SsmStream::close(void)
{
    if (m_pSsm)
    {
#ifdef VBOX_COM_INPROC
        int vrc = m_pVMM->pfnSSMR3Close(m_pSsm);
#else
        int vrc = SSMR3Close(m_pSsm);
#endif
        AssertRCReturn(vrc, vrc);
    }

    if (m_hVfsFile != NIL_RTVFSFILE)
        RTVfsFileRelease(m_hVfsFile);

    m_hVfsFile = NIL_RTVFSFILE;
    m_pSsm     = NULL;
#ifdef VBOX_COM_INPROC
    m_pVMM     = NULL;
#endif

    return VINF_SUCCESS;
}
