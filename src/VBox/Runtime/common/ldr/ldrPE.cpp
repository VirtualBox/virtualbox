/* $Id: ldrPE.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, Portable Executable (PE).
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
#define LOG_GROUP RTLOGGROUP_LDR
#include <iprt/ldr.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/asm-mem.h>
#include <iprt/dbg.h>
#include <iprt/err.h>
#include <iprt/latin1.h>
#include <iprt/log.h>
#include <iprt/md5.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/x86.h>
#if !defined(IPRT_WITHOUT_LDR_VERIFY) || !defined(IPRT_WITHOUT_LDR_PAGE_HASHING)
# include <iprt/zero.h>
#endif
#ifndef IPRT_WITHOUT_LDR_VERIFY
# include <iprt/crypto/pkcs7.h>
# include <iprt/crypto/spc.h>
# include <iprt/crypto/x509.h>
#endif
#include <iprt/formats/codeview.h>
#include <iprt/formats/pecoff.h>
#include "internal/ldr.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Converts rva to a type.
 * @param   pvBits  Pointer to base of image bits.
 * @param   rva     Relative virtual address.
 * @param   type    Type.
 */
#define PE_RVA2TYPE(pvBits, rva, type)  ((type) ((uintptr_t)pvBits + (uintptr_t)(rva)) )

/** The max size of the security directory. */
#ifdef IN_RING3
# define RTLDRMODPE_MAX_SECURITY_DIR_SIZE   _4M
#else
# define RTLDRMODPE_MAX_SECURITY_DIR_SIZE   _1M
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The PE loader structure.
 */
typedef struct RTLDRMODPE
{
    /** Core module structure. */
    RTLDRMODINTERNAL        Core;
    /** Pointer to internal copy of image bits.
     * @todo the reader should take care of this. */
    void                   *pvBits;
    /** The offset of the NT headers. */
    RTFOFF                  offNtHdrs;
    /** The offset of the first byte after the section table. */
    RTFOFF                  offEndOfHdrs;

    /** The machine type (IMAGE_FILE_HEADER::Machine). */
    uint16_t                u16Machine;
    /** The file flags (IMAGE_FILE_HEADER::Characteristics). */
    uint16_t                fFile;
    /** Number of sections (IMAGE_FILE_HEADER::NumberOfSections). */
    unsigned                cSections;
    /** Pointer to an array of the section headers related to the file. */
    PIMAGE_SECTION_HEADER   paSections;

    /** The RVA of the entry point (IMAGE_OPTIONAL_HEADER32::AddressOfEntryPoint). */
    RTUINTPTR               uEntryPointRVA;
    /** The base address of the image at link time (IMAGE_OPTIONAL_HEADER32::ImageBase). */
    RTUINTPTR               uImageBase;
    /** The size of the loaded image (IMAGE_OPTIONAL_HEADER32::SizeOfImage). */
    uint32_t                cbImage;
    /** Size of the header (IMAGE_OPTIONAL_HEADER32::SizeOfHeaders). */
    uint32_t                cbHeaders;
    /** Section alignment (IMAGE_OPTIONAL_HEADER32::SectionAlignment). */
    uint32_t                uSectionAlign;
    /** The image timestamp. */
    uint32_t                uTimestamp;
    /** The number of imports.  UINT32_MAX if not determined. */
    uint32_t                cImports;
    /** Set if the image is 64-bit, clear if 32-bit. */
    bool                    f64Bit;
    /** The import data directory entry. */
    IMAGE_DATA_DIRECTORY    ImportDir;
    /** The base relocation data directory entry. */
    IMAGE_DATA_DIRECTORY    RelocDir;
    /** The export data directory entry. */
    IMAGE_DATA_DIRECTORY    ExportDir;
    /** The debug directory entry. */
    IMAGE_DATA_DIRECTORY    DebugDir;
    /** The security directory entry. */
    IMAGE_DATA_DIRECTORY    SecurityDir;
    /** The exception data directory entry. */
    IMAGE_DATA_DIRECTORY    ExceptionDir;

    /** Offset of the first PKCS \#7 SignedData signature if present. */
    uint32_t                offPkcs7SignedData;
    /** Size of the first PKCS \#7 SignedData. */
    uint32_t                cbPkcs7SignedData;

    /** Copy of the optional header field DllCharacteristics. */
    uint16_t                fDllCharacteristics;
} RTLDRMODPE;
/** Pointer to the instance data for a PE loader module. */
typedef RTLDRMODPE *PRTLDRMODPE;


/**
 * PE Loader module operations.
 *
 * The PE loader has one operation which is a bit different between 32-bit and 64-bit PE images,
 * and for historical and performance reasons have been split into separate functions. Thus the
 * PE loader extends the RTLDROPS structure with this one entry.
 */
typedef struct RTLDROPSPE
{
    /** The usual ops. */
    RTLDROPS Core;

    /**
     * Resolves all imports.
     *
     * @returns iprt status code.
     * @param   pModPe          Pointer to the PE loader module structure.
     * @param   pvBitsR         Where to read raw image bits. (optional)
     * @param   pvBitsW         Where to store the imports. The size of this buffer is equal or
     *                          larger to the value returned by pfnGetImageSize().
     * @param   pfnGetImport    The callback function to use to resolve imports (aka unresolved externals).
     * @param   pvUser          User argument to pass to the callback.
     */
    DECLCALLBACKMEMBER(int, pfnResolveImports,(PRTLDRMODPE pModPe, const void *pvBitsR, void *pvBitsW, PFNRTLDRIMPORT pfnGetImport, void *pvUser));

    /** Dummy entry to make sure we've initialized it all. */
    RTUINT  uDummy;
} RTLDROPSPE, *PRTLDROPSPE;


/**
 * PE hash context union.
 */
typedef union RTLDRPEHASHCTXUNION
{
    RTSHA512CONTEXT Sha512;
    RTSHA384CONTEXT Sha384;
    RTSHA256CONTEXT Sha256;
    RTSHA1CONTEXT   Sha1;
    RTMD5CONTEXT    Md5;
} RTLDRPEHASHCTXUNION;
/** Pointer to a PE hash context union. */
typedef RTLDRPEHASHCTXUNION *PRTLDRPEHASHCTXUNION;


/**
 * PE hash digests
 */
typedef union RTLDRPEHASHRESUNION
{
    uint8_t abSha512[RTSHA512_HASH_SIZE];
    uint8_t abSha384[RTSHA384_HASH_SIZE];
    uint8_t abSha256[RTSHA256_HASH_SIZE];
    uint8_t abSha1[RTSHA1_HASH_SIZE];
    uint8_t abMd5[RTMD5_HASH_SIZE];
} RTLDRPEHASHRESUNION;
/** Pointer to a PE hash work set. */
typedef RTLDRPEHASHRESUNION *PRTLDRPEHASHRESUNION;

/**
 * Special places to watch out for when hashing a PE image.
 */
typedef struct RTLDRPEHASHSPECIALS
{
    uint32_t    cbToHash;
    uint32_t    offCksum;
    uint32_t    cbCksum;
    uint32_t    offSecDir;
    uint32_t    cbSecDir;
    uint32_t    offEndSpecial;
} RTLDRPEHASHSPECIALS;
/** Pointer to the structure with the special hash places. */
typedef RTLDRPEHASHSPECIALS *PRTLDRPEHASHSPECIALS;


#ifndef IPRT_WITHOUT_LDR_VERIFY
/**
 * Parsed data for one signature.
 */
typedef struct RTLDRPESIGNATUREONE
{
    /** The outer content info wrapper. */
    PRTCRPKCS7CONTENTINFO       pContentInfo;
    /** Pointer to the decoded SignedData inside the ContentInfo member. */
    PRTCRPKCS7SIGNEDDATA        pSignedData;
    /** Pointer to the indirect data content. */
    PRTCRSPCINDIRECTDATACONTENT pIndData;
    /** The digest type employed by the signature. */
    RTDIGESTTYPE                enmDigest;
    /** Set if we've already validate the image hash. */
    bool                        fValidatedImageHash;
    /** The signature number. */
    uint16_t                    iSignature;
    /** Hash result. */
    RTLDRPEHASHRESUNION         HashRes;
} RTLDRPESIGNATUREONE;
/** Pointer to the parsed data of one signature. */
typedef RTLDRPESIGNATUREONE *PRTLDRPESIGNATUREONE;

/**
 * Parsed signature data.
 */
typedef struct RTLDRPESIGNATURE
{
    /** Pointer to the raw signatures.  This is allocated in the continuation of
     * this structure to keep things simple.  The size is given by  the security
     * export directory. */
    WIN_CERTIFICATE const      *pRawData;
    /** The outer content info wrapper (primary signature). */
    RTCRPKCS7CONTENTINFO        PrimaryContentInfo;
    /** The info for the primary signature. */
    RTLDRPESIGNATUREONE         Primary;
    /** Number of nested signatures (zero if none). */
    uint16_t                    cNested;
    /** Pointer to an array of nested signatures (NULL if none). */
    PRTLDRPESIGNATUREONE        paNested;
    /** Hash scratch data. */
    RTLDRPEHASHCTXUNION         HashCtx;
} RTLDRPESIGNATURE;
/** Pointed to SigneData parsing stat and output. */
typedef RTLDRPESIGNATURE *PRTLDRPESIGNATURE;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtldrPEConvert32BitOptionalHeaderTo64Bit(PIMAGE_OPTIONAL_HEADER64 pOptHdr);
static void rtldrPEConvert32BitLoadConfigTo64Bit(PIMAGE_LOAD_CONFIG_DIRECTORY64 pLoadCfg);
static int  rtldrPEApplyFixups(PRTLDRMODPE pModPe, const void *pvBitsR, void *pvBitsW, RTUINTPTR BaseAddress, RTUINTPTR OldBaseAddress);
#ifndef IPRT_WITHOUT_LDR_PAGE_HASHING
static int  rtLdrPE_QueryPageHashes(PRTLDRMODPE pModPe, RTDIGESTTYPE enmDigest, void *pvBuf, size_t cbBuf, size_t *pcbRet);
static uint32_t rtLdrPE_GetHashablePages(PRTLDRMODPE pModPe);
#endif



/**
 * Reads a section of a PE image given by RVA + size, using mapped bits if
 * available or allocating heap memory and reading from the file.
 *
 * @returns IPRT status code.
 * @param   pThis               Pointer to the PE loader module structure.
 * @param   pvBits              Read only bits if available. NULL if not.
 * @param   uRva                The RVA to read at.
 * @param   cbMem               The number of bytes to read.
 * @param   ppvMem              Where to return the memory on success (heap or
 *                              inside pvBits).
 */
static int rtldrPEReadPartByRva(PRTLDRMODPE pThis, const void *pvBits, uint32_t uRva, uint32_t cbMem, void const **ppvMem)
{
    *ppvMem = NULL;
    if (!cbMem)
        return VINF_SUCCESS;

    /*
     * Use bits if we've got some.
     */
    if (pvBits)
    {
        *ppvMem = (uint8_t const *)pvBits + uRva;
        return VINF_SUCCESS;
    }
    if (pThis->pvBits)
    {
        *ppvMem = (uint8_t const *)pThis->pvBits + uRva;
        return VINF_SUCCESS;
    }

    /*
     * Allocate a buffer and read the bits from the file (or whatever).
     */
    if (!pThis->Core.pReader)
        return VERR_ACCESS_DENIED;

    uint8_t *pbMem = (uint8_t *)RTMemAllocZ(cbMem);
    if (!pbMem)
        return VERR_NO_MEMORY;
    *ppvMem = pbMem;

    /* Do the reading on a per section base. */
    uint64_t const cbFile = pThis->Core.pReader->pfnSize(pThis->Core.pReader);
    for (;;)
    {
        /* Translate the RVA into a file offset. */
        uint32_t offFile  = uRva;
        uint32_t cbToRead = cbMem;
        uint32_t cbToAdv  = cbMem;

        if (uRva < pThis->paSections[0].VirtualAddress)
        {
            /* Special header section. */
            cbToRead = pThis->paSections[0].VirtualAddress - uRva;
            if (cbToRead > cbMem)
                cbToRead = cbMem;
            cbToAdv = cbToRead;

            /* The following capping is an approximation. */
            uint32_t offFirstRawData = RT_ALIGN(pThis->cbHeaders, _4K);
            if (   pThis->paSections[0].PointerToRawData > 0
                && pThis->paSections[0].SizeOfRawData > 0)
                offFirstRawData = pThis->paSections[0].PointerToRawData;
            if (offFile >= offFirstRawData)
                cbToRead = 0;
            else if (offFile + cbToRead > offFirstRawData)
                cbToRead = offFile - offFirstRawData;
        }
        else
        {
            /* Find the matching section and its mapping size. */
            uint32_t j          = 0;
            uint32_t cbMapping  = 0;
            uint32_t offSection = 0;
            while (j < pThis->cSections)
            {
                cbMapping = (j + 1 < pThis->cSections ? pThis->paSections[j + 1].VirtualAddress : pThis->cbImage)
                          - pThis->paSections[j].VirtualAddress;
                offSection = uRva - pThis->paSections[j].VirtualAddress;
                if (offSection < cbMapping)
                    break;
                j++;
            }
            if (j >= cbMapping)
                break; /* This shouldn't happen, just return zeros if it does. */

            /* Adjust the sizes and calc the file offset. */
            if (offSection + cbToAdv > cbMapping)
                cbToAdv = cbToRead = cbMapping - offSection;

            if (   pThis->paSections[j].PointerToRawData > 0
                && pThis->paSections[j].SizeOfRawData > 0)
            {
                offFile = offSection;
                if (offFile + cbToRead > pThis->paSections[j].SizeOfRawData)
                    cbToRead = pThis->paSections[j].SizeOfRawData - offFile;
                offFile += pThis->paSections[j].PointerToRawData;
            }
            else
            {
                offFile  = UINT32_MAX;
                cbToRead = 0;
            }
        }

        /* Perform the read after adjusting a little (paranoia). */
        if (offFile > cbFile)
            cbToRead = 0;
        if (cbToRead)
        {
            if ((uint64_t)offFile + cbToRead > cbFile)
                cbToRead = (uint32_t)(cbFile - (uint64_t)offFile);
            int rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, pbMem, cbToRead, offFile);
            if (RT_FAILURE(rc))
            {
                RTMemFree((void *)*ppvMem);
                *ppvMem = NULL;
                return rc;
            }
        }

        /* Advance */
        if (cbMem <= cbToAdv)
            break;
        cbMem -= cbToAdv;
        pbMem += cbToAdv;
        uRva  += cbToAdv;
    }

    return VINF_SUCCESS;
}


/**
 * Reads a part of a PE file from the file and into a heap block.
 *
 * @returns IRPT status code.
 * @param   pThis               Pointer to the PE loader module structure..
 * @param   offFile             The file offset.
 * @param   cbMem               The number of bytes to read.
 * @param   ppvMem              Where to return the heap block with the bytes on
 *                              success.
 */
static int rtldrPEReadPartFromFile(PRTLDRMODPE pThis, uint32_t offFile, uint32_t cbMem, void const **ppvMem)
{
    *ppvMem = NULL;
    if (!cbMem)
        return VINF_SUCCESS;

    /*
     * Allocate a buffer and read the bits from the file (or whatever).
     */
    if (!pThis->Core.pReader)
        return VERR_ACCESS_DENIED;

    uint8_t *pbMem = (uint8_t *)RTMemAlloc(cbMem);
    if (!pbMem)
        return VERR_NO_MEMORY;

    int rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, pbMem, cbMem, offFile);
    if (RT_FAILURE(rc))
    {
        RTMemFree((void *)*ppvMem);
        return rc;
    }

    *ppvMem = pbMem;
    return VINF_SUCCESS;
}


/**
 * Reads a part of a PE image into memory one way or another.
 *
 * Either the RVA or the offFile must be valid.  We'll prefer the RVA if
 * possible.
 *
 * @returns IPRT status code.
 * @param   pThis               Pointer to the PE loader module structure.
 * @param   pvBits              Read only bits if available. NULL if not.
 * @param   uRva                The RVA to read at.
 * @param   offFile             The file offset.
 * @param   cbMem               The number of bytes to read.
 * @param   ppvMem              Where to return the memory on success (heap or
 *                              inside pvBits).
 */
static int rtldrPEReadPart(PRTLDRMODPE pThis, const void *pvBits, RTFOFF offFile, RTLDRADDR uRva,
                           uint32_t cbMem, void const **ppvMem)
{
    if (   uRva == NIL_RTLDRADDR
        || uRva         > pThis->cbImage
        || cbMem        > pThis->cbImage
        || uRva + cbMem > pThis->cbImage)
    {
        if (offFile < 0 || offFile >= UINT32_MAX)
            return VERR_INVALID_PARAMETER;
        return rtldrPEReadPartFromFile(pThis, (uint32_t)offFile, cbMem, ppvMem);
    }
    return rtldrPEReadPartByRva(pThis, pvBits, (uint32_t)uRva, cbMem, ppvMem);
}


/**
 * Frees up memory returned by rtldrPEReadPart*.
 *
 * @param   pThis               Pointer to the PE loader module structure..
 * @param   pvBits              Read only bits if available. NULL if not..
 * @param   pvMem               The memory we were given by the reader method.
 */
static void rtldrPEFreePart(PRTLDRMODPE pThis, const void *pvBits, void const *pvMem)
{
    if (!pvMem)
        return;

    if (pvBits        && (uintptr_t)pvMem - (uintptr_t)pvBits        < pThis->cbImage)
        return;
    if (pThis->pvBits && (uintptr_t)pvMem - (uintptr_t)pThis->pvBits < pThis->cbImage)
        return;

    RTMemFree((void *)pvMem);
}


/**
 * Reads a section of a PE image given by RVA + size.
 *
 * @returns IPRT status code.
 * @param   pThis               Pointer to the PE loader module structure.
 * @param   pvBits              Read only bits if available. NULL if not.
 * @param   uRva                The RVA to read at.
 * @param   cbMem               The number of bytes to read.
 * @param   pvDst               The destination buffer.
 */
static int rtldrPEReadPartByRvaInfoBuf(PRTLDRMODPE pThis, const void *pvBits, uint32_t uRva, uint32_t cbMem, void *pvDst)
{
    /** @todo consider optimizing this.   */
    const void *pvSrc = NULL;
    int rc = rtldrPEReadPartByRva(pThis, pvBits, uRva, cbMem, &pvSrc);
    if (RT_SUCCESS(rc))
    {
        memcpy(pvDst, pvSrc, cbMem);
        rtldrPEFreePart(pThis, NULL, pvSrc);
    }
    return rc;
}





/** @interface_method_impl{RTLDROPS,pfnGetImageSize} */
static DECLCALLBACK(size_t) rtldrPEGetImageSize(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;
    return pModPe->cbImage;
}


/**
 * Reads the image into memory.
 *
 * @returns iprt status code.
 * @param   pModPe      The PE module.
 * @param   pvBits      Where to store the bits, this buffer is at least pItem->Core.cbImage in size.
 */
static int rtldrPEGetBitsNoImportsNorFixups(PRTLDRMODPE pModPe, void *pvBits)
{
    /*
     * Both these checks are related to pfnDone().
     */
    PRTLDRREADER pReader = pModPe->Core.pReader;
    if (!pReader)
    {
        AssertMsgFailed(("You've called done!\n"));
        return VERR_WRONG_ORDER;
    }
    if (!pvBits)
        return VERR_NO_MEMORY;

    /*
     * Zero everything (could be done per section).
     */
    memset(pvBits, 0, pModPe->cbImage);

#ifdef PE_FILE_OFFSET_EQUALS_RVA
    /*
     * Read the entire image / file.
     */
    const uint64_t cbRawImage = pReader->pfnSize(pReader)
    rc = pReader->pfnRead(pReader, pvBits, RT_MIN(pModPe->cbImage, cbRawImage), 0);
    if (RT_FAILURE(rc))
        Log(("rtldrPE: %s: Reading %#x bytes at offset %#x failed, %Rrc!!! (the entire image)\n",
             pReader->pfnLogName(pReader), RT_MIN(pModPe->cbImage, cbRawImage), 0, rc));
#else

    /*
     * Read the headers.
     */
    int rc = pReader->pfnRead(pReader, pvBits, pModPe->cbHeaders, 0);
    if (RT_SUCCESS(rc))
    {
        /*
         * Read the sections.
         */
        PIMAGE_SECTION_HEADER pSH = pModPe->paSections;
        for (unsigned cLeft = pModPe->cSections; cLeft > 0; cLeft--, pSH++)
            if (   pSH->SizeOfRawData
                && pSH->Misc.VirtualSize
                && !(pSH->Characteristics & IMAGE_SCN_TYPE_NOLOAD))
            {
                uint32_t const cbToRead = RT_MIN(pSH->SizeOfRawData, pModPe->cbImage - pSH->VirtualAddress);
                Assert(pSH->VirtualAddress <= pModPe->cbImage);

                rc = pReader->pfnRead(pReader, (uint8_t *)pvBits + pSH->VirtualAddress, cbToRead, pSH->PointerToRawData);
                if (RT_FAILURE(rc))
                {
                    Log(("rtldrPE: %s: Reading %#x bytes at offset %#x failed, %Rrc - section #%d '%.*s'!!!\n",
                         pReader->pfnLogName(pReader), pSH->SizeOfRawData, pSH->PointerToRawData, rc,
                         pSH - pModPe->paSections, sizeof(pSH->Name), pSH->Name));
                    break;
                }
            }
    }
    else
        Log(("rtldrPE: %s: Reading %#x bytes at offset %#x failed, %Rrc!!!\n",
             pReader->pfnLogName(pReader), pModPe->cbHeaders, 0, rc));
#endif
    return rc;
}


/**
 * Reads the bits into the internal buffer pointed to by PRTLDRMODPE::pvBits.
 *
 * @returns iprt status code.
 * @param   pModPe      The PE module.
 */
static int rtldrPEReadBits(PRTLDRMODPE pModPe)
{
    Assert(!pModPe->pvBits);
    void *pvBitsW = RTMemAllocZ(pModPe->cbImage);
    if (!pvBitsW)
        return VERR_NO_MEMORY;
    int rc = rtldrPEGetBitsNoImportsNorFixups(pModPe, pvBitsW);
    if (RT_SUCCESS(rc))
        pModPe->pvBits = pvBitsW;
    else
        RTMemFree(pvBitsW);
    return rc;
}


/** @interface_method_impl{RTLDROPS,pfnGetBits} */
static DECLCALLBACK(int) rtldrPEGetBits(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR BaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;

    /*
     * Read the image.
     */
    int rc = rtldrPEGetBitsNoImportsNorFixups(pModPe, pvBits);
    if (RT_SUCCESS(rc))
    {
        /*
         * Resolve imports.
         */
        if (pfnGetImport)
            rc = ((PRTLDROPSPE)pMod->pOps)->pfnResolveImports(pModPe, pvBits, pvBits, pfnGetImport, pvUser);
        if (RT_SUCCESS(rc))
        {
            /*
             * Apply relocations.
             */
            rc = rtldrPEApplyFixups(pModPe, pvBits, pvBits, BaseAddress, pModPe->uImageBase);
            if (RT_SUCCESS(rc))
                return rc;
            AssertMsgFailed(("Failed to apply fixups. rc=%Rrc\n", rc));
        }
#ifndef IN_SUP_HARDENED_R3
        else
            AssertMsgFailed(("Failed to resolve imports. rc=%Rrc\n", rc));
#endif
    }
    return rc;
}


/* The image_thunk_data32/64 structures are not very helpful except for getting RSI. keep them around till all the code has been converted. */
typedef struct _IMAGE_THUNK_DATA32
{
    union
    {
        uint32_t  ForwarderString;
        uint32_t  Function;
        uint32_t  Ordinal;
        uint32_t  AddressOfData;
    } u1;
} IMAGE_THUNK_DATA32;
typedef IMAGE_THUNK_DATA32 *PIMAGE_THUNK_DATA32;


