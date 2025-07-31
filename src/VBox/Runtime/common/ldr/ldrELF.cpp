/* $Id: ldrELF.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, Executable and Linker Format (ELF).
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

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/dbg.h>
#include <iprt/string.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/crypto/digest.h>
#include <iprt/formats/elf32.h>
#include <iprt/formats/elf64.h>
#include <iprt/formats/elf-i386.h>
#include <iprt/formats/elf-amd64.h>
#include "internal/ldr.h"
#include "internal/dbgmod.h"



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Finds an ELF symbol table string. */
#define ELF_STR(pHdrs, iStr)        ((pHdrs)->Rel.pStr + (iStr))
/** Finds an ELF symbol table string. */
#define ELF_DYN_STR(pHdrs, iStr)    ((pHdrs)->Dyn.pStr + (iStr))
/** Finds an ELF section header string. */
#define ELF_SH_STR(pHdrs, iStr)     ((pHdrs)->pShStr + (iStr))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Magic string for RTLDRLNXMODSIG::achMagic   */
#define RTLDRLNXMODSIG_MAGIC "~Module signature appended~\n"
AssertCompile(sizeof(RTLDRLNXMODSIG_MAGIC) == 29);

/**
 * Linux kernel module signature footer - found at the end of the file.
 */
typedef struct RTLDRLNXMODSIG
{
    /** Zero. */
    uint8_t         bAlgo;
    /** Zero. */
    uint8_t         bHash;
    /** Signature type (RTLDRLNXMODSIG_TYPE_PKCS7). */
    uint8_t         bType;
    /** Zero. */
    uint8_t         cbSignerName;
    /** Zero. */
    uint8_t         cbKeyId;
    /** Zero padding. */
    uint8_t         abReserved[3];
    /** The length of the signature preceding this footer structure. */
    uint32_t        cbSignature;
    /** Magic value identifying this structure.   */
    char            achMagic[sizeof(RTLDRLNXMODSIG_MAGIC) - 1];
} RTLDRLNXMODSIG;
typedef RTLDRLNXMODSIG *PRTLDRLNXMODSIG;
typedef RTLDRLNXMODSIG const *PCRTLDRLNXMODSIG;
/** Signature type.   */
#define RTLDRLNXMODSIG_TYPE_PKCS7   2



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtLdrELFLnxKModQueryPropIsSigned(PRTLDRREADER pReader, bool *pfRet);
static int rtLdrELFLnxKModQueryPropPkcs7SignedData(PRTLDRREADER pReader, void *pvBuf, size_t cbBuf, size_t *pcbRet);
static DECLCALLBACK(int) rtldrELFLnxKModHashImage(PRTLDRMODINTERNAL pMod, RTDIGESTTYPE enmDigest, uint8_t *pabHash, size_t cbHash);
#ifdef LOG_ENABLED
static const char *rtldrElfGetShdrType(uint32_t iType);
static const char *rtldrElfGetPhdrType(uint32_t iType);
#endif


/* Select ELF mode and include the template. */
#define ELF_MODE            32
#define Elf_Reloc           Elf_Rel
#include "ldrELFRelocatable.cpp.h"
#undef ELF_MODE
#undef Elf_Reloc


#define ELF_MODE            64
#define Elf_Reloc           Elf_Rela
#include "ldrELFRelocatable.cpp.h"
#undef ELF_MODE
#undef Elf_Reloc


#ifdef LOG_ENABLED

/**
 * Gets the section type.
 *
 * @returns Pointer to read only string.
 * @param   iType       The section type index.
 */
static const char *rtldrElfGetShdrType(uint32_t iType)
{
    switch (iType)
    {
        RT_CASE_RET_STR(SHT_NULL);
        RT_CASE_RET_STR(SHT_PROGBITS);
        RT_CASE_RET_STR(SHT_SYMTAB);
        RT_CASE_RET_STR(SHT_STRTAB);
        RT_CASE_RET_STR(SHT_RELA);
        RT_CASE_RET_STR(SHT_HASH);
        RT_CASE_RET_STR(SHT_DYNAMIC);
        RT_CASE_RET_STR(SHT_NOTE);
        RT_CASE_RET_STR(SHT_NOBITS);
        RT_CASE_RET_STR(SHT_REL);
        RT_CASE_RET_STR(SHT_SHLIB);
        RT_CASE_RET_STR(SHT_DYNSYM);
        default:
            return "";
    }
}

