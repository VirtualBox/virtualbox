/* $Id: tstVBoxCrypto.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstVBoxCrypto - Testcase for the cryptographic support module.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/VBoxCryptoIf.h>
#include <VBox/err.h>

#include <iprt/file.h>
#include <iprt/test.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/memsafer.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/vfs.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;
static const uint8_t g_abDek[64]          = { 0x42 };
static const char    g_szPassword[]       = "testtesttest";
static const char    g_szPasswordWrong[]  = "testtest";

static const char *g_aCiphers[] =
{
    "AES-XTS128-PLAIN64",
    "AES-GCM128",
    "AES-CTR128",

    "AES-XTS256-PLAIN64",
    "AES-GCM256",
    "AES-CTR256"
};

#define CHECK_STR(str1, str2)  do { if (strcmp(str1, str2)) { RTTestIFailed("line %u: '%s' != '%s' (*)", __LINE__, str1, str2); } } while (0)
#define CHECK_BYTES(bytes1, bytes2, size)  do { if (memcmp(bytes1, bytes2, size)) { RTTestIFailed("line %u: '%s' != '%s' (*)", __LINE__, #bytes1, bytes2); } } while (0)


/**
 * Creates a new cryptographic context and returns the encoded string version on success.
 *
 * @returns VBox status code.
 * @param   pCryptoIf           Pointer to the cryptographic interface.
 * @param   pszCipher           The cipher to use.
 * @param   pszPassword         The password to use.
 * @param   ppszCtx             Where to store the pointer to the context on success.
 */
static int tstCryptoCtxCreate(PCVBOXCRYPTOIF pCryptoIf, const char *pszCipher, const char *pszPassword, char **ppszCtx)
{
    VBOXCRYPTOCTX hCryptoCtx;

    int rc = pCryptoIf->pfnCryptoCtxCreate(pszCipher, pszPassword, &hCryptoCtx);
    if (RT_SUCCESS(rc))
    {
        rc = pCryptoIf->pfnCryptoCtxSave(hCryptoCtx, ppszCtx);
        int rc2 = pCryptoIf->pfnCryptoCtxDestroy(hCryptoCtx);
        AssertReleaseRC(rc2);
    }

    return rc;
}


/**
 * Writes data to the given file until the given size is reached.
 *
 * @returns VBox status code.
 * @param   hVfsFile            The file handle to write to.
 * @param   cbWrite             Number of bytes to write.
 */
static int tstCryptoVfsWrite(RTVFSFILE hVfsFile, size_t cbWrite)
{
    RTTestISub("Writing to encrypted file");

    int rc = VINF_SUCCESS;
    size_t cbBufLeft = _128K;
    void *pv = RTMemTmpAllocZ(cbBufLeft);
    if (pv)
    {
        size_t cbLeft = cbWrite;
        uint32_t cCounter = 0;
        uint8_t *pb = (uint8_t *)pv;

        /* Fill the counter buffer. */
        uint32_t *pu32 = (uint32_t *)pv;
        for (uint32_t i = 0; i < cbBufLeft / sizeof(uint32_t); i++)
            *pu32++ = cCounter++;


        for (;;)
        {
            size_t cbThisWrite = RTRandU64Ex(1, RT_MIN(cbBufLeft, cbLeft));
            rc = RTVfsFileWrite(hVfsFile, pb, cbThisWrite, NULL /*pcbWritten*/);
            if (RT_FAILURE(rc))
            {
                RTTestIFailed("Writing to file failed with %Rrc (cbLeft=%zu, cbBufLeft=%zu, cbThisWrite=%zu)",
                              rc, cbLeft, cbBufLeft, cbThisWrite);
                break;
            }

            cbLeft    -= cbThisWrite;
            cbBufLeft -= cbThisWrite;
            pb        += cbThisWrite;

            if (!cbBufLeft)
            {
                /* Fill the counter buffer again. */
                pu32 = (uint32_t *)pv;
                pb = (uint8_t *)pv;
                cbBufLeft = _128K;
                for (uint32_t i = 0; i < cbBufLeft / sizeof(uint32_t); i++)
                    *pu32++ = cCounter++;
            }

            if (!cbLeft)
                break;
        }

        RTMemTmpFree(pv);
    }
    else
    {
        RTTestIFailed("Allocating write buffer failed - out of memory");
        rc = VERR_NO_MEMORY;
    }

    RTTestISubDone();
    return rc;
}


/**
 * Writes data to the given file until the given size is reached.
 *
 * @returns VBox status code.
 * @param   hVfsFile            The file handle to write to.
 * @param   cbFile              Size of the file payload in bytes.
 */