/** @copydoc RTLDROPSPE::pfnResolveImports */
static DECLCALLBACK(int) rtldrPEResolveImports32(PRTLDRMODPE pModPe, const void *pvBitsR, void *pvBitsW, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    /*
     * Check if there is actually anything to work on.
     */
    if (    !pModPe->ImportDir.VirtualAddress
        ||  !pModPe->ImportDir.Size)
        return 0;

    /*
     * Walk the IMAGE_IMPORT_DESCRIPTOR table.
     */
    int                         rc = VINF_SUCCESS;
    PIMAGE_IMPORT_DESCRIPTOR    pImps;
    for (pImps = PE_RVA2TYPE(pvBitsR, pModPe->ImportDir.VirtualAddress, PIMAGE_IMPORT_DESCRIPTOR);
         !rc && pImps->Name != 0 && pImps->FirstThunk != 0;
         pImps++)
    {
        AssertReturn(pImps->Name < pModPe->cbImage, VERR_BAD_EXE_FORMAT);
        const char *pszModName = PE_RVA2TYPE(pvBitsR, pImps->Name, const char *);
        AssertReturn(pImps->FirstThunk < pModPe->cbImage, VERR_BAD_EXE_FORMAT);
        AssertReturn(pImps->u.OriginalFirstThunk < pModPe->cbImage, VERR_BAD_EXE_FORMAT);

        Log3(("RTLdrPE: Import descriptor: %s\n", pszModName));
        Log4(("RTLdrPE:   OriginalFirstThunk = %#RX32\n"
              "RTLdrPE:   TimeDateStamp      = %#RX32\n"
              "RTLdrPE:   ForwarderChain     = %#RX32\n"
              "RTLdrPE:   Name               = %#RX32\n"
              "RTLdrPE:   FirstThunk         = %#RX32\n",
              pImps->u.OriginalFirstThunk, pImps->TimeDateStamp,
              pImps->ForwarderChain, pImps->Name, pImps->FirstThunk));

        /*
         * Walk the thunks table(s).
         */
        PIMAGE_THUNK_DATA32 pFirstThunk = PE_RVA2TYPE(pvBitsW, pImps->FirstThunk, PIMAGE_THUNK_DATA32); /* update this. */
        PIMAGE_THUNK_DATA32 pThunk      = pImps->u.OriginalFirstThunk == 0                              /* read from this. */
                                        ? PE_RVA2TYPE(pvBitsR, pImps->FirstThunk, PIMAGE_THUNK_DATA32)
                                        : PE_RVA2TYPE(pvBitsR, pImps->u.OriginalFirstThunk, PIMAGE_THUNK_DATA32);
        while (!rc && pThunk->u1.Ordinal != 0)
        {
            RTUINTPTR Value = 0;
            if (pThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
            {
                rc = pfnGetImport(&pModPe->Core, pszModName, NULL, IMAGE_ORDINAL32(pThunk->u1.Ordinal), &Value, pvUser);
                Log4((RT_SUCCESS(rc) ? "RTLdrPE:  %RTptr #%u\n" : "RTLdrPE:  %08RX32 #%u rc=%Rrc\n",
                      (uint32_t)Value, IMAGE_ORDINAL32(pThunk->u1.Ordinal), rc));
            }
            else if (   pThunk->u1.Ordinal > 0
                     && pThunk->u1.Ordinal < pModPe->cbImage)
            {
                rc = pfnGetImport(&pModPe->Core, pszModName,
                                  PE_RVA2TYPE(pvBitsR, (char*)(uintptr_t)pThunk->u1.AddressOfData + 2, const char *),
                                  ~0U, &Value, pvUser);
                Log4((RT_SUCCESS(rc) ? "RTLdrPE:  %RTptr %s\n" : "RTLdrPE:  %08RX32 %s rc=%Rrc\n",
                      (uint32_t)Value, PE_RVA2TYPE(pvBitsR, (char*)(uintptr_t)pThunk->u1.AddressOfData + 2, const char *), rc));
            }
            else
            {
                AssertMsgFailed(("bad import data thunk!\n"));
                rc = VERR_BAD_EXE_FORMAT;
            }
            pFirstThunk->u1.Function = (uint32_t)Value;
            if (pFirstThunk->u1.Function != Value)
            {
                AssertMsgFailed(("external symbol address to big!\n"));
                rc = VERR_ADDRESS_CONFLICT; /** @todo get me a better error status code. */
            }
            pThunk++;
            pFirstThunk++;
        }
    }

    return rc;
}


/* The image_thunk_data32/64 structures are not very helpful except for getting RSI. keep them around till all the code has been converted. */
typedef struct _IMAGE_THUNK_DATA64
{
    union
    {
        uint64_t  ForwarderString;
        uint64_t  Function;
        uint64_t  Ordinal;
        uint64_t  AddressOfData;
    } u1;
} IMAGE_THUNK_DATA64;
typedef IMAGE_THUNK_DATA64 *PIMAGE_THUNK_DATA64;


/** @copydoc RTLDROPSPE::pfnResolveImports */
static DECLCALLBACK(int) rtldrPEResolveImports64(PRTLDRMODPE pModPe, const void *pvBitsR, void *pvBitsW,
                                                 PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    /*
     * Check if there is actually anything to work on.
     */
    if (    !pModPe->ImportDir.VirtualAddress
        ||  !pModPe->ImportDir.Size)
        return 0;

    /*
     * Walk the IMAGE_IMPORT_DESCRIPTOR table.
     */
    int                         rc = VINF_SUCCESS;
    PIMAGE_IMPORT_DESCRIPTOR    pImps;
    for (pImps = PE_RVA2TYPE(pvBitsR, pModPe->ImportDir.VirtualAddress, PIMAGE_IMPORT_DESCRIPTOR);
         !rc && pImps->Name != 0 && pImps->FirstThunk != 0;
         pImps++)
    {
        AssertReturn(pImps->Name < pModPe->cbImage, VERR_BAD_EXE_FORMAT);
        const char *pszModName = PE_RVA2TYPE(pvBitsR, pImps->Name, const char *);
        AssertReturn(pImps->FirstThunk < pModPe->cbImage, VERR_BAD_EXE_FORMAT);
        AssertReturn(pImps->u.OriginalFirstThunk < pModPe->cbImage, VERR_BAD_EXE_FORMAT);

        Log3(("RTLdrPE: Import descriptor: %s\n", pszModName));
        Log4(("RTLdrPE:   OriginalFirstThunk = %#RX32\n"
              "RTLdrPE:   TimeDateStamp      = %#RX32\n"
              "RTLdrPE:   ForwarderChain     = %#RX32\n"
              "RTLdrPE:   Name               = %#RX32\n"
              "RTLdrPE:   FirstThunk         = %#RX32\n",
              pImps->u.OriginalFirstThunk, pImps->TimeDateStamp,
              pImps->ForwarderChain, pImps->Name, pImps->FirstThunk));

        /*
         * Walk the thunks table(s).
         */
        PIMAGE_THUNK_DATA64 pFirstThunk = PE_RVA2TYPE(pvBitsW, pImps->FirstThunk, PIMAGE_THUNK_DATA64); /* update this. */
        PIMAGE_THUNK_DATA64 pThunk      = pImps->u.OriginalFirstThunk == 0                              /* read from this. */
                                        ? PE_RVA2TYPE(pvBitsR, pImps->FirstThunk, PIMAGE_THUNK_DATA64)
                                        : PE_RVA2TYPE(pvBitsR, pImps->u.OriginalFirstThunk, PIMAGE_THUNK_DATA64);
        while (!rc && pThunk->u1.Ordinal != 0)
        {
            RTUINTPTR Value = 0;
            if (pThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64)
            {
                rc = pfnGetImport(&pModPe->Core, pszModName, NULL, (unsigned)IMAGE_ORDINAL64(pThunk->u1.Ordinal), &Value, pvUser);
                Log4((RT_SUCCESS(rc) ? "RTLdrPE:  %016RX64 #%u\n" : "RTLdrPE:  %016RX64 #%u rc=%Rrc\n",
                      (uint64_t)Value, (unsigned)IMAGE_ORDINAL64(pThunk->u1.Ordinal), rc));
            }
            else if (   pThunk->u1.Ordinal > 0
                     && pThunk->u1.Ordinal < pModPe->cbImage)
            {
                /** @todo add validation of the string pointer! */
                rc = pfnGetImport(&pModPe->Core, pszModName, PE_RVA2TYPE(pvBitsR, (uintptr_t)pThunk->u1.AddressOfData + 2, const char *),
                                  ~0U, &Value, pvUser);
                Log4((RT_SUCCESS(rc) ? "RTLdrPE:  %016RX64 %s\n" : "RTLdrPE:  %016RX64 %s rc=%Rrc\n",
                      (uint64_t)Value, PE_RVA2TYPE(pvBitsR, (uintptr_t)pThunk->u1.AddressOfData + 2, const char *), rc));
            }
            else
            {
                AssertMsgFailed(("bad import data thunk!\n"));
                rc = VERR_BAD_EXE_FORMAT;
            }
            pFirstThunk->u1.Function = Value;
            pThunk++;
            pFirstThunk++;
        }
    }

    return rc;
}


/**
 * Applies fixups.
 */
static int rtldrPEApplyFixups(PRTLDRMODPE pModPe, const void *pvBitsR, void *pvBitsW, RTUINTPTR BaseAddress,
                              RTUINTPTR OldBaseAddress)
{
    if (    !pModPe->RelocDir.VirtualAddress
        ||  !pModPe->RelocDir.Size)
        return 0;

    /*
     * Apply delta fixups iterating fixup chunks.
     */
    PIMAGE_BASE_RELOCATION  pbr = PE_RVA2TYPE(pvBitsR, pModPe->RelocDir.VirtualAddress, PIMAGE_BASE_RELOCATION);
    PIMAGE_BASE_RELOCATION  pBaseRelocs = pbr;
    unsigned                cbBaseRelocs = pModPe->RelocDir.Size;
    RTUINTPTR               uDelta = BaseAddress - OldBaseAddress;
    Log2(("RTLdrPE: Fixups: uDelta=%#RTptr BaseAddress=%#RTptr OldBaseAddress=%#RTptr\n", uDelta, BaseAddress, OldBaseAddress));
    Log4(("RTLdrPE: BASERELOC: VirtualAddres=%RX32 Size=%RX32\n", pModPe->RelocDir.VirtualAddress, pModPe->RelocDir.Size));
    Assert(sizeof(*pbr) == sizeof(uint32_t) * 2);

    while (   (uintptr_t)pbr - (uintptr_t)pBaseRelocs + 8 < cbBaseRelocs /* 8= VirtualAddress and SizeOfBlock members */
           && pbr->SizeOfBlock >= 8)
    {
        uint16_t   *pwoffFixup   = (uint16_t *)(pbr + 1);
        uint32_t    cRelocations = (pbr->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
        Log3(("RTLdrPE: base relocs for %#010x, size %#06x (%d relocs)\n", pbr->VirtualAddress, pbr->SizeOfBlock, cRelocations));

        /* Some bound checking just to be sure it works... */
        if ((uintptr_t)pbr - (uintptr_t)pBaseRelocs + pbr->SizeOfBlock > cbBaseRelocs)
            cRelocations = (uint32_t)(  (((uintptr_t)pBaseRelocs + cbBaseRelocs) - (uintptr_t)pbr - sizeof(IMAGE_BASE_RELOCATION))
                                      / sizeof(uint16_t) );

        /*
         * Loop thru the fixups in this chunk.
         */
        while (cRelocations != 0)
        {
            /*
             * Common fixup
             */
            static const char * const s_apszReloc[16] =
            {
                "ABS", "HIGH", "LOW", "HIGHLOW", "HIGHADJ", "MIPS_JMPADDR", "RES6", "RES7",
                "RES8", "IA64_IMM64", "DIR64", "HIGH3ADJ", "RES12", "RES13", "RES14", "RES15"
            }; NOREF(s_apszReloc);
            union
            {
                uint16_t   *pu16;
                uint32_t   *pu32;
                uint64_t   *pu64;
            } u;
            const int offFixup  = *pwoffFixup & 0xfff;
            u.pu32 = PE_RVA2TYPE(pvBitsW, offFixup + pbr->VirtualAddress, uint32_t *);
            const int fType     = *pwoffFixup >> 12;
            Log4(("RTLdrPE: %08x %s\n", offFixup + pbr->VirtualAddress, s_apszReloc[fType]));
            switch (fType)
            {
                case IMAGE_REL_BASED_HIGHLOW:   /* 32-bit, add delta. */
                    *u.pu32 += (uint32_t)uDelta;
                    break;
                case IMAGE_REL_BASED_DIR64:     /* 64-bit, add delta. */
                    *u.pu64 += (RTINTPTR)uDelta;
                    break;
                case IMAGE_REL_BASED_ABSOLUTE:  /* Alignment placeholder. */
                    break;
                /* odd ones */
                case IMAGE_REL_BASED_LOW:       /* 16-bit, add 1st 16-bit part of the delta. */
                    *u.pu16 += (uint16_t)uDelta;
                    break;
                case IMAGE_REL_BASED_HIGH:      /* 16-bit, add 2nd 16-bit part of the delta. */
                    *u.pu16 += (uint16_t)(uDelta >> 16);
                    break;
                /* never ever seen these next two, and I'm not 100% sure they are correctly implemented here. */
                case IMAGE_REL_BASED_HIGHADJ:
                {
                    if (cRelocations <= 1)
                    {
                        AssertMsgFailed(("HIGHADJ missing 2nd record!\n"));
                        return VERR_BAD_EXE_FORMAT;
                    }
                    cRelocations--;
                    pwoffFixup++;
                    int32_t i32 = (uint32_t)(*u.pu16 << 16) | *pwoffFixup;
                    i32 += (uint32_t)uDelta;
                    i32 += 0x8000; //??
                    *u.pu16 = (uint16_t)(i32 >> 16);
                    break;
                }
                case IMAGE_REL_BASED_HIGH3ADJ:
                {
                    if (cRelocations <= 2)
                    {
                        AssertMsgFailed(("HIGHADJ3 missing 2nd record!\n"));
                        return VERR_BAD_EXE_FORMAT;
                    }
                    cRelocations -= 2;
                    pwoffFixup++;
                    int64_t i64 = ((uint64_t)*u.pu16 << 32) | *(uint32_t *)pwoffFixup++;
                    i64 += (int64_t)uDelta << 16; //??
                    i64 += 0x80000000;//??
                    *u.pu16 = (uint16_t)(i64 >> 32);
                    break;
                }
                default:
                    AssertMsgFailed(("Unknown fixup type %d offset=%#x\n", fType, offFixup));
                    break;
            }

            /*
             * Next offset/type
             */
            pwoffFixup++;
            cRelocations--;
        } /* while loop */

        /*
         * Next Fixup chunk. (i.e. next page)
         */
        pbr = (PIMAGE_BASE_RELOCATION)((uintptr_t)pbr + pbr->SizeOfBlock);
    } /* while loop */

    return 0;
}


/** @interface_method_impl{RTLDROPS,pfnRelocate} */
static DECLCALLBACK(int) rtldrPERelocate(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR NewBaseAddress, RTUINTPTR OldBaseAddress,
                                         PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;

    /*
     * Do we have to read the image bits?
     */
    if (!pModPe->pvBits)
    {
        int rc = rtldrPEReadBits(pModPe);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Process imports.
     */
    int rc = ((PRTLDROPSPE)pMod->pOps)->pfnResolveImports(pModPe, pModPe->pvBits, pvBits, pfnGetImport, pvUser);
    if (RT_SUCCESS(rc))
    {
        /*
         * Apply relocations.
         */
        rc = rtldrPEApplyFixups(pModPe, pModPe->pvBits, pvBits, NewBaseAddress, OldBaseAddress);
        AssertRC(rc);
    }
    return rc;
}


/**
 * Internal worker for pfnGetSymbolEx and pfnQueryForwarderInfo.
 *
 * @returns IPRT status code.
 * @param   pModPe              The PE module instance.
 * @param   iOrdinal            The symbol ordinal, UINT32_MAX if named symbol.
 * @param   pszSymbol           The symbol name.
 * @param   ppvBits             The image bits pointer (input/output).
 * @param   puRvaExport         Where to return the symbol RVA.
 * @param   puOrdinal           Where to return the ordinal number. Optional.
 */
static int rtLdrPE_ExportToRva(PRTLDRMODPE pModPe, uint32_t iOrdinal, const char *pszSymbol,
                               const void **ppvBits, uint32_t *puRvaExport, uint32_t *puOrdinal)
{
    /*
     * Check if there is actually anything to work on.
     */
    if (   !pModPe->ExportDir.VirtualAddress
        || !pModPe->ExportDir.Size)
        return VERR_SYMBOL_NOT_FOUND;

    /*
     * No bits supplied? Do we need to read the bits?
     */
    void const *pvBits = *ppvBits;
    if (!pvBits)
    {
        if (!pModPe->pvBits)
        {
            int rc = rtldrPEReadBits(pModPe);
            if (RT_FAILURE(rc))
                return rc;
        }
        *ppvBits = pvBits = pModPe->pvBits;
    }

    PIMAGE_EXPORT_DIRECTORY pExpDir = PE_RVA2TYPE(pvBits, pModPe->ExportDir.VirtualAddress, PIMAGE_EXPORT_DIRECTORY);
    int                     iExpOrdinal = 0;    /* index into address table. */
    if (iOrdinal != UINT32_MAX)
    {
        /*
         * Find ordinal export: Simple table lookup.
         */
        if (    iOrdinal >= pExpDir->Base + RT_MAX(pExpDir->NumberOfNames, pExpDir->NumberOfFunctions)
            ||  iOrdinal < pExpDir->Base)
            return VERR_SYMBOL_NOT_FOUND;
        iExpOrdinal = iOrdinal - pExpDir->Base;
    }
    else
    {
        /*
         * Find Named Export: Do binary search on the name table.
         */
        uint32_t   *paRVANames = PE_RVA2TYPE(pvBits, pExpDir->AddressOfNames, uint32_t *);
        uint16_t   *paOrdinals = PE_RVA2TYPE(pvBits, pExpDir->AddressOfNameOrdinals, uint16_t *);
        int         iStart = 1;
        int         iEnd = pExpDir->NumberOfNames;

        for (;;)
        {
            /* end of search? */
            if (iStart > iEnd)
            {
#ifdef RT_STRICT
                /* do a linear search just to verify the correctness of the above algorithm */
                for (unsigned i = 0; i < pExpDir->NumberOfNames; i++)
                {
                    AssertMsg(i == 0 || strcmp(PE_RVA2TYPE(pvBits, paRVANames[i], const char *), PE_RVA2TYPE(pvBits, paRVANames[i - 1], const char *)) > 0,
                              ("bug in binary export search!!!\n"));
                    AssertMsg(strcmp(PE_RVA2TYPE(pvBits, paRVANames[i], const char *), pszSymbol) != 0,
                              ("bug in binary export search!!!\n"));
                }
#endif
                return VERR_SYMBOL_NOT_FOUND;
            }

            int i  = (iEnd - iStart) / 2 + iStart;
            const char *pszExpName  = PE_RVA2TYPE(pvBits, paRVANames[i - 1], const char *);
            int         diff        = strcmp(pszExpName, pszSymbol);
            if (diff > 0)       /* pszExpName > pszSymbol: search chunck before i */
                iEnd = i - 1;
            else if (diff)      /* pszExpName < pszSymbol: search chunk after i */
                iStart = i + 1;
            else                /* pszExpName == pszSymbol */
            {
                iExpOrdinal = paOrdinals[i - 1];
                break;
            }
        } /* binary search thru name table */
    }

    /*
     * Found export (iExpOrdinal).
     */
    uint32_t *paAddress = PE_RVA2TYPE(pvBits, pExpDir->AddressOfFunctions, uint32_t *);
    *puRvaExport = paAddress[iExpOrdinal];
    if (puOrdinal)
        *puOrdinal = iExpOrdinal;
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTLDROPS,pfnGetSymbolEx} */
static DECLCALLBACK(int) rtldrPEGetSymbolEx(PRTLDRMODINTERNAL pMod, const void *pvBits, RTUINTPTR BaseAddress,
                                            uint32_t iOrdinal, const char *pszSymbol, RTUINTPTR *pValue)
{
    PRTLDRMODPE pThis = (PRTLDRMODPE)pMod;
    uint32_t uRvaExport;
    int rc = rtLdrPE_ExportToRva(pThis, iOrdinal, pszSymbol, &pvBits, &uRvaExport, NULL);
    if (RT_SUCCESS(rc))
    {

        uint32_t offForwarder = uRvaExport - pThis->ExportDir.VirtualAddress;
        if (offForwarder >= pThis->ExportDir.Size)
            /* Get plain export address */
            *pValue = PE_RVA2TYPE(BaseAddress, uRvaExport, RTUINTPTR);
        else
        {
            /* Return the approximate length of the forwarder buffer. */
            const char *pszForwarder = PE_RVA2TYPE(pvBits, uRvaExport, const char *);
            *pValue = sizeof(RTLDRIMPORTINFO) + RTStrNLen(pszForwarder, offForwarder - pThis->ExportDir.Size);
            rc = VERR_LDR_FORWARDER;
        }
    }
    return rc;
}


/** @interface_method_impl{RTLDROPS,pfnQueryForwarderInfo} */
static DECLCALLBACK(int) rtldrPE_QueryForwarderInfo(PRTLDRMODINTERNAL pMod, const void *pvBits,  uint32_t iOrdinal,
                                                    const char *pszSymbol, PRTLDRIMPORTINFO pInfo, size_t cbInfo)
{
    AssertReturn(cbInfo >= sizeof(*pInfo), VERR_INVALID_PARAMETER);

    PRTLDRMODPE pThis = (PRTLDRMODPE)pMod;
    uint32_t uRvaExport;
    int rc = rtLdrPE_ExportToRva(pThis, iOrdinal, pszSymbol, &pvBits, &uRvaExport, &iOrdinal);
    if (RT_SUCCESS(rc))
    {
        uint32_t offForwarder = uRvaExport - pThis->ExportDir.VirtualAddress;
        if (offForwarder < pThis->ExportDir.Size)
        {
            const char *pszForwarder = PE_RVA2TYPE(pvBits, uRvaExport, const char *);

            /*
             * Parse and validate the string.  We must make sure it's valid
             * UTF-8, so we restrict it to ASCII.
             */
            const char *pszEnd = RTStrEnd(pszForwarder, offForwarder - pThis->ExportDir.Size);
            if (pszEnd)
            {
                /* The module name. */
                char ch;
                uint32_t off = 0;
                while ((ch = pszForwarder[off]) != '.' && ch != '\0')
                {
                    if (RT_UNLIKELY((uint8_t)ch >= 0x80))
                        return VERR_LDR_BAD_FORWARDER;
                    off++;
                }
                if (RT_UNLIKELY(ch != '.'))
                    return VERR_LDR_BAD_FORWARDER;
                uint32_t const offDot = off;
                off++;

                /* The function name or ordinal number. Ordinals starts with a hash. */
                uint32_t iImpOrdinal;
                if (pszForwarder[off] != '#')
                {
                    iImpOrdinal = UINT32_MAX;
                    while ((ch = pszForwarder[off]) != '\0')
                    {
                        if (RT_UNLIKELY((uint8_t)ch >= 0x80))
                            return VERR_LDR_BAD_FORWARDER;
                        off++;
                    }
                    if (RT_UNLIKELY(off == offDot + 1))
                        return VERR_LDR_BAD_FORWARDER;
                }
                else
                {
                    rc = RTStrToUInt32Full(&pszForwarder[off + 1], 10, &iImpOrdinal);
                    if (RT_UNLIKELY(rc != VINF_SUCCESS || iImpOrdinal > UINT16_MAX))
                        return VERR_LDR_BAD_FORWARDER;
                }

                /*
                 * Enough buffer?
                 */
                uint32_t cbNeeded = RT_UOFFSETOF_DYN(RTLDRIMPORTINFO, szModule[iImpOrdinal != UINT32_MAX ? offDot + 1 : off + 1]);
                if (cbNeeded > cbInfo)
                    return VERR_BUFFER_OVERFLOW;

                /*
                 * Fill in the return buffer.
                 */
                pInfo->iSelfOrdinal = iOrdinal;
                pInfo->iOrdinal     = iImpOrdinal;
                if (iImpOrdinal == UINT32_MAX)
                {
                    pInfo->pszSymbol = &pInfo->szModule[offDot + 1];
                    memcpy(&pInfo->szModule[0], pszForwarder, off + 1);
                }
                else
                {
                    pInfo->pszSymbol = NULL;
                    memcpy(&pInfo->szModule[0], pszForwarder, offDot);
                }
                pInfo->szModule[offDot] = '\0';
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_LDR_BAD_FORWARDER;
        }
        else
            rc = VERR_LDR_NOT_FORWARDER;
    }
    return rc;
}


/**
 * Slow version of rtldrPEEnumSymbols that'll work without all of the image
 * being accessible.
 *
 * This is mainly for use in debuggers and similar.
 */
static int rtldrPEEnumSymbolsSlow(PRTLDRMODPE pThis, unsigned fFlags, RTUINTPTR BaseAddress,
                                  PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    /*
     * We enumerates by ordinal, which means using a slow linear search for
     * getting any name
     */
    PCIMAGE_EXPORT_DIRECTORY pExpDir = NULL;
    int rc = rtldrPEReadPartByRva(pThis, NULL, pThis->ExportDir.VirtualAddress, pThis->ExportDir.Size,
                                  (void const **)&pExpDir);
    if (RT_FAILURE(rc))
        return rc;
    uint32_t const cOrdinals = RT_MAX(pExpDir->NumberOfNames, pExpDir->NumberOfFunctions);

    uint32_t const *paAddress  = NULL;
    rc = rtldrPEReadPartByRva(pThis, NULL, pExpDir->AddressOfFunctions, cOrdinals * sizeof(uint32_t),
                              (void const **)&paAddress);
    uint32_t const *paRVANames = NULL;
    if (RT_SUCCESS(rc) && pExpDir->NumberOfNames)
        rc = rtldrPEReadPartByRva(pThis, NULL, pExpDir->AddressOfNames, pExpDir->NumberOfNames * sizeof(uint32_t),
                                  (void const **)&paRVANames);
    uint16_t const *paOrdinals = NULL;
    if (RT_SUCCESS(rc) && pExpDir->NumberOfNames)
        rc = rtldrPEReadPartByRva(pThis, NULL, pExpDir->AddressOfNameOrdinals, pExpDir->NumberOfNames * sizeof(uint16_t),
                                  (void const **)&paOrdinals);
    if (RT_SUCCESS(rc))
    {
        uint32_t uNamePrev = 0;
        for (uint32_t uOrdinal = 0; uOrdinal < cOrdinals; uOrdinal++)
        {
            if (paAddress[uOrdinal] /* needed? */)
            {
                /*
                 * Look for name.
                 */
                uint32_t    uRvaName = UINT32_MAX;
                /* Search from previous + 1 to the end.  */
                unsigned    uName = uNamePrev + 1;
                while (uName < pExpDir->NumberOfNames)
                {
                    if (paOrdinals[uName] == uOrdinal)
                    {
                        uRvaName = paRVANames[uName];
                        uNamePrev = uName;
                        break;
                    }
                    uName++;
                }
                if (uRvaName == UINT32_MAX)
                {
                    /* Search from start to the previous. */
                    uName = 0;
                    for (uName = 0 ; uName <= uNamePrev; uName++)
                    {
                        if (paOrdinals[uName] == uOrdinal)
                        {
                            uRvaName = paRVANames[uName];
                            uNamePrev = uName;
                            break;
                        }
                    }
                }

                /*
                 * Get address.
                 */
                uint32_t  uRVAExport = paAddress[uOrdinal];
                RTUINTPTR Value;
                if (uRVAExport - pThis->ExportDir.VirtualAddress >= pThis->ExportDir.Size)
                    Value = PE_RVA2TYPE(BaseAddress, uRVAExport, RTUINTPTR);
                else if (!(fFlags & RTLDR_ENUM_SYMBOL_FLAGS_NO_FWD))
                    Value = RTLDR_ENUM_SYMBOL_FWD_ADDRESS;
                else
                    continue;

                /* Read in the name if found one. */
                char szAltName[32];
                const char *pszName = NULL;
                if (uRvaName != UINT32_MAX)
                {
                    uint32_t cbName = 0x1000 - (uRvaName & 0xfff);
                    if (cbName < 10 || cbName > 512)
                        cbName = 128;
                    rc = rtldrPEReadPartByRva(pThis, NULL, uRvaName, cbName, (void const **)&pszName);
                    while (RT_SUCCESS(rc) && RTStrNLen(pszName, cbName) == cbName)
                    {
                        rtldrPEFreePart(pThis, NULL, pszName);
                        pszName = NULL;
                        if (cbName >= _4K)
                            break;
                        cbName += 128;
                        rc = rtldrPEReadPartByRva(pThis, NULL, uRvaName, cbName, (void const **)&pszName);
                    }
                }
                if (!pszName)
                {
                    RTStrPrintf(szAltName, sizeof(szAltName), "Ordinal%#x", uOrdinal);
                    pszName = szAltName;
                }

                /*
                 * Call back.
                 */
                rc = pfnCallback(&pThis->Core, pszName, uOrdinal + pExpDir->Base, Value, pvUser);
                if (pszName != szAltName && pszName)
                    rtldrPEFreePart(pThis, NULL, pszName);
                if (rc)
                    break;
            }
        }
    }

    rtldrPEFreePart(pThis, NULL, paOrdinals);
    rtldrPEFreePart(pThis, NULL, paRVANames);
    rtldrPEFreePart(pThis, NULL, paAddress);
    rtldrPEFreePart(pThis, NULL, pExpDir);
    return rc;

}


/** @interface_method_impl{RTLDROPS,pfnEnumSymbols} */
static DECLCALLBACK(int) rtldrPEEnumSymbols(PRTLDRMODINTERNAL pMod, unsigned fFlags, const void *pvBits, RTUINTPTR BaseAddress,
                                            PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;
    NOREF(fFlags); /* ignored ... */

    /*
     * Check if there is actually anything to work on.
     */
    if (   !pModPe->ExportDir.VirtualAddress
        || !pModPe->ExportDir.Size)
        return VERR_SYMBOL_NOT_FOUND;

    /*
     * No bits supplied? Do we need to read the bits?
     */
    if (!pvBits)
    {
        if (!pModPe->pvBits)
        {
            int rc = rtldrPEReadBits(pModPe);
            if (RT_FAILURE(rc))
                return rtldrPEEnumSymbolsSlow(pModPe, fFlags, BaseAddress, pfnCallback, pvUser);
        }
        pvBits = pModPe->pvBits;
    }

    /*
     * We enumerates by ordinal, which means using a slow linear search for
     * getting any name
     */
    PIMAGE_EXPORT_DIRECTORY pExpDir = PE_RVA2TYPE(pvBits, pModPe->ExportDir.VirtualAddress, PIMAGE_EXPORT_DIRECTORY);
    uint32_t   *paAddress  = PE_RVA2TYPE(pvBits, pExpDir->AddressOfFunctions, uint32_t *);
    uint32_t   *paRVANames = PE_RVA2TYPE(pvBits, pExpDir->AddressOfNames, uint32_t *);
    uint16_t   *paOrdinals = PE_RVA2TYPE(pvBits, pExpDir->AddressOfNameOrdinals, uint16_t *);
    uint32_t    uNamePrev  = 0;
    unsigned    cOrdinals  = RT_MAX(pExpDir->NumberOfNames, pExpDir->NumberOfFunctions);
    for (unsigned uOrdinal = 0; uOrdinal < cOrdinals; uOrdinal++)
    {
        if (paAddress[uOrdinal] /* needed? */)
        {
            /*
             * Look for name.
             */
            const char *pszName = NULL;
            /* Search from previous + 1 to the end.  */
            uint32_t uName = uNamePrev + 1;
            while (uName < pExpDir->NumberOfNames)
            {
                if (paOrdinals[uName] == uOrdinal)
                {
                    pszName = PE_RVA2TYPE(pvBits, paRVANames[uName], const char *);
                    uNamePrev = uName;
                    break;
                }
                uName++;
            }
            if (!pszName)
            {
                /* Search from start to the previous. */
                uName = 0;
                for (uName = 0 ; uName <= uNamePrev; uName++)
                {
                    if (paOrdinals[uName] == uOrdinal)
                    {
                        pszName = PE_RVA2TYPE(pvBits, paRVANames[uName], const char *);
                        uNamePrev = uName;
                        break;
                    }
                }
            }

            /*
             * Get address.
             */
            uint32_t  uRVAExport = paAddress[uOrdinal];
            RTUINTPTR Value;
            if (uRVAExport - pModPe->ExportDir.VirtualAddress >= pModPe->ExportDir.Size)
                Value = PE_RVA2TYPE(BaseAddress, uRVAExport, RTUINTPTR);
            else if (!(fFlags & RTLDR_ENUM_SYMBOL_FLAGS_NO_FWD))
                Value = RTLDR_ENUM_SYMBOL_FWD_ADDRESS;
            else
                continue;

            /*
             * Call back.
             */
            int rc = pfnCallback(pMod, pszName, uOrdinal + pExpDir->Base, Value, pvUser);
            if (rc)
                return rc;
        }
    }

    return VINF_SUCCESS;
}


/** @interface_method_impl{RTLDROPS,pfnEnumDbgInfo} */
static DECLCALLBACK(int) rtldrPE_EnumDbgInfo(PRTLDRMODINTERNAL pMod, const void *pvBits,
                                             PFNRTLDRENUMDBG pfnCallback, void *pvUser)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;
    int rc;

    /*
     * Debug info directory empty?
     */
    if (   !pModPe->DebugDir.VirtualAddress
        || !pModPe->DebugDir.Size)
        return VINF_SUCCESS;

    /*
     * Allocate temporary memory for a path buffer (this code is also compiled
     * and maybe even used in stack starved environments).
     */
    char *pszPath = (char *)RTMemTmpAlloc(RTPATH_MAX);
    if (!pszPath)
        return VERR_NO_TMP_MEMORY;

    /*
     * Get the debug directory.
     */
    if (!pvBits)
        pvBits = pModPe->pvBits;

    PCIMAGE_DEBUG_DIRECTORY paDbgDir;
    int rcRet = rtldrPEReadPartByRva(pModPe, pvBits, pModPe->DebugDir.VirtualAddress, pModPe->DebugDir.Size,
                                     (void const **)&paDbgDir);
    if (RT_FAILURE(rcRet))
    {
        RTMemTmpFree(pszPath);
        return rcRet;
    }

    /*
     * Enumerate the debug directory.
     */
    uint32_t const cEntries = pModPe->DebugDir.Size / sizeof(paDbgDir[0]);
    for (uint32_t i = 0; i < cEntries; i++)
    {
        if (paDbgDir[i].PointerToRawData < pModPe->offEndOfHdrs)
            continue;
        if (paDbgDir[i].SizeOfData < 4)
            continue;

        void const     *pvPart = NULL;
        RTLDRDBGINFO    DbgInfo;
        RT_ZERO(DbgInfo.u);
        DbgInfo.iDbgInfo    = i;
        DbgInfo.offFile     = paDbgDir[i].PointerToRawData;
        DbgInfo.LinkAddress =    paDbgDir[i].AddressOfRawData < pModPe->cbImage
                              && paDbgDir[i].AddressOfRawData >= pModPe->offEndOfHdrs
                            ? paDbgDir[i].AddressOfRawData : NIL_RTLDRADDR;
        DbgInfo.cb          = paDbgDir[i].SizeOfData;
        DbgInfo.pszExtFile  = NULL;

        rc = VINF_SUCCESS;
        switch (paDbgDir[i].Type)
        {
            case IMAGE_DEBUG_TYPE_CODEVIEW:
                DbgInfo.enmType = RTLDRDBGINFOTYPE_CODEVIEW;
                DbgInfo.u.Cv.cbImage    = pModPe->cbImage;
                DbgInfo.u.Cv.uMajorVer  = paDbgDir[i].MajorVersion;
                DbgInfo.u.Cv.uMinorVer  = paDbgDir[i].MinorVersion;
                DbgInfo.u.Cv.uTimestamp = paDbgDir[i].TimeDateStamp;
                if (   paDbgDir[i].SizeOfData < RTPATH_MAX
                    && paDbgDir[i].SizeOfData > 16
                    && (   DbgInfo.LinkAddress != NIL_RTLDRADDR
                        || DbgInfo.offFile > 0)
                    )
                {
                    rc = rtldrPEReadPart(pModPe, pvBits, DbgInfo.offFile, DbgInfo.LinkAddress, paDbgDir[i].SizeOfData, &pvPart);
                    if (RT_SUCCESS(rc))
                    {
                        PCCVPDB20INFO pCv20 = (PCCVPDB20INFO)pvPart;
                        if (   pCv20->u32Magic   == CVPDB20INFO_MAGIC
                            && pCv20->offDbgInfo == 0
                            && paDbgDir[i].SizeOfData > RT_UOFFSETOF(CVPDB20INFO, szPdbFilename) )
                        {
                            DbgInfo.enmType             = RTLDRDBGINFOTYPE_CODEVIEW_PDB20;
                            DbgInfo.u.Pdb20.cbImage     = pModPe->cbImage;
                            DbgInfo.u.Pdb20.uTimestamp  = pCv20->uTimestamp;
                            DbgInfo.u.Pdb20.uAge        = pCv20->uAge;
                            DbgInfo.pszExtFile          = (const char *)&pCv20->szPdbFilename[0];
                        }
                        else if (   pCv20->u32Magic == CVPDB70INFO_MAGIC
                                 && paDbgDir[i].SizeOfData > RT_UOFFSETOF(CVPDB70INFO, szPdbFilename) )
                        {
                            PCCVPDB70INFO pCv70 = (PCCVPDB70INFO)pCv20;
                            DbgInfo.enmType             = RTLDRDBGINFOTYPE_CODEVIEW_PDB70;
                            DbgInfo.u.Pdb70.cbImage     = pModPe->cbImage;
                            DbgInfo.u.Pdb70.Uuid        = pCv70->PdbUuid;
                            DbgInfo.u.Pdb70.uAge        = pCv70->uAge;
                            DbgInfo.pszExtFile          = (const char *)&pCv70->szPdbFilename[0];
                        }
                    }
                    else
                        rcRet = rc;
                }
                break;

            case IMAGE_DEBUG_TYPE_MISC:
                DbgInfo.enmType = RTLDRDBGINFOTYPE_UNKNOWN;
                if (   paDbgDir[i].SizeOfData < RTPATH_MAX
                    && paDbgDir[i].SizeOfData > RT_UOFFSETOF(IMAGE_DEBUG_MISC, Data))
                {
                    DbgInfo.enmType             = RTLDRDBGINFOTYPE_CODEVIEW_DBG;
                    DbgInfo.u.Dbg.cbImage       = pModPe->cbImage;
                    if (DbgInfo.LinkAddress != NIL_RTLDRADDR)
                        DbgInfo.u.Dbg.uTimestamp = paDbgDir[i].TimeDateStamp;
                    else
                        DbgInfo.u.Dbg.uTimestamp = pModPe->uTimestamp; /* NT4 SP1 ntfs.sys hack. Generic? */

                    rc = rtldrPEReadPart(pModPe, pvBits, DbgInfo.offFile, DbgInfo.LinkAddress, paDbgDir[i].SizeOfData, &pvPart);
                    if (RT_SUCCESS(rc))
                    {
                        PCIMAGE_DEBUG_MISC pMisc = (PCIMAGE_DEBUG_MISC)pvPart;
                        if (   pMisc->DataType == IMAGE_DEBUG_MISC_EXENAME
                            && pMisc->Length   == paDbgDir[i].SizeOfData)
                        {
                            if (!pMisc->Unicode)
                                DbgInfo.pszExtFile      = (const char *)&pMisc->Data[0];
                            else
                            {
                                rc = RTUtf16ToUtf8Ex((PCRTUTF16)&pMisc->Data[0],
                                                     (pMisc->Length - RT_UOFFSETOF(IMAGE_DEBUG_MISC, Data)) / sizeof(RTUTF16),
                                                     &pszPath, RTPATH_MAX, NULL);
                                if (RT_SUCCESS(rc))
                                    DbgInfo.pszExtFile = pszPath;
                                else
                                    rcRet = rc; /* continue without a filename. */
                            }
                        }
                    }
                    else
                        rcRet = rc; /* continue without a filename. */
                }
                break;

            case IMAGE_DEBUG_TYPE_COFF:
                DbgInfo.enmType = RTLDRDBGINFOTYPE_COFF;
                DbgInfo.u.Coff.cbImage    = pModPe->cbImage;
                DbgInfo.u.Coff.uMajorVer  = paDbgDir[i].MajorVersion;
                DbgInfo.u.Coff.uMinorVer  = paDbgDir[i].MinorVersion;
                DbgInfo.u.Coff.uTimestamp = paDbgDir[i].TimeDateStamp;
                break;

            default:
                DbgInfo.enmType = RTLDRDBGINFOTYPE_UNKNOWN;
                break;
        }

        /* Fix (hack) the file name encoding.  We don't have Windows-1252 handy,
           so we'll be using Latin-1 as a reasonable approximation.
           (I don't think we know exactly which encoding this is anyway, as
           it's probably the current ANSI/Windows code page for the process
           generating the image anyways.) */
        if (DbgInfo.pszExtFile && DbgInfo.pszExtFile != pszPath)
        {
            rc = RTLatin1ToUtf8Ex(DbgInfo.pszExtFile,
                                  paDbgDir[i].SizeOfData - ((uintptr_t)DbgInfo.pszExtFile - (uintptr_t)pvBits),
                                  &pszPath, RTPATH_MAX, NULL);
            if (RT_FAILURE(rc))
            {
                rcRet = rc;
                DbgInfo.pszExtFile = NULL;
            }
        }
        if (DbgInfo.pszExtFile)
            RTPathChangeToUnixSlashes(pszPath, true /*fForce*/);

        rc = pfnCallback(pMod, &DbgInfo, pvUser);
        rtldrPEFreePart(pModPe, pvBits, pvPart);
        if (rc != VINF_SUCCESS)
        {
            rcRet = rc;
            break;
        }
    }

    rtldrPEFreePart(pModPe, pvBits, paDbgDir);
    RTMemTmpFree(pszPath);
    return rcRet;
}


/** @interface_method_impl{RTLDROPS,pfnEnumSegments} */
static DECLCALLBACK(int) rtldrPE_EnumSegments(PRTLDRMODINTERNAL pMod, PFNRTLDRENUMSEGS pfnCallback, void *pvUser)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;
    RTLDRSEG    SegInfo;

    /*
     * The first section is a fake one covering the headers.
     */
    SegInfo.pszName     = "NtHdrs";
    SegInfo.cchName     = 6;
    SegInfo.SelFlat     = 0;
    SegInfo.Sel16bit    = 0;
    SegInfo.fFlags      = 0;
    SegInfo.fProt       = RTMEM_PROT_READ;
    SegInfo.Alignment   = 1;
    SegInfo.LinkAddress = pModPe->uImageBase;
    SegInfo.RVA         = 0;
    SegInfo.offFile     = 0;
    SegInfo.cb          = pModPe->cbHeaders;
    SegInfo.cbFile      = pModPe->cbHeaders;
    SegInfo.cbMapped    = pModPe->cbHeaders;
    if (!(pModPe->paSections[0].Characteristics & IMAGE_SCN_TYPE_NOLOAD))
        SegInfo.cbMapped = pModPe->paSections[0].VirtualAddress;
    int rc = pfnCallback(pMod, &SegInfo, pvUser);

    /*
     * Then all the normal sections.
     */
    PCIMAGE_SECTION_HEADER pSh = pModPe->paSections;
    for (uint32_t i = 0; i < pModPe->cSections && rc == VINF_SUCCESS; i++, pSh++)
    {
        char szName[32];
        SegInfo.pszName         = (const char *)&pSh->Name[0];
        SegInfo.cchName         = (uint32_t)RTStrNLen(SegInfo.pszName, sizeof(pSh->Name));
        if (SegInfo.cchName >= sizeof(pSh->Name))
        {
            memcpy(szName, &pSh->Name[0], sizeof(pSh->Name));
            szName[sizeof(pSh->Name)] = '\0';
            SegInfo.pszName = szName;
        }
        else if (SegInfo.cchName == 0)
        {
            SegInfo.pszName = szName;
            SegInfo.cchName = (uint32_t)RTStrPrintf(szName, sizeof(szName), "UnamedSect%02u", i);
        }
        SegInfo.SelFlat         = 0;
        SegInfo.Sel16bit        = 0;
        SegInfo.fFlags          = 0;
        SegInfo.fProt           = RTMEM_PROT_NONE;
        if (pSh->Characteristics & IMAGE_SCN_MEM_READ)
            SegInfo.fProt       |= RTMEM_PROT_READ;
        if (pSh->Characteristics & IMAGE_SCN_MEM_WRITE)
            SegInfo.fProt       |= RTMEM_PROT_WRITE;
        if (pSh->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            SegInfo.fProt       |= RTMEM_PROT_EXEC;
        SegInfo.Alignment       = (pSh->Characteristics & IMAGE_SCN_ALIGN_MASK) >> IMAGE_SCN_ALIGN_SHIFT;
        if (SegInfo.Alignment > 0)
            SegInfo.Alignment   = RT_BIT_64(SegInfo.Alignment - 1);
        else
            SegInfo.Alignment   = pModPe->uSectionAlign;
        if (pSh->Characteristics & IMAGE_SCN_TYPE_NOLOAD)
        {
            SegInfo.LinkAddress = NIL_RTLDRADDR;
            SegInfo.RVA         = NIL_RTLDRADDR;
            SegInfo.cbMapped    = 0;
        }
        else
        {
            SegInfo.LinkAddress = pSh->VirtualAddress + pModPe->uImageBase;
            SegInfo.RVA         = pSh->VirtualAddress;
            SegInfo.cbMapped    = RT_ALIGN(pSh->Misc.VirtualSize, SegInfo.Alignment);
            if (i + 1 < pModPe->cSections && !(pSh[1].Characteristics & IMAGE_SCN_TYPE_NOLOAD))
                SegInfo.cbMapped = pSh[1].VirtualAddress - pSh->VirtualAddress;
        }
        SegInfo.cb              = pSh->Misc.VirtualSize;
        if (pSh->PointerToRawData == 0 || pSh->SizeOfRawData == 0)
        {
            SegInfo.offFile     = -1;
            SegInfo.cbFile      = 0;
        }
        else
        {
            SegInfo.offFile     = pSh->PointerToRawData;
            SegInfo.cbFile      = pSh->SizeOfRawData;
        }

        rc = pfnCallback(pMod, &SegInfo, pvUser);
    }

    return rc;
}


/** @interface_method_impl{RTLDROPS,pfnLinkAddressToSegOffset} */
static DECLCALLBACK(int) rtldrPE_LinkAddressToSegOffset(PRTLDRMODINTERNAL pMod, RTLDRADDR LinkAddress,
                                                        uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;

    LinkAddress -= pModPe->uImageBase;

    /* Special header segment. */
    if (LinkAddress < pModPe->paSections[0].VirtualAddress)
    {
        *piSeg   = 0;
        *poffSeg = LinkAddress;
        return VINF_SUCCESS;
    }

    /*
     * Search the normal sections. (Could do this in binary fashion, they're
     * sorted, but too much bother right now.)
     */
    if (LinkAddress > pModPe->cbImage)
        return VERR_LDR_INVALID_LINK_ADDRESS;
    uint32_t                i       = pModPe->cSections;
    PCIMAGE_SECTION_HEADER  paShs   = pModPe->paSections;
    while (i-- > 0)
        if (!(paShs[i].Characteristics & IMAGE_SCN_TYPE_NOLOAD))
        {
            uint32_t uAddr = paShs[i].VirtualAddress;
            if (LinkAddress >= uAddr)
            {
                *poffSeg = LinkAddress - uAddr;
                *piSeg   = i + 1;
                return VINF_SUCCESS;
            }
        }

    return VERR_LDR_INVALID_LINK_ADDRESS;
}


/** @interface_method_impl{RTLDROPS,pfnLinkAddressToRva} */
static DECLCALLBACK(int) rtldrPE_LinkAddressToRva(PRTLDRMODINTERNAL pMod, RTLDRADDR LinkAddress, PRTLDRADDR pRva)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;

    LinkAddress -= pModPe->uImageBase;
    if (LinkAddress > pModPe->cbImage)
        return VERR_LDR_INVALID_LINK_ADDRESS;
    *pRva = LinkAddress;

    return VINF_SUCCESS;
}


/** @interface_method_impl{RTLDROPS,pfnSegOffsetToRva} */
static DECLCALLBACK(int) rtldrPE_SegOffsetToRva(PRTLDRMODINTERNAL pMod, uint32_t iSeg, RTLDRADDR offSeg,
                                                PRTLDRADDR pRva)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;

    if (iSeg > pModPe->cSections)
        return VERR_LDR_INVALID_SEG_OFFSET;

    /** @todo should validate offSeg here... too lazy right now. */
    if (iSeg == 0)
        *pRva = offSeg;
    else if (!(pModPe->paSections[iSeg - 1].Characteristics & IMAGE_SCN_TYPE_NOLOAD))
        *pRva = offSeg + pModPe->paSections[iSeg - 1].VirtualAddress;
    else
        return VERR_LDR_INVALID_SEG_OFFSET;
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTLDROPS,pfnRvaToSegOffset} */
static DECLCALLBACK(int) rtldrPE_RvaToSegOffset(PRTLDRMODINTERNAL pMod, RTLDRADDR Rva,
                                                uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;
    int rc = rtldrPE_LinkAddressToSegOffset(pMod, Rva + pModPe->uImageBase, piSeg, poffSeg);
    if (RT_FAILURE(rc))
        rc = VERR_LDR_INVALID_RVA;
    return rc;
}


/**
 * Worker for rtLdrPE_QueryProp and rtLdrPE_QueryImportModule that counts the
 * number of imports, storing the result in RTLDRMODPE::cImports.
 *
 * @returns IPRT status code.
 * @param   pThis           The PE module instance.
 * @param   pvBits          Image bits if the caller had them available, NULL if
 *                          not. Saves a couple of file accesses.
 */
static int rtLdrPE_CountImports(PRTLDRMODPE pThis, void const *pvBits)
{
    PCIMAGE_IMPORT_DESCRIPTOR paImpDescs;
    int rc = rtldrPEReadPartByRva(pThis, pvBits, pThis->ImportDir.VirtualAddress, pThis->ImportDir.Size,
                                  (void const **)&paImpDescs);
    if (RT_SUCCESS(rc))
    {
        uint32_t const cMax = pThis->ImportDir.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
        uint32_t       i = 0;
        while (   i < cMax
               && paImpDescs[i].Name > pThis->offNtHdrs
               && paImpDescs[i].Name < pThis->cbImage
               && paImpDescs[i].FirstThunk > pThis->offNtHdrs
               && paImpDescs[i].FirstThunk < pThis->cbImage)
            i++;
        pThis->cImports = i;

        rtldrPEFreePart(pThis, pvBits, paImpDescs);
    }
    return rc;
}

/**
 * Worker for rtLdrPE_QueryImportModule and rtLdrPE_QueryInternalName that
 * copies a zero termianted string at the given RVA into the RTLdrQueryPropEx
 * output buffer.
 *
 * @returns IPRT status code. If VERR_BUFFER_OVERFLOW, pcbBuf is required size.
 * @param   pThis           The PE module instance.
 * @param   pvBits          Image bits if the caller had them available, NULL if
 *                          not. Saves a couple of file accesses.
 * @param   uRvaString      The RVA of the string to copy.
 * @param   cbMaxString     The max string length.
 * @param   pvBuf           The output buffer.
 * @param   cbBuf           The buffer size.
 * @param   pcbRet          Where to return the number of bytes we've returned
 *                          (or in case of VERR_BUFFER_OVERFLOW would have).
 */
static int rtLdrPE_QueryNameAtRva(PRTLDRMODPE pThis, void const *pvBits, uint32_t uRvaString, uint32_t cbMaxString,
                                  void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    int rc;
    if (   uRvaString >= pThis->cbHeaders
        && uRvaString < pThis->cbImage)
    {
        /*
         * Limit the string.
         */
        uint32_t cbMax = pThis->cbImage - uRvaString;
        if (cbMax > cbMaxString)
            cbMax = cbMaxString;
        char *pszString;
        rc = rtldrPEReadPartByRva(pThis, pvBits, uRvaString, cbMax, (void const **)&pszString);
        if (RT_SUCCESS(rc))
        {
            /*
             * Make sure it's null terminated and valid UTF-8 encoding.
             *
             * Which encoding this really is isn't defined, I think,
             * but we need to make sure we don't get bogus UTF-8 into
             * the process, so making sure it's valid UTF-8 is a good
             * as anything else since it covers ASCII.
             */
            size_t cchString = RTStrNLen(pszString, cbMaxString);
            if (cchString < cbMaxString)
            {
                rc = RTStrValidateEncodingEx(pszString, cchString, 0 /*fFlags*/);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Copy out the result and we're done.
                     * (We have to do all the cleanup code though, so no return success here.)
                     */
                    *pcbRet = cchString + 1;
                    if (cbBuf >= cchString + 1)
                        memcpy(pvBuf, pszString, cchString + 1);
                    else
                        rc = VERR_BUFFER_OVERFLOW;
                }
            }
            else
                rc = VERR_BAD_EXE_FORMAT;
            rtldrPEFreePart(pThis, pvBits, pszString);
        }
    }
    else
        rc = VERR_BAD_EXE_FORMAT;
    return rc;
}