/**
 * Gets the program header type.
 *
 * @returns Pointer to read only string.
 * @param   iType       The section type index.
 */
static const char *rtldrElfGetPhdrType(uint32_t iType)
{
    switch (iType)
    {
        RT_CASE_RET_STR(PT_NULL);
        RT_CASE_RET_STR(PT_LOAD);
        RT_CASE_RET_STR(PT_DYNAMIC);
        RT_CASE_RET_STR(PT_INTERP);
        RT_CASE_RET_STR(PT_NOTE);
        RT_CASE_RET_STR(PT_SHLIB);
        RT_CASE_RET_STR(PT_PHDR);
        RT_CASE_RET_STR(PT_TLS);
        RT_CASE_RET_STR(PT_GNU_EH_FRAME);
        RT_CASE_RET_STR(PT_GNU_STACK);
        RT_CASE_RET_STR(PT_GNU_RELRO);
        RT_CASE_RET_STR(PT_GNU_PROPERTY);
        default:
            return "";
    }
}

#endif /* LOG_ENABLED*/

/**
 * Reads in what migt be a linux kernel module signature footer.
 */
static int rtLdrELFLnxKModReadFooter(PRTLDRREADER pReader, PRTLDRLNXMODSIG pSigFooter, uint64_t *pcbFile)
{
    /*
     * Look for the linux module signature at the end of the file.
     * This should be safe to read w/o any size checking as it is smaller than the elf header.
     */
    uint64_t cbFile = pReader->pfnSize(pReader);
    *pcbFile = cbFile;

    AssertCompile(sizeof(*pSigFooter) <= sizeof(Elf32_Ehdr));
    return pReader->pfnRead(pReader, pSigFooter, sizeof(*pSigFooter), cbFile - sizeof(*pSigFooter));
}


/**
 * Check that a linux kernel module signature footer is valid.
 */
static bool rtLdrELFLnxKModIsFooterValid(PCRTLDRLNXMODSIG pSigFooter, uint64_t cbFile)
{
    if (memcmp(pSigFooter->achMagic, RTLDRLNXMODSIG_MAGIC, sizeof(pSigFooter->achMagic)) == 0)
    {
        uint32_t const cbSignature = RT_N2H_U32(pSigFooter->cbSignature);
        if (cbSignature > 32 && cbSignature + sizeof(*pSigFooter) < cbFile)
            return pSigFooter->bAlgo        == 0
                && pSigFooter->bHash        == 0
                && pSigFooter->cbSignerName == 0
                && pSigFooter->cbKeyId      == 0;
    }
    return false;
}


/**
 * Handles the linux kernel module signature part of RTLDRPROP_IS_SIGNED
 * queries.
 */
static int rtLdrELFLnxKModQueryPropIsSigned(PRTLDRREADER pReader, bool *pfRet)
{
    *pfRet = false;
    AssertReturn(pReader, VERR_INVALID_STATE);

    uint64_t       cbFile;
    RTLDRLNXMODSIG SigFooter;
    int rc = rtLdrELFLnxKModReadFooter(pReader, &SigFooter, &cbFile);
    if (RT_SUCCESS(rc))
        *pfRet = rtLdrELFLnxKModIsFooterValid(&SigFooter, cbFile);
    return rc;
}


/**
 * Handles the linux kernel module signature part of RTLDRPROP_IS_SIGNED
 * queries.
 */