static int tstCryptoVfsReadAndVerify(RTVFSFILE hVfsFile, size_t cbFile)
{
    RTTestISub("Reading from encrypted file and verifying data");

    int rc = VINF_SUCCESS;
    void *pv = RTMemTmpAllocZ(_128K);
    if (pv)
    {
        size_t cbLeft = cbFile;
        uint32_t cCounter = 0;

        for (;;)
        {
            /* Read the data in multiple calls. */
            size_t cbBufLeft = RT_MIN(cbLeft, _128K);
            uint8_t *pb = (uint8_t *)pv;

            while (cbBufLeft)
            {
                size_t cbThisRead = RTRandU64Ex(1, RT_MIN(cbBufLeft, cbLeft));
                rc = RTVfsFileRead(hVfsFile, pb, cbThisRead, NULL /*pcbWritten*/);
                if (RT_FAILURE(rc))
                {
                    RTTestIFailed("Reading from file failed with %Rrc (cbLeft=%zu, cbBufLeft=%zu, cbThisRead=%zu)",
                                  rc, cbLeft, cbBufLeft, cbThisRead);
                    break;
                }

                cbBufLeft -= cbThisRead;
                pb        += cbThisRead;
            }

            if (RT_FAILURE(rc))
                break;

            /* Verify the read data. */
            size_t cbInBuffer = RT_MIN(cbLeft, _128K);
            Assert(!(cbInBuffer % sizeof(uint32_t)));
            uint32_t *pu32 = (uint32_t *)pv;

            for (uint32_t i = 0; i < cbInBuffer / sizeof(uint32_t); i++)
            {
                if (*pu32 != cCounter)
                {
                    RTTestIFailed("Reading from file resulted in corrupted data (expected '%#x' got '%#x')",
                                  cCounter, *pu32);
                    break;
                }

                pu32++;
                cCounter++;
            }

            cbLeft -= RT_MIN(cbLeft, _128K);
            if (!cbLeft)
                break;
        }

        RTMemTmpFree(pv);
    }
    else
    {
        RTTestIFailed("Allocating read buffer failed - out of memory");
        rc = VERR_NO_MEMORY;
    }

    RTTestISubDone();
    return rc;
}


/**
 * Testing some basics of the encrypted file VFS code.
 *
 * @param   pCryptoIf           Pointer to the callback table.
 */
static void tstCryptoVfsBasics(PCVBOXCRYPTOIF pCryptoIf)
{
    RTTestISub("Encrypted file - Basics");

    RTTestDisableAssertions(g_hTest);

    char *pszCtx = NULL;
    int rc = tstCryptoCtxCreate(pCryptoIf, g_aCiphers[4], g_szPassword, &pszCtx);
    if (RT_SUCCESS(rc))
    {
        /* Create the memory file to write to. */
        RTVFSFILE hVfsFile;
        rc = RTVfsMemFileCreate(NIL_RTVFSIOSTREAM, 0 /*cbEstimate*/, &hVfsFile);
        if (RT_SUCCESS(rc))
        {
            RTVFSFILE hVfsFileEnc;

            RTTestISub("Creating encrypted file");

            rc = pCryptoIf->pfnCryptoFileFromVfsFile(hVfsFile, pszCtx, g_szPassword, &hVfsFileEnc);
            if (RT_SUCCESS(rc))
            {
                RTTestISubDone();

                size_t cbFile = RT_ALIGN_Z(RTRandU32Ex(_1K, 10 * _1M), sizeof(uint32_t)); /* Align to full counter field size. */
                rc = tstCryptoVfsWrite(hVfsFileEnc, cbFile);
                RTVfsFileRelease(hVfsFileEnc); /* Close file. */
                if (RT_SUCCESS(rc))
                {
                    /* Reopen for reading. */
                    RTTestISub("Open encrypted file");

                    /* Reset the memory file offset. */
                    RTVfsFileSeek(hVfsFile, 0, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);

                    rc = pCryptoIf->pfnCryptoFileFromVfsFile(hVfsFile, pszCtx, g_szPassword, &hVfsFileEnc);
                    if (RT_SUCCESS(rc))
                    {
                        RTTestISubDone();

                        RTTestISub("Query encrypted file size");
                        uint64_t cbFileRd;
                        rc = RTVfsFileQuerySize(hVfsFileEnc, &cbFileRd);
                        if (RT_SUCCESS(rc))
                        {
                            if (cbFile != cbFileRd)
                                RTTestIFailed("Unexpected file size, got %#llx expected %#zx", cbFileRd, cbFile);

                            RTTestISubDone();
                            tstCryptoVfsReadAndVerify(hVfsFileEnc, cbFile);
                        }
                        else
                            RTTestIFailed("Querying encrypted file size failed %Rrc", rc);

                        RTVfsFileRelease(hVfsFileEnc); /* Close file. */
                    }
                    else
                        RTTestIFailed("Opening encrypted file for reading failed with %Rrc", rc);

                }
                /* Error set on failure. */
            }
            else
                RTTestIFailed("Creating encrypted file handle failed with %Rrc", rc);

            RTVfsFileRelease(hVfsFile);
        }
        else
            RTTestIFailed("Creating a new encrypted file failed with %Rrc", rc);

        RTMemFree(pszCtx);
    }
    else
        RTTestIFailed("Creating a new encrypted context failed with %Rrc", rc);

    RTTestRestoreAssertions(g_hTest);
    RTTestISubDone();
}