/**
 * Worker for rtLdrPE_QueryProp that retrievs the name of an import DLL.
 *
 * @returns IPRT status code. If VERR_BUFFER_OVERFLOW, pcbBuf is required size.
 * @param   pThis           The PE module instance.
 * @param   pvBits          Image bits if the caller had them available, NULL if
 *                          not. Saves a couple of file accesses.
 * @param   iImport         The index of the import table descriptor to fetch
 *                          the name from.
 * @param   pvBuf           The output buffer.
 * @param   cbBuf           The buffer size.
 * @param   pcbRet          Where to return the number of bytes we've returned
 *                          (or in case of VERR_BUFFER_OVERFLOW would have).
 */
static int rtLdrPE_QueryImportModule(PRTLDRMODPE pThis, void const *pvBits, uint32_t iImport,
                                     void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    /*
     * Make sure we got the import count.
     */
    int rc;
    if (pThis->cImports == UINT32_MAX)
    {
        rc = rtLdrPE_CountImports(pThis, pvBits);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Check the index first, converting it to an RVA.
     */
    if (iImport < pThis->cImports)
    {
        uint32_t offEntry = iImport * sizeof(IMAGE_IMPORT_DESCRIPTOR) + pThis->ImportDir.VirtualAddress;

        /*
         * Retrieve the import table descriptor.
         * Using 1024 as the max name length (should be more than enough).
         */
        PCIMAGE_IMPORT_DESCRIPTOR pImpDesc;
        rc = rtldrPEReadPartByRva(pThis, pvBits, offEntry, sizeof(*pImpDesc), (void const **)&pImpDesc);
        if (RT_SUCCESS(rc))
        {
            rc = rtLdrPE_QueryNameAtRva(pThis, pvBits, pImpDesc->Name, 1024 /*cchMaxString*/, pvBuf, cbBuf, pcbRet);
            rtldrPEFreePart(pThis, pvBits, pImpDesc);
        }
    }
    else
        rc = VERR_NOT_FOUND;

    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    *pcbRet = 0;
    return rc;
}


/**
 * Worker for rtLdrPE_QueryProp that retrieves the internal module name.
 *
 * @returns IPRT status code. If VERR_BUFFER_OVERFLOW, pcbBuf is required size.
 * @param   pThis           The PE module instance.
 * @param   pvBits          Image bits if the caller had them available, NULL if
 *                          not. Saves a couple of file accesses.
 * @param   pvBuf           The output buffer.
 * @param   cbBuf           The buffer size.
 * @param   pcbRet          Where to return the number of bytes we've returned
 *                          (or in case of VERR_BUFFER_OVERFLOW would have).
 */
static int rtLdrPE_QueryInternalName(PRTLDRMODPE pThis, void const *pvBits, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    *pcbRet = 0;

    if (   pThis->ExportDir.Size < sizeof(IMAGE_EXPORT_DIRECTORY)
        || pThis->ExportDir.VirtualAddress == 0)
        return VERR_NOT_FOUND;

    PCIMAGE_EXPORT_DIRECTORY pExpDir;
    int rc = rtldrPEReadPartByRva(pThis, pvBits, pThis->ExportDir.VirtualAddress, sizeof(*pExpDir), (void const **)&pExpDir);
    if (RT_SUCCESS(rc))
    {
        rc = rtLdrPE_QueryNameAtRva(pThis, pvBits, pExpDir->Name, 1024 /*cchMaxString*/, pvBuf, cbBuf, pcbRet);
        rtldrPEFreePart(pThis, pvBits, pExpDir);
    }

    return rc;
}


/**
 * Worker for rtLdrPE_QueryProp that retrieves unwind information.
 *
 * @returns IPRT status code. If VERR_BUFFER_OVERFLOW, pcbBuf is required size.
 * @param   pThis           The PE module instance.
 * @param   pvBits          Image bits if the caller had them available, NULL if
 *                          not. Saves a couple of file accesses.
 * @param   pvBuf           The output buffer.
 * @param   cbBuf           The buffer size.
 * @param   pcbRet          Where to return the number of bytes we've returned
 *                          (or in case of VERR_BUFFER_OVERFLOW would have).
 */
static int rtLdrPE_QueryUnwindTable(PRTLDRMODPE pThis, void const *pvBits, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    int rc;
    uint32_t const cbSrc = pThis->ExceptionDir.Size;
    if (   cbSrc > 0
        && pThis->ExceptionDir.VirtualAddress > 0)
    {
        *pcbRet = cbSrc;
        if (cbBuf >= cbSrc)
            rc = rtldrPEReadPartByRvaInfoBuf(pThis, pvBits, pThis->ExceptionDir.VirtualAddress, cbSrc, pvBuf);
        else
            rc = VERR_BUFFER_OVERFLOW;
    }
    else
    {
        *pcbRet = 0;
        rc = VERR_NOT_FOUND;
    }
    return rc;
}


/** @interface_method_impl{RTLDROPS,pfnQueryProp} */
static DECLCALLBACK(int) rtldrPE_QueryProp(PRTLDRMODINTERNAL pMod, RTLDRPROP enmProp, void const *pvBits,
                                           void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;
    switch (enmProp)
    {
        case RTLDRPROP_TIMESTAMP_SECONDS:
            Assert(*pcbRet == cbBuf);
            if (cbBuf == sizeof(int32_t))
                *(int32_t *)pvBuf = pModPe->uTimestamp;
            else if (cbBuf == sizeof(int64_t))
                *(int64_t *)pvBuf = pModPe->uTimestamp;
            else
                AssertFailedReturn(VERR_INTERNAL_ERROR_3);
            break;

        case RTLDRPROP_IS_SIGNED:
            Assert(cbBuf == sizeof(bool));
            Assert(*pcbRet == cbBuf);
            *(bool *)pvBuf = pModPe->offPkcs7SignedData != 0;
            break;

        case RTLDRPROP_PKCS7_SIGNED_DATA:
        {
            if (pModPe->cbPkcs7SignedData == 0)
                return VERR_NOT_FOUND;
            Assert(pModPe->offPkcs7SignedData > pModPe->SecurityDir.VirtualAddress);

            *pcbRet = pModPe->cbPkcs7SignedData;
            if (cbBuf < pModPe->cbPkcs7SignedData)
                return VERR_BUFFER_OVERFLOW;
            return pModPe->Core.pReader->pfnRead(pModPe->Core.pReader, pvBuf, pModPe->cbPkcs7SignedData,
                                                 pModPe->offPkcs7SignedData);
        }

#ifndef IPRT_WITHOUT_LDR_PAGE_HASHING
        case RTLDRPROP_HASHABLE_PAGES:
            *pcbRet = sizeof(uint32_t);
            *(uint32_t *)pvBuf = rtLdrPE_GetHashablePages(pModPe);
            return VINF_SUCCESS;

        case RTLDRPROP_SHA1_PAGE_HASHES:
            return rtLdrPE_QueryPageHashes(pModPe, RTDIGESTTYPE_SHA1, pvBuf, cbBuf, pcbRet);

        case RTLDRPROP_SHA256_PAGE_HASHES:
            return rtLdrPE_QueryPageHashes(pModPe, RTDIGESTTYPE_SHA256, pvBuf, cbBuf, pcbRet);
#endif

        case RTLDRPROP_SIGNATURE_CHECKS_ENFORCED:
            Assert(cbBuf == sizeof(bool));
            Assert(*pcbRet == cbBuf);
            *(bool *)pvBuf = pModPe->offPkcs7SignedData > 0
                          && (pModPe->fDllCharacteristics & IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY);
            break;

        case RTLDRPROP_IMPORT_COUNT:
            Assert(cbBuf == sizeof(uint32_t));
            Assert(*pcbRet == cbBuf);
            if (pModPe->cImports == UINT32_MAX)
            {
                int rc = rtLdrPE_CountImports(pModPe, pvBits);
                if (RT_FAILURE(rc))
                    return rc;
            }
            *(uint32_t *)pvBuf = pModPe->cImports;
            break;

        case RTLDRPROP_IMPORT_MODULE:
            Assert(cbBuf >= sizeof(uint32_t));
            return rtLdrPE_QueryImportModule(pModPe, pvBits, *(uint32_t *)pvBuf, pvBuf, cbBuf, pcbRet);

        case RTLDRPROP_FILE_OFF_HEADER:
            Assert(cbBuf == sizeof(uint32_t) || cbBuf == sizeof(uint64_t));
            if (cbBuf == sizeof(uint32_t))
                *(uint32_t *)pvBuf = pModPe->offNtHdrs;
            else
                *(uint64_t *)pvBuf = pModPe->offNtHdrs;
            return VINF_SUCCESS;

        case RTLDRPROP_INTERNAL_NAME:
            return rtLdrPE_QueryInternalName(pModPe, pvBits, pvBuf, cbBuf, pcbRet);

        case RTLDRPROP_UNWIND_TABLE:
            return rtLdrPE_QueryUnwindTable(pModPe, pvBits, pvBuf, cbBuf, pcbRet);

        case RTLDRPROP_UNWIND_INFO:
        {
            uint32_t uRva = *(uint32_t const *)pvBuf;
            if (uRva < pModPe->cbImage)
            {
                uint32_t cbLeft   = pModPe->cbImage - uRva;
                uint32_t cbToRead = (uint32_t)RT_MIN(cbLeft, cbBuf);
                *pcbRet = cbToRead;
                return rtldrPEReadPartByRvaInfoBuf(pModPe, pvBits, uRva, cbToRead, pvBuf);
            }
            *pcbRet = 0;
            return VINF_SUCCESS;
        }

        default:
            return VERR_NOT_FOUND;
    }
    return VINF_SUCCESS;
}



/*
 * Lots of Authenticode fun ahead.
 */


/**
 * Initializes the hash context.
 *
 * @returns VINF_SUCCESS or VERR_NOT_SUPPORTED.
 * @param   pHashCtx            The hash context union.
 * @param   enmDigest           The hash type we're calculating..
 */
static int rtLdrPE_HashInit(PRTLDRPEHASHCTXUNION pHashCtx, RTDIGESTTYPE enmDigest)
{
    switch (enmDigest)
    {
        case RTDIGESTTYPE_SHA512:  RTSha512Init(&pHashCtx->Sha512); break;
        case RTDIGESTTYPE_SHA384:  RTSha384Init(&pHashCtx->Sha384); break;
        case RTDIGESTTYPE_SHA256:  RTSha256Init(&pHashCtx->Sha256); break;
        case RTDIGESTTYPE_SHA1:    RTSha1Init(&pHashCtx->Sha1); break;
        case RTDIGESTTYPE_MD5:     RTMd5Init(&pHashCtx->Md5); break;
        default:                   AssertFailedReturn(VERR_NOT_SUPPORTED);
    }
    return VINF_SUCCESS;
}


/**
 * Updates the hash with more data.
 *
 * @param   pHashCtx            The hash context union.
 * @param   enmDigest           The hash type we're calculating..
 * @param   pvBuf               Pointer to a buffer with bytes to add to thash.
 * @param   cbBuf               How many bytes to add from @a pvBuf.
 */
static void rtLdrPE_HashUpdate(PRTLDRPEHASHCTXUNION pHashCtx, RTDIGESTTYPE enmDigest, void const *pvBuf, size_t cbBuf)
{
    switch (enmDigest)
    {
        case RTDIGESTTYPE_SHA512:  RTSha512Update(&pHashCtx->Sha512, pvBuf, cbBuf); break;
        case RTDIGESTTYPE_SHA384:  RTSha384Update(&pHashCtx->Sha384, pvBuf, cbBuf); break;
        case RTDIGESTTYPE_SHA256:  RTSha256Update(&pHashCtx->Sha256, pvBuf, cbBuf); break;
        case RTDIGESTTYPE_SHA1:    RTSha1Update(&pHashCtx->Sha1, pvBuf, cbBuf); break;
        case RTDIGESTTYPE_MD5:     RTMd5Update(&pHashCtx->Md5, pvBuf, cbBuf); break;
        default:                   AssertReleaseFailed();
    }
}


/**
 * Finalizes the hash calculations.
 *
 * @param   pHashCtx            The hash context union.
 * @param   enmDigest           The hash type we're calculating..
 * @param   pHashRes            The hash result union.
 */
static void rtLdrPE_HashFinalize(PRTLDRPEHASHCTXUNION pHashCtx, RTDIGESTTYPE enmDigest, PRTLDRPEHASHRESUNION pHashRes)
{
    switch (enmDigest)
    {
        case RTDIGESTTYPE_SHA512:  RTSha512Final(&pHashCtx->Sha512, pHashRes->abSha512); break;
        case RTDIGESTTYPE_SHA384:  RTSha384Final(&pHashCtx->Sha384, pHashRes->abSha384); break;
        case RTDIGESTTYPE_SHA256:  RTSha256Final(&pHashCtx->Sha256, pHashRes->abSha256); break;
        case RTDIGESTTYPE_SHA1:    RTSha1Final(&pHashCtx->Sha1, pHashRes->abSha1); break;
        case RTDIGESTTYPE_MD5:     RTMd5Final(pHashRes->abMd5, &pHashCtx->Md5); break;
        default:                   AssertReleaseFailed();
    }
}


/**
 * Returns the digest size for the given digest type.
 *
 * @returns Size in bytes.
 * @param   enmDigest           The hash type in question.
 */
static uint32_t rtLdrPE_HashGetHashSize(RTDIGESTTYPE enmDigest)
{
    switch (enmDigest)
    {
        case RTDIGESTTYPE_SHA512:  return RTSHA512_HASH_SIZE;
        case RTDIGESTTYPE_SHA384:  return RTSHA384_HASH_SIZE;
        case RTDIGESTTYPE_SHA256:  return RTSHA256_HASH_SIZE;
        case RTDIGESTTYPE_SHA1:    return RTSHA1_HASH_SIZE;
        case RTDIGESTTYPE_MD5:     return RTMD5_HASH_SIZE;
        default:                   AssertReleaseFailedReturn(0);
    }
}


#ifndef IPRT_WITHOUT_LDR_VERIFY
/**
 * Checks if the hash type is supported.
 *
 * @returns true/false.
 * @param   enmDigest           The hash type in question.
 */
static bool rtLdrPE_HashIsSupported(RTDIGESTTYPE enmDigest)
{
    switch (enmDigest)
    {
        case RTDIGESTTYPE_SHA512:
        case RTDIGESTTYPE_SHA384:
        case RTDIGESTTYPE_SHA256:
        case RTDIGESTTYPE_SHA1:
        case RTDIGESTTYPE_MD5:
            return true;
        default:
            return false;
    }
}
#endif


/**
 * Calculate the special too watch out for when hashing the image.
 *
 * @returns IPRT status code.
 * @param   pModPe              The PE module.
 * @param   pPlaces             The structure where to store the special places.
 * @param   pErrInfo            Optional error info.
 */
static int rtldrPe_CalcSpecialHashPlaces(PRTLDRMODPE pModPe, PRTLDRPEHASHSPECIALS pPlaces, PRTERRINFO pErrInfo)
{
    /*
     * If we're here despite a missing signature, we need to get the file size.
     */
    pPlaces->cbToHash = pModPe->SecurityDir.VirtualAddress;
    if (pPlaces->cbToHash == 0)
    {
        uint64_t cbFile = pModPe->Core.pReader->pfnSize(pModPe->Core.pReader);
        pPlaces->cbToHash = (uint32_t)cbFile;
        if (pPlaces->cbToHash != (uint64_t)cbFile)
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_FILE_LENGTH_ERROR, "File is too large: %RTfoff", cbFile);
    }

    /*
     * Calculate the special places.
     */
    pPlaces->offCksum       = (uint32_t)pModPe->offNtHdrs
                            + (pModPe->f64Bit
                               ? RT_UOFFSETOF(IMAGE_NT_HEADERS64, OptionalHeader.CheckSum)
                               : RT_UOFFSETOF(IMAGE_NT_HEADERS32, OptionalHeader.CheckSum));
    pPlaces->cbCksum        = RT_SIZEOFMEMB(IMAGE_NT_HEADERS32, OptionalHeader.CheckSum);
    pPlaces->offSecDir      = (uint32_t)pModPe->offNtHdrs
                            + (pModPe->f64Bit
                               ? RT_UOFFSETOF(IMAGE_NT_HEADERS64, OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY])
                               : RT_UOFFSETOF(IMAGE_NT_HEADERS32, OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY]));
    pPlaces->cbSecDir       = sizeof(IMAGE_DATA_DIRECTORY);
    pPlaces->offEndSpecial  = pPlaces->offSecDir + pPlaces->cbSecDir;
    return VINF_SUCCESS;
}


