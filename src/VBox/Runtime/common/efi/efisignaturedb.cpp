/* $Id: efisignaturedb.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - EFI signature database helpers.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/efi.h>

#include <iprt/cdefs.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/sg.h>
#include <iprt/uuid.h>

#include <iprt/formats/efi-signature.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * EFI signature entry.
 */
typedef struct RTEFISIGNATURE
{
    /** List node. */
    RTLISTNODE              NdLst;
    /** The signature owner. */
    RTUUID                  UuidOwner;
    /** Size of the signature data in bytes. */
    uint32_t                cbSignature;
    /** The signature data (variable in size). */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t                 abSignature[RT_FLEXIBLE_ARRAY];
} RTEFISIGNATURE;
/** Pointer to a EFI signature entry. */
typedef RTEFISIGNATURE *PRTEFISIGNATURE;
/** Pointer to a const EFI signature entry. */
typedef const RTEFISIGNATURE *PCRTEFISIGNATURE;


/**
 * The EFI signature database instance data.
 */
typedef struct RTEFISIGDBINT
{
    /** List head of the various signature types. */
    RTLISTANCHOR            aLstSigTypes[RTEFISIGTYPE_FIRST_INVALID];
} RTEFISIGDBINT;
/** Pointer to the EFI signature database instance data. */
typedef RTEFISIGDBINT *PRTEFISIGDBINT;


/**
 * Signature type descriptor.
 */
typedef struct RTEFISIGDBDESC
{
    /** The EFI GUID identifying the signature type. */
    EFI_GUID                GuidSignatureType;
    /** The additional signature header for this signature type. */
    uint32_t                cbSigHdr;
    /** Size of the signature data (including EFI_SIGNATURE_DATA),
     * can be 0 size varies with each signature (X.509 for example). */
    uint32_t                cbSig;
    /** The internal signature type enum. */
    RTEFISIGTYPE            enmSigType;
    /** Human readable string of the signature type. */
    const char              *pszName;
} RTEFISIGDBDESC;
/** Pointer to a signature type descriptor. */
typedef RTEFISIGDBDESC *PRTEFISIGDBDESC;
/** Pointer to a const signature type descriptor. */
typedef const RTEFISIGDBDESC *PCRTEFISIGDBDESC;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Mapping of EFI signature GUIDs to their IPRT signature type equivalent.
 */
static const RTEFISIGDBDESC g_aGuid2SigTypeMapping[] =
{
    { EFI_NULL_GUID,                          0,                                    0,  RTEFISIGTYPE_INVALID,        "INVALID"          },
    { EFI_SIGNATURE_TYPE_GUID_SHA256,         0, EFI_SIGNATURE_TYPE_SZ_SHA256,          RTEFISIGTYPE_SHA256,         "SHA-256"          },
    { EFI_SIGNATURE_TYPE_GUID_RSA2048,        0, EFI_SIGNATURE_TYPE_SZ_RSA2048,         RTEFISIGTYPE_RSA2048,        "RSA-2048"         },
    { EFI_SIGNATURE_TYPE_GUID_RSA2048_SHA256, 0, EFI_SIGNATURE_TYPE_SZ_RSA2048_SHA256,  RTEFISIGTYPE_RSA2048_SHA256, "RSA-2048/SHA-256" },
    { EFI_SIGNATURE_TYPE_GUID_SHA1,           0, EFI_SIGNATURE_TYPE_SZ_SHA1,            RTEFISIGTYPE_SHA1,           "SHA-1"            },
    { EFI_SIGNATURE_TYPE_GUID_RSA2048_SHA1,   0, EFI_SIGNATURE_TYPE_SZ_RSA2048_SHA1,    RTEFISIGTYPE_RSA2048_SHA1,   "RSA-2048/SHA-1"   },
    { EFI_SIGNATURE_TYPE_GUID_X509,           0,                                    0,  RTEFISIGTYPE_X509,           "X.509"            }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Returns the internal siganture type descriptor for the given EFI GUID.
 *
 * @returns Pointer to the descriptor if found or NULL if not.
 * @param   pGuid               The EFI signature type GUID to look for.
 */
static PCRTEFISIGDBDESC rtEfiSigDbGetDescByGuid(PCEFI_GUID pGuid)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aGuid2SigTypeMapping); i++)
        if (!RTEfiGuidCompare(&g_aGuid2SigTypeMapping[i].GuidSignatureType, pGuid))
            return &g_aGuid2SigTypeMapping[i];

    return NULL;
}