/**
 * Testing some basics of the crypto keystore code.
 *
 * @param   pCryptoIf           Pointer to the callback table.
 */
static void tstCryptoKeyStoreBasics(PCVBOXCRYPTOIF pCryptoIf)
{
    RTTestISub("Crypto Keystore - Basics");

    RTTestDisableAssertions(g_hTest);

    for (uint32_t i = 0; i < RT_ELEMENTS(g_aCiphers); i++)
    {
        RTTestISubF("Creating a new keystore for cipher '%s'", g_aCiphers[i]);

        char *pszKeystoreEnc = NULL; /**< The encoded keystore. */
        int rc = pCryptoIf->pfnCryptoKeyStoreCreate(g_szPassword, &g_abDek[0], sizeof(g_abDek),
                                                    g_aCiphers[i], &pszKeystoreEnc);
        if (RT_SUCCESS(rc))
        {
            uint8_t *pbKey = NULL;
            size_t cbKey = 0;
            char *pszCipher = NULL;

            RTTestSub(g_hTest, "Trying to unlock DEK with wrong password");
            rc = pCryptoIf->pfnCryptoKeyStoreGetDekFromEncoded(pszKeystoreEnc, g_szPasswordWrong,
                                                               &pbKey, &cbKey, &pszCipher);
            RTTESTI_CHECK_RC(rc, VERR_VD_PASSWORD_INCORRECT);

            RTTestSub(g_hTest, "Trying to unlock DEK with correct password");
            rc = pCryptoIf->pfnCryptoKeyStoreGetDekFromEncoded(pszKeystoreEnc, g_szPassword,
                                                               &pbKey, &cbKey, &pszCipher);
            RTTESTI_CHECK_RC_OK(rc);
            if (RT_SUCCESS(rc))
            {
                RTTESTI_CHECK(cbKey == sizeof(g_abDek));
                CHECK_STR(pszCipher, g_aCiphers[i]);
                CHECK_BYTES(pbKey, &g_abDek[0], sizeof(g_abDek));

                RTMemSaferFree(pbKey, cbKey);
            }

            RTMemFree(pszKeystoreEnc);
        }
        else
            RTTestIFailed("Creating a new keystore failed with %Rrc", rc);
    }

    RTTestRestoreAssertions(g_hTest);
}


int main(int argc, char *argv[])
{
    /*
     * Initialization.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate("tstVBoxCrypto", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    RTTestSub(g_hTest, "Loading the cryptographic support module");
    const char *pszModCrypto = NULL;
    if (argc == 2)
    {
        /* The module to load is given on the command line. */
        pszModCrypto = argv[1];
    }
    else
    {
        /* Try find it in the extension pack. */
        /** @todo */
        RTTestSkipped(g_hTest, "Getting the module from the extension pack is not implemented yet, skipping testcase");
    }

    if (pszModCrypto)
    {
        RTLDRMOD hLdrModCrypto = NIL_RTLDRMOD;
        int rc = RTLdrLoad(pszModCrypto, &hLdrModCrypto);
        if (RT_SUCCESS(rc))
        {
            PFNVBOXCRYPTOENTRY pfnCryptoEntry = NULL;
            rc = RTLdrGetSymbol(hLdrModCrypto, VBOX_CRYPTO_MOD_ENTRY_POINT, (void **)&pfnCryptoEntry);
            if (RT_SUCCESS(rc))
            {
                PCVBOXCRYPTOIF pCryptoIf = NULL;
                rc = pfnCryptoEntry(&pCryptoIf);
                if (RT_SUCCESS(rc))
                {
                    /* Loading succeeded, now we can start real testing. */
                    tstCryptoKeyStoreBasics(pCryptoIf);
                    tstCryptoVfsBasics(pCryptoIf);
                }
                else
                    RTTestIFailed("Calling '%s' failed with %Rrc", VBOX_CRYPTO_MOD_ENTRY_POINT, rc);
            }
            else
                RTTestIFailed("Failed to resolve entry point '%s' with %Rrc", VBOX_CRYPTO_MOD_ENTRY_POINT, rc);
        }
        else
            RTTestIFailed("Failed to load the crypto module '%s' with %Rrc", pszModCrypto, rc);
    }

    return RTTestSummaryAndDestroy(g_hTest);
}