/**
 * Calculates the whole image hash.
 *
 * The Authenticode_PE.docx version 1.0 explains how the hash is calculated,
 * points 8 thru 14 are bogus.  If you study them a little carefully, it is
 * clear that the algorithm will only work if the raw data for the section have
 * no gaps between them or in front of them.  So, this elaborate section sorting
 * by PointerToRawData and working them section by section could simply be
 * replaced by one point:
 *
 *      8. Add all the file content between SizeOfHeaders and the
 *         attribute certificate table to the hash.  Then finalize
 *         the hash.
 *
 * Not sure if Microsoft is screwing with us on purpose here or whether they
 * assigned some of this work to less talented engineers and tech writers.  I
 * love fact that they say it's "simplified" and should yield the correct hash
 * for "almost all" files.  Stupid, Stupid, Microsofties!!
 *
 * My simplified implementation that just hashes the entire file up to the
 * signature or end of the file produces the same SHA1 values as "signtool
 * verify /v" does both for edited executables with gaps between/before/after
 * sections raw data and normal executables without any gaps.
 *
 * @returns IPRT status code.
 * @param   pModPe      The PE module.
 * @param   pvScratch   Scratch buffer.
 * @param   cbScratch   Size of the scratch buffer.
 * @param   enmDigest   The hash digest type we're calculating.
 * @param   pHashCtx    Hash context scratch area.
 * @param   pHashRes    Hash result buffer.
 * @param   pErrInfo    Optional error info buffer.
 */
static int rtldrPE_HashImageCommon(PRTLDRMODPE pModPe, void *pvScratch, uint32_t cbScratch, RTDIGESTTYPE enmDigest,
                                   PRTLDRPEHASHCTXUNION pHashCtx, PRTLDRPEHASHRESUNION pHashRes, PRTERRINFO pErrInfo)
{
    int rc = rtLdrPE_HashInit(pHashCtx, enmDigest);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Calculate the special places.
     */
    RTLDRPEHASHSPECIALS SpecialPlaces = { 0, 0, 0, 0, 0, 0 }; /* shut up gcc */
    rc = rtldrPe_CalcSpecialHashPlaces(pModPe, &SpecialPlaces, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Work our way thru the image data.
     */
    uint32_t off = 0;
    while (off < SpecialPlaces.cbToHash)
    {
        uint32_t cbRead = RT_MIN(SpecialPlaces.cbToHash - off, cbScratch);
        uint8_t *pbCur  = (uint8_t *)pvScratch;
        rc = pModPe->Core.pReader->pfnRead(pModPe->Core.pReader, pbCur, cbRead, off);
        if (RT_FAILURE(rc))
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_READ_ERROR_HASH, "Hash read error at %#x: %Rrc (cbRead=%#zx)",
                                 off, rc, cbRead);

        if (off < SpecialPlaces.offEndSpecial)
        {
            if (off < SpecialPlaces.offCksum)
            {
                /* Hash everything up to the checksum. */
                uint32_t cbChunk = RT_MIN(SpecialPlaces.offCksum - off, cbRead);
                rtLdrPE_HashUpdate(pHashCtx, enmDigest, pbCur, cbChunk);
                pbCur  += cbChunk;
                cbRead -= cbChunk;
                off    += cbChunk;
            }

            if (off < SpecialPlaces.offCksum + SpecialPlaces.cbCksum && off >= SpecialPlaces.offCksum)
            {
                /* Skip the checksum */
                uint32_t cbChunk = RT_MIN(SpecialPlaces.offCksum + SpecialPlaces.cbCksum - off, cbRead);
                pbCur  += cbChunk;
                cbRead -= cbChunk;
                off    += cbChunk;
            }

            if (off < SpecialPlaces.offSecDir && off >= SpecialPlaces.offCksum + SpecialPlaces.cbCksum)
            {
                /* Hash everything between the checksum and the data dir entry. */
                uint32_t cbChunk = RT_MIN(SpecialPlaces.offSecDir - off, cbRead);
                rtLdrPE_HashUpdate(pHashCtx, enmDigest,  pbCur, cbChunk);
                pbCur  += cbChunk;
                cbRead -= cbChunk;
                off    += cbChunk;
            }

            if (off < SpecialPlaces.offSecDir + SpecialPlaces.cbSecDir && off >= SpecialPlaces.offSecDir)
            {
                /* Skip the security data directory entry. */
                uint32_t cbChunk = RT_MIN(SpecialPlaces.offSecDir + SpecialPlaces.cbSecDir - off, cbRead);
                pbCur  += cbChunk;
                cbRead -= cbChunk;
                off    += cbChunk;
            }
        }

        rtLdrPE_HashUpdate(pHashCtx, enmDigest, pbCur, cbRead);

        /* Advance */
        off += cbRead;
    }

    /*
     * If there isn't a signature, experiments with signtool indicates that we
     * have to zero padd the file size until it's a multiple of 8.  (This is
     * most likely to give 64-bit values in the certificate a natural alignment
     * when memory mapped.)
     */
    if (   pModPe->SecurityDir.VirtualAddress != SpecialPlaces.cbToHash
        && SpecialPlaces.cbToHash != RT_ALIGN_32(SpecialPlaces.cbToHash, WIN_CERTIFICATE_ALIGNMENT))
    {
        static const uint8_t s_abZeros[WIN_CERTIFICATE_ALIGNMENT] = { 0,0,0,0, 0,0,0,0 };
        rtLdrPE_HashUpdate(pHashCtx, enmDigest, s_abZeros,
                           RT_ALIGN_32(SpecialPlaces.cbToHash, WIN_CERTIFICATE_ALIGNMENT) - SpecialPlaces.cbToHash);
    }

    /*
     * Done. Finalize the hashes.
     */
    rtLdrPE_HashFinalize(pHashCtx, enmDigest, pHashRes);
    return VINF_SUCCESS;
}

#ifndef IPRT_WITHOUT_LDR_PAGE_HASHING

/**
 * Returns the size of the page hashes, including the terminator entry.
 *
 * Used for handling RTLDRPROP_HASHABLE_PAGES.
 *
 * @returns Number of page hashes.
 * @param   pModPe              The PE module.
 */
static uint32_t rtLdrPE_GetHashablePages(PRTLDRMODPE pModPe)
{
    uint32_t const  cbPage = _4K;
    uint32_t        cPages = 1; /* termination entry */

    /* Add implicit header section: */
    cPages += (pModPe->cbHeaders + cbPage - 1) / cbPage;

    /* Add on disk pages for each section.  Each starts with a fresh page and
       we ASSUMES that it is page aligned (in memory). */
    for (uint32_t i = 0; i < pModPe->cSections; i++)
    {
        uint32_t const cbRawData = pModPe->paSections[i].SizeOfRawData;
        if (cbRawData > 0)
            cPages += (cbRawData + cbPage - 1) / cbPage;
    }

    return cPages;
}


/**
 * Worker for rtLdrPE_QueryPageHashes.
 *
 * Keep in mind that rtldrPE_VerifyAllPageHashes does similar work, so some
 * fixes may apply both places.
 */
static int rtLdrPE_CalcPageHashes(PRTLDRMODPE pModPe, RTDIGESTTYPE const enmDigest, uint32_t const cbHash,
                                  uint8_t *pbDst, uint8_t *pbScratch, uint32_t cbScratch, uint32_t const cbPage)
{
    /*
     * Calculate the special places.
     */
    RTLDRPEHASHSPECIALS SpecialPlaces = { 0, 0, 0, 0, 0, 0 }; /* shut up gcc */
    int rc = rtldrPe_CalcSpecialHashPlaces(pModPe, &SpecialPlaces, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Walk section table and hash the pages in each.  Because the headers are
     * in an implicit section, the loop advancing is a little funky.
     */
    int32_t const   cSections   = pModPe->cSections;
    int32_t         iSection    = -1;
    uint32_t        offRawData  = 0;
    uint32_t        cbRawData   = pModPe->cbHeaders;
    uint32_t        offLastPage = 0;
    uint32_t        cbLastPage  = 0;

    uint32_t const  cbScratchReadMax = cbScratch / cbPage * cbPage;
    uint32_t        cbScratchRead    = 0;
    uint32_t        offScratchRead   = 0;

    for (;;)
    {
        /*
         * Process the pages in this section.
         */
        uint32_t cPagesInSection = (cbRawData + cbPage - 1) / cbPage;
        for (uint32_t iPage = 0; iPage < cPagesInSection; iPage++)
        {
            uint32_t const offPageInSect = iPage * cbPage;
            uint32_t const offPageInFile = offRawData + offPageInSect;
            uint32_t const cbPageInFile  = RT_MIN(cbPage, cbRawData - offPageInSect);
            offLastPage = offPageInFile;
            cbLastPage  = cbPageInFile;

            /* Calculate and output the page offset. */
            *(uint32_t *)pbDst = offPageInFile;
            pbDst += sizeof(uint32_t);

            /*
             * Read/find in the raw page.
             */
            /* Did we get a cache hit? */
            uint8_t *pbCur = pbScratch;
            if (   offPageInFile + cbPageInFile <= offScratchRead + cbScratchRead
                && offPageInFile                >= offScratchRead)
                pbCur += offPageInFile - offScratchRead;
            /* Missed, read more. */
            else
            {
                offScratchRead = offPageInFile;
                cbScratchRead  = SpecialPlaces.cbToHash - offPageInFile;
                if (cbScratchRead > cbScratchReadMax)
                    cbScratchRead = cbScratchReadMax;
                rc = pModPe->Core.pReader->pfnRead(pModPe->Core.pReader, pbCur, cbScratchRead, offScratchRead);
                if (RT_FAILURE(rc))
                    return VERR_LDRVI_READ_ERROR_HASH;
            }

            /*
             * Hash it.
             */
            RTLDRPEHASHCTXUNION HashCtx;
            rc = rtLdrPE_HashInit(&HashCtx, enmDigest);
            AssertRCReturn(rc, rc);

            /* Deal with special places. */
            uint32_t cbLeft = cbPageInFile;
            if (offPageInFile < SpecialPlaces.offEndSpecial)
            {
                uint32_t off = offPageInFile;
                if (off < SpecialPlaces.offCksum)
                {
                    /* Hash everything up to the checksum. */
                    uint32_t cbChunk = RT_MIN(SpecialPlaces.offCksum - off, cbLeft);
                    rtLdrPE_HashUpdate(&HashCtx, enmDigest, pbCur, cbChunk);
                    pbCur  += cbChunk;
                    cbLeft -= cbChunk;
                    off    += cbChunk;
                }

                if (off < SpecialPlaces.offCksum + SpecialPlaces.cbCksum && off >= SpecialPlaces.offCksum)
                {
                    /* Skip the checksum */
                    uint32_t cbChunk = RT_MIN(SpecialPlaces.offCksum + SpecialPlaces.cbCksum - off, cbLeft);
                    pbCur  += cbChunk;
                    cbLeft -= cbChunk;
                    off    += cbChunk;
                }

                if (off < SpecialPlaces.offSecDir && off >= SpecialPlaces.offCksum + SpecialPlaces.cbCksum)
                {
                    /* Hash everything between the checksum and the data dir entry. */
                    uint32_t cbChunk = RT_MIN(SpecialPlaces.offSecDir - off, cbLeft);
                    rtLdrPE_HashUpdate(&HashCtx, enmDigest, pbCur, cbChunk);
                    pbCur  += cbChunk;
                    cbLeft -= cbChunk;
                    off    += cbChunk;
                }

                if (off < SpecialPlaces.offSecDir + SpecialPlaces.cbSecDir && off >= SpecialPlaces.offSecDir)
                {
                    /* Skip the security data directory entry. */
                    uint32_t cbChunk = RT_MIN(SpecialPlaces.offSecDir + SpecialPlaces.cbSecDir - off, cbLeft);
                    pbCur  += cbChunk;
                    cbLeft -= cbChunk;
                    off    += cbChunk;
                }
            }

            rtLdrPE_HashUpdate(&HashCtx, enmDigest, pbCur, cbLeft);
            if (cbPageInFile < cbPage)
                rtLdrPE_HashUpdate(&HashCtx, enmDigest, g_abRTZero4K, cbPage - cbPageInFile);

            /*
             * Finish the hash calculation storing it in the table.
             */
            rtLdrPE_HashFinalize(&HashCtx, enmDigest, (PRTLDRPEHASHRESUNION)pbDst);
            pbDst += cbHash;
        }

        /*
         * Advance to the next section.
         */
        iSection++;
        if (iSection >= cSections)
            break;
        offRawData = pModPe->paSections[iSection].PointerToRawData;
        cbRawData  = pModPe->paSections[iSection].SizeOfRawData;
    }

    /*
     * Add the terminator entry.
     */
    *(uint32_t *)pbDst = offLastPage + cbLastPage;
    RT_BZERO(&pbDst[sizeof(uint32_t)], cbHash);

    return VINF_SUCCESS;
}


/**
 * Creates the page hash table for the image.
 *
 * Used for handling RTLDRPROP_SHA1_PAGE_HASHES and
 * RTLDRPROP_SHA256_PAGE_HASHES.
 *
 * @returns IPRT status code.
 * @param   pModPe              The PE module.
 * @param   enmDigest           The digest to use when hashing the pages.
 * @param   pvBuf               Where to return the page hash table.
 * @param   cbBuf               The size of the buffer @a pvBuf points to.
 * @param   pcbRet              Where to return the output/needed size.
 */
static int rtLdrPE_QueryPageHashes(PRTLDRMODPE pModPe, RTDIGESTTYPE enmDigest, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    /*
     * Check that we've got enough buffer space.
     */
    uint32_t const cbPage   = _4K;
    uint32_t const cEntries = rtLdrPE_GetHashablePages(pModPe);
    uint32_t const cbHash   = rtLdrPE_HashGetHashSize(enmDigest);
    AssertReturn(cbHash > 0, VERR_INTERNAL_ERROR_3);

    size_t const   cbNeeded = (size_t)(cbHash + 4) * cEntries;
    *pcbRet = cbNeeded;
    if (cbNeeded > cbBuf)
        return VERR_BUFFER_OVERFLOW;

    /*
     * Allocate a scratch buffer and call worker to do the real job.
     */
# ifdef IN_RING0
    uint32_t    cbScratch = _256K - _4K;
# else
    uint32_t    cbScratch = _1M;
# endif
    void       *pvScratch = RTMemTmpAlloc(cbScratch);
    if (!pvScratch)
    {
        cbScratch = _4K;
        pvScratch = RTMemTmpAlloc(cbScratch);
        if (!pvScratch)
            return VERR_NO_TMP_MEMORY;
    }

    int rc = rtLdrPE_CalcPageHashes(pModPe, enmDigest, cbHash, (uint8_t *)pvBuf, (uint8_t *)pvScratch, cbScratch, cbPage);

    RTMemTmpFree(pvScratch);
    return rc;
}

#endif /* !IPRT_WITHOUT_LDR_PAGE_HASHING */
#ifndef IPRT_WITHOUT_LDR_VERIFY

/**
 * Verifies image preconditions not checked by the open validation code.
 *
 * @returns IPRT status code.
 * @param   pModPe              The PE module.
 * @param   pErrInfo            Optional error info buffer.
 */
static int rtldrPE_VerifySignatureImagePrecoditions(PRTLDRMODPE pModPe, PRTERRINFO pErrInfo)
{
    /*
     * Validate the sections.  While doing so, track the amount of section raw
     * section data in the file so we can use this to validate the signature
     * table location later.
     */
    uint32_t offNext = pModPe->cbHeaders; /* same */
    for (uint32_t i = 0; i < pModPe->cSections; i++)
        if (pModPe->paSections[i].SizeOfRawData > 0)
        {
            uint64_t offEnd = (uint64_t)pModPe->paSections[i].PointerToRawData + pModPe->paSections[i].SizeOfRawData;
            if (offEnd > offNext)
            {
                if (offEnd >= _2G)
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_SECTION_RAW_DATA_VALUES,
                                         "Section %#u specifies file data after 2GB: PointerToRawData=%#x SizeOfRawData=%#x",
                                         i, pModPe->paSections[i].PointerToRawData, pModPe->paSections[i].SizeOfRawData);
                offNext = (uint32_t)offEnd;
            }
        }
    uint32_t offEndOfSectionData = offNext;

    /*
     * Validate the signature.
     */
    if (!pModPe->SecurityDir.Size)
        return RTErrInfoSet(pErrInfo, VERR_LDRVI_NOT_SIGNED, "Not signed.");

    uint32_t const offSignature = pModPe->SecurityDir.VirtualAddress;
    uint32_t const cbSignature  = pModPe->SecurityDir.Size;
    if (   cbSignature  <= sizeof(WIN_CERTIFICATE)
        || cbSignature  >= RTLDRMODPE_MAX_SECURITY_DIR_SIZE
        || offSignature >= _2G)
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_INVALID_SECURITY_DIR_ENTRY,
                             "Invalid security data dir entry: cb=%#x off=%#x", cbSignature, offSignature);

    if (offSignature < offEndOfSectionData)
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_INVALID_SECURITY_DIR_ENTRY,
                             "Invalid security data dir entry offset: %#x offEndOfSectionData=%#x",
                             offSignature, offEndOfSectionData);

    if (RT_ALIGN_32(offSignature, WIN_CERTIFICATE_ALIGNMENT) != offSignature)
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_INVALID_SECURITY_DIR_ENTRY,
                             "Misaligned security dir entry offset: %#x (alignment=%#x)",
                             offSignature, WIN_CERTIFICATE_ALIGNMENT);


    return VINF_SUCCESS;
}


/**
 * Reads and checks the raw signature data.
 *
 * @returns IPRT status code.
 * @param   pModPe              The PE module.
 * @param   ppSignature         Where to return the pointer to the parsed
 *                              signature data.  Pass to
 *                              rtldrPE_VerifySignatureDestroy when done.
 * @param   pErrInfo            Optional error info buffer.
 */
static int rtldrPE_VerifySignatureRead(PRTLDRMODPE pModPe, PRTLDRPESIGNATURE *ppSignature, PRTERRINFO pErrInfo)
{
    *ppSignature = NULL;
    AssertReturn(pModPe->SecurityDir.Size > 0, VERR_INTERNAL_ERROR_2);

    /*
     * Allocate memory for reading and parsing it.
     */
    if (pModPe->SecurityDir.Size >= RTLDRMODPE_MAX_SECURITY_DIR_SIZE)
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_INVALID_SECURITY_DIR_ENTRY,
                             "Signature directory is to large: %#x", pModPe->SecurityDir.Size);

    PRTLDRPESIGNATURE pSignature = (PRTLDRPESIGNATURE)RTMemTmpAllocZ(sizeof(*pSignature) + 64 + pModPe->SecurityDir.Size);
    if (!pSignature)
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_NO_MEMORY_SIGNATURE, "Failed to allocate %zu bytes",
                             sizeof(*pSignature) + 64 + pModPe->SecurityDir.Size);
    pSignature->pRawData = RT_ALIGN_PT(pSignature + 1, 64, WIN_CERTIFICATE const *);


    /*
     * Read it.
     */
    int rc = pModPe->Core.pReader->pfnRead(pModPe->Core.pReader, (void *)pSignature->pRawData,
                                           pModPe->SecurityDir.Size, pModPe->SecurityDir.VirtualAddress);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check the table we've read in.
         */
        uint32_t               cbLeft = pModPe->SecurityDir.Size;
        WIN_CERTIFICATE const *pEntry = pSignature->pRawData;
        for (;;)
        {
            if (   cbLeft           < sizeof(*pEntry)
                || pEntry->dwLength > cbLeft
                || pEntry->dwLength < sizeof(*pEntry))
                rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_HDR_LENGTH,
                                   "Bad WIN_CERTIFICATE length: %#x  (max %#x, signature=%u)",
                                   pEntry->dwLength, cbLeft, 0);
            else if (pEntry->wRevision != WIN_CERT_REVISION_2_0)
                rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_HDR_REVISION,
                                   "Unsupported WIN_CERTIFICATE revision value: %#x (signature=%u)",
                                   pEntry->wRevision, 0);
            else if (pEntry->wCertificateType != WIN_CERT_TYPE_PKCS_SIGNED_DATA)
                rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_HDR_TYPE,
                                   "Unsupported WIN_CERTIFICATE certificate type: %#x (signature=%u)",
                                   pEntry->wCertificateType, 0);
            else
            {
                /* advance */
                uint32_t cbEntry = RT_ALIGN(pEntry->dwLength, WIN_CERTIFICATE_ALIGNMENT);
                if (cbEntry >= cbLeft)
                    break;
                cbLeft -= cbEntry;
                pEntry = (WIN_CERTIFICATE *)((uintptr_t)pEntry + cbEntry);

                /* For now, only one entry is supported. */
                rc = RTErrInfoSet(pErrInfo, VERR_LDRVI_BAD_CERT_MULTIPLE, "Multiple WIN_CERTIFICATE entries are not supported.");
            }
            break;
        }
        if (RT_SUCCESS(rc))
        {
            *ppSignature = pSignature;
            return VINF_SUCCESS;
        }
    }
    else
        rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_READ_ERROR_SIGNATURE, "Signature read error: %Rrc", rc);
    RTMemTmpFree(pSignature);
    return rc;
}


/**
 * Destroys the parsed signature.
 *
 * @param   pModPe              The PE module.
 * @param   pSignature          The signature data to destroy.
 */
static void rtldrPE_VerifySignatureDestroy(PRTLDRMODPE pModPe, PRTLDRPESIGNATURE pSignature)
{
    RT_NOREF_PV(pModPe);
    RTCrPkcs7ContentInfo_Delete(&pSignature->PrimaryContentInfo);
    if (pSignature->paNested)
    {
        RTMemTmpFree(pSignature->paNested);
        pSignature->paNested = NULL;
    }
    RTMemTmpFree(pSignature);
}


/**
 * Handles nested signatures.
 *
 * @returns IPRT status code.
 * @param   pSignature          The signature status structure.  Returns with
 *                              cNested = 0 and paNested = NULL if no nested
 *                              signatures.
 * @param   pErrInfo            Where to return extended error info (optional).
 */