/**
 * Validates the given signature list header.
 *
 * @returns Flag whether the list header is considered valid.
 * @param   pLstHdr             The list header to validate.
 * @param   pDesc               The descriptor for the signature type of the given list.
 */
static bool rtEfiSigDbSigHdrValidate(PCEFI_SIGNATURE_LIST pLstHdr, PCRTEFISIGDBDESC pDesc)
{
    uint32_t cbSigLst = RT_LE2H_U32(pLstHdr->cbSigLst);
    uint32_t cbSigHdr = RT_LE2H_U32(pLstHdr->cbSigHdr);
    uint32_t cbSig    = RT_LE2H_U32(pLstHdr->cbSig);

    if (cbSigHdr != pDesc->cbSigHdr)
        return false;
    if (cbSig < sizeof(EFI_SIGNATURE_DATA))
        return false;
    if (   pDesc->cbSig
        && pLstHdr->cbSig != pDesc->cbSig)
        return false;
    if (   cbSigLst <= sizeof(*pLstHdr)
        || cbSigLst <= cbSigHdr
        || cbSigLst <= cbSig)
        return false;
    if ((cbSigLst - sizeof(*pLstHdr) - cbSigHdr) % cbSig)
        return false;

    return true;
}


/**
 * Loads a single signature list into the given signature database from the given file.
 *
 * @returns IPRT status code.
 * @param   pThis               The signature database instance.
 * @param   hVfsFileIn          The file to load the signature list from.
 * @param   pcbConsumed         Where to store the number of bytes consumed for this signature list on success.
 */