static int rtLdrELFLnxKModQueryPropPkcs7SignedData(PRTLDRREADER pReader, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    AssertReturn(pReader, VERR_INVALID_STATE);

    uint64_t       cbFile;
    RTLDRLNXMODSIG SigFooter;
    int rc = rtLdrELFLnxKModReadFooter(pReader, &SigFooter, &cbFile);
    if (RT_SUCCESS(rc))
    {
        if (   rtLdrELFLnxKModIsFooterValid(&SigFooter, cbFile)
            && SigFooter.bType == RTLDRLNXMODSIG_TYPE_PKCS7)
        {
            uint32_t const cbSignature = RT_N2H_U32(SigFooter.cbSignature);
            *pcbRet = cbSignature;
            if (cbSignature <= cbBuf)
                rc = pReader->pfnRead(pReader, pvBuf, cbSignature, cbFile - sizeof(SigFooter) - cbSignature);
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
        else
            rc = VERR_NOT_FOUND;
    }
    return rc;
}


/**
 * @interface_method_impl{RTLDROPS,pfnHashImage,
 * Handles the linux kernel module signatures.}
 */
static DECLCALLBACK(int) rtldrELFLnxKModHashImage(PRTLDRMODINTERNAL pMod, RTDIGESTTYPE enmDigest, uint8_t *pabHash, size_t cbHash)
{
    PRTLDRREADER pReader = pMod->pReader;
    AssertReturn(pReader, VERR_INVALID_STATE);

    /*
     * Get the file size and subtract any linux kernel module signature from it
     * since it's not part of the hash.
     */
    uint64_t       cbFile;
    RTLDRLNXMODSIG SigFooter;
    int rc = rtLdrELFLnxKModReadFooter(pReader, &SigFooter, &cbFile);
    if (RT_SUCCESS(rc))
    {
        if (rtLdrELFLnxKModIsFooterValid(&SigFooter, cbFile))
            cbFile -= sizeof(SigFooter) + RT_N2H_U32(SigFooter.cbSignature);

        /*
         * Now hash the file.
         */
        RTCRDIGEST hDigest;
        rc = RTCrDigestCreateByType(&hDigest, enmDigest);
        if (RT_SUCCESS(rc))
        {
            uint32_t cbBuf = _64K;
            void    *pvBuf = RTMemTmpAlloc(_64K);
            void    *pvBufFree = pvBuf;
            if (!pvBuf)
            {
                cbBuf = _4K;
                pvBuf = alloca(_4K);
            }

            for (uint64_t offFile = 0; offFile < cbFile; )
            {
                uint64_t cbLeft = cbFile - offFile;
                uint32_t cbToRead = cbLeft >= cbBuf ? cbBuf : (uint32_t)cbLeft;
                rc = pReader->pfnRead(pReader, pvBuf, cbToRead, offFile);
                AssertRCBreak(rc);

                rc = RTCrDigestUpdate(hDigest, pvBuf, cbToRead);
                offFile += cbToRead;
                AssertRCBreak(rc);
            }

            RTMemTmpFree(pvBufFree);

            if (RT_SUCCESS(rc))
                rc = RTCrDigestFinal(hDigest, pabHash, cbHash);
            RTCrDigestRelease(hDigest);
        }
    }
    return rc;
}


/**
 * Open an ELF image.
 *
 * @returns iprt status code.
 * @param   pReader     The loader reader instance which will provide the raw image bits.
 * @param   fFlags      Reserved, MBZ.
 * @param   enmArch     Architecture specifier.
 * @param   phLdrMod    Where to store the handle.
 * @param   pErrInfo    Where to return extended error information. Optional.
 */
DECLHIDDEN(int) rtldrELFOpen(PRTLDRREADER pReader, uint32_t fFlags, RTLDRARCH enmArch, PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo)
{
    const char *pszLogName = pReader->pfnLogName(pReader); NOREF(pszLogName);

    /*
     * Read the ident to decide if this is 32-bit or 64-bit
     * and worth dealing with.
     */
    uint8_t e_ident[EI_NIDENT];
    int rc = pReader->pfnRead(pReader, &e_ident, sizeof(e_ident), 0);
    if (RT_FAILURE(rc))
        return rc;

    if (    e_ident[EI_MAG0] != ELFMAG0
        ||  e_ident[EI_MAG1] != ELFMAG1
        ||  e_ident[EI_MAG2] != ELFMAG2
        ||  e_ident[EI_MAG3] != ELFMAG3
        ||  (   e_ident[EI_CLASS] != ELFCLASS32
             && e_ident[EI_CLASS] != ELFCLASS64)
       )
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: Unsupported/invalid ident %.*Rhxs", pszLogName, sizeof(e_ident), e_ident);

    if (e_ident[EI_DATA] != ELFDATA2LSB)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRELF_ODD_ENDIAN,
                                   "%s: ELF endian %x is unsupported", pszLogName, e_ident[EI_DATA]);

    if (e_ident[EI_CLASS] == ELFCLASS32)
        rc = rtldrELF32Open(pReader, fFlags, enmArch, phLdrMod, pErrInfo);
    else
        rc = rtldrELF64Open(pReader, fFlags, enmArch, phLdrMod, pErrInfo);
    return rc;
}