static int rtldrPE_VerifySignatureDecodeNested(PRTLDRPESIGNATURE pSignature, PRTERRINFO pErrInfo)
{
    Assert(pSignature->cNested == 0);
    Assert(pSignature->paNested == NULL);

    /*
     * Count nested signatures.
     */
    uint32_t cNested = 0;
    for (uint32_t iSignerInfo = 0; iSignerInfo < pSignature->Primary.pSignedData->SignerInfos.cItems; iSignerInfo++)
    {
        PCRTCRPKCS7SIGNERINFO pSignerInfo = pSignature->Primary.pSignedData->SignerInfos.papItems[iSignerInfo];
        for (uint32_t iAttrib = 0; iAttrib < pSignerInfo->UnauthenticatedAttributes.cItems; iAttrib++)
        {
            PCRTCRPKCS7ATTRIBUTE pAttrib = pSignerInfo->UnauthenticatedAttributes.papItems[iAttrib];
            if (pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE)
            {
                Assert(pAttrib->uValues.pContentInfos);
                cNested += pAttrib->uValues.pContentInfos->cItems;
            }
        }
    }
    if (!cNested)
        return VINF_SUCCESS;

    /*
     * Allocate and populate the info structures.
     */
    pSignature->paNested = (PRTLDRPESIGNATUREONE)RTMemTmpAllocZ(sizeof(pSignature->paNested[0]) * cNested);
    if (!pSignature->paNested)
        return RTErrInfoSetF(pErrInfo, VERR_NO_TMP_MEMORY, "Failed to allocate space for %u nested signatures", cNested);
    pSignature->cNested = cNested;

    cNested = 0;
    for (uint32_t iSignerInfo = 0; iSignerInfo < pSignature->Primary.pSignedData->SignerInfos.cItems; iSignerInfo++)
    {
        PCRTCRPKCS7SIGNERINFO pSignerInfo = pSignature->Primary.pSignedData->SignerInfos.papItems[iSignerInfo];
        for (uint32_t iAttrib = 0; iAttrib < pSignerInfo->UnauthenticatedAttributes.cItems; iAttrib++)
        {
            PCRTCRPKCS7ATTRIBUTE pAttrib = pSignerInfo->UnauthenticatedAttributes.papItems[iAttrib];
            if (pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE)
            {
                for (uint32_t iItem = 0; iItem < pAttrib->uValues.pContentInfos->cItems; iItem++, cNested++)
                {
                    PRTLDRPESIGNATUREONE  pInfo        = &pSignature->paNested[cNested];
                    PRTCRPKCS7CONTENTINFO pContentInfo = pAttrib->uValues.pContentInfos->papItems[iItem];
                    pInfo->pContentInfo = pContentInfo;
                    pInfo->iSignature   = cNested;

                    if (RTCrPkcs7ContentInfo_IsSignedData(pInfo->pContentInfo))
                    { /* likely */ }
                    else
                        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_EXPECTED_INDIRECT_DATA_CONTENT_OID, /** @todo error code*/
                                             "Nested#%u: PKCS#7 is not 'signedData': %s", cNested, pInfo->pContentInfo->ContentType.szObjId);
                    PRTCRPKCS7SIGNEDDATA pSignedData = pContentInfo->u.pSignedData;
                    pInfo->pSignedData = pSignedData;

                    /*
                     * Check the authenticode bits.
                     */
                    if (!strcmp(pSignedData->ContentInfo.ContentType.szObjId, RTCRSPCINDIRECTDATACONTENT_OID))
                    { /* likely */ }
                    else
                        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_EXPECTED_INDIRECT_DATA_CONTENT_OID,
                                             "Nested#%u: Unknown pSignedData.ContentInfo.ContentType.szObjId value: %s (expected %s)",
                                             cNested, pSignedData->ContentInfo.ContentType.szObjId, RTCRSPCINDIRECTDATACONTENT_OID);
                    pInfo->pIndData = pSignedData->ContentInfo.u.pIndirectDataContent;
                    Assert(pInfo->pIndData);

                    /*
                     * Check that things add up.
                     */
                    int rc = RTCrPkcs7SignedData_CheckSanity(pSignedData,
                                                             RTCRPKCS7SIGNEDDATA_SANITY_F_AUTHENTICODE
                                                             | RTCRPKCS7SIGNEDDATA_SANITY_F_ONLY_KNOWN_HASH
                                                             | RTCRPKCS7SIGNEDDATA_SANITY_F_SIGNING_CERT_PRESENT,
                                                             pErrInfo, "SD");
                    if (RT_SUCCESS(rc))
                        rc = RTCrSpcIndirectDataContent_CheckSanityEx(pInfo->pIndData,
                                                                      pSignedData,
                                                                      RTCRSPCINDIRECTDATACONTENT_SANITY_F_ONLY_KNOWN_HASH,
                                                                      pErrInfo);
                    if (RT_SUCCESS(rc))
                    {
                        PCRTCRX509ALGORITHMIDENTIFIER pDigestAlgorithm = &pInfo->pIndData->DigestInfo.DigestAlgorithm;
                        pInfo->enmDigest = RTCrX509AlgorithmIdentifier_GetDigestType(pDigestAlgorithm,
                                                                                     true /*fPureDigestsOnly*/);
                        AssertReturn(pInfo->enmDigest != RTDIGESTTYPE_INVALID, VERR_INTERNAL_ERROR_4); /* Checked above! */
                    }
                    else
                        return rc;
                }
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Decodes the raw signature.
 *
 * @returns IPRT status code.
 * @param   pModPe              The PE module.
 * @param   pSignature          The signature data.
 * @param   pErrInfo            Optional error info buffer.
 */
static int rtldrPE_VerifySignatureDecode(PRTLDRMODPE pModPe, PRTLDRPESIGNATURE pSignature, PRTERRINFO pErrInfo)
{
    WIN_CERTIFICATE const  *pEntry = pSignature->pRawData;
    AssertReturn(pEntry->wCertificateType == WIN_CERT_TYPE_PKCS_SIGNED_DATA, VERR_INTERNAL_ERROR_2);
    AssertReturn(pEntry->wRevision        == WIN_CERT_REVISION_2_0, VERR_INTERNAL_ERROR_2);
    RT_NOREF_PV(pModPe);

    RTASN1CURSORPRIMARY PrimaryCursor;
    RTAsn1CursorInitPrimary(&PrimaryCursor,
                            &pEntry->bCertificate[0],
                            pEntry->dwLength - RT_UOFFSETOF(WIN_CERTIFICATE, bCertificate),
                            pErrInfo,
                            &g_RTAsn1DefaultAllocator,
                            0,
                            "WinCert");

    PRTLDRPESIGNATUREONE pInfo = &pSignature->Primary;
    pInfo->pContentInfo = &pSignature->PrimaryContentInfo;
    int rc = RTCrPkcs7ContentInfo_DecodeAsn1(&PrimaryCursor.Cursor, 0, pInfo->pContentInfo, "CI");
    if (RT_SUCCESS(rc))
    {
        if (RTCrPkcs7ContentInfo_IsSignedData(pInfo->pContentInfo))
        {
            pInfo->pSignedData = pInfo->pContentInfo->u.pSignedData;

            /*
             * Decode the authenticode bits.
             */
            if (!strcmp(pInfo->pSignedData->ContentInfo.ContentType.szObjId, RTCRSPCINDIRECTDATACONTENT_OID))
            {
                pInfo->pIndData = pInfo->pSignedData->ContentInfo.u.pIndirectDataContent;
                Assert(pInfo->pIndData);

                /*
                 * Check that things add up.
                 */
                rc = RTCrPkcs7SignedData_CheckSanity(pInfo->pSignedData,
                                                     RTCRPKCS7SIGNEDDATA_SANITY_F_AUTHENTICODE
                                                     | RTCRPKCS7SIGNEDDATA_SANITY_F_ONLY_KNOWN_HASH
                                                     | RTCRPKCS7SIGNEDDATA_SANITY_F_SIGNING_CERT_PRESENT,
                                                     pErrInfo, "SD");
                if (RT_SUCCESS(rc))
                    rc = RTCrSpcIndirectDataContent_CheckSanityEx(pInfo->pIndData,
                                                                  pInfo->pSignedData,
                                                                  RTCRSPCINDIRECTDATACONTENT_SANITY_F_ONLY_KNOWN_HASH,
                                                                  pErrInfo);
                if (RT_SUCCESS(rc))
                {
                    PCRTCRX509ALGORITHMIDENTIFIER pDigestAlgorithm = &pInfo->pIndData->DigestInfo.DigestAlgorithm;
                    pInfo->enmDigest = RTCrX509AlgorithmIdentifier_GetDigestType(pDigestAlgorithm, true /*fPureDigestsOnly*/);
                    AssertReturn(pInfo->enmDigest != RTDIGESTTYPE_INVALID, VERR_INTERNAL_ERROR_4); /* Checked above! */

                    /*
                     * Deal with nested signatures.
                     */
                    rc = rtldrPE_VerifySignatureDecodeNested(pSignature, pErrInfo);
                }
            }
            else
                rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_EXPECTED_INDIRECT_DATA_CONTENT_OID,
                                   "Unknown pSignedData.ContentInfo.ContentType.szObjId value: %s (expected %s)",
                                   pInfo->pSignedData->ContentInfo.ContentType.szObjId, RTCRSPCINDIRECTDATACONTENT_OID);
        }
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_EXPECTED_INDIRECT_DATA_CONTENT_OID, /** @todo error code*/
                               "PKCS#7 is not 'signedData': %s", pInfo->pContentInfo->ContentType.szObjId);
    }
    return rc;
}



static int rtldrPE_VerifyAllPageHashes(PRTLDRMODPE pModPe, PCRTCRSPCSERIALIZEDOBJECTATTRIBUTE pAttrib, RTDIGESTTYPE enmDigest,
                                       void *pvScratch, size_t cbScratch, uint32_t iSignature, PRTERRINFO pErrInfo)
{
    AssertReturn(cbScratch >= _4K, VERR_INTERNAL_ERROR_3);

    /*
     * Calculate the special places.
     */
    RTLDRPEHASHSPECIALS SpecialPlaces = { 0, 0, 0, 0, 0, 0 }; /* shut up gcc */
    int rc = rtldrPe_CalcSpecialHashPlaces(pModPe, &SpecialPlaces, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    uint32_t const cbHash = rtLdrPE_HashGetHashSize(enmDigest);
    uint32_t const cPages = pAttrib->u.pPageHashes->RawData.Asn1Core.cb / (cbHash + 4);
    if (cPages * (cbHash + 4) != pAttrib->u.pPageHashes->RawData.Asn1Core.cb)
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_PAGE_HASH_TAB_SIZE_OVERFLOW,
                             "Signature #%u - Page hashes size issue in: cb=%#x cbHash=%#x",
                             iSignature, pAttrib->u.pPageHashes->RawData.Asn1Core.cb, cbHash);

    /*
     * Walk the table.
     */
    uint32_t const  cbScratchReadMax = cbScratch & ~(uint32_t)(_4K - 1);
    uint32_t        cbScratchRead    = 0;
    uint32_t        offScratchRead   = 0;

    uint32_t        offPrev    = 0;
#ifdef COMPLICATED_AND_WRONG
    uint32_t        offSectEnd = pModPe->cbHeaders;
    uint32_t        iSh        = UINT32_MAX;
#endif
    uint8_t const  *pbHashTab  = pAttrib->u.pPageHashes->RawData.Asn1Core.uData.pu8;
    for (uint32_t iPage = 0; iPage < cPages - 1; iPage++)
    {
        /* Decode the page offset. */
        uint32_t const offPageInFile = RT_MAKE_U32_FROM_U8(pbHashTab[0], pbHashTab[1], pbHashTab[2], pbHashTab[3]);
        if (RT_UNLIKELY(offPageInFile >= SpecialPlaces.cbToHash))
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_PAGE_HASH_TAB_TOO_LONG,
                                 "Signature #%u - Page hash entry #%u is beyond the signature table start: %#x, %#x",
                                 iSignature, iPage, offPageInFile, SpecialPlaces.cbToHash);
        if (RT_UNLIKELY(offPageInFile < offPrev))
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_PAGE_HASH_TAB_NOT_STRICTLY_SORTED,
                                 "Signature #%u - Page hash table is not strictly sorted: entry #%u @%#x, previous @%#x\n",
                                 iSignature, iPage, offPageInFile, offPrev);

#ifdef COMPLICATED_AND_WRONG
        /* Figure out how much to read and how much to zero.  Need keep track
           of the on-disk section boundraries. */
        if (offPageInFile >= offSectEnd)
        {
            iSh++;
            if (   iSh < pModPe->cSections
                && offPageInFile - pModPe->paSections[iSh].PointerToRawData < pModPe->paSections[iSh].SizeOfRawData)
                offSectEnd = pModPe->paSections[iSh].PointerToRawData + pModPe->paSections[iSh].SizeOfRawData;
            else
            {
                iSh = 0;
                while (   iSh < pModPe->cSections
                       && offPageInFile - pModPe->paSections[iSh].PointerToRawData >= pModPe->paSections[iSh].SizeOfRawData)
                    iSh++;
                if (iSh < pModPe->cSections)
                    offSectEnd = pModPe->paSections[iSh].PointerToRawData + pModPe->paSections[iSh].SizeOfRawData;
                else
                    return RTErrInfoSetF(pErrInfo, VERR_PAGE_HASH_TAB_HASHES_NON_SECTION_DATA,
                                         "Signature #%u - Page hash entry #%u isn't in any section: %#x",
                                         iSignature, iPage, offPageInFile);
            }
        }

#else
        /* Figure out how much to read and how much take as zero.  Use the next
           page offset and the signature as upper boundraries.  */
#endif
        uint32_t cbPageInFile = _4K;
#ifdef COMPLICATED_AND_WRONG
        if (offPageInFile + cbPageInFile > offSectEnd)
            cbPageInFile = offSectEnd - offPageInFile;
#else
        if (iPage + 1 < cPages)
        {
            uint32_t offNextPage = RT_MAKE_U32_FROM_U8(pbHashTab[0 + 4 + cbHash], pbHashTab[1 + 4 + cbHash],
                                                       pbHashTab[2 + 4 + cbHash], pbHashTab[3 + 4 + cbHash]);
            if (offNextPage - offPageInFile < cbPageInFile)
                cbPageInFile = offNextPage - offPageInFile;
        }
#endif

        if (offPageInFile + cbPageInFile > SpecialPlaces.cbToHash)
            cbPageInFile = SpecialPlaces.cbToHash - offPageInFile;

        /* Did we get a cache hit? */
        uint8_t *pbCur = (uint8_t *)pvScratch;
        if (   offPageInFile + cbPageInFile <= offScratchRead + cbScratchRead
            && offPageInFile          >= offScratchRead)
            pbCur += offPageInFile - offScratchRead;
        /* Missed, read more. */
        else
        {
            offScratchRead = offPageInFile;
#ifdef COMPLICATED_AND_WRONG
            cbScratchRead  = offSectEnd - offPageInFile;
#else
            cbScratchRead  = SpecialPlaces.cbToHash - offPageInFile;
#endif
            if (cbScratchRead > cbScratchReadMax)
                cbScratchRead = cbScratchReadMax;
            rc = pModPe->Core.pReader->pfnRead(pModPe->Core.pReader, pbCur, cbScratchRead, offScratchRead);
            if (RT_FAILURE(rc))
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_READ_ERROR_HASH,
                                     "Signature #%u - Page hash read error at %#x: %Rrc (cbScratchRead=%#zx)",
                                     iSignature, offScratchRead, rc, cbScratchRead);
        }

        /*
         * Hash it.
         */
        RTLDRPEHASHCTXUNION HashCtx;
        rc = rtLdrPE_HashInit(&HashCtx, enmDigest);
        AssertRCReturn(rc, rc);

        /* Deal with special places. */
        uint32_t       cbLeft = cbPageInFile;
        if (offPageInFile < SpecialPlaces.offEndSpecial)
        {
            uint32_t off = offPageInFile;
            if (off < SpecialPlaces.offCksum)
            {
                /* Hash everything up to the checksum. */
                uint32_t cbChunk = RT_MIN(SpecialPlaces.offCksum - off, cbLeft);
                rtLdrPE_HashUpdate(&HashCtx, enmDigest, pbCur, cbChunk);
                pbCur  += cbChunk;
                cbLeft -= cbChunk;
                off    += cbChunk;
            }

            if (off < SpecialPlaces.offCksum + SpecialPlaces.cbCksum && off >= SpecialPlaces.offCksum)
            {
                /* Skip the checksum */
                uint32_t cbChunk = RT_MIN(SpecialPlaces.offCksum + SpecialPlaces.cbCksum - off, cbLeft);
                pbCur  += cbChunk;
                cbLeft -= cbChunk;
                off    += cbChunk;
            }

            if (off < SpecialPlaces.offSecDir && off >= SpecialPlaces.offCksum + SpecialPlaces.cbCksum)
            {
                /* Hash everything between the checksum and the data dir entry. */
                uint32_t cbChunk = RT_MIN(SpecialPlaces.offSecDir - off, cbLeft);
                rtLdrPE_HashUpdate(&HashCtx, enmDigest, pbCur, cbChunk);
                pbCur  += cbChunk;
                cbLeft -= cbChunk;
                off    += cbChunk;
            }

            if (off < SpecialPlaces.offSecDir + SpecialPlaces.cbSecDir && off >= SpecialPlaces.offSecDir)
            {
                /* Skip the security data directory entry. */
                uint32_t cbChunk = RT_MIN(SpecialPlaces.offSecDir + SpecialPlaces.cbSecDir - off, cbLeft);
                pbCur  += cbChunk;
                cbLeft -= cbChunk;
                off    += cbChunk;
            }
        }

        rtLdrPE_HashUpdate(&HashCtx, enmDigest, pbCur, cbLeft);
        if (cbPageInFile < _4K)
            rtLdrPE_HashUpdate(&HashCtx, enmDigest, g_abRTZero4K, _4K - cbPageInFile);

        /*
         * Finish the hash calculation and compare the result.
         */
        RTLDRPEHASHRESUNION HashRes;
        rtLdrPE_HashFinalize(&HashCtx, enmDigest, &HashRes);

        pbHashTab += 4;
        if (memcmp(pbHashTab, &HashRes, cbHash) != 0)
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_PAGE_HASH_MISMATCH,
                                 "Signature #%u - Page hash failed for page #%u, @%#x, %#x bytes: %.*Rhxs != %.*Rhxs",
                                 iSignature, iPage, offPageInFile, cbPageInFile, (size_t)cbHash, pbHashTab,
                                 (size_t)cbHash, &HashRes);
        pbHashTab += cbHash;
        offPrev = offPageInFile;
    }

    /*
     * Check that the last table entry has a hash value of zero.
     */
    if (!ASMMemIsZero(pbHashTab + 4, cbHash))
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_PAGE_HASH_TAB_TOO_LONG,
                             "Signature #%u - Malformed final page hash table entry: #%u %#010x %.*Rhxs",
                             iSignature, cPages - 1, RT_MAKE_U32_FROM_U8(pbHashTab[0], pbHashTab[1], pbHashTab[2], pbHashTab[3]),
                             (size_t)cbHash, pbHashTab + 4);
    return VINF_SUCCESS;
}


static int rtldrPE_VerifySignatureValidateOnePageHashes(PRTLDRMODPE pModPe, PRTLDRPESIGNATUREONE pInfo,
                                                        void *pvScratch, uint32_t cbScratch, PRTERRINFO pErrInfo)
{
    /*
     * Compare the page hashes if present.
     *
     * Seems the difference between V1 and V2 page hash attributes is
     * that v1 uses SHA-1 while v2 uses SHA-256. The data structures
     * seems to be identical otherwise.  Initially we assumed the digest
     * algorithm was supposed to be RTCRSPCINDIRECTDATACONTENT::DigestInfo,
     * i.e. the same as for the whole image hash.  The initial approach
     * worked just fine, but this makes more sense.
     *
     * (See also comments in osslsigncode.c (google it).)
     */
    PCRTCRSPCSERIALIZEDOBJECTATTRIBUTE pAttrib;
    /* V2 - SHA-256: */
    pAttrib = RTCrSpcIndirectDataContent_GetPeImageObjAttrib(pInfo->pIndData,
                                                             RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V2);
    if (pAttrib)
        return rtldrPE_VerifyAllPageHashes(pModPe, pAttrib, RTDIGESTTYPE_SHA256, pvScratch, cbScratch,
                                           pInfo->iSignature + 1, pErrInfo);

    /* V1 - SHA-1: */
    pAttrib = RTCrSpcIndirectDataContent_GetPeImageObjAttrib(pInfo->pIndData,
                                                             RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V1);
    if (pAttrib)
        return rtldrPE_VerifyAllPageHashes(pModPe, pAttrib, RTDIGESTTYPE_SHA1, pvScratch, cbScratch,
                                           pInfo->iSignature + 1, pErrInfo);

    /* No page hashes: */
    return VINF_SUCCESS;
}


static int rtldrPE_VerifySignatureValidateOneImageHash(PRTLDRMODPE pModPe, PRTLDRPESIGNATURE pSignature,
                                                       PRTLDRPESIGNATUREONE pInfo, void *pvScratch, uint32_t cbScratch,
                                                       PRTERRINFO pErrInfo)
{
    /*
     * Assert sanity.
     */
    AssertReturn(pInfo->enmDigest > RTDIGESTTYPE_INVALID && pInfo->enmDigest < RTDIGESTTYPE_END, VERR_INTERNAL_ERROR_4);
    AssertPtrReturn(pInfo->pIndData, VERR_INTERNAL_ERROR_5);
    AssertReturn(RTASN1CORE_IS_PRESENT(&pInfo->pIndData->DigestInfo.Digest.Asn1Core), VERR_INTERNAL_ERROR_5);
    AssertPtrReturn(pInfo->pIndData->DigestInfo.Digest.Asn1Core.uData.pv, VERR_INTERNAL_ERROR_5);

    /* Check that the hash is supported by the code here before continuing. */
    AssertReturn(rtLdrPE_HashIsSupported(pInfo->enmDigest),
                 RTErrInfoSetF(pErrInfo, VERR_CR_DIGEST_NOT_SUPPORTED, "Unsupported digest type: %d", pInfo->enmDigest));

    /*
     * Skip it if we've already verified it.
     */
    if (pInfo->fValidatedImageHash)
        return VINF_SUCCESS;

    /*
     * Calculate it.
     */
    uint32_t const cbHash = rtLdrPE_HashGetHashSize(pInfo->enmDigest);
    AssertReturn(pInfo->pIndData->DigestInfo.Digest.Asn1Core.cb == cbHash, VERR_INTERNAL_ERROR_5);

    int rc = rtldrPE_HashImageCommon(pModPe, pvScratch, cbScratch, pInfo->enmDigest,
                                     &pSignature->HashCtx, &pInfo->HashRes, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        pInfo->fValidatedImageHash = true;
        if (memcmp(&pInfo->HashRes, pInfo->pIndData->DigestInfo.Digest.Asn1Core.uData.pv, cbHash) == 0)
        {
            /*
             * Verify other signatures with the same digest type.
             */
            RTLDRPEHASHRESUNION const * const pHashRes      = &pInfo->HashRes;
            RTDIGESTTYPE const                enmDigestType = pInfo->enmDigest;
            for (uint32_t i = 0; i < pSignature->cNested; i++)
            {
                pInfo = &pSignature->paNested[i]; /* Note! pInfo changes! */
                if (   !pInfo->fValidatedImageHash
                    && pInfo->enmDigest == enmDigestType
                    /* paranoia from the top of this function: */
                    && pInfo->pIndData
                    && RTASN1CORE_IS_PRESENT(&pInfo->pIndData->DigestInfo.Digest.Asn1Core)
                    && pInfo->pIndData->DigestInfo.Digest.Asn1Core.uData.pv
                    && pInfo->pIndData->DigestInfo.Digest.Asn1Core.cb == cbHash)
                {
                    pInfo->fValidatedImageHash = true;
                    if (memcmp(pHashRes, pInfo->pIndData->DigestInfo.Digest.Asn1Core.uData.pv, cbHash) != 0)
                    {
                        rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_IMAGE_HASH_MISMATCH,
                                           "Full image signature #%u mismatch: %.*Rhxs, expected %.*Rhxs", pInfo->iSignature + 1,
                                           cbHash, pHashRes,
                                           cbHash, pInfo->pIndData->DigestInfo.Digest.Asn1Core.uData.pv);
                        break;
                    }
                }
            }
        }
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_IMAGE_HASH_MISMATCH,
                               "Full image signature #%u mismatch: %.*Rhxs, expected %.*Rhxs", pInfo->iSignature + 1,
                               cbHash, &pInfo->HashRes,
                               cbHash, pInfo->pIndData->DigestInfo.Digest.Asn1Core.uData.pv);
    }
    return rc;
}


/**
 * Validates the image hash, including page hashes if present.
 *
 * @returns IPRT status code.
 * @param   pModPe              The PE module.
 * @param   pSignature          The decoded signature data.
 * @param   pErrInfo            Optional error info buffer.
 */
static int rtldrPE_VerifySignatureValidateHash(PRTLDRMODPE pModPe, PRTLDRPESIGNATURE pSignature, PRTERRINFO pErrInfo)
{
    /*
     * Allocate a temporary memory buffer.
     * Note! The _4K that gets subtracted is to avoid that the 16-byte heap
     *       block header in ring-0 (iprt) caused any unnecessary internal
     *       heap fragmentation.
     */
# ifdef IN_RING0
    uint32_t    cbScratch = _256K - _4K;
# else
    uint32_t    cbScratch = _1M;
# endif
    void       *pvScratch = RTMemTmpAlloc(cbScratch);
    if (!pvScratch)
    {
        cbScratch = _4K;
        pvScratch = RTMemTmpAlloc(cbScratch);
        if (!pvScratch)
            return RTErrInfoSet(pErrInfo, VERR_NO_TMP_MEMORY, "Failed to allocate 4KB of scratch space for hashing image.");
    }

    /*
     * Verify signatures.
     */
    /* Image hashes: */
    int rc = rtldrPE_VerifySignatureValidateOneImageHash(pModPe, pSignature, &pSignature->Primary,
                                                         pvScratch, cbScratch, pErrInfo);
    for (unsigned i = 0; i < pSignature->cNested && RT_SUCCESS(rc); i++)
        rc = rtldrPE_VerifySignatureValidateOneImageHash(pModPe, pSignature, &pSignature->paNested[i],
                                                         pvScratch, cbScratch, pErrInfo);

    /* Page hashes: */
    if (RT_SUCCESS(rc))
    {
        rc = rtldrPE_VerifySignatureValidateOnePageHashes(pModPe, &pSignature->Primary, pvScratch, cbScratch, pErrInfo);
        for (unsigned i = 0; i < pSignature->cNested && RT_SUCCESS(rc); i++)
            rc = rtldrPE_VerifySignatureValidateOnePageHashes(pModPe, &pSignature->paNested[i], pvScratch, cbScratch, pErrInfo);
    }

    /*
     * Ditch the scratch buffer.
     */
    RTMemTmpFree(pvScratch);
    return rc;
}

#endif /* !IPRT_WITHOUT_LDR_VERIFY */


/** @interface_method_impl{RTLDROPS,pfnVerifySignature} */
static DECLCALLBACK(int) rtldrPE_VerifySignature(PRTLDRMODINTERNAL pMod, PFNRTLDRVALIDATESIGNEDDATA pfnCallback, void *pvUser,
                                                 PRTERRINFO pErrInfo)
{
#ifndef IPRT_WITHOUT_LDR_VERIFY
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;

    int rc = rtldrPE_VerifySignatureImagePrecoditions(pModPe, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        PRTLDRPESIGNATURE pSignature = NULL;
        rc = rtldrPE_VerifySignatureRead(pModPe, &pSignature, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            rc = rtldrPE_VerifySignatureDecode(pModPe, pSignature, pErrInfo);
            if (RT_SUCCESS(rc))
                rc = rtldrPE_VerifySignatureValidateHash(pModPe, pSignature, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Work the callback.
                 */
                /* The primary signature: */
                RTLDRSIGNATUREINFO Info;
                Info.iSignature     = 0;
                Info.cSignatures    = (uint16_t)(1 + pSignature->cNested);
                Info.enmType        = RTLDRSIGNATURETYPE_PKCS7_SIGNED_DATA;
                Info.pvSignature    = pSignature->Primary.pContentInfo;
                Info.cbSignature    = sizeof(*pSignature->Primary.pContentInfo);
                Info.pvExternalData = NULL;
                Info.cbExternalData = 0;
                rc = pfnCallback(&pModPe->Core, &Info, pErrInfo, pvUser);

                /* The nested signatures: */
                for (uint32_t iNested = 0; iNested < pSignature->cNested && rc == VINF_SUCCESS; iNested++)
                {
                    Info.iSignature     = (uint16_t)(1 + iNested);
                    Info.cSignatures    = (uint16_t)(1 + pSignature->cNested);
                    Info.enmType        = RTLDRSIGNATURETYPE_PKCS7_SIGNED_DATA;
                    Info.pvSignature    = pSignature->paNested[iNested].pContentInfo;
                    Info.cbSignature    = sizeof(*pSignature->paNested[iNested].pContentInfo);
                    Info.pvExternalData = NULL;
                    Info.cbExternalData = 0;
                    rc = pfnCallback(&pModPe->Core, &Info, pErrInfo, pvUser);
                }
            }
            rtldrPE_VerifySignatureDestroy(pModPe, pSignature);
        }
    }
    return rc;
#else
    RT_NOREF_PV(pMod); RT_NOREF_PV(pfnCallback); RT_NOREF_PV(pvUser); RT_NOREF_PV(pErrInfo);
    return VERR_NOT_SUPPORTED;
#endif
}



/**
 * @interface_method_impl{RTLDROPS,pfnHashImage}
 */