static int rtEfiSigDbLoadSigList(PRTEFISIGDBINT pThis, RTVFSFILE hVfsFileIn, uint64_t *pcbConsumed)
{
    EFI_SIGNATURE_LIST LstHdr;
    int rc = RTVfsFileRead(hVfsFileIn, &LstHdr, sizeof(LstHdr), NULL /*pcbRead*/);
    if (RT_SUCCESS(rc))
    {
        PCRTEFISIGDBDESC pDesc = rtEfiSigDbGetDescByGuid(&LstHdr.GuidSigType);
        if (pDesc)
        {
            if (rtEfiSigDbSigHdrValidate(&LstHdr, pDesc))
            {
                RTLISTANCHOR LstTmp;
                uint32_t cbSig     = RT_LE2H_U32(LstHdr.cbSig);
                uint32_t cbSigData = cbSig - sizeof(EFI_SIGNATURE_DATA);
                uint32_t cSigs     = (RT_LE2H_U32(LstHdr.cbSigLst) - RT_LE2H_U32(LstHdr.cbSigHdr)) / cbSig;

                /** @todo Skip/parse signature header if we have to add a type which has this != 0. */
                RTListInit(&LstTmp);
                for (uint32_t i = 0; i < cSigs && RT_SUCCESS(rc); i++)
                {
                    PRTEFISIGNATURE pSig = (PRTEFISIGNATURE)RTMemAllocZ(RT_UOFFSETOF_DYN(RTEFISIGNATURE, abSignature[cbSigData]));
                    if (pSig)
                    {
                        EFI_SIGNATURE_DATA SigData;
                        rc = RTVfsFileRead(hVfsFileIn, &SigData, sizeof(SigData), NULL /*pcbRead*/);
                        if (RT_SUCCESS(rc))
                            rc = RTVfsFileRead(hVfsFileIn, &pSig->abSignature[0], cbSigData, NULL /*pcbRead*/);
                        if (RT_SUCCESS(rc))
                        {
                            RTEfiGuidToUuid(&pSig->UuidOwner, &SigData.GuidOwner);
                            pSig->cbSignature = cbSigData;
                            RTListAppend(&LstTmp, &pSig->NdLst);
                        }
                        else
                            RTMemFree(pSig);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }

                if (RT_SUCCESS(rc))
                {
                    /* Add the signatures to the list. */
                    RTListConcatenate(&pThis->aLstSigTypes[pDesc->enmSigType], &LstTmp);
                    *pcbConsumed = sizeof(LstHdr) + RT_LE2H_U32(LstHdr.cbSigHdr) + cSigs * cbSig;
                }
                else
                {
                    /* Destroy the temporary list. */
                    PRTEFISIGNATURE pIt, pItNext;

                    RTListForEachSafe(&LstTmp, pIt, pItNext, RTEFISIGNATURE, NdLst)
                    {
                        RTListNodeRemove(&pIt->NdLst);
                        RTMemFree(pIt);
                    }
                }
            }
            else
                rc = VERR_NOT_SUPPORTED;
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }

    return rc;
}


/**
 * De-duplicate a signature database.
 *
 * @returns IPRT status code.
 * @param   pThis               The signature database instance.
 */
static int rtEfiSigDbDeduplicate(PRTEFISIGDBINT pThis)
{
    /** @todo This currently deduplicates list nodes as a whole, not looking into signature lists.
     * Good enough for the X.509 certificates which matter most to eliminate multiple enrollments. */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aLstSigTypes); i++)
    {
        PRTEFISIGNATURE pIt, pItNext;
        RTListForEachSafe(&pThis->aLstSigTypes[i], pIt, pItNext, RTEFISIGNATURE, NdLst)
        {
            PRTEFISIGNATURE pIt2;
            RTListForEach(&pThis->aLstSigTypes[i], pIt2, RTEFISIGNATURE, NdLst)
            {
                /* Compare up to element before pIt. */
                if (pIt == pIt2)
                    break;
                if (   pIt->cbSignature == pIt2->cbSignature
                    && !RTUuidCompare(&pIt->UuidOwner, &pIt2->UuidOwner)
                    && !memcmp(&pIt->abSignature[0], &pIt2->abSignature[0], pIt->cbSignature))
                {
                    RTListNodeRemove(&pIt->NdLst);
                    RTMemFree(pIt);
                    break;
                }
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Variant for writing a list of signatures where each signature gets its own signature list header
 * (for types where each signature can differ in size like X.509).
 *
 * @returns IPRT status code.
 * @param   pLst                The list of signatures to write.
 * @param   pDesc               The signature type descriptor.
 * @param   hVfsFileOut         The file to write the database to.
 * @param   pcbThisWritten      Where to store the number of bytes written for the given signature list.
 */
static int rtEfiSigDbWriteListSingle(PRTLISTANCHOR pLst, PCRTEFISIGDBDESC pDesc, RTVFSFILE hVfsFileOut, size_t *pcbThisWritten)
{
    int rc = VINF_SUCCESS;
    size_t cbWritten = 0;
    PRTEFISIGNATURE pIt;

    RTListForEach(pLst, pIt, RTEFISIGNATURE, NdLst)
    {
        EFI_SIGNATURE_LIST LstHdr;
        EFI_SIGNATURE_DATA SigData;
        LstHdr.GuidSigType = pDesc->GuidSignatureType;
        LstHdr.cbSigLst    = RT_H2LE_U32(sizeof(LstHdr) + sizeof(SigData) + pDesc->cbSigHdr + pIt->cbSignature);
        LstHdr.cbSigHdr    = RT_H2LE_U32(pDesc->cbSigHdr);
        LstHdr.cbSig       = RT_H2LE_U32(pIt->cbSignature + sizeof(SigData));
        RTEfiGuidFromUuid(&SigData.GuidOwner, &pIt->UuidOwner);

        RTSGSEG aSegs[3];
        RTSGBUF SgBuf;

        Assert(!pDesc->cbSigHdr);
        aSegs[0].pvSeg = &LstHdr;
        aSegs[0].cbSeg = sizeof(LstHdr);
        aSegs[1].pvSeg = &SigData;
        aSegs[1].cbSeg = sizeof(SigData);
        aSegs[2].pvSeg = &pIt->abSignature[0];
        aSegs[2].cbSeg = pIt->cbSignature;
        RTSgBufInit(&SgBuf, &aSegs[0], RT_ELEMENTS(aSegs));
        rc = RTVfsFileSgWrite(hVfsFileOut, -1, &SgBuf, true /*fBlocking*/, NULL /*pcbWritten*/);
        if (RT_FAILURE(rc))
            break;

        cbWritten += sizeof(LstHdr) + sizeof(SigData) + pDesc->cbSigHdr + pIt->cbSignature;
    }

    if (RT_SUCCESS(rc))
        *pcbThisWritten = cbWritten;

    return rc;
}


/**
 * Writes the given signature list to the database in the given file.
 *
 * @returns IPRT status code.
 * @param   pLst                The list of signatures to write.
 * @param   pDesc               The signature type descriptor.
 * @param   hVfsFileOut         The file to write the database to.
 * @param   pcbThisWritten      Where to store the number of bytes written for the given signature list.
 */
static int rtEfiSigDbWriteList(PRTLISTANCHOR pLst, PCRTEFISIGDBDESC pDesc, RTVFSFILE hVfsFileOut, size_t *pcbThisWritten)
{
    /*
     * For signature lists where each signature can have a different size (X.509 for example)
     * writing a new list for each signature is required which is done by a dedicated method.
     */
    if (!pDesc->cbSig)
        return rtEfiSigDbWriteListSingle(pLst, pDesc, hVfsFileOut, pcbThisWritten);


    /* Count the number of signatures first. */
    uint32_t cSigs = 0;
    PRTEFISIGNATURE pIt;

    RTListForEach(pLst, pIt, RTEFISIGNATURE, NdLst)
    {
        cSigs++;
    }

    EFI_SIGNATURE_LIST LstHdr;
    LstHdr.GuidSigType = pDesc->GuidSignatureType;
    LstHdr.cbSigLst    = RT_H2LE_U32(sizeof(LstHdr) + pDesc->cbSigHdr + cSigs * pDesc->cbSig);
    LstHdr.cbSigHdr    = RT_H2LE_U32(pDesc->cbSigHdr);
    LstHdr.cbSig       = RT_H2LE_U32(pDesc->cbSig);

    int rc = RTVfsFileWrite(hVfsFileOut, &LstHdr, sizeof(LstHdr), NULL /*pcbWritten*/);
    if (RT_SUCCESS(rc))
    {
        RTListForEach(pLst, pIt, RTEFISIGNATURE, NdLst)
        {
            RTSGSEG aSegs[2];
            RTSGBUF SgBuf;
            EFI_SIGNATURE_DATA SigData;
            RTEfiGuidFromUuid(&SigData.GuidOwner, &pIt->UuidOwner);

            Assert(pDesc->cbSig == pIt->cbSignature);
            aSegs[0].pvSeg = &SigData;
            aSegs[0].cbSeg = sizeof(SigData);
            aSegs[1].pvSeg = &pIt->abSignature[0];
            aSegs[1].cbSeg = pIt->cbSignature;
            RTSgBufInit(&SgBuf, &aSegs[0], RT_ELEMENTS(aSegs));
            rc = RTVfsFileSgWrite(hVfsFileOut, -1, &SgBuf, true /*fBlocking*/, NULL /*pcbWritten*/);
            if (RT_FAILURE(rc))
                break;
        }
    }

    if (RT_SUCCESS(rc))
        *pcbThisWritten = sizeof(LstHdr) + pDesc->cbSigHdr + cSigs * pDesc->cbSig;

    return rc;
}


/**
 * Allocate a new signature of the given size.
 *
 * @returns Pointer to the new signature or NULL if out of memory.
 * @param   pUuidOwner          The UUID of the signature owner.
 * @param   cbSig               Size of the signature data in bytes.
 */
static PRTEFISIGNATURE rtEfiSigDbAllocSignature(PCRTUUID pUuidOwner, uint32_t cbSig)
{
    PRTEFISIGNATURE pSig = (PRTEFISIGNATURE)RTMemAllocZ(RT_UOFFSETOF_DYN(RTEFISIGNATURE, abSignature[cbSig]));
    if (pSig)
    {
        pSig->UuidOwner   = *pUuidOwner;
        pSig->cbSignature = cbSig;
    }

    return pSig;
}


RTDECL(int) RTEfiSigDbCreate(PRTEFISIGDB phEfiSigDb)
{
    AssertPtrReturn(phEfiSigDb, VERR_INVALID_POINTER);

    PRTEFISIGDBINT pThis = (PRTEFISIGDBINT)RTMemAllocZ(sizeof(*pThis));
    if (RT_LIKELY(pThis))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aLstSigTypes); i++)
            RTListInit(&pThis->aLstSigTypes[i]);
        *phEfiSigDb = pThis;
        return VINF_SUCCESS;
    }

    return VERR_NO_MEMORY;
}


RTDECL(int) RTEfiSigDbDestroy(RTEFISIGDB hEfiSigDb)
{
    PRTEFISIGDBINT pThis = hEfiSigDb;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aLstSigTypes); i++)
    {
        PRTEFISIGNATURE pIt, pItNext;

        RTListForEachSafe(&pThis->aLstSigTypes[i], pIt, pItNext, RTEFISIGNATURE, NdLst)
        {
            RTListNodeRemove(&pIt->NdLst);
            RTMemFree(pIt);
        }
    }

    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int) RTEfiSigDbAddFromExistingDb(RTEFISIGDB hEfiSigDb, RTVFSFILE hVfsFileIn)
{
    PRTEFISIGDBINT pThis = hEfiSigDb;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    uint64_t cbFile;
    int rc = RTVfsFileQuerySize(hVfsFileIn, &cbFile);
    if (   RT_SUCCESS(rc)
        && cbFile)
    {
        do
        {
            uint64_t cbConsumed = 0;
            rc = rtEfiSigDbLoadSigList(pThis, hVfsFileIn, &cbConsumed);
            cbFile -= cbConsumed;
        } while (   RT_SUCCESS(rc)
                 && cbFile);
    }

    int rc2 = rtEfiSigDbDeduplicate(pThis);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}