static DECLCALLBACK(int) rtldrPE_HashImage(PRTLDRMODINTERNAL pMod, RTDIGESTTYPE enmDigest, uint8_t *pabHash, size_t cbHash)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;

    /*
     * Allocate a temporary memory buffer.
     */
    uint32_t    cbScratch = _16K;
    void       *pvScratch = RTMemTmpAlloc(cbScratch);
    if (!pvScratch)
    {
        cbScratch = _4K;
        pvScratch = RTMemTmpAlloc(cbScratch);
        if (!pvScratch)
            return VERR_NO_TMP_MEMORY;
    }

    /*
     * Do the hashing.
     */
    RTLDRPEHASHCTXUNION HashCtx;
    RTLDRPEHASHRESUNION HashRes;
    int rc = rtldrPE_HashImageCommon(pModPe, pvScratch, cbScratch, enmDigest, &HashCtx, &HashRes, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Copy out the result.
         */
        RT_NOREF(cbHash); /* verified by caller */
        switch (enmDigest)
        {
            case RTDIGESTTYPE_SHA512:  memcpy(pabHash, HashRes.abSha512, sizeof(HashRes.abSha512)); break;
            case RTDIGESTTYPE_SHA256:  memcpy(pabHash, HashRes.abSha256, sizeof(HashRes.abSha256)); break;
            case RTDIGESTTYPE_SHA1:    memcpy(pabHash, HashRes.abSha1,   sizeof(HashRes.abSha1)); break;
            case RTDIGESTTYPE_MD5:     memcpy(pabHash, HashRes.abMd5,    sizeof(HashRes.abMd5)); break;
            default:                   AssertFailedReturn(VERR_INTERNAL_ERROR_3);
        }
    }
    return rc;
}


/**
 * Binary searches the lookup table.
 *
 * @returns RVA of unwind info on success, UINT32_MAX on failure.
 * @param   paFunctions     The table to lookup @a uRva in.
 * @param   iEnd            Size of the table.
 * @param   uRva            The RVA of the function we want.
 */
DECLINLINE(PCIMAGE_RUNTIME_FUNCTION_ENTRY)
rtldrPE_LookupRuntimeFunctionEntry(PCIMAGE_RUNTIME_FUNCTION_ENTRY paFunctions, size_t iEnd, uint32_t uRva)
{
    size_t iBegin = 0;
    while (iBegin < iEnd)
    {
        size_t const i = iBegin  + (iEnd - iBegin) / 2;
        PCIMAGE_RUNTIME_FUNCTION_ENTRY pEntry = &paFunctions[i];
        if (uRva < pEntry->BeginAddress)
            iEnd = i;
        else if (uRva > pEntry->EndAddress)
            iBegin = i + 1;
        else
            return pEntry;
    }
    return NULL;
}


/**
 * Processes an IRET frame.
 *
 * @returns IPRT status code.
 * @param   pState          The unwind state being worked.
 * @param   fErrCd          Non-zero if there is an error code on the stack.
 */
static int rtldrPE_UnwindFrame_Amd64_IRet(PRTDBGUNWINDSTATE pState, uint8_t fErrCd)
{
    /* POP ErrCd (optional): */
    Assert(fErrCd <= 1);
    int rcRet;
    if (fErrCd)
    {
        pState->u.x86.uErrCd = 0;
        pState->u.x86.Loaded.s.fErrCd = 1;
        rcRet = RTDbgUnwindLoadStackU64(pState, pState->u.x86.auRegs[X86_GREG_xSP], &pState->u.x86.uErrCd);
        pState->u.x86.auRegs[X86_GREG_xSP] += 8;
    }
    else
    {
        pState->u.x86.Loaded.s.fErrCd = 0;
        rcRet = VINF_SUCCESS;
    }

    /* Set return type and frame pointer. */
    pState->enmRetType          = RTDBGRETURNTYPE_IRET64;
    pState->u.x86.FrameAddr.off = pState->u.x86.auRegs[X86_GREG_xSP] - /* pretend rbp is pushed on the stack */ 8;
    pState->u.x86.FrameAddr.sel = pState->u.x86.auSegs[X86_SREG_SS];

    /* POP RIP: */
    int rc = RTDbgUnwindLoadStackU64(pState, pState->u.x86.auRegs[X86_GREG_xSP], &pState->uPc);
    if (RT_FAILURE(rc))
        rcRet = rc;
    pState->u.x86.auRegs[X86_GREG_xSP] += 8;

    /* POP CS: */
    rc = RTDbgUnwindLoadStackU16(pState, pState->u.x86.auRegs[X86_GREG_xSP], &pState->u.x86.auSegs[X86_SREG_CS]);
    if (RT_FAILURE(rc))
        rcRet = rc;
    pState->u.x86.auRegs[X86_GREG_xSP] += 8;

    /* POP RFLAGS: */
    rc = RTDbgUnwindLoadStackU64(pState, pState->u.x86.auRegs[X86_GREG_xSP], &pState->u.x86.uRFlags);
    if (RT_FAILURE(rc))
        rcRet = rc;
    pState->u.x86.auRegs[X86_GREG_xSP] += 8;

    /* POP RSP, part 1: */
    uint64_t uNewRsp = (pState->u.x86.auRegs[X86_GREG_xSP] - 8) & ~(uint64_t)15;
    rc = RTDbgUnwindLoadStackU64(pState, pState->u.x86.auRegs[X86_GREG_xSP], &uNewRsp);
    if (RT_FAILURE(rc))
        rcRet = rc;
    pState->u.x86.auRegs[X86_GREG_xSP] += 8;

    /* POP SS: */
    rc = RTDbgUnwindLoadStackU16(pState, pState->u.x86.auRegs[X86_GREG_xSP], &pState->u.x86.auSegs[X86_SREG_SS]);
    if (RT_FAILURE(rc))
        rcRet = rc;
    pState->u.x86.auRegs[X86_GREG_xSP] += 8;

    /* POP RSP, part 2: */
    pState->u.x86.auRegs[X86_GREG_xSP] = uNewRsp;

    /* Set loaded indicators: */
    pState->u.x86.Loaded.s.fRegs        |= RT_BIT(X86_GREG_xSP);
    pState->u.x86.Loaded.s.fSegs        |= RT_BIT(X86_SREG_CS) | RT_BIT(X86_SREG_SS);
    pState->u.x86.Loaded.s.fPc           = 1;
    pState->u.x86.Loaded.s.fFrameAddr    = 1;
    pState->u.x86.Loaded.s.fRFlags       = 1;
    return VINF_SUCCESS;
}


static int rtldrPE_UnwindFrame_Amd64(PRTLDRMODPE pThis, void const *pvBits, PRTDBGUNWINDSTATE pState, uint32_t uRvaPc,
                                     PCIMAGE_RUNTIME_FUNCTION_ENTRY pEntry)
{
    /* Did we find any unwind information? */
    if (!pEntry)
        return VERR_DBG_UNWIND_INFO_NOT_FOUND;

    /*
     * Do the unwinding.
     */
    IMAGE_RUNTIME_FUNCTION_ENTRY ChainedEntry;
    unsigned iFrameReg   = ~0U;
    unsigned offFrameReg = 0;

    int      fInEpilog = -1; /* -1: not-determined-assume-false;  0: false;  1: true. */
    uint8_t  cbEpilog  = 0;
    uint8_t  offEpilog = UINT8_MAX;
    int      rcRet = VINF_SUCCESS;
    int      rc;
    for (unsigned cChainLoops = 0; ; cChainLoops++)
    {
        /*
         * Get the info.
         */
        union
        {
            uint32_t uRva;
            uint8_t  ab[  RT_OFFSETOF(IMAGE_UNWIND_INFO, aOpcodes)
                        + sizeof(IMAGE_UNWIND_CODE) * 256
                        + sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY)];
        } uBuf;
        rc = rtldrPEReadPartByRvaInfoBuf(pThis, pvBits, pEntry->UnwindInfoAddress, sizeof(uBuf), &uBuf);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Check the info.
         */
        ASMCompilerBarrier(); /* we're aliasing */
        PCIMAGE_UNWIND_INFO pInfo = (PCIMAGE_UNWIND_INFO)&uBuf;

        if (pInfo->Version != 1 && pInfo->Version != 2)
            return VERR_DBG_MALFORMED_UNWIND_INFO;

        /*
         * Execute the opcodes.
         */
        unsigned const cOpcodes = pInfo->CountOfCodes;
        unsigned       iOpcode  = 0;

        /*
         * Check for epilog opcodes at the start and see if we're in an epilog.
         */
        if (   pInfo->Version >= 2
            && iOpcode < cOpcodes
            && pInfo->aOpcodes[iOpcode].u.UnwindOp == IMAGE_AMD64_UWOP_EPILOG)
        {
            if (fInEpilog == -1)
            {
                cbEpilog = pInfo->aOpcodes[iOpcode].u.CodeOffset;
                Assert(cbEpilog > 0);

                uint32_t uRvaEpilog = pEntry->EndAddress - cbEpilog;
                iOpcode++;
                if (   (pInfo->aOpcodes[iOpcode - 1].u.OpInfo & 1)
                    && uRvaPc >= uRvaEpilog)
                {
                    offEpilog = uRvaPc - uRvaEpilog;
                    fInEpilog = 1;
                }
                else
                {
                    fInEpilog = 0;
                    while (iOpcode < cOpcodes && pInfo->aOpcodes[iOpcode].u.UnwindOp == IMAGE_AMD64_UWOP_EPILOG)
                    {
                        uRvaEpilog = pEntry->EndAddress
                                   - (pInfo->aOpcodes[iOpcode].u.CodeOffset + (pInfo->aOpcodes[iOpcode].u.OpInfo << 8));
                        iOpcode++;
                        if (uRvaPc - uRvaEpilog < cbEpilog)
                        {
                            offEpilog = uRvaPc - uRvaEpilog;
                            fInEpilog = 1;
                            break;
                        }
                    }
                }
            }
            while (iOpcode < cOpcodes && pInfo->aOpcodes[iOpcode].u.UnwindOp == IMAGE_AMD64_UWOP_EPILOG)
                iOpcode++;
        }
        if (fInEpilog != 1)
        {
            /*
             * Skip opcodes that doesn't apply to us if we're in the prolog.
             */
            uint32_t offPc = uRvaPc - pEntry->BeginAddress;
            if (offPc < pInfo->SizeOfProlog)
                while (iOpcode < cOpcodes && pInfo->aOpcodes[iOpcode].u.CodeOffset > offPc)
                    iOpcode++;

            /*
             * Execute the opcodes.
             */
            if (pInfo->FrameRegister != 0)
            {
                iFrameReg   = pInfo->FrameRegister;
                offFrameReg = pInfo->FrameOffset * 16;
            }
            while (iOpcode < cOpcodes)
            {
                Assert(pInfo->aOpcodes[iOpcode].u.CodeOffset <= offPc);
                uint8_t const uOpInfo   = pInfo->aOpcodes[iOpcode].u.OpInfo;
                uint8_t const uUnwindOp = pInfo->aOpcodes[iOpcode].u.UnwindOp;
                switch (uUnwindOp)
                {
                    case IMAGE_AMD64_UWOP_PUSH_NONVOL:
                        rc = RTDbgUnwindLoadStackU64(pState, pState->u.x86.auRegs[X86_GREG_xSP], &pState->u.x86.auRegs[uOpInfo]);
                        if (RT_FAILURE(rc))
                            rcRet = rc;
                        pState->u.x86.Loaded.s.fRegs |= RT_BIT(uOpInfo);
                        pState->u.x86.auRegs[X86_GREG_xSP] += 8;
                        iOpcode++;
                        break;

                    case IMAGE_AMD64_UWOP_ALLOC_LARGE:
                        if (uOpInfo == 0)
                        {
                            iOpcode += 2;
                            AssertBreak(iOpcode <= cOpcodes);
                            pState->u.x86.auRegs[X86_GREG_xSP] += pInfo->aOpcodes[iOpcode - 1].FrameOffset * 8;
                        }
                        else
                        {
                            iOpcode += 3;
                            AssertBreak(iOpcode <= cOpcodes);
                            pState->u.x86.auRegs[X86_GREG_xSP] += RT_MAKE_U32(pInfo->aOpcodes[iOpcode - 2].FrameOffset,
                                                                              pInfo->aOpcodes[iOpcode - 1].FrameOffset);
                        }
                        break;

                    case IMAGE_AMD64_UWOP_ALLOC_SMALL:
                        AssertBreak(iOpcode <= cOpcodes);
                        pState->u.x86.auRegs[X86_GREG_xSP] += uOpInfo * 8 + 8;
                        iOpcode++;
                        break;

                    case IMAGE_AMD64_UWOP_SET_FPREG:
                        iFrameReg = uOpInfo;
                        offFrameReg = pInfo->FrameOffset * 16;
                        pState->u.x86.auRegs[X86_GREG_xSP] = pState->u.x86.auRegs[iFrameReg] - offFrameReg;
                        iOpcode++;
                        break;

                    case IMAGE_AMD64_UWOP_SAVE_NONVOL:
                    case IMAGE_AMD64_UWOP_SAVE_NONVOL_FAR:
                    {
                        uint32_t off = 0;
                        iOpcode++;
                        if (iOpcode < cOpcodes)
                        {
                            off = pInfo->aOpcodes[iOpcode].FrameOffset;
                            iOpcode++;
                            if (uUnwindOp == IMAGE_AMD64_UWOP_SAVE_NONVOL_FAR && iOpcode < cOpcodes)
                            {
                                off |= (uint32_t)pInfo->aOpcodes[iOpcode].FrameOffset << 16;
                                iOpcode++;
                            }
                        }
                        off *= 8;
                        rc = RTDbgUnwindLoadStackU64(pState, pState->u.x86.auRegs[X86_GREG_xSP] + off,
                                                     &pState->u.x86.auRegs[uOpInfo]);
                        if (RT_FAILURE(rc))
                            rcRet = rc;
                        pState->u.x86.Loaded.s.fRegs |= RT_BIT(uOpInfo);
                        break;
                    }

                    case IMAGE_AMD64_UWOP_SAVE_XMM128:
                        iOpcode += 2;
                        break;

                    case IMAGE_AMD64_UWOP_SAVE_XMM128_FAR:
                        iOpcode += 3;
                        break;

                    case IMAGE_AMD64_UWOP_PUSH_MACHFRAME:
                        return rtldrPE_UnwindFrame_Amd64_IRet(pState, uOpInfo);

                    case IMAGE_AMD64_UWOP_EPILOG:
                        iOpcode += 1;
                        break;

                    case IMAGE_AMD64_UWOP_RESERVED_7:
                        AssertFailedReturn(VERR_DBG_MALFORMED_UNWIND_INFO);

                    default:
                        AssertMsgFailedReturn(("%u\n", uUnwindOp), VERR_DBG_MALFORMED_UNWIND_INFO);
                }
            }
        }
        else
        {
            /*
             * We're in the POP sequence of an epilog.  The POP sequence should
             * mirror the PUSH sequence exactly.
             *
             * Note! We should only end up here for the initial frame (just consider
             *       RSP, stack allocations, non-volatile register restores, ++).
             */
            while (iOpcode < cOpcodes)
            {
                uint8_t const uOpInfo   = pInfo->aOpcodes[iOpcode].u.OpInfo;
                uint8_t const uUnwindOp = pInfo->aOpcodes[iOpcode].u.UnwindOp;
                switch (uUnwindOp)
                {
                    case IMAGE_AMD64_UWOP_PUSH_NONVOL:
                        pState->u.x86.auRegs[X86_GREG_xSP] += 8;
                        if (offEpilog == 0)
                        {
                            rc = RTDbgUnwindLoadStackU64(pState, pState->u.x86.auRegs[X86_GREG_xSP],
                                                         &pState->u.x86.auRegs[uOpInfo]);
                            if (RT_FAILURE(rc))
                                rcRet = rc;
                            pState->u.x86.Loaded.s.fRegs |= RT_BIT(uOpInfo);
                        }
                        else
                        {
                            /* Decrement offEpilog by estimated POP instruction length. */
                            offEpilog -= 1;
                            if (offEpilog > 0 && uOpInfo >= 8)
                                offEpilog -= 1;
                        }
                        iOpcode++;
                        break;

                    case IMAGE_AMD64_UWOP_PUSH_MACHFRAME: /* Must terminate an epilog, so always execute this. */
                        return rtldrPE_UnwindFrame_Amd64_IRet(pState, uOpInfo);

                    case IMAGE_AMD64_UWOP_ALLOC_SMALL:
                    case IMAGE_AMD64_UWOP_SET_FPREG:
                    case IMAGE_AMD64_UWOP_EPILOG:
                        iOpcode++;
                        break;
                    case IMAGE_AMD64_UWOP_SAVE_NONVOL:
                    case IMAGE_AMD64_UWOP_SAVE_XMM128:
                        iOpcode += 2;
                        break;
                    case IMAGE_AMD64_UWOP_ALLOC_LARGE:
                    case IMAGE_AMD64_UWOP_SAVE_NONVOL_FAR:
                    case IMAGE_AMD64_UWOP_SAVE_XMM128_FAR:
                        iOpcode += 3;
                        break;

                    default:
                        AssertMsgFailedReturn(("%u\n", uUnwindOp), VERR_DBG_MALFORMED_UNWIND_INFO);
                }
            }
        }

        /*
         * Chained stuff?
         */
        if (!(pInfo->Flags & IMAGE_UNW_FLAGS_CHAININFO))
            break;
        ChainedEntry = *(PCIMAGE_RUNTIME_FUNCTION_ENTRY)&pInfo->aOpcodes[(cOpcodes + 1) & ~1];
        pEntry = &ChainedEntry;
        AssertReturn(cChainLoops < 32, VERR_DBG_MALFORMED_UNWIND_INFO);
    }

    /*
     * RSP should now give us the return address, so perform a RET.
     */
    pState->enmRetType = RTDBGRETURNTYPE_NEAR64;

    pState->u.x86.FrameAddr.off = pState->u.x86.auRegs[X86_GREG_xSP] - /* pretend rbp is pushed on the stack */ 8;
    pState->u.x86.FrameAddr.sel = pState->u.x86.auSegs[X86_SREG_SS];
    pState->u.x86.Loaded.s.fFrameAddr = 1;

    rc = RTDbgUnwindLoadStackU64(pState, pState->u.x86.auRegs[X86_GREG_xSP], &pState->uPc);
    if (RT_FAILURE(rc))
        rcRet = rc;
    pState->u.x86.auRegs[X86_GREG_xSP] += 8;
    pState->u.x86.Loaded.s.fPc = 1;
    return rcRet;
}


/**
 * @interface_method_impl{RTLDROPS,pfnUnwindFrame}
 */
static DECLCALLBACK(int) rtldrPE_UnwindFrame(PRTLDRMODINTERNAL pMod, void const *pvBits,
                                             uint32_t iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    PRTLDRMODPE pThis = (PRTLDRMODPE)pMod;

    /*
     * Translate the segment + offset into an RVA.
     */
    RTLDRADDR uRvaPc = off;
    if (iSeg != UINT32_MAX)
    {
        int rc = rtldrPE_SegOffsetToRva(pMod, iSeg, off, &uRvaPc);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Check for unwind info and match the architecture.
     */
    if (   pThis->ExceptionDir.Size == 0
        || pThis->ExceptionDir.VirtualAddress < pThis->cbHeaders)
        return VERR_DBG_NO_UNWIND_INFO;
    if (pThis->Core.enmArch != pState->enmArch)
        return VERR_DBG_UNWIND_INFO_NOT_FOUND;

    /* Currently only AMD64 unwinding is implemented, so head it off right away. */
    if (pThis->Core.enmArch != RTLDRARCH_AMD64)
        return VERR_DBG_UNWIND_INFO_NOT_FOUND;

    /*
     * Make the lookup table available to us.
     */
    void const    *pvTable = NULL;
    uint32_t const cbTable = pThis->ExceptionDir.Size;
    AssertReturn(   cbTable < pThis->cbImage
                 && pThis->ExceptionDir.VirtualAddress < pThis->cbImage
                 && pThis->ExceptionDir.VirtualAddress + cbTable <= pThis->cbImage, VERR_INTERNAL_ERROR_3);
    int rc = rtldrPEReadPartByRva(pThis, pvBits, pThis->ExceptionDir.VirtualAddress, pThis->ExceptionDir.Size, &pvTable);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * The rest is architecture dependent.
     *
     * Note! On windows we try catch access violations so we can safely use
     *       this code on mapped images during assertions.
     */
#if defined(_MSC_VER) && defined(IN_RING3) && !defined(IN_SUP_HARDENED_R3)
    __try
    {
#endif
        switch (pThis->Core.enmArch)
        {
            case RTLDRARCH_AMD64:
                rc = rtldrPE_UnwindFrame_Amd64(pThis, pvBits, pState, uRvaPc,
                                               rtldrPE_LookupRuntimeFunctionEntry((PCIMAGE_RUNTIME_FUNCTION_ENTRY)pvTable,
                                                                                  cbTable / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY),
                                                                                  (uint32_t)uRvaPc));
                break;

            default:
                rc = VERR_DBG_UNWIND_INFO_NOT_FOUND;
                break;
        }
#if defined(_MSC_VER) && defined(IN_RING3) && !defined(IN_SUP_HARDENED_R3)
    }
    __except (1 /*EXCEPTION_EXECUTE_HANDLER*/)
    {
        rc = VERR_DBG_UNWIND_INFO_NOT_FOUND;
    }
#endif
    rtldrPEFreePart(pThis, pvBits, pvTable);
    return rc;
}


/** @interface_method_impl{RTLDROPS,pfnDone}   */
static DECLCALLBACK(int) rtldrPEDone(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;
    if (pModPe->pvBits)
    {
        RTMemFree(pModPe->pvBits);
        pModPe->pvBits = NULL;
    }
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTLDROPS,pfnClose}  */
static DECLCALLBACK(int) rtldrPEClose(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODPE pModPe = (PRTLDRMODPE)pMod;
    if (pModPe->paSections)
    {
        RTMemFree(pModPe->paSections);
        pModPe->paSections = NULL;
    }
    if (pModPe->pvBits)
    {
        RTMemFree(pModPe->pvBits);
        pModPe->pvBits = NULL;
    }
    return VINF_SUCCESS;
}


/**
 * Operations for a 32-bit PE module.
 */
static const RTLDROPSPE s_rtldrPE32Ops =
{
    {
        "pe32",
        rtldrPEClose,
        NULL,
        rtldrPEDone,
        rtldrPEEnumSymbols,
        /* ext */
        rtldrPEGetImageSize,
        rtldrPEGetBits,
        rtldrPERelocate,
        rtldrPEGetSymbolEx,
        rtldrPE_QueryForwarderInfo,
        rtldrPE_EnumDbgInfo,
        rtldrPE_EnumSegments,
        rtldrPE_LinkAddressToSegOffset,
        rtldrPE_LinkAddressToRva,
        rtldrPE_SegOffsetToRva,
        rtldrPE_RvaToSegOffset,
        NULL,
        rtldrPE_QueryProp,
        rtldrPE_VerifySignature,
        rtldrPE_HashImage,
        NULL /*pfnUnwindFrame*/,
        42
    },
    rtldrPEResolveImports32,
    42
};


/**
 * Operations for a 64-bit PE module.
 */
static const RTLDROPSPE s_rtldrPE64Ops =
{
    {
        "pe64",
        rtldrPEClose,
        NULL,
        rtldrPEDone,
        rtldrPEEnumSymbols,
        /* ext */
        rtldrPEGetImageSize,
        rtldrPEGetBits,
        rtldrPERelocate,
        rtldrPEGetSymbolEx,
        rtldrPE_QueryForwarderInfo,
        rtldrPE_EnumDbgInfo,
        rtldrPE_EnumSegments,
        rtldrPE_LinkAddressToSegOffset,
        rtldrPE_LinkAddressToRva,
        rtldrPE_SegOffsetToRva,
        rtldrPE_RvaToSegOffset,
        NULL,
        rtldrPE_QueryProp,
        rtldrPE_VerifySignature,
        rtldrPE_HashImage,
        rtldrPE_UnwindFrame,
        42
    },
    rtldrPEResolveImports64,
    42
};


/**
 * Converts the optional header from 32 bit to 64 bit.
 * This is a rather simple task, if you start from the right end.
 *
 * @param   pOptHdr     On input this is a PIMAGE_OPTIONAL_HEADER32.
 *                      On output this will be a PIMAGE_OPTIONAL_HEADER64.
 */
static void rtldrPEConvert32BitOptionalHeaderTo64Bit(PIMAGE_OPTIONAL_HEADER64 pOptHdr)
{
    /*
     * volatile everywhere! Trying to prevent the compiler being a smarta$$ and reorder stuff.
     */
    IMAGE_OPTIONAL_HEADER32 volatile *pOptHdr32 = (IMAGE_OPTIONAL_HEADER32 volatile *)pOptHdr;
    IMAGE_OPTIONAL_HEADER64 volatile *pOptHdr64 = pOptHdr;

    /* from LoaderFlags and out the difference is 4 * 32-bits. */
    Assert(RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER32, LoaderFlags) + 16 == RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER64, LoaderFlags));
    Assert(     RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER32, DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES]) + 16
           ==   RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER64, DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES]));
    uint32_t volatile       *pu32Dst     = (uint32_t *)&pOptHdr64->DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES] - 1;
    const uint32_t volatile *pu32Src     = (uint32_t *)&pOptHdr32->DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES] - 1;
    const uint32_t volatile *pu32SrcLast = (uint32_t *)&pOptHdr32->LoaderFlags;
    while (pu32Src >= pu32SrcLast)
        *pu32Dst-- = *pu32Src--;

    /* the previous 4 fields are 32/64 and needs special attention. */
    pOptHdr64->SizeOfHeapCommit   = pOptHdr32->SizeOfHeapCommit;
    pOptHdr64->SizeOfHeapReserve  = pOptHdr32->SizeOfHeapReserve;
    pOptHdr64->SizeOfStackCommit  = pOptHdr32->SizeOfStackCommit;
    uint32_t u32SizeOfStackReserve = pOptHdr32->SizeOfStackReserve;
    pOptHdr64->SizeOfStackReserve = u32SizeOfStackReserve;

    /* The rest matches except for BaseOfData which has been merged into ImageBase in the 64-bit version..
     * Thus, ImageBase needs some special treatment. It will probably work fine assigning one to the
     * other since this is all declared volatile, but taking now chances, we'll use a temp variable.
     */
    Assert(RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER32, SizeOfStackReserve) == RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER64, SizeOfStackReserve));
    Assert(RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER32, BaseOfData)         == RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER64, ImageBase));
    Assert(RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER32, SectionAlignment)   == RT_UOFFSETOF(IMAGE_OPTIONAL_HEADER64, SectionAlignment));
    uint32_t u32ImageBase = pOptHdr32->ImageBase;
    pOptHdr64->ImageBase = u32ImageBase;
}


/**
 * Converts the load config directory from 32 bit to 64 bit.
 * This is a rather simple task, if you start from the right end.
 *
 * @param   pLoadCfg    On input this is a PIMAGE_LOAD_CONFIG_DIRECTORY32.
 *                      On output this will be a PIMAGE_LOAD_CONFIG_DIRECTORY64.
 */
static void rtldrPEConvert32BitLoadConfigTo64Bit(PIMAGE_LOAD_CONFIG_DIRECTORY64 pLoadCfg)
{
    /*
     * volatile everywhere! Trying to prevent the compiler being a smarta$$ and reorder stuff.
     */
    IMAGE_LOAD_CONFIG_DIRECTORY32_V13 volatile *pLoadCfg32 = (IMAGE_LOAD_CONFIG_DIRECTORY32_V13 volatile *)pLoadCfg;
    IMAGE_LOAD_CONFIG_DIRECTORY64_V13 volatile *pLoadCfg64 = pLoadCfg;

    pLoadCfg64->CastGuardOsDeterminedFailureMode         = pLoadCfg32->CastGuardOsDeterminedFailureMode;
    pLoadCfg64->GuardXFGTableDispatchFunctionPointer     = pLoadCfg32->GuardXFGTableDispatchFunctionPointer;
    pLoadCfg64->GuardXFGDispatchFunctionPointer          = pLoadCfg32->GuardXFGDispatchFunctionPointer;
    pLoadCfg64->GuardXFGCheckFunctionPointer             = pLoadCfg32->GuardXFGCheckFunctionPointer;
    pLoadCfg64->GuardEHContinuationCount                 = pLoadCfg32->GuardEHContinuationCount;
    pLoadCfg64->GuardEHContinuationTable                 = pLoadCfg32->GuardEHContinuationTable;
    pLoadCfg64->VolatileMetadataPointer                  = pLoadCfg32->VolatileMetadataPointer;
    pLoadCfg64->EnclaveConfigurationPointer              = pLoadCfg32->EnclaveConfigurationPointer;
    pLoadCfg64->Reserved3                                = pLoadCfg32->Reserved3;
    pLoadCfg64->HotPatchTableOffset                      = pLoadCfg32->HotPatchTableOffset;
    pLoadCfg64->GuardRFVerifyStackPointerFunctionPointer = pLoadCfg32->GuardRFVerifyStackPointerFunctionPointer;
    pLoadCfg64->Reserved2                                = pLoadCfg32->Reserved2;
    pLoadCfg64->DynamicValueRelocTableSection            = pLoadCfg32->DynamicValueRelocTableSection;
    pLoadCfg64->DynamicValueRelocTableOffset             = pLoadCfg32->DynamicValueRelocTableOffset;
    pLoadCfg64->GuardRFFailureRoutineFunctionPointer     = pLoadCfg32->GuardRFFailureRoutineFunctionPointer;
    pLoadCfg64->GuardRFFailureRoutine                    = pLoadCfg32->GuardRFFailureRoutine;
    pLoadCfg64->CHPEMetadataPointer                      = pLoadCfg32->CHPEMetadataPointer;
    pLoadCfg64->DynamicValueRelocTable                   = pLoadCfg32->DynamicValueRelocTable;
    pLoadCfg64->GuardLongJumpTargetCount                 = pLoadCfg32->GuardLongJumpTargetCount;
    pLoadCfg64->GuardLongJumpTargetTable                 = pLoadCfg32->GuardLongJumpTargetTable;
    pLoadCfg64->GuardAddressTakenIatEntryCount           = pLoadCfg32->GuardAddressTakenIatEntryCount;
    pLoadCfg64->GuardAddressTakenIatEntryTable           = pLoadCfg32->GuardAddressTakenIatEntryTable;
    pLoadCfg64->CodeIntegrity.Reserved                   = pLoadCfg32->CodeIntegrity.Reserved;
    pLoadCfg64->CodeIntegrity.CatalogOffset              = pLoadCfg32->CodeIntegrity.CatalogOffset;
    pLoadCfg64->CodeIntegrity.Catalog                    = pLoadCfg32->CodeIntegrity.Catalog;
    pLoadCfg64->CodeIntegrity.Flags                      = pLoadCfg32->CodeIntegrity.Flags;
    pLoadCfg64->GuardFlags                               = pLoadCfg32->GuardFlags;
    pLoadCfg64->GuardCFFunctionCount                     = pLoadCfg32->GuardCFFunctionCount;
    pLoadCfg64->GuardCFFunctionTable                     = pLoadCfg32->GuardCFFunctionTable;
    pLoadCfg64->GuardCFDispatchFunctionPointer           = pLoadCfg32->GuardCFDispatchFunctionPointer;
    pLoadCfg64->GuardCFCCheckFunctionPointer             = pLoadCfg32->GuardCFCCheckFunctionPointer;
    pLoadCfg64->SEHandlerCount                           = pLoadCfg32->SEHandlerCount;
    pLoadCfg64->SEHandlerTable                           = pLoadCfg32->SEHandlerTable;
    pLoadCfg64->SecurityCookie                           = pLoadCfg32->SecurityCookie;
    pLoadCfg64->EditList                                 = pLoadCfg32->EditList;
    pLoadCfg64->DependentLoadFlags                       = pLoadCfg32->DependentLoadFlags;
    pLoadCfg64->CSDVersion                               = pLoadCfg32->CSDVersion;
    pLoadCfg64->ProcessHeapFlags                         = pLoadCfg32->ProcessHeapFlags; /* switched place with ProcessAffinityMask, but we're more than 16 byte off by now so it doesn't matter. */
    pLoadCfg64->ProcessAffinityMask                      = pLoadCfg32->ProcessAffinityMask;
    pLoadCfg64->VirtualMemoryThreshold                   = pLoadCfg32->VirtualMemoryThreshold;
    pLoadCfg64->MaximumAllocationSize                    = pLoadCfg32->MaximumAllocationSize;
    pLoadCfg64->LockPrefixTable                          = pLoadCfg32->LockPrefixTable;
    pLoadCfg64->DeCommitTotalFreeThreshold               = pLoadCfg32->DeCommitTotalFreeThreshold;
    uint32_t u32DeCommitFreeBlockThreshold               = pLoadCfg32->DeCommitFreeBlockThreshold;
    pLoadCfg64->DeCommitFreeBlockThreshold               = u32DeCommitFreeBlockThreshold;
    /* the rest is equal. */
    Assert(     RT_UOFFSETOF(IMAGE_LOAD_CONFIG_DIRECTORY32, DeCommitFreeBlockThreshold)
           ==   RT_UOFFSETOF(IMAGE_LOAD_CONFIG_DIRECTORY64, DeCommitFreeBlockThreshold));
}


/**
 * Translate the PE/COFF machine name to a string.
 *
 * @returns Name string (read-only).
 * @param   uMachine            The PE/COFF machine.
 */
static const char *rtldrPEGetArchName(uint16_t uMachine)
{
    switch (uMachine)
    {
        case IMAGE_FILE_MACHINE_I386:           return "X86_32";
        case IMAGE_FILE_MACHINE_AMD64:          return "AMD64";

        case IMAGE_FILE_MACHINE_UNKNOWN:        return "UNKNOWN";
        case IMAGE_FILE_MACHINE_AM33:           return "AM33";
        case IMAGE_FILE_MACHINE_ARM:            return "ARM";
        case IMAGE_FILE_MACHINE_THUMB:          return "THUMB";
        case IMAGE_FILE_MACHINE_ARMNT:          return "ARMNT";
        case IMAGE_FILE_MACHINE_ARM64:          return "ARM64";
        case IMAGE_FILE_MACHINE_EBC:            return "EBC";
        case IMAGE_FILE_MACHINE_IA64:           return "IA64";
        case IMAGE_FILE_MACHINE_M32R:           return "M32R";
        case IMAGE_FILE_MACHINE_MIPS16:         return "MIPS16";
        case IMAGE_FILE_MACHINE_MIPSFPU:        return "MIPSFPU";
        case IMAGE_FILE_MACHINE_MIPSFPU16:      return "MIPSFPU16";
        case IMAGE_FILE_MACHINE_WCEMIPSV2:      return "WCEMIPSV2";
        case IMAGE_FILE_MACHINE_POWERPC:        return "POWERPC";
        case IMAGE_FILE_MACHINE_POWERPCFP:      return "POWERPCFP";
        case IMAGE_FILE_MACHINE_R4000:          return "R4000";
        case IMAGE_FILE_MACHINE_SH3:            return "SH3";
        case IMAGE_FILE_MACHINE_SH3DSP:         return "SH3DSP";
        case IMAGE_FILE_MACHINE_SH4:            return "SH4";
        case IMAGE_FILE_MACHINE_SH5:            return "SH5";
        default:                                return "UnknownMachine";
    }
}


/**
 * Validates the file header.
 *
 * @returns iprt status code.
 * @param   pFileHdr    Pointer to the file header that needs validating.
 * @param   fFlags      Valid RTLDR_O_XXX combination.
 * @param   pszLogName  The log name to  prefix the errors with.
 * @param   penmArch    Where to store the CPU architecture.
 * @param   pErrInfo    Where to return additional error information.
 */
static int rtldrPEValidateFileHeader(PIMAGE_FILE_HEADER pFileHdr, uint32_t fFlags, const char *pszLogName,
                                     PRTLDRARCH penmArch, PRTERRINFO pErrInfo)
{
    RT_NOREF_PV(pszLogName);

    size_t cbOptionalHeader;
    switch (pFileHdr->Machine)
    {
        case IMAGE_FILE_MACHINE_I386:
            cbOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
            *penmArch = RTLDRARCH_X86_32;
            break;
        case IMAGE_FILE_MACHINE_AMD64:
            cbOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
            *penmArch = RTLDRARCH_AMD64;
            break;
        case IMAGE_FILE_MACHINE_ARM64:
            cbOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
            *penmArch = RTLDRARCH_ARM64;
            break;

        default:
            Log(("rtldrPEOpen: %s: Unsupported Machine=%#x\n", pszLogName, pFileHdr->Machine));
            *penmArch = RTLDRARCH_INVALID;
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "Unsupported Machine=%#x", pFileHdr->Machine);
    }
    if (pFileHdr->SizeOfOptionalHeader != cbOptionalHeader)
    {
        Log(("rtldrPEOpen: %s: SizeOfOptionalHeader=%#x expected %#x\n", pszLogName, pFileHdr->SizeOfOptionalHeader, cbOptionalHeader));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "SizeOfOptionalHeader=%#x expected %#x",
                                   pFileHdr->SizeOfOptionalHeader, cbOptionalHeader);
    }
    /* This restriction needs to be implemented elsewhere. */
    if (   (pFileHdr->Characteristics & IMAGE_FILE_RELOCS_STRIPPED)
        && !(fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION)))
    {
        Log(("rtldrPEOpen: %s: IMAGE_FILE_RELOCS_STRIPPED\n", pszLogName));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "IMAGE_FILE_RELOCS_STRIPPED");
    }
    if (pFileHdr->NumberOfSections > 42)
    {
        Log(("rtldrPEOpen: %s: NumberOfSections=%d - our limit is 42, please raise it if the binary makes sense.(!!!)\n",
             pszLogName, pFileHdr->NumberOfSections));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "NumberOfSections=%d, implementation max is 42", pFileHdr->NumberOfSections);
    }
    if (pFileHdr->NumberOfSections < 1)
    {
        Log(("rtldrPEOpen: %s: NumberOfSections=%d - we can't have an image without sections (!!!)\n",
             pszLogName, pFileHdr->NumberOfSections));
        return RTERRINFO_LOG_SET(pErrInfo, VERR_BAD_EXE_FORMAT, "Image has no sections");
    }
    return VINF_SUCCESS;
}


/**
 * Validates the optional header (64/32-bit)
 *
 * @returns iprt status code.
 * @param   pOptHdr     Pointer to the optional header which needs validation.
 * @param   pszLogName  The log name to  prefix the errors with.
 * @param   offNtHdrs   The offset of the NT headers from the start of the file.
 * @param   pFileHdr    Pointer to the file header (valid).
 * @param   cbRawImage  The raw image size.
 * @param   fFlags      Loader flags, RTLDR_O_XXX.
 * @param   pErrInfo    Where to return additional error information.
 */
static int rtldrPEValidateOptionalHeader(const IMAGE_OPTIONAL_HEADER64 *pOptHdr, const char *pszLogName, RTFOFF offNtHdrs,
                                         const IMAGE_FILE_HEADER *pFileHdr, uint64_t cbRawImage, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    RT_NOREF_PV(pszLogName);

    const uint16_t CorrectMagic = pFileHdr->SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER32)
                                ? IMAGE_NT_OPTIONAL_HDR32_MAGIC : IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    if (pOptHdr->Magic != CorrectMagic)
    {
        Log(("rtldrPEOpen: %s: Magic=%#x - expected %#x!!!\n", pszLogName, pOptHdr->Magic, CorrectMagic));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "Magic=%#x, expected %#x", pOptHdr->Magic, CorrectMagic);
    }
    const uint32_t cbImage = pOptHdr->SizeOfImage;
    if (cbImage > _1G)
    {
        Log(("rtldrPEOpen: %s: SizeOfImage=%#x - Our limit is 1GB (%#x)!!!\n", pszLogName, cbImage, _1G));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "SizeOfImage=%#x - Our limit is 1GB (%#x)", cbImage, _1G);
    }
    const uint32_t cbMinImageSize = pFileHdr->SizeOfOptionalHeader + sizeof(*pFileHdr) + 4 + (uint32_t)offNtHdrs;
    if (cbImage < cbMinImageSize)
    {
        Log(("rtldrPEOpen: %s: SizeOfImage=%#x to small, minimum %#x!!!\n", pszLogName, cbImage, cbMinImageSize));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "SizeOfImage=%#x to small, minimum %#x", cbImage, cbMinImageSize);
    }
    if (pOptHdr->AddressOfEntryPoint >= cbImage)
    {
        Log(("rtldrPEOpen: %s: AddressOfEntryPoint=%#x - beyond image size (%#x)!!!\n",
             pszLogName, pOptHdr->AddressOfEntryPoint, cbImage));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "AddressOfEntryPoint=%#x - beyond image size (%#x)", pOptHdr->AddressOfEntryPoint, cbImage);
    }
    if (pOptHdr->BaseOfCode >= cbImage)
    {
        Log(("rtldrPEOpen: %s: BaseOfCode=%#x - beyond image size (%#x)!!!\n",
             pszLogName, pOptHdr->BaseOfCode, cbImage));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "BaseOfCode=%#x - beyond image size (%#x)", pOptHdr->BaseOfCode, cbImage);
    }
#if 0/* only in 32-bit header */
    if (pOptHdr->BaseOfData >= cbImage)
    {
        Log(("rtldrPEOpen: %s: BaseOfData=%#x - beyond image size (%#x)!!!\n",
             pszLogName, pOptHdr->BaseOfData, cbImage));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "BaseOfData=%#x - beyond image size (%#x)", pOptHdr->BaseOfData, cbImage);
    }
#endif
    if (!RT_IS_POWER_OF_TWO(pOptHdr->SectionAlignment))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "SectionAlignment=%#x - not a power of two", pOptHdr->SectionAlignment);
    if (pOptHdr->SectionAlignment < 16 || pOptHdr->SectionAlignment > _128K)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "SectionAlignment=%#x - unsupported value, not between 16 and 128KB", pOptHdr->SectionAlignment);
    if (pOptHdr->SizeOfHeaders >= cbImage)
    {
        Log(("rtldrPEOpen: %s: SizeOfHeaders=%#x - beyond image size (%#x)!!!\n",
             pszLogName, pOptHdr->SizeOfHeaders, cbImage));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "SizeOfHeaders=%#x - beyond image size (%#x)", pOptHdr->SizeOfHeaders, cbImage);
    }
    /* don't know how to do the checksum, so ignore it. */
    if (pOptHdr->Subsystem == IMAGE_SUBSYSTEM_UNKNOWN)
    {
        Log(("rtldrPEOpen: %s: Subsystem=%#x (unknown)!!!\n", pszLogName, pOptHdr->Subsystem));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "Subsystem=%#x (unknown)", pOptHdr->Subsystem);
    }
    if (pOptHdr->SizeOfHeaders < cbMinImageSize + pFileHdr->NumberOfSections * sizeof(IMAGE_SECTION_HEADER))
    {
        Log(("rtldrPEOpen: %s: SizeOfHeaders=%#x - cbMinImageSize %#x + sections %#x = %#llx!!!\n",
             pszLogName, pOptHdr->SizeOfHeaders,
             cbMinImageSize, pFileHdr->NumberOfSections * sizeof(IMAGE_SECTION_HEADER),
             cbMinImageSize + pFileHdr->NumberOfSections * sizeof(IMAGE_SECTION_HEADER)));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "SizeOfHeaders=%#x - cbMinImageSize %#x + sections %#x = %#llx",
                                   pOptHdr->SizeOfHeaders, cbMinImageSize,
                                   pFileHdr->NumberOfSections * sizeof(IMAGE_SECTION_HEADER),
                                   cbMinImageSize + pFileHdr->NumberOfSections * sizeof(IMAGE_SECTION_HEADER) );
    }
    if (pOptHdr->SizeOfStackReserve < pOptHdr->SizeOfStackCommit)
    {
        Log(("rtldrPEOpen: %s: SizeOfStackReserve %#x < SizeOfStackCommit %#x!!!\n",
             pszLogName, pOptHdr->SizeOfStackReserve, pOptHdr->SizeOfStackCommit));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "SizeOfStackReserve %#x < SizeOfStackCommit %#x",
                                   pOptHdr->SizeOfStackReserve, pOptHdr->SizeOfStackCommit);
    }
    if (pOptHdr->SizeOfHeapReserve < pOptHdr->SizeOfHeapCommit)
    {
        Log(("rtldrPEOpen: %s: SizeOfStackReserve %#x < SizeOfStackCommit %#x!!!\n",
             pszLogName, pOptHdr->SizeOfStackReserve, pOptHdr->SizeOfStackCommit));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "SizeOfStackReserve %#x < SizeOfStackCommit %#x\n",
                                   pOptHdr->SizeOfStackReserve, pOptHdr->SizeOfStackCommit);
    }

    /* DataDirectory */
    if (pOptHdr->NumberOfRvaAndSizes != RT_ELEMENTS(pOptHdr->DataDirectory))
    {
        Log(("rtldrPEOpen: %s: NumberOfRvaAndSizes=%d!!!\n", pszLogName, pOptHdr->NumberOfRvaAndSizes));
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "NumberOfRvaAndSizes=%d, expected %d",
                                   pOptHdr->NumberOfRvaAndSizes, RT_ELEMENTS(pOptHdr->DataDirectory));
    }
    for (unsigned i = 0; i < RT_ELEMENTS(pOptHdr->DataDirectory); i++)
    {
        IMAGE_DATA_DIRECTORY const *pDir = &pOptHdr->DataDirectory[i];
        if (!pDir->Size)
            continue;
        size_t cb = cbImage;
        switch (i)
        {
            case IMAGE_DIRECTORY_ENTRY_EXPORT:        // 0
            case IMAGE_DIRECTORY_ENTRY_IMPORT:        // 1
            case IMAGE_DIRECTORY_ENTRY_RESOURCE:      // 2
            case IMAGE_DIRECTORY_ENTRY_EXCEPTION:     // 3
            case IMAGE_DIRECTORY_ENTRY_BASERELOC:     // 5
            case IMAGE_DIRECTORY_ENTRY_DEBUG:         // 6
            case IMAGE_DIRECTORY_ENTRY_COPYRIGHT:     // 7
            case IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT:  // 11
            case IMAGE_DIRECTORY_ENTRY_IAT:           // 12  /* Import Address Table */
                break;
            case IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG:   // 10 - need to check for lock prefixes.
                /* Delay inspection after section table is validated. */
                break;

            case IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT:  // 13
                if (fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION))
                    break;
                Log(("rtldrPEOpen: %s: dir no. %d (DELAY_IMPORT) VirtualAddress=%#x Size=%#x is not supported!!!\n",
                     pszLogName, i, pDir->VirtualAddress, pDir->Size));
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRPE_DELAY_IMPORT,
                                           "DELAY_IMPORT VirtualAddress=%#x Size=%#x: not supported", pDir->VirtualAddress, pDir->Size);

            case IMAGE_DIRECTORY_ENTRY_SECURITY:      // 4
                /* The VirtualAddress is a PointerToRawData. */
                cb = (size_t)cbRawImage; Assert((uint64_t)cb == cbRawImage);
                Log(("rtldrPEOpen: %s: dir no. %d (SECURITY) VirtualAddress=%#x Size=%#x\n", pszLogName, i, pDir->VirtualAddress, pDir->Size));
                if (pDir->Size < sizeof(WIN_CERTIFICATE))
                {
                    Log(("rtldrPEOpen: %s: Security directory #%u is too small: %#x bytes\n", pszLogName, i, pDir->Size));
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRPE_CERT_MALFORMED,
                                               "Security directory is too small: %#x bytes", pDir->Size);
                }
                if (pDir->Size >= RTLDRMODPE_MAX_SECURITY_DIR_SIZE)
                {
                    Log(("rtldrPEOpen: %s: Security directory #%u is too large: %#x bytes\n", pszLogName, i, pDir->Size));
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRPE_CERT_MALFORMED,
                                               "Security directory is too large: %#x bytes", pDir->Size);
                }
                if (pDir->VirtualAddress & 7)
                {
                    Log(("rtldrPEOpen: %s: Security directory #%u is misaligned: %#x\n", pszLogName, i, pDir->VirtualAddress));
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRPE_CERT_MALFORMED,
                                               "Security directory is misaligned: %#x", pDir->VirtualAddress);
                }
                /* When using the in-memory reader with a debugger, we may get
                   into trouble here since we might not have access to the whole
                   physical file.  So skip the tests below. Makes VBoxGuest.sys
                   load and check out just fine, for instance. */
                if (fFlags & RTLDR_O_FOR_DEBUG)
                    continue;
                break;

            case IMAGE_DIRECTORY_ENTRY_GLOBALPTR:     // 8   /* (MIPS GP) */
                Log(("rtldrPEOpen: %s: dir no. %d (GLOBALPTR) VirtualAddress=%#x Size=%#x is not supported!!!\n",
                     pszLogName, i, pDir->VirtualAddress, pDir->Size));
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRPE_GLOBALPTR, "GLOBALPTR VirtualAddress=%#x Size=%#x: not supported",
                                           pDir->VirtualAddress, pDir->Size);

            case IMAGE_DIRECTORY_ENTRY_TLS:           // 9
                if (fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION))
                    break;
                Log(("rtldrPEOpen: %s: dir no. %d (TLS) VirtualAddress=%#x Size=%#x is not supported!!!\n",
                     pszLogName, i, pDir->VirtualAddress, pDir->Size));
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRPE_TLS, "TLS VirtualAddress=%#x Size=%#x: not supported",
                                           pDir->VirtualAddress, pDir->Size);

            case IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR:// 14
                if (fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION))
                    break;
                Log(("rtldrPEOpen: %s: dir no. %d (COM_DESCRIPTOR) VirtualAddress=%#x Size=%#x is not supported!!!\n",
                     pszLogName, i, pDir->VirtualAddress, pDir->Size));
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRPE_COM_DESCRIPTOR,
                                           "COM_DESCRIPTOR VirtualAddress=%#x Size=%#x: not supported",
                                           pDir->VirtualAddress, pDir->Size);

            default:
                Log(("rtldrPEOpen: %s: dir no. %d VirtualAddress=%#x Size=%#x is not supported!!!\n",
                     pszLogName, i, pDir->VirtualAddress, pDir->Size));
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "dir no. %d VirtualAddress=%#x Size=%#x is not supported",
                                           i, pDir->VirtualAddress, pDir->Size);
        }
        if (pDir->VirtualAddress >= cb)
        {
            Log(("rtldrPEOpen: %s: dir no. %d VirtualAddress=%#x is invalid (limit %#x)!!!\n",
                 pszLogName, i, pDir->VirtualAddress, cb));
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "dir no. %d VirtualAddress=%#x is invalid (limit %#x)",
                                       i, pDir->VirtualAddress, cb);
        }
        if (pDir->Size > cb - pDir->VirtualAddress)
        {
            Log(("rtldrPEOpen: %s: dir no. %d Size=%#x is invalid (rva=%#x, limit=%#x)!!!\n",
                 pszLogName, i, pDir->Size, pDir->VirtualAddress, cb));
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "dir no. %d Size=%#x is invalid (rva=%#x, limit=%#x)",
                                       i, pDir->Size, pDir->VirtualAddress, cb);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Validates and touch up the section headers.
 *
 * The touching up is restricted to setting the VirtualSize field for old-style
 * linkers that sets it to zero.
 *
 * @returns iprt status code.
 * @param   paSections  Pointer to the array of sections that is to be validated.
 * @param   cSections   Number of sections in that array.
 * @param   pszLogName  The log name to  prefix the errors with.
 * @param   pOptHdr     Pointer to the optional header (valid).
 * @param   cbRawImage  The raw image size.
 * @param   fFlags      Loader flags, RTLDR_O_XXX.
 * @param   fNoCode     Verify that the image contains no code.
 */
static int rtldrPEValidateAndTouchUpSectionHeaders(IMAGE_SECTION_HEADER *paSections, unsigned cSections, const char *pszLogName,
                                                   const IMAGE_OPTIONAL_HEADER64 *pOptHdr, uint64_t cbRawImage, uint32_t fFlags,
                                                   bool fNoCode)
{
    RT_NOREF_PV(pszLogName);

    /*
     * Do a quick pass to detect linker setting VirtualSize to zero.
     */
    bool                  fFixupVirtualSize = true;
    IMAGE_SECTION_HEADER *pSH = &paSections[0];
    for (unsigned cSHdrsLeft = cSections; cSHdrsLeft > 0; cSHdrsLeft--, pSH++)
        if (    pSH->Misc.VirtualSize != 0
            && !(pSH->Characteristics & IMAGE_SCN_TYPE_NOLOAD))
        {
            fFixupVirtualSize = false;
            break;
        }

    /*
     * Actual pass.
     */
    const uint32_t              cbImage  = pOptHdr->SizeOfImage;
    uint32_t                    uRvaPrev = pOptHdr->SizeOfHeaders;
    pSH = &paSections[0];
    Log3(("RTLdrPE: Section Headers:\n"));
    for (unsigned cSHdrsLeft = cSections; cSHdrsLeft > 0; cSHdrsLeft--, pSH++)
    {
        const unsigned iSH = (unsigned)(pSH - &paSections[0]); NOREF(iSH);
        Log3(("RTLdrPE: #%d '%-8.8s'  Characteristics: %08RX32\n"
              "RTLdrPE: VirtAddr: %08RX32  VirtSize: %08RX32\n"
              "RTLdrPE:  FileOff: %08RX32  FileSize: %08RX32\n"
              "RTLdrPE: RelocOff: %08RX32   #Relocs: %08RX32\n"
              "RTLdrPE:  LineOff: %08RX32    #Lines: %08RX32\n",
              iSH, pSH->Name, pSH->Characteristics,
              pSH->VirtualAddress, pSH->Misc.VirtualSize,
              pSH->PointerToRawData, pSH->SizeOfRawData,
              pSH->PointerToRelocations, pSH->NumberOfRelocations,
              pSH->PointerToLinenumbers, pSH->NumberOfLinenumbers));

        AssertCompile(IMAGE_SCN_MEM_16BIT == IMAGE_SCN_MEM_PURGEABLE);
        if (  (  pSH->Characteristics & (IMAGE_SCN_MEM_PURGEABLE | IMAGE_SCN_MEM_PRELOAD | IMAGE_SCN_MEM_FARDATA) )
            && !(fFlags & RTLDR_O_FOR_DEBUG)) /* purgable/16-bit seen on w2ksp0 hal.dll, ignore the bunch. */
        {
            Log(("rtldrPEOpen: %s: Unsupported section flag(s) %#x section #%d '%.*s'!!!\n",
                 pszLogName, pSH->Characteristics, iSH, sizeof(pSH->Name), pSH->Name));
            return VERR_BAD_EXE_FORMAT;
        }

        if (    pSH->PointerToRawData > cbRawImage /// @todo pSH->PointerToRawData >= cbRawImage ?
            ||  pSH->SizeOfRawData > cbRawImage
            ||  pSH->PointerToRawData + pSH->SizeOfRawData > cbRawImage)
        {
            Log(("rtldrPEOpen: %s: PointerToRawData=%#x SizeOfRawData=%#x - beyond end of file (%#llx) - section #%d '%.*s'!!!\n",
                 pszLogName, pSH->PointerToRawData, pSH->SizeOfRawData, cbRawImage,
                 iSH, sizeof(pSH->Name), pSH->Name));
            return VERR_BAD_EXE_FORMAT;
        }

        if (pSH->PointerToRawData & (pOptHdr->FileAlignment - 1)) //ASSUMES power of 2 alignment.
        {
            Log(("rtldrPEOpen: %s: PointerToRawData=%#x misaligned (%#x) - section #%d '%.*s'!!!\n",
                 pszLogName, pSH->PointerToRawData, pOptHdr->FileAlignment, iSH, sizeof(pSH->Name), pSH->Name));
            return VERR_BAD_EXE_FORMAT;
        }

        if (!(pSH->Characteristics & IMAGE_SCN_TYPE_NOLOAD)) /* binutils uses this for '.stab' even if it's reserved/obsoleted by MS. */
        {
            /* Calc VirtualSize if necessary.  This is for internal reasons. */
            if (   pSH->Misc.VirtualSize == 0
                && fFixupVirtualSize)
            {
                pSH->Misc.VirtualSize = cbImage - RT_MIN(pSH->VirtualAddress, cbImage);
                for (uint32_t i = 1; i < cSHdrsLeft; i++)
                    if (   !(pSH[i].Characteristics & IMAGE_SCN_TYPE_NOLOAD)
                        && pSH[i].VirtualAddress >= pSH->VirtualAddress)
                    {
                        pSH->Misc.VirtualSize = RT_MIN(pSH[i].VirtualAddress - pSH->VirtualAddress, pSH->Misc.VirtualSize);
                        break;
                    }
            }

            if (pSH->Misc.VirtualSize > 0)
            {
                if (pSH->VirtualAddress < uRvaPrev)
                {
                    Log(("rtldrPEOpen: %s: Overlaps previous section or sections aren't in ascending order, VirtualAddress=%#x uRvaPrev=%#x - section #%d '%.*s'!!!\n",
                         pszLogName, pSH->VirtualAddress, uRvaPrev, iSH, sizeof(pSH->Name), pSH->Name));
                    return VERR_BAD_EXE_FORMAT;
                }
                if (pSH->VirtualAddress > cbImage)
                {
                    Log(("rtldrPEOpen: %s: VirtualAddress=%#x - beyond image size (%#x) - section #%d '%.*s'!!!\n",
                         pszLogName, pSH->VirtualAddress, cbImage, iSH, sizeof(pSH->Name), pSH->Name));
                    return VERR_BAD_EXE_FORMAT;
                }

                if (pSH->VirtualAddress & (pOptHdr->SectionAlignment - 1)) //ASSUMES power of 2 alignment.
                {
                    Log(("rtldrPEOpen: %s: VirtualAddress=%#x misaligned (%#x) - section #%d '%.*s'!!!\n",
                         pszLogName, pSH->VirtualAddress, pOptHdr->SectionAlignment, iSH, sizeof(pSH->Name), pSH->Name));
                    return VERR_BAD_EXE_FORMAT;
                }

#ifdef PE_FILE_OFFSET_EQUALS_RVA
                /* Our loader code assume rva matches the file offset. */
                if (    pSH->SizeOfRawData
                    &&  pSH->PointerToRawData != pSH->VirtualAddress)
                {
                    Log(("rtldrPEOpen: %s: ASSUMPTION FAILED: file offset %#x != RVA %#x - section #%d '%.*s'!!!\n",
                         pszLogName, pSH->PointerToRawData, pSH->VirtualAddress, iSH, sizeof(pSH->Name), pSH->Name));
                    return VERR_BAD_EXE_FORMAT;
                }
#endif

                uRvaPrev = pSH->VirtualAddress + pSH->Misc.VirtualSize;
            }
        }

        /* ignore the relocations and linenumbers. */
    }

    /*
     * Do a separate run if we need to validate the no-code claim from the
     * optional header.
     */
    if (fNoCode)
    {
        pSH = &paSections[0];
        for (unsigned cSHdrsLeft = cSections; cSHdrsLeft > 0; cSHdrsLeft--, pSH++)
            if (pSH->Characteristics & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE))
                return VERR_LDR_ARCH_MISMATCH;
    }


    /** @todo r=bird: more sanity checks! */
    return VINF_SUCCESS;
}