RTDECL(int) RTEfiSigDbAddSignatureFromFile(RTEFISIGDB hEfiSigDb, RTEFISIGTYPE enmSigType, PCRTUUID pUuidOwner, RTVFSFILE hVfsFileIn)
{
    PRTEFISIGDBINT pThis = hEfiSigDb;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(enmSigType >= RTEFISIGTYPE_FIRST_VALID && enmSigType < RTEFISIGTYPE_FIRST_INVALID, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pUuidOwner, VERR_INVALID_POINTER);

    PCRTEFISIGDBDESC pDesc = &g_aGuid2SigTypeMapping[enmSigType];
    uint64_t cbSig = 0;
    int rc = RTVfsFileQuerySize(hVfsFileIn, &cbSig);
    if (RT_SUCCESS(rc))
    {
        if (   (   !pDesc->cbSig
                || pDesc->cbSig - sizeof(EFI_SIGNATURE_DATA) == cbSig)
            && cbSig < UINT32_MAX)
        {
            PRTEFISIGNATURE pSig = rtEfiSigDbAllocSignature(pUuidOwner, (uint32_t)cbSig);
            if (pSig)
            {
                rc = RTVfsFileRead(hVfsFileIn, &pSig->abSignature[0], (size_t)cbSig, NULL /*pcbRead*/);
                if (RT_SUCCESS(rc))
                    RTListAppend(&pThis->aLstSigTypes[enmSigType], &pSig->NdLst);
                else
                    RTMemFree(pSig);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    int rc2 = rtEfiSigDbDeduplicate(pThis);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}


RTDECL(int) RTEfiSigDbAddSignatureFromBuf(RTEFISIGDB hEfiSigDb, RTEFISIGTYPE enmSigType, PCRTUUID pUuidOwner,
                                          const void *pvBuf, size_t cbBuf)
{
    PRTEFISIGDBINT pThis = hEfiSigDb;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(enmSigType >= RTEFISIGTYPE_FIRST_VALID && enmSigType < RTEFISIGTYPE_FIRST_INVALID, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pUuidOwner, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf && cbBuf < UINT32_MAX, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    PCRTEFISIGDBDESC pDesc = &g_aGuid2SigTypeMapping[enmSigType];
    if (   !pDesc->cbSig
        || pDesc->cbSig - sizeof(EFI_SIGNATURE_DATA) == cbBuf)
    {
        PRTEFISIGNATURE pSig = rtEfiSigDbAllocSignature(pUuidOwner, (uint32_t)cbBuf);
        if (pSig)
        {
            memcpy(&pSig->abSignature[0], pvBuf, cbBuf);
            RTListAppend(&pThis->aLstSigTypes[enmSigType], &pSig->NdLst);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    int rc2 = rtEfiSigDbDeduplicate(pThis);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}


RTDECL(int) RTEfiSigDbWriteToFile(RTEFISIGDB hEfiSigDb, RTVFSFILE hVfsFileOut)
{
    PRTEFISIGDBINT pThis = hEfiSigDb;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    size_t cbSigDb = 0;
    for (uint32_t i = RTEFISIGTYPE_FIRST_VALID; i < RT_ELEMENTS(pThis->aLstSigTypes) && RT_SUCCESS(rc); i++)
    {
        if (!RTListIsEmpty(&pThis->aLstSigTypes[i]))
        {
            size_t cbThisWritten = 0;
            rc = rtEfiSigDbWriteList(&pThis->aLstSigTypes[i], &g_aGuid2SigTypeMapping[i], hVfsFileOut, &cbThisWritten);
            if (RT_SUCCESS(rc))
                cbSigDb += cbThisWritten;
        }
    }

    if (RT_SUCCESS(rc))
        rc = RTVfsFileSetSize(hVfsFileOut, cbSigDb, RTVFSFILE_SIZE_F_NORMAL);

    return rc;
}


RTDECL(int) RTEfiSigDbEnum(RTEFISIGDB hEfiSigDb, PFNRTEFISIGDBENUMSIG pfnEnumSig, void *pvUser)
{
    PRTEFISIGDBINT pThis = hEfiSigDb;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    for (uint32_t i = RTEFISIGTYPE_FIRST_VALID; i < RT_ELEMENTS(pThis->aLstSigTypes); i++)
    {
        PRTEFISIGNATURE pIt;

        RTListForEach(&pThis->aLstSigTypes[i], pIt, RTEFISIGNATURE, NdLst)
        {
            int rc = pfnEnumSig(pThis, (RTEFISIGTYPE)i, &pIt->UuidOwner, &pIt->abSignature[0], pIt->cbSignature, pvUser);
            if (rc != VINF_SUCCESS)
                return rc;
        }
    }

    return VINF_SUCCESS;
}


RTDECL(const char *) RTEfiSigDbTypeStringify(RTEFISIGTYPE enmSigType)
{
    AssertReturn(enmSigType < RTEFISIGTYPE_FIRST_INVALID, NULL);
    return g_aGuid2SigTypeMapping[enmSigType].pszName;
}


RTDECL(PCEFI_GUID) RTEfiSigDbTypeGetGuid(RTEFISIGTYPE enmSigType)
{
    AssertReturn(enmSigType < RTEFISIGTYPE_FIRST_INVALID, NULL);
    return &g_aGuid2SigTypeMapping[enmSigType].GuidSignatureType;
}