/**
 * Reads image data by RVA using the section headers.
 *
 * @returns iprt status code.
 * @param   pModPe      The PE module instance.
 * @param   pvBuf       Where to store the bits.
 * @param   cb          Number of bytes to tread.
 * @param   RVA         Where to read from.
 */
static int rtldrPEReadRVA(PRTLDRMODPE pModPe, void *pvBuf, uint32_t cb, uint32_t RVA)
{
    const IMAGE_SECTION_HEADER *pSH = pModPe->paSections;
    PRTLDRREADER                pReader = pModPe->Core.pReader;
    uint32_t                    cbRead;
    int                         rc;

    /*
     * Is it the headers, i.e. prior to the first section.
     */
    if (RVA < pModPe->cbHeaders)
    {
        cbRead = RT_MIN(pModPe->cbHeaders - RVA, cb);
        rc = pReader->pfnRead(pReader, pvBuf, cbRead, RVA);
        if (    cbRead == cb
            ||  RT_FAILURE(rc))
            return rc;
        cb -= cbRead;
        RVA += cbRead;
        pvBuf = (uint8_t *)pvBuf + cbRead;
    }

    /* In the zero space between headers and the first section? */
    if (RVA < pSH->VirtualAddress)
    {
        cbRead = RT_MIN(pSH->VirtualAddress - RVA, cb);
        memset(pvBuf, 0, cbRead);
        if (cbRead == cb)
            return VINF_SUCCESS;
        cb -= cbRead;
        RVA += cbRead;
        pvBuf = (uint8_t *)pvBuf + cbRead;
    }

    /*
     * Iterate the sections.
     */
    for (unsigned cLeft = pModPe->cSections;
         cLeft > 0;
         cLeft--, pSH++)
    {
        uint32_t off = RVA - pSH->VirtualAddress;
        if (off < pSH->Misc.VirtualSize)
        {
            cbRead = RT_MIN(pSH->Misc.VirtualSize - off, cb);
            rc = pReader->pfnRead(pReader, pvBuf, cbRead, pSH->PointerToRawData + off);
            if (    cbRead == cb
                ||  RT_FAILURE(rc))
                return rc;
            cb -= cbRead;
            RVA += cbRead;
            pvBuf = (uint8_t *)pvBuf + cbRead;
        }
        uint32_t RVANext = cLeft ? pSH[1].VirtualAddress : pModPe->cbImage;
        if (RVA < RVANext)
        {
            cbRead = RT_MIN(RVANext - RVA, cb);
            memset(pvBuf, 0, cbRead);
            if (cbRead == cb)
                return VINF_SUCCESS;
            cb -= cbRead;
            RVA += cbRead;
            pvBuf = (uint8_t *)pvBuf + cbRead;
        }
    }

    AssertFailed();
    return VERR_INTERNAL_ERROR;
}


/**
 * Validates the data of some selected data directories entries and remember
 * important bits for later.
 *
 * This requires a valid section table and thus has to wait till after we've
 * read and validated it.
 *
 * @returns iprt status code.
 * @param   pModPe      The PE module instance.
 * @param   pOptHdr     Pointer to the optional header (valid).
 * @param   fFlags      Loader flags, RTLDR_O_XXX.
 * @param   pErrInfo    Where to return extended error information. Optional.
 */
static int rtldrPEValidateDirectoriesAndRememberStuff(PRTLDRMODPE pModPe, const IMAGE_OPTIONAL_HEADER64 *pOptHdr, uint32_t fFlags,
                                                      PRTERRINFO pErrInfo)
{
    const char *pszLogName = pModPe->Core.pReader->pfnLogName(pModPe->Core.pReader); NOREF(pszLogName);
    union /* combine stuff we're reading to help reduce stack usage. */
    {
        IMAGE_LOAD_CONFIG_DIRECTORY64   Cfg64;
        uint8_t                         abZeros[sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64) * 4];
    } u;

    /*
     * The load config entry may include lock prefix tables and whatnot which we don't implement.
     * It does also include a lot of stuff which we can ignore, so we'll have to inspect the
     * actual data before we can make up our mind about it all.
     */
    IMAGE_DATA_DIRECTORY Dir = pOptHdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    if (Dir.Size)
    {
        const size_t cbExpectV13 = !pModPe->f64Bit
                                 ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V13)
                                 : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V13);
        const size_t cbExpectV12 = !pModPe->f64Bit
                                 ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V12)
                                 : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V12);
        const size_t cbExpectV11 = !pModPe->f64Bit
                                 ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V11)
                                 : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V11);
        const size_t cbExpectV10 = !pModPe->f64Bit
                                 ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V10)
                                 : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V10);
        const size_t cbExpectV9 = !pModPe->f64Bit
                                ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V9)
                                : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V9);
        const size_t cbExpectV8 = !pModPe->f64Bit
                                ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V8)
                                : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V8);
        const size_t cbExpectV7 = !pModPe->f64Bit
                                ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V7)
                                : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V7);
        const size_t cbExpectV6 = !pModPe->f64Bit
                                ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V6)
                                : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V6);
        const size_t cbExpectV5 = !pModPe->f64Bit
                                ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V5)
                                : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V5);
        const size_t cbExpectV4 = !pModPe->f64Bit
                                ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V4)
                                : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V4);
        const size_t cbExpectV3 = !pModPe->f64Bit
                                ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V3)
                                : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V3);
        const size_t cbExpectV2 = !pModPe->f64Bit
                                ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V2)
                                : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V2);
        const size_t cbExpectV1 = !pModPe->f64Bit
                                ? sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_V1)
                                : sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64_V2) /*No V1*/;
        const size_t cbNewHack  = cbExpectV5; /* Playing safe here since there might've been revisions between V5 and V6 we don't know about . */
        const size_t cbMaxKnown = cbExpectV12;

        bool fNewerStructureHack = false;
        if (   Dir.Size != cbExpectV13
            && Dir.Size != cbExpectV12
            && Dir.Size != cbExpectV11
            && Dir.Size != cbExpectV10
            && Dir.Size != cbExpectV9
            && Dir.Size != cbExpectV8
            && Dir.Size != cbExpectV7
            && Dir.Size != cbExpectV6
            && Dir.Size != cbExpectV5
            && Dir.Size != cbExpectV4
            && Dir.Size != cbExpectV3
            && Dir.Size != cbExpectV2
            && Dir.Size != cbExpectV1)
        {
            fNewerStructureHack = Dir.Size > cbNewHack /* These structure changes are slowly getting to us! More futher down. */
                               && Dir.Size <= sizeof(u);
            Log(("rtldrPEOpen: %s: load cfg dir: unexpected dir size of %u bytes, expected %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, or %zu.%s\n",
                 pszLogName, Dir.Size, cbExpectV13, cbExpectV12, cbExpectV11, cbExpectV10, cbExpectV9, cbExpectV8, cbExpectV7, cbExpectV6, cbExpectV5, cbExpectV4, cbExpectV3, cbExpectV2, cbExpectV1,
                 fNewerStructureHack ? " Will try ignore extra bytes if all zero." : ""));
            if (!fNewerStructureHack)
                return RTErrInfoSetF(pErrInfo, VERR_LDRPE_LOAD_CONFIG_SIZE,
                                     "Unexpected load config dir size of %u bytes; supported sized: %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, or %zu",
                                     Dir.Size, cbExpectV13, cbExpectV12, cbExpectV11, cbExpectV10, cbExpectV9, cbExpectV8, cbExpectV7, cbExpectV6, cbExpectV5, cbExpectV4, cbExpectV3, cbExpectV2, cbExpectV1);
        }

        /*
         * Read, check new stuff and convert to 64-bit.
         *
         * If we accepted a newer structures when loading for debug or validation,
         * otherwise we require the new bits to be all zero and hope that they are
         * insignificant where image loading is concerned (that's mostly been the
         * case even for non-zero bits, only hard exception is LockPrefixTable).
         */
        RT_ZERO(u.Cfg64);
        int rc = rtldrPEReadRVA(pModPe, &u.Cfg64, Dir.Size, Dir.VirtualAddress);
        if (RT_FAILURE(rc))
            return rc;
        if (   fNewerStructureHack
            && Dir.Size > cbMaxKnown
            && !(fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION))
            && !ASMMemIsZero(&u.abZeros[cbMaxKnown], Dir.Size - cbMaxKnown))
        {
            Log(("rtldrPEOpen: %s: load cfg dir: Unexpected bytes are non-zero (%u bytes of which %u expected to be zero): %.*Rhxs\n",
                 pszLogName, Dir.Size, Dir.Size - cbMaxKnown, Dir.Size - cbMaxKnown, &u.abZeros[cbMaxKnown]));
            return RTErrInfoSetF(pErrInfo, VERR_LDRPE_LOAD_CONFIG_SIZE,
                                 "Grown load config (%u to %u bytes) includes non-zero bytes: %.*Rhxs",
                                 cbMaxKnown, Dir.Size, Dir.Size - cbMaxKnown, &u.abZeros[cbMaxKnown]);
        }
        rtldrPEConvert32BitLoadConfigTo64Bit(&u.Cfg64);

        if (u.Cfg64.Size != Dir.Size)
        {
            /* Kludge #1: ntdll.dll from XP seen with Dir.Size=0x40 and Cfg64.Size=0x00. */
            if (Dir.Size == 0x40 && u.Cfg64.Size == 0x00 && !pModPe->f64Bit)
            {
                Log(("rtldrPEOpen: %s: load cfg dir: Header (%d) and directory (%d) size mismatch, applying the XP kludge.\n",
                     pszLogName, u.Cfg64.Size, Dir.Size));
                u.Cfg64.Size = Dir.Size;
            }
            /* Kludge #2: This happens a lot. Structure changes, but the linker doesn't get
               updated and stores some old size in the directory.  Use the header size. */
            else if (   u.Cfg64.Size == cbExpectV13
                     || u.Cfg64.Size == cbExpectV12
                     || u.Cfg64.Size == cbExpectV11
                     || u.Cfg64.Size == cbExpectV10
                     || u.Cfg64.Size == cbExpectV9
                     || u.Cfg64.Size == cbExpectV8
                     || u.Cfg64.Size == cbExpectV7
                     || u.Cfg64.Size == cbExpectV6
                     || u.Cfg64.Size == cbExpectV5
                     || u.Cfg64.Size == cbExpectV4
                     || u.Cfg64.Size == cbExpectV3
                     || u.Cfg64.Size == cbExpectV2
                     || u.Cfg64.Size == cbExpectV1
                     || (fNewerStructureHack = (u.Cfg64.Size > cbNewHack && u.Cfg64.Size <= sizeof(u))) )
            {
                Log(("rtldrPEOpen: %s: load cfg dir: Header (%d) and directory (%d) size mismatch, applying the old linker kludge.\n",
                     pszLogName, u.Cfg64.Size, Dir.Size));

                uint32_t const uOrgDir = Dir.Size;
                Dir.Size = u.Cfg64.Size;
                RT_ZERO(u.Cfg64);
                rc = rtldrPEReadRVA(pModPe, &u.Cfg64, Dir.Size, Dir.VirtualAddress);
                if (RT_FAILURE(rc))
                    return rc;
                if (   fNewerStructureHack
                    && Dir.Size > cbMaxKnown
                    && !(fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION))
                    && !ASMMemIsZero(&u.abZeros[cbMaxKnown], Dir.Size - cbMaxKnown))
                {
                    Log(("rtldrPEOpen: %s: load cfg dir: Unknown bytes are non-zero (%u bytes of which %u expected to be zero): %.*Rhxs\n",
                         pszLogName, Dir.Size, Dir.Size - cbMaxKnown, Dir.Size - cbMaxKnown, &u.abZeros[cbMaxKnown]));
                    return RTErrInfoSetF(pErrInfo, VERR_LDRPE_LOAD_CONFIG_SIZE,
                                         "Grown load config (%u to %u bytes, dir %u) includes non-zero bytes: %.*Rhxs",
                                         cbMaxKnown, Dir.Size, uOrgDir, Dir.Size - cbMaxKnown, &u.abZeros[cbMaxKnown]);
                }
                rtldrPEConvert32BitLoadConfigTo64Bit(&u.Cfg64);
                AssertReturn(u.Cfg64.Size == Dir.Size,
                             RTErrInfoSetF(pErrInfo, VERR_LDRPE_LOAD_CONFIG_SIZE, "Data changed while reading! (%d vs %d)\n",
                                           u.Cfg64.Size, Dir.Size));
            }
            else
            {
                Log(("rtldrPEOpen: %s: load cfg hdr: unexpected hdr size of %u bytes (dir %u), expected %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, or %zu.\n",
                     pszLogName, u.Cfg64.Size, Dir.Size, cbExpectV12, cbExpectV11, cbExpectV10, cbExpectV9, cbExpectV8, cbExpectV7, cbExpectV6, cbExpectV5, cbExpectV4, cbExpectV3, cbExpectV2, cbExpectV1));
                return RTErrInfoSetF(pErrInfo, VERR_LDRPE_LOAD_CONFIG_SIZE,
                                     "Unexpected load config header size of %u bytes (dir %u); supported sized: %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu, or %zu",
                                     u.Cfg64.Size, Dir.Size, cbExpectV12, cbExpectV11, cbExpectV10, cbExpectV9, cbExpectV8, cbExpectV7, cbExpectV6, cbExpectV5, cbExpectV4, cbExpectV3, cbExpectV2, cbExpectV1);
            }
        }
        if (u.Cfg64.LockPrefixTable && !(fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION)))
        {
            Log(("rtldrPEOpen: %s: load cfg dir: lock prefix table at %RX64. We don't support lock prefix tables!\n",
                 pszLogName, u.Cfg64.LockPrefixTable));
            return RTErrInfoSetF(pErrInfo, VERR_LDRPE_LOCK_PREFIX_TABLE,
                                 "Lock prefix table not supported: %RX64", u.Cfg64.LockPrefixTable);
        }
#if 0/* this seems to be safe to ignore. */
        if (    u.Cfg64.SEHandlerTable
            ||  u.Cfg64.SEHandlerCount)
        {
            Log(("rtldrPEOpen: %s: load cfg dir: SEHandlerTable=%RX64 and SEHandlerCount=%RX64 are unsupported!\n",
                 pszLogName, u.Cfg64.SEHandlerTable, u.Cfg64.SEHandlerCount));
            return VERR_BAD_EXE_FORMAT;
        }
#endif
        if (u.Cfg64.EditList && !(fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION)))
        {
            Log(("rtldrPEOpen: %s: load cfg dir: EditList=%RX64 is unsupported!\n",
                 pszLogName, u.Cfg64.EditList));
            return RTErrInfoSetF(pErrInfo, VERR_BAD_EXE_FORMAT, "Load config EditList=%RX64 is not supported", u.Cfg64.EditList);
        }
        /** @todo GuardCFC? Possibly related to:
         *         http://research.microsoft.com/pubs/69217/ccs05-cfi.pdf
         * Not trusting something designed by bakas who don't know how to modify a
         * structure without messing up its natural alignment. */
        if (    (   u.Cfg64.GuardCFCCheckFunctionPointer
                 || u.Cfg64.GuardCFDispatchFunctionPointer
                 || u.Cfg64.GuardCFFunctionTable
                 || u.Cfg64.GuardCFFunctionCount
                 || u.Cfg64.GuardFlags
                 || u.Cfg64.GuardAddressTakenIatEntryTable
                 || u.Cfg64.GuardAddressTakenIatEntryCount
                 || u.Cfg64.GuardLongJumpTargetTable
                 || u.Cfg64.GuardLongJumpTargetCount)
            && !(fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION)) )
        {
            Log(("rtldrPEOpen: %s: load cfg dir: Guard stuff: %RX64,%RX64,%RX64,%RX64,%RX32,%RX64,%RX64,%RX64,%RX64!\n",
                 pszLogName, u.Cfg64.GuardCFCCheckFunctionPointer, u.Cfg64.GuardCFDispatchFunctionPointer,
                 u.Cfg64.GuardCFFunctionTable, u.Cfg64.GuardCFFunctionCount, u.Cfg64.GuardFlags,
                 u.Cfg64.GuardAddressTakenIatEntryTable, u.Cfg64.GuardAddressTakenIatEntryCount,
                 u.Cfg64.GuardLongJumpTargetTable, u.Cfg64.GuardLongJumpTargetCount ));
#if 0 /* ntdll 15002 uses this. */
            return RTErrInfoSetF(pErrInfo, VERR_LDRPE_GUARD_CF_STUFF,
                                 "Guard bits in load config: %RX64,%RX64,%RX64,%RX64,%RX32,%RX64,%RX64,%RX64,%RX64!",
                                 u.Cfg64.GuardCFCCheckFunctionPointer, u.Cfg64.GuardCFDispatchFunctionPointer,
                                 u.Cfg64.GuardCFFunctionTable, u.Cfg64.GuardCFFunctionCount, u.Cfg64.GuardFlags,
                                 u.Cfg64.GuardAddressTakenIatEntryTable, u.Cfg64.GuardAddressTakenIatEntryCount,
                                 u.Cfg64.GuardLongJumpTargetTable, u.Cfg64.GuardLongJumpTargetCount);
#endif
        }
    }

    /*
     * If the image is signed and we're not doing this for debug purposes,
     * take a look at the signature.
     */
    Dir = pOptHdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
    if (Dir.Size)
    {
        PWIN_CERTIFICATE pFirst = (PWIN_CERTIFICATE)RTMemTmpAlloc(Dir.Size);
        if (!pFirst)
            return VERR_NO_TMP_MEMORY;
        int rc = pModPe->Core.pReader->pfnRead(pModPe->Core.pReader, pFirst, Dir.Size, Dir.VirtualAddress);
        if (RT_SUCCESS(rc))
        {
            uint32_t off  = 0;
            do
            {
                PWIN_CERTIFICATE pCur = (PWIN_CERTIFICATE)((uint8_t *)pFirst + off);

                /* validate the members. */
                if (   pCur->dwLength < sizeof(WIN_CERTIFICATE)
                    || pCur->dwLength + off > Dir.Size)
                {
                    Log(("rtldrPEOpen: %s: cert at %#x/%#x: dwLength=%#x\n", pszLogName, off, Dir.Size, pCur->dwLength));
                    rc = RTErrInfoSetF(pErrInfo, VERR_LDRPE_CERT_MALFORMED,
                                       "Cert at %#x LB %#x: Bad header length value: %#x", off, Dir.Size, pCur->dwLength);
                    break;
                }
                if (    pCur->wRevision != WIN_CERT_REVISION_2_0
                    &&  pCur->wRevision != WIN_CERT_REVISION_1_0)
                {
                    Log(("rtldrPEOpen: %s: cert at %#x/%#x: wRevision=%#x\n", pszLogName, off, Dir.Size, pCur->wRevision));
                    if (pCur->wRevision >= WIN_CERT_REVISION_1_0)
                        rc = RTErrInfoSetF(pErrInfo, VERR_LDRPE_CERT_UNSUPPORTED,
                                           "Cert at %#x LB %#x: Unsupported revision: %#x", off, Dir.Size, pCur->wRevision);
                    else
                        rc = RTErrInfoSetF(pErrInfo, VERR_LDRPE_CERT_MALFORMED,
                                           "Cert at %#x LB %#x: Malformed revision: %#x", off, Dir.Size, pCur->wRevision);
                    break;
                }
                if (    pCur->wCertificateType != WIN_CERT_TYPE_PKCS_SIGNED_DATA
                    &&  pCur->wCertificateType != WIN_CERT_TYPE_X509
                    /*&&  pCur->wCertificateType != WIN_CERT_TYPE_RESERVED_1*/
                    /*&&  pCur->wCertificateType != WIN_CERT_TYPE_TS_STACK_SIGNED*/
                    &&  pCur->wCertificateType != WIN_CERT_TYPE_EFI_PKCS115
                    &&  pCur->wCertificateType != WIN_CERT_TYPE_EFI_GUID
                   )
                {
                    Log(("rtldrPEOpen: %s: cert at %#x/%#x: wCertificateType=%#x\n", pszLogName, off, Dir.Size, pCur->wCertificateType));
                    if (pCur->wCertificateType)
                        rc = RTErrInfoSetF(pErrInfo, VERR_LDRPE_CERT_UNSUPPORTED,
                                           "Cert at %#x LB %#x: Unsupported certificate type: %#x",
                                           off, Dir.Size, pCur->wCertificateType);
                    else
                        rc = RTErrInfoSetF(pErrInfo, VERR_LDRPE_CERT_MALFORMED,
                                           "Cert at %#x LB %#x: Malformed certificate type: %#x",
                                           off, Dir.Size, pCur->wCertificateType);
                    break;
                }

                /* Remember the first signed data certificate. */
                if (   pCur->wCertificateType == WIN_CERT_TYPE_PKCS_SIGNED_DATA
                    && pModPe->offPkcs7SignedData == 0)
                {
                    pModPe->offPkcs7SignedData = Dir.VirtualAddress
                                               + (uint32_t)((uintptr_t)&pCur->bCertificate[0] - (uintptr_t)pFirst);
                    pModPe->cbPkcs7SignedData  = pCur->dwLength - RT_UOFFSETOF(WIN_CERTIFICATE, bCertificate);
                }

                /* next */
                off += RT_ALIGN(pCur->dwLength, WIN_CERTIFICATE_ALIGNMENT);
            } while (off < Dir.Size);
        }
        RTMemTmpFree(pFirst);
        if (RT_FAILURE(rc) && !(fFlags & RTLDR_O_FOR_DEBUG))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Open a PE image.
 *
 * @returns iprt status code.
 * @param   pReader     The loader reader instance which will provide the raw image bits.
 * @param   fFlags      Loader flags, RTLDR_O_XXX.
 * @param   enmArch     Architecture specifier.
 * @param   offNtHdrs   The offset of the NT headers (where you find "PE\0\0").
 * @param   phLdrMod    Where to store the handle.
 * @param   pErrInfo    Where to return extended error information. Optional.
 */
DECLHIDDEN(int) rtldrPEOpen(PRTLDRREADER pReader, uint32_t fFlags, RTLDRARCH enmArch, RTFOFF offNtHdrs,
                            PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo)
{
    /*
     * Read and validate the file header.
     */
    IMAGE_FILE_HEADER FileHdr;
    int rc = pReader->pfnRead(pReader, &FileHdr, sizeof(FileHdr), offNtHdrs + 4);
    if (RT_FAILURE(rc))
        return rc;
    RTLDRARCH enmArchImage;
    const char *pszLogName = pReader->pfnLogName(pReader);
    rc = rtldrPEValidateFileHeader(&FileHdr, fFlags, pszLogName, &enmArchImage, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Match the CPU architecture.
     */
    bool fArchNoCodeCheckPending = false;
    if (    enmArch != enmArchImage
        &&  (   enmArch != RTLDRARCH_WHATEVER
             && !(fFlags & RTLDR_O_WHATEVER_ARCH)) )
    {
        if (!(fFlags & RTLDR_O_IGNORE_ARCH_IF_NO_CODE))
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDR_ARCH_MISMATCH, "Image is for '%s', only accepting images for '%s'.",
                                       rtldrPEGetArchName(FileHdr.Machine), RTLdrArchName(enmArch));
        fArchNoCodeCheckPending = true;
    }

    /*
     * Read and validate the "optional" header. Convert 32->64 if necessary.
     */
    IMAGE_OPTIONAL_HEADER64 OptHdr;
    rc = pReader->pfnRead(pReader, &OptHdr, FileHdr.SizeOfOptionalHeader, offNtHdrs + 4 + sizeof(IMAGE_FILE_HEADER));
    if (RT_FAILURE(rc))
        return rc;
    if (FileHdr.SizeOfOptionalHeader != sizeof(OptHdr))
        rtldrPEConvert32BitOptionalHeaderTo64Bit(&OptHdr);
    rc = rtldrPEValidateOptionalHeader(&OptHdr, pszLogName, offNtHdrs, &FileHdr, pReader->pfnSize(pReader), fFlags, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;
    if (fArchNoCodeCheckPending && OptHdr.SizeOfCode != 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDR_ARCH_MISMATCH,
                                   "Image is for '%s' and contains code (%#x), only accepting images for '%s' with code.",
                                   rtldrPEGetArchName(FileHdr.Machine), OptHdr.SizeOfCode, RTLdrArchName(enmArch));

    /*
     * Read and validate section headers.
     */
    const size_t cbSections = sizeof(IMAGE_SECTION_HEADER) * FileHdr.NumberOfSections;
    PIMAGE_SECTION_HEADER paSections = (PIMAGE_SECTION_HEADER)RTMemAlloc(cbSections);
    if (!paSections)
        return VERR_NO_MEMORY;
    rc = pReader->pfnRead(pReader, paSections, cbSections,
                          offNtHdrs + 4 + sizeof(IMAGE_FILE_HEADER) + FileHdr.SizeOfOptionalHeader);
    if (RT_SUCCESS(rc))
    {
        rc = rtldrPEValidateAndTouchUpSectionHeaders(paSections, FileHdr.NumberOfSections, pszLogName,
                                                     &OptHdr, pReader->pfnSize(pReader), fFlags, fArchNoCodeCheckPending);
        if (RT_SUCCESS(rc))
        {
            /*
             * Allocate and initialize the PE module structure.
             */
            PRTLDRMODPE pModPe = (PRTLDRMODPE)RTMemAllocZ(sizeof(*pModPe));
            if (pModPe)
            {
                pModPe->Core.u32Magic = RTLDRMOD_MAGIC;
                pModPe->Core.eState   = LDR_STATE_OPENED;
                if (FileHdr.SizeOfOptionalHeader == sizeof(OptHdr))
                    pModPe->Core.pOps = &s_rtldrPE64Ops.Core;
                else
                    pModPe->Core.pOps = &s_rtldrPE32Ops.Core;
                pModPe->Core.pReader  = pReader;
                pModPe->Core.enmFormat= RTLDRFMT_PE;
                pModPe->Core.enmType  = FileHdr.Characteristics & IMAGE_FILE_DLL
                                      ? FileHdr.Characteristics & IMAGE_FILE_RELOCS_STRIPPED
                                        ? RTLDRTYPE_EXECUTABLE_FIXED
                                        : RTLDRTYPE_EXECUTABLE_RELOCATABLE
                                      : FileHdr.Characteristics & IMAGE_FILE_RELOCS_STRIPPED
                                        ? RTLDRTYPE_SHARED_LIBRARY_FIXED
                                        : RTLDRTYPE_SHARED_LIBRARY_RELOCATABLE;
                pModPe->Core.enmEndian= RTLDRENDIAN_LITTLE;
                pModPe->Core.enmArch  = FileHdr.Machine == IMAGE_FILE_MACHINE_I386
                                      ? RTLDRARCH_X86_32
                                      : FileHdr.Machine == IMAGE_FILE_MACHINE_AMD64
                                      ? RTLDRARCH_AMD64
                                      : RTLDRARCH_WHATEVER;
                pModPe->pvBits        = NULL;
                pModPe->offNtHdrs     = offNtHdrs;
                pModPe->offEndOfHdrs  = offNtHdrs + 4 + sizeof(IMAGE_FILE_HEADER) + FileHdr.SizeOfOptionalHeader + cbSections;
                pModPe->u16Machine    = FileHdr.Machine;
                pModPe->fFile         = FileHdr.Characteristics;
                pModPe->cSections     = FileHdr.NumberOfSections;
                pModPe->paSections    = paSections;
                pModPe->uEntryPointRVA= OptHdr.AddressOfEntryPoint;
                pModPe->uImageBase    = (RTUINTPTR)OptHdr.ImageBase;
                pModPe->cbImage       = OptHdr.SizeOfImage;
                pModPe->cbHeaders     = OptHdr.SizeOfHeaders;
                pModPe->uSectionAlign = OptHdr.SectionAlignment;
                pModPe->uTimestamp    = FileHdr.TimeDateStamp;
                pModPe->cImports      = UINT32_MAX;
                pModPe->f64Bit        = FileHdr.SizeOfOptionalHeader == sizeof(OptHdr);
                pModPe->ImportDir     = OptHdr.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
                pModPe->RelocDir      = OptHdr.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
                pModPe->ExportDir     = OptHdr.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
                pModPe->DebugDir      = OptHdr.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
                pModPe->SecurityDir   = OptHdr.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
                pModPe->ExceptionDir  = OptHdr.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
                pModPe->fDllCharacteristics = OptHdr.DllCharacteristics;

                /*
                 * Perform validation of some selected data directories which requires
                 * inspection of the actual data.  This also saves some certificate
                 * information.
                 */
                rc = rtldrPEValidateDirectoriesAndRememberStuff(pModPe, &OptHdr, fFlags, pErrInfo);
                if (RT_SUCCESS(rc))
                {
                    *phLdrMod = &pModPe->Core;
                    return VINF_SUCCESS;
                }
                RTMemFree(pModPe);
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }
    RTMemFree(paSections);
    return rc;
}

