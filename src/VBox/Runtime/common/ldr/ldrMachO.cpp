/* $Id: ldrMachO.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * kLdr - The Module Interpreter for the MACH-O format.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
 * --------------------------------------------------------------------
 *
 * This code is based on: kLdr/kLdrModMachO.c from kStuff r113.
 *
 * Copyright (c) 2006-2013 Knut St. Osmundsen <bird-kStuff-spamix@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_LDR
#include <iprt/ldr.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/asm-mem.h>
#include <iprt/assert.h>
#include <iprt/base64.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/sha.h>
#include <iprt/crypto/digest.h>

#include <iprt/formats/mach-o.h>
#include <iprt/crypto/applecodesign.h>
#include "internal/ldr.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def RTLDRMODMACHO_STRICT
 * Define RTLDRMODMACHO_STRICT to enabled strict checks in RTLDRMODMACHO. */
#define RTLDRMODMACHO_STRICT 1
#define RTLDRMODMACHO_STRICT2

/** @def RTLDRMODMACHO_ASSERT
 * Assert that an expression is true when KLDR_STRICT is defined.
 */
#ifdef RTLDRMODMACHO_STRICT
# define RTLDRMODMACHO_ASSERT(expr)  Assert(expr)
#else
# define RTLDRMODMACHO_ASSERT(expr)  do {} while (0)
#endif

/** @def RTLDRMODMACHO_CHECK_RETURN
 * Checks that an expression is true and return if it isn't.
 * This is a debug aid.
 */
#ifdef RTLDRMODMACHO_STRICT2
# define RTLDRMODMACHO_CHECK_RETURN(expr, rc)  AssertReturn(expr, rc)
#else
# define RTLDRMODMACHO_CHECK_RETURN(expr, rc)  do { if (RT_LIKELY(expr)) {/* likely */ } else return (rc); } while (0)
#endif

/** @def RTLDRMODMACHO_CHECK_MSG_RETURN
 * Checks that an expression is true and return if it isn't.
 * This is a debug aid.
 */
#ifdef RTLDRMODMACHO_STRICT2
# define RTLDRMODMACHO_CHECK_MSG_RETURN(expr, msgargs, rc)  AssertMsgReturn(expr, msgargs, rc)
#else
# define RTLDRMODMACHO_CHECK_MSG_RETURN(expr, msgargs, rc)  do { if (RT_LIKELY(expr)) {/* likely */ } else return (rc); } while (0)
#endif

/** @def RTLDRMODMACHO_CHECK_RETURN
 * Checks that an expression is true and return if it isn't.
 * This is a debug aid.
 */
#ifdef RTLDRMODMACHO_STRICT2
# define RTLDRMODMACHO_FAILED_RETURN(rc)  AssertFailedReturn(rc)
#else
# define RTLDRMODMACHO_FAILED_RETURN(rc)  return (rc)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Mach-O section details.
 */
typedef struct RTLDRMODMACHOSECT
{
    /** The size of the section (in bytes). */
    RTLDRADDR               cb;
    /** The link address of this section. */
    RTLDRADDR               LinkAddress;
    /** The RVA of this section. */
    RTLDRADDR               RVA;
    /** The file offset of this section.
     * This is -1 if the section doesn't have a file backing. */
    RTFOFF                  offFile;
    /** The number of fixups. */
    uint32_t                cFixups;
    /** The array of fixups. (lazy loaded) */
    macho_relocation_union_t *paFixups;
    /** Array of virgin data running parallel to paFixups */
    PRTUINT64U              pauFixupVirginData;
    /** The file offset of the fixups for this section.
     * This is -1 if the section doesn't have any fixups. */
    RTFOFF                  offFixups;
    /** Mach-O section flags. */
    uint32_t                fFlags;
    /** kLdr segment index. */
    uint32_t                iSegment;
    /** Pointer to the Mach-O section structure. */
    void                   *pvMachoSection;
} RTLDRMODMACHOSECT, *PRTLDRMODMACHOSECT;

/**
 * Extra per-segment info.
 *
 * This is corresponds to a kLdr segment, not a Mach-O segment!
 */
typedef struct RTLDRMODMACHOSEG
{
    /** Common segment info. */
    RTLDRSEG                SegInfo;

    /** The orignal segment number (in case we had to resort it). */
    uint32_t                iOrgSegNo;
    /** The number of sections in the segment. */
    uint32_t                cSections;
    /** Pointer to the sections belonging to this segment.
     * The array resides in the big memory chunk allocated for
     * the module handle, so it doesn't need freeing. */
    PRTLDRMODMACHOSECT      paSections;

} RTLDRMODMACHOSEG, *PRTLDRMODMACHOSEG;

/**
 * Instance data for the Mach-O MH_OBJECT module interpreter.
 * @todo interpret the other MH_* formats.
 */
typedef struct RTLDRMODMACHO
{
    /** Core module structure. */
    RTLDRMODINTERNAL        Core;

    /** The minium cpu this module was built for.
     * This might not be accurate, so use kLdrModCanExecuteOn() to check. */
    RTLDRCPU                enmCpu;
    /** The number of segments in the module. */
    uint32_t                cSegments;

    /** Pointer to the RDR file mapping of the raw file bits. NULL if not mapped. */
    const void             *pvBits;
    /** Pointer to the user mapping. */
    void                   *pvMapping;
    /** The module open flags. */
    uint32_t                fOpenFlags;

    /** The offset of the image. (FAT fun.) */
    RTFOFF                  offImage;
    /** The link address. */
    RTLDRADDR               LinkAddress;
    /** The size of the mapped image. */
    RTLDRADDR               cbImage;
    /** Whether we're capable of loading the image. */
    bool                    fCanLoad;
    /** Whether we're creating a global offset table segment.
     * This dependes on the cputype and image type. */
    bool                    fMakeGot;
    /** The size of a indirect GOT jump stub entry.
     * This is 0 if not needed. */
    uint32_t                cbJmpStub;
    /** Effective file type.  If the original was a MH_OBJECT file, the
     * corresponding MH_DSYM needs the segment translation of a MH_OBJECT too.
     * The MH_DSYM normally has a separate __DWARF segment, but this is
     * automatically skipped during the transation. */
    uint32_t                uEffFileType;
    /** Pointer to the load commands. (endian converted) */
    uint8_t                *pbLoadCommands;
    /** The Mach-O header. (endian converted)
     * @remark The reserved field is only valid for real 64-bit headers. */
    mach_header_64_t        Hdr;

    /** The offset of the symbol table. */
    RTFOFF                  offSymbols;
    /** The number of symbols. */
    uint32_t                cSymbols;
    /** The pointer to the loaded symbol table. */
    void                   *pvaSymbols;
    /** The offset of the string table. */
    RTFOFF                  offStrings;
    /** The size of the of the string table. */
    uint32_t                cchStrings;
    /** Pointer to the loaded string table. */
    char                   *pchStrings;
    /** Pointer to the dynamic symbol table command if present. */
    dysymtab_command_t     *pDySymTab;
    /** The indirect symbol table (size given by pDySymTab->nindirectsymb).
     * @remarks Host endian. */
    uint32_t               *paidxIndirectSymbols;
    /** Dynamic relocations, first pDySymTab->nextrel external relocs followed by
     *  pDySymTab->nlocrel local ones. */
    macho_relocation_union_t *paRelocations;
    /** Array of virgin data running parallel to paRelocations */
    PRTUINT64U              pauRelocationsVirginData;

    /** The image UUID, all zeros if not found. */
    uint8_t                 abImageUuid[16];

    /** The code signature offset.   */
    uint32_t                offCodeSignature;
    /** The code signature size (0 if not signed). */
    uint32_t                cbCodeSignature;
    /** Pointer to the code signature blob if loaded. */
    union
    {
        uint8_t                *pb;
        PCRTCRAPLCSSUPERBLOB    pSuper;
    }                       PtrCodeSignature;
    /** File offset of segment 0 (relative to Mach-O header). */
    uint64_t                offSeg0ForCodeSign;
    /** File size of segment 0. */
    uint64_t                cbSeg0ForCodeSign;
    /** Segment 0 flags. */
    uint64_t                fSeg0ForCodeSign;

    /** The RVA of the Global Offset Table. */
    RTLDRADDR               GotRVA;
    /** The RVA of the indirect GOT jump stubs.  */
    RTLDRADDR               JmpStubsRVA;

    /** The number of sections. */
    uint32_t                cSections;
    /** Pointer to the section array running in parallel to the Mach-O one. */
    PRTLDRMODMACHOSECT      paSections;

    /** Array of segments parallel to the one in KLDRMOD. */
    RTLDRMODMACHOSEG        aSegments[1];
} RTLDRMODMACHO;
/** Pointer instance data for an Mach-O module. */
typedef RTLDRMODMACHO *PRTLDRMODMACHO;

/**
 * Code directory data.
 */
typedef struct RTLDRMACHCODEDIR
{
    PCRTCRAPLCSCODEDIRECTORY    pCodeDir;
    /** The slot type. */
    uint32_t                    uSlot;
    /** The naturalized size. */
    uint32_t                    cb;
    /** The digest type. */
    RTDIGESTTYPE                enmDigest;
} RTLDRMACHCODEDIR;
/** Pointer to code directory data. */
typedef RTLDRMACHCODEDIR *PRTLDRMACHCODEDIR;

/**
 * Decoded apple Mach-O signature data.
 * @note The raw signature data lives in RTLDRMODMACHO::PtrCodeSignature.
 */
typedef struct RTLDRMACHOSIGNATURE
{
    /** Number of code directory slots. */
    uint32_t                    cCodeDirs;
    /** Code directories. */
    RTLDRMACHCODEDIR            aCodeDirs[6];

    /** The index of the PKCS#7 slot. */
    uint32_t                    idxPkcs7;
    /** The size of the PKCS#7 data. */
    uint32_t                    cbPkcs7;
    /** Pointer to the PKCS#7 data. */
    uint8_t const              *pbPkcs7;
    /** Parsed PKCS#7 data. */
    RTCRPKCS7CONTENTINFO        ContentInfo;
    /** Pointer to the decoded SignedData inside the ContentInfo member. */
    PRTCRPKCS7SIGNEDDATA        pSignedData;
} RTLDRMACHOSIGNATURE;
/** Pointer to decoded apple code signing data. */
typedef RTLDRMACHOSIGNATURE *PRTLDRMACHOSIGNATURE;



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#if 0
static int32_t kldrModMachONumberOfImports(PRTLDRMODINTERNAL pMod, const void *pvBits);
#endif
static DECLCALLBACK(int) rtldrMachO_RelocateBits(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR NewBaseAddress,
                                                 RTUINTPTR OldBaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser);


static int  kldrModMachOPreParseLoadCommands(uint8_t *pbLoadCommands, const mach_header_32_t *pHdr, PRTLDRREADER pRdr, RTFOFF offImage,
                                             uint32_t fOpenFlags, uint32_t *pcSegments, uint32_t *pcSections, uint32_t *pcbStringPool,
                                             bool *pfCanLoad, PRTLDRADDR pLinkAddress, uint8_t *puEffFileType, PRTERRINFO pErrInfo);
static int  kldrModMachOParseLoadCommands(PRTLDRMODMACHO pThis, char *pbStringPool, uint32_t cbStringPool);

static int  kldrModMachOLoadObjSymTab(PRTLDRMODMACHO pThis);
static int  kldrModMachOLoadFixups(PRTLDRMODMACHO pThis, RTFOFF offFixups, uint32_t cFixups, macho_relocation_union_t **ppaFixups);

static int  kldrModMachODoQuerySymbol32Bit(PRTLDRMODMACHO pThis, const macho_nlist_32_t *paSyms, uint32_t cSyms, const char *pchStrings,
                                           uint32_t cchStrings, RTLDRADDR BaseAddress, uint32_t iSymbol, const char *pchSymbol,
                                           uint32_t cchSymbol, PRTLDRADDR puValue, uint32_t *pfKind);
static int  kldrModMachODoQuerySymbol64Bit(PRTLDRMODMACHO pThis, const macho_nlist_64_t *paSyms, uint32_t cSyms, const char *pchStrings,
                                           uint32_t cchStrings, RTLDRADDR BaseAddress, uint32_t iSymbol, const char *pchSymbol,
                                           uint32_t cchSymbol, PRTLDRADDR puValue, uint32_t *pfKind);
static int  kldrModMachODoEnumSymbols32Bit(PRTLDRMODMACHO pThis, const macho_nlist_32_t *paSyms, uint32_t cSyms,
                                           const char *pchStrings, uint32_t cchStrings, RTLDRADDR BaseAddress,
                                           uint32_t fFlags, PFNRTLDRENUMSYMS pfnCallback, void *pvUser);
static int  kldrModMachODoEnumSymbols64Bit(PRTLDRMODMACHO pThis, const macho_nlist_64_t *paSyms, uint32_t cSyms,
                                           const char *pchStrings, uint32_t cchStrings, RTLDRADDR BaseAddress,
                                           uint32_t fFlags, PFNRTLDRENUMSYMS pfnCallback, void *pvUser);
static int  kldrModMachOObjDoImports(PRTLDRMODMACHO pThis, RTLDRADDR BaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser);
static int  kldrModMachOObjDoFixups(PRTLDRMODMACHO pThis, void *pvMapping, RTLDRADDR NewBaseAddress);
static int  kldrModMachOApplyFixupsGeneric32Bit(PRTLDRMODMACHO pThis, uint8_t *pbSectBits, size_t cbSectBits, RTLDRADDR uBitsRva,
                                                RTLDRADDR uBitsLinkAddr, const macho_relocation_union_t *paFixups,
                                                const uint32_t cFixups, PCRTUINT64U const pauVirginData,
                                                macho_nlist_32_t *paSyms, uint32_t cSyms, RTLDRADDR NewBaseAddress);
static int  kldrModMachOApplyFixupsAMD64(PRTLDRMODMACHO pThis, uint8_t *pbSectBits, size_t cbSectBits, RTLDRADDR uBitsRva,
                                         const macho_relocation_union_t *paFixups,
                                         const uint32_t cFixups, PCRTUINT64U const pauVirginData,
                                         macho_nlist_64_t *paSyms, uint32_t cSyms, RTLDRADDR NewBaseAddress);

static int  kldrModMachOMakeGOT(PRTLDRMODMACHO pThis, void *pvBits, RTLDRADDR NewBaseAddress);

/*static int  kldrModMachODoFixups(PRTLDRMODMACHO pThis, void *pvMapping, RTLDRADDR NewBaseAddress, RTLDRADDR OldBaseAddress);
static int  kldrModMachODoImports(PRTLDRMODMACHO pThis, void *pvMapping, PFNRTLDRIMPORT pfnGetImport, void *pvUser);*/



/**
 * Separate function for reading creating the Mach-O module instance to
 * simplify cleanup on failure.
 */
static int kldrModMachODoCreate(PRTLDRREADER pRdr, RTFOFF offImage, uint32_t fOpenFlags,
                                PRTLDRMODMACHO *ppModMachO, PRTERRINFO pErrInfo)
{
    *ppModMachO = NULL;

    /*
     * Read the Mach-O header.
     */
    union
    {
        mach_header_32_t    Hdr32;
        mach_header_64_t    Hdr64;
    } s;
    Assert(&s.Hdr32.magic == &s.Hdr64.magic);
    Assert(&s.Hdr32.flags == &s.Hdr64.flags);
    int rc = pRdr->pfnRead(pRdr, &s, sizeof(s), offImage);
    if (rc)
        return RTErrInfoSetF(pErrInfo, rc, "Error reading Mach-O header at %RTfoff: %Rrc", offImage, rc);
    if (    s.Hdr32.magic != IMAGE_MACHO32_SIGNATURE
        &&  s.Hdr32.magic != IMAGE_MACHO64_SIGNATURE)
    {
        if (    s.Hdr32.magic == IMAGE_MACHO32_SIGNATURE_OE
            ||  s.Hdr32.magic == IMAGE_MACHO64_SIGNATURE_OE)
            return VERR_LDRMACHO_OTHER_ENDIAN_NOT_SUPPORTED;
        return VERR_INVALID_EXE_SIGNATURE;
    }

    /* sanity checks. */
    if (    s.Hdr32.sizeofcmds > pRdr->pfnSize(pRdr) - sizeof(mach_header_32_t)
        ||  s.Hdr32.sizeofcmds < sizeof(load_command_t) * s.Hdr32.ncmds
        ||  (s.Hdr32.flags & ~MH_VALID_FLAGS))
        return VERR_LDRMACHO_BAD_HEADER;

    bool fMakeGot;
    uint8_t cbJmpStub;
    switch (s.Hdr32.cputype)
    {
        case CPU_TYPE_X86:
            fMakeGot = false;
            cbJmpStub = 0;
            break;
        case CPU_TYPE_X86_64:
            fMakeGot = s.Hdr32.filetype == MH_OBJECT || s.Hdr32.filetype == MH_KEXT_BUNDLE;
            cbJmpStub = fMakeGot ? 8 : 0;
            break;
        case CPU_TYPE_ARM64:
            fMakeGot = s.Hdr32.filetype == MH_OBJECT || s.Hdr32.filetype == MH_KEXT_BUNDLE;
            cbJmpStub = fMakeGot ? 8 : 0; /** @todo Not sure if this is right. Need to expore ARM64/MachO a bit more... */
            break;
        default:
            return VERR_LDRMACHO_UNSUPPORTED_MACHINE;
    }

    if (   s.Hdr32.filetype != MH_OBJECT
        && s.Hdr32.filetype != MH_EXECUTE
        && s.Hdr32.filetype != MH_DYLIB
        && s.Hdr32.filetype != MH_BUNDLE
        && s.Hdr32.filetype != MH_DSYM
        && s.Hdr32.filetype != MH_KEXT_BUNDLE)
        return VERR_LDRMACHO_UNSUPPORTED_FILE_TYPE;

    /*
     * Read and pre-parse the load commands to figure out how many segments we'll be needing.
     */
    uint8_t *pbLoadCommands = (uint8_t *)RTMemAlloc(s.Hdr32.sizeofcmds);
    if (!pbLoadCommands)
        return VERR_NO_MEMORY;
    rc = pRdr->pfnRead(pRdr, pbLoadCommands, s.Hdr32.sizeofcmds,
                          s.Hdr32.magic == IMAGE_MACHO32_SIGNATURE
                       || s.Hdr32.magic == IMAGE_MACHO32_SIGNATURE_OE
                       ? sizeof(mach_header_32_t) + offImage
                       : sizeof(mach_header_64_t) + offImage);

    uint32_t    cSegments    = 0;
    uint32_t    cSections    = 0;
    uint32_t    cbStringPool = 0;
    bool        fCanLoad     = true;
    RTLDRADDR   LinkAddress  = NIL_RTLDRADDR;
    uint8_t     uEffFileType = 0;
    if (RT_SUCCESS(rc))
        rc = kldrModMachOPreParseLoadCommands(pbLoadCommands, &s.Hdr32, pRdr, offImage, fOpenFlags,
                                              &cSegments, &cSections, &cbStringPool, &fCanLoad, &LinkAddress, &uEffFileType,
                                              pErrInfo);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pbLoadCommands);
        return rc;
    }
    cSegments += fMakeGot;


    /*
     * Calc the instance size, allocate and initialize it.
     */
    size_t const cbModAndSegs = RT_ALIGN_Z(RT_UOFFSETOF_DYN(RTLDRMODMACHO, aSegments[cSegments])
                                           + sizeof(RTLDRMODMACHOSECT) * cSections, 16);
    PRTLDRMODMACHO pThis = (PRTLDRMODMACHO)RTMemAlloc(cbModAndSegs + cbStringPool);
    if (!pThis)
        return VERR_NO_MEMORY;
    *ppModMachO = pThis;
    pThis->pbLoadCommands = pbLoadCommands;
    pThis->offImage = offImage;

    /* Core & CPU.*/
    pThis->Core.u32Magic  = 0;          /* set by caller */
    pThis->Core.eState    = LDR_STATE_OPENED;
    pThis->Core.pOps      = NULL;       /* set by caller. */
    pThis->Core.pReader   = pRdr;
    switch (s.Hdr32.cputype)
    {
        case CPU_TYPE_X86:
            pThis->Core.enmArch   = RTLDRARCH_X86_32;
            pThis->Core.enmEndian = RTLDRENDIAN_LITTLE;
            switch (s.Hdr32.cpusubtype)
            {
                case CPU_SUBTYPE_I386_ALL: /* == CPU_SUBTYPE_386 */
                    pThis->enmCpu = RTLDRCPU_X86_32_BLEND;
                    break;
                case CPU_SUBTYPE_486:
                    pThis->enmCpu = RTLDRCPU_I486;
                    break;
                case CPU_SUBTYPE_486SX:
                    pThis->enmCpu = RTLDRCPU_I486SX;
                    break;
                case CPU_SUBTYPE_PENT: /* == CPU_SUBTYPE_586 */
                    pThis->enmCpu = RTLDRCPU_I586;
                    break;
                case CPU_SUBTYPE_PENTPRO:
                case CPU_SUBTYPE_PENTII_M3:
                case CPU_SUBTYPE_PENTII_M5:
                case CPU_SUBTYPE_CELERON:
                case CPU_SUBTYPE_CELERON_MOBILE:
                case CPU_SUBTYPE_PENTIUM_3:
                case CPU_SUBTYPE_PENTIUM_3_M:
                case CPU_SUBTYPE_PENTIUM_3_XEON:
                    pThis->enmCpu = RTLDRCPU_I686;
                    break;
                case CPU_SUBTYPE_PENTIUM_M:
                case CPU_SUBTYPE_PENTIUM_4:
                case CPU_SUBTYPE_PENTIUM_4_M:
                case CPU_SUBTYPE_XEON:
                case CPU_SUBTYPE_XEON_MP:
                    pThis->enmCpu = RTLDRCPU_P4;
                    break;

                default:
                    /* Hack for kextutil output. */
                    if (   s.Hdr32.cpusubtype == 0
                        && s.Hdr32.filetype   == MH_OBJECT)
                        break;
                    return VERR_LDRMACHO_UNSUPPORTED_MACHINE;
            }
            break;

        case CPU_TYPE_X86_64:
            pThis->Core.enmArch   = RTLDRARCH_AMD64;
            pThis->Core.enmEndian = RTLDRENDIAN_LITTLE;
            switch (s.Hdr32.cpusubtype & ~CPU_SUBTYPE_MASK)
            {
                case CPU_SUBTYPE_X86_64_ALL: pThis->enmCpu = RTLDRCPU_AMD64_BLEND; break;
                default:
                    return VERR_LDRMACHO_UNSUPPORTED_MACHINE;
            }
            break;

        case CPU_TYPE_ARM64:
            pThis->Core.enmArch   = RTLDRARCH_ARM64;
            pThis->Core.enmEndian = RTLDRENDIAN_LITTLE;
            switch (s.Hdr32.cpusubtype & ~CPU_SUBTYPE_MASK)
            {
                case CPU_SUBTYPE_ARM64_ALL: pThis->enmCpu = RTLDRCPU_ARM64_BLEND; break;
                case CPU_SUBTYPE_ARM64_V8:  pThis->enmCpu = RTLDRCPU_ARM64_V8; break;
                case CPU_SUBTYPE_ARM64E:    pThis->enmCpu = RTLDRCPU_ARM64E; break;
                default:
                    return VERR_LDRMACHO_UNSUPPORTED_MACHINE;
            }
            break;

        default:
            return VERR_LDRMACHO_UNSUPPORTED_MACHINE;
    }

    pThis->Core.enmFormat = RTLDRFMT_MACHO;
    switch (s.Hdr32.filetype)
    {
        case MH_OBJECT:     pThis->Core.enmType = RTLDRTYPE_OBJECT; break;
        case MH_EXECUTE:    pThis->Core.enmType = RTLDRTYPE_EXECUTABLE_FIXED; break;
        case MH_DYLIB:      pThis->Core.enmType = RTLDRTYPE_SHARED_LIBRARY_RELOCATABLE; break;
        case MH_BUNDLE:     pThis->Core.enmType = RTLDRTYPE_SHARED_LIBRARY_RELOCATABLE; break;
        case MH_KEXT_BUNDLE:pThis->Core.enmType = RTLDRTYPE_SHARED_LIBRARY_RELOCATABLE; break;
        case MH_DSYM:       pThis->Core.enmType = RTLDRTYPE_DEBUG_INFO; break;
        default:
            return VERR_LDRMACHO_UNSUPPORTED_FILE_TYPE;
    }

    /* RTLDRMODMACHO */
    pThis->cSegments = cSegments;
    pThis->pvBits = NULL;
    pThis->pvMapping = NULL;
    pThis->fOpenFlags = fOpenFlags;
    pThis->Hdr = s.Hdr64;
    if (    s.Hdr32.magic == IMAGE_MACHO32_SIGNATURE
        ||  s.Hdr32.magic == IMAGE_MACHO32_SIGNATURE_OE)
        pThis->Hdr.reserved = 0;
    pThis->LinkAddress = LinkAddress;
    pThis->cbImage = 0;
    pThis->fCanLoad = fCanLoad;
    pThis->fMakeGot = fMakeGot;
    pThis->cbJmpStub = cbJmpStub;
    pThis->uEffFileType = uEffFileType;
    pThis->offSymbols = 0;
    pThis->cSymbols = 0;
    pThis->pvaSymbols = NULL;
    pThis->pDySymTab = NULL;
    pThis->paRelocations = NULL;
    pThis->pauRelocationsVirginData = NULL;
    pThis->paidxIndirectSymbols = NULL;
    pThis->offStrings = 0;
    pThis->cchStrings = 0;
    pThis->pchStrings = NULL;
    memset(pThis->abImageUuid, 0, sizeof(pThis->abImageUuid));
    pThis->offCodeSignature = 0;
    pThis->cbCodeSignature = 0;
    pThis->PtrCodeSignature.pb = NULL;
    pThis->GotRVA = NIL_RTLDRADDR;
    pThis->JmpStubsRVA = NIL_RTLDRADDR;
    pThis->cSections = cSections;
    pThis->paSections = (PRTLDRMODMACHOSECT)&pThis->aSegments[pThis->cSegments];

    /*
     * Setup the KLDRMOD segment array.
     */
    rc = kldrModMachOParseLoadCommands(pThis, (char *)pThis + cbModAndSegs, cbStringPool);

    /*
     * We're done.
     */
    return rc;
}


/**
 * Converts, validates and preparses the load commands before we carve
 * out the module instance.
 *
 * The conversion that's preformed is format endian to host endian.  The
 * preparsing has to do with segment counting, section counting and string pool
 * sizing.
 *
 * Segment are created in two different ways, depending on the file type.
 *
 * For object files there is only one segment command without a given segment
 * name. The sections inside that segment have different segment names and are
 * not sorted by their segname attribute.  We create one segment for each
 * section, with the segment name being 'segname.sectname' in order to hopefully
 * keep the names unique.  Debug sections does not get segments.
 *
 * For non-object files, one kLdr segment is created for each Mach-O segment.
 * Debug segments is not exposed by kLdr via the kLdr segment table, but via the
 * debug enumeration callback API.
 *
 * @returns IPRT status code.
 * @param   pbLoadCommands  The load commands to parse.
 * @param   pHdr            The header.
 * @param   pRdr            The file reader.
 * @param   offImage        The image header (FAT fun).
 * @param   fOpenFlags      RTLDR_O_XXX.
 * @param   pcSegments      Where to store the segment count.
 * @param   pcSections      Where to store the section count.
 * @param   pcbStringPool   Where to store the string pool size.
 * @param   pfCanLoad       Where to store the can-load-image indicator.
 * @param   pLinkAddress    Where to store the image link address (i.e. the
 *                          lowest segment address).
 * @param   puEffFileType   Where to store the effective file type.
 * @param   pErrInfo        Where to return additional error info. Optional.
 */
static int kldrModMachOPreParseLoadCommands(uint8_t *pbLoadCommands, const mach_header_32_t *pHdr, PRTLDRREADER pRdr,
                                            RTFOFF offImage, uint32_t fOpenFlags, uint32_t *pcSegments, uint32_t *pcSections,
                                            uint32_t *pcbStringPool, bool *pfCanLoad, PRTLDRADDR pLinkAddress,
                                            uint8_t *puEffFileType, PRTERRINFO pErrInfo)
{
    union
    {
        uint8_t              *pb;
        load_command_t       *pLoadCmd;
        segment_command_32_t *pSeg32;
        segment_command_64_t *pSeg64;
        thread_command_t     *pThread;
        symtab_command_t     *pSymTab;
        dysymtab_command_t   *pDySymTab;
        uuid_command_t       *pUuid;
    } u;
    const uint64_t cbFile         = pRdr->pfnSize(pRdr) - offImage;
    int const      fConvertEndian = pHdr->magic == IMAGE_MACHO32_SIGNATURE_OE
                                 || pHdr->magic == IMAGE_MACHO64_SIGNATURE_OE;
    uint32_t cSegments = 0;
    uint32_t cSections = 0;
    size_t cbStringPool = 0;
    uint32_t cLeft = pHdr->ncmds;
    uint32_t cbLeft = pHdr->sizeofcmds;
    uint8_t *pb = pbLoadCommands;
    int cSegmentCommands = 0;
    int cSymbolTabs = 0;
    uint32_t cSymbols = 0; /* Copy of u.pSymTab->nsyms. */
    uint32_t cDySymbolTabs = 0;
    bool fDySymbolTabWithRelocs = false;
    uint32_t cSectionsWithRelocs = 0;
    uint8_t uEffFileType = *puEffFileType = pHdr->filetype;

    *pcSegments = 0;
    *pcSections = 0;
    *pcbStringPool = 0;
    *pfCanLoad = true;
    *pLinkAddress = ~(RTLDRADDR)0;

    while (cLeft-- > 0)
    {
        u.pb = pb;

        /*
         * Convert and validate command header.
         */
        RTLDRMODMACHO_CHECK_RETURN(cbLeft >= sizeof(load_command_t), VERR_LDRMACHO_BAD_LOAD_COMMAND);
        if (fConvertEndian)
        {
            u.pLoadCmd->cmd = RT_BSWAP_U32(u.pLoadCmd->cmd);
            u.pLoadCmd->cmdsize = RT_BSWAP_U32(u.pLoadCmd->cmdsize);
        }
        RTLDRMODMACHO_CHECK_RETURN(u.pLoadCmd->cmdsize <= cbLeft, VERR_LDRMACHO_BAD_LOAD_COMMAND);
        cbLeft -= u.pLoadCmd->cmdsize;
        pb += u.pLoadCmd->cmdsize;

        /*
         * Segment macros for avoiding code duplication.
         */
        /* Validation code shared with the 64-bit variant. */
        #define VALIDATE_AND_ADD_SEGMENT(a_cBits) \
            do { \
                bool fSkipSeg = !strcmp(pSrcSeg->segname, "__DWARF")   /* Note: Not for non-object files. */ \
                              || (   !strcmp(pSrcSeg->segname, "__CTF") /* Their CTF tool did/does weird things, */ \
                                  && pSrcSeg->vmsize == 0)                   /* overlapping vmaddr and zero vmsize. */ \
                              || (cSectionsLeft > 0 && (pFirstSect->flags & S_ATTR_DEBUG)); \
                \
                /* MH_DSYM files for MH_OBJECT files must have MH_OBJECT segment translation. */ \
                if (   uEffFileType == MH_DSYM \
                    && cSegmentCommands == 0 \
                    && pSrcSeg->segname[0] == '\0') \
                    *puEffFileType = uEffFileType = MH_OBJECT; \
                \
                RTLDRMODMACHO_CHECK_RETURN(   pSrcSeg->filesize == 0 \
                                          || (   pSrcSeg->fileoff <= cbFile \
                                              && (uint64_t)pSrcSeg->fileoff + pSrcSeg->filesize <= cbFile), \
                                          VERR_LDRMACHO_BAD_LOAD_COMMAND); \
                RTLDRMODMACHO_CHECK_RETURN(   pSrcSeg->filesize <= pSrcSeg->vmsize \
                                          || (fSkipSeg && !strcmp(pSrcSeg->segname, "__CTF") /* see above */), \
                                          VERR_LDRMACHO_BAD_LOAD_COMMAND); \
                RTLDRMODMACHO_CHECK_RETURN(!(~pSrcSeg->maxprot & pSrcSeg->initprot), \
                                          VERR_LDRMACHO_BAD_LOAD_COMMAND); \
                RTLDRMODMACHO_CHECK_MSG_RETURN(!(pSrcSeg->flags & ~(SG_HIGHVM | SG_FVMLIB | SG_NORELOC | SG_PROTECTED_VERSION_1 | SG_READ_ONLY)), \
                                               ("flags=%#x %s\n", pSrcSeg->flags, pSrcSeg->segname), \
                                               VERR_LDRMACHO_BAD_LOAD_COMMAND); \
                RTLDRMODMACHO_CHECK_RETURN(   pSrcSeg->nsects * sizeof(section_##a_cBits##_t) \
                                          <= u.pLoadCmd->cmdsize - sizeof(segment_command_##a_cBits##_t), \
                                          VERR_LDRMACHO_BAD_LOAD_COMMAND); \
                RTLDRMODMACHO_CHECK_RETURN(   uEffFileType != MH_OBJECT \
                                          || cSegmentCommands == 0 \
                                          || (   cSegmentCommands == 1 \
                                              && uEffFileType == MH_OBJECT \
                                              && pHdr->filetype == MH_DSYM \
                                              && fSkipSeg), \
                                          VERR_LDRMACHO_BAD_OBJECT_FILE); \
                cSegmentCommands++; \
                \
                /* Add the segment, if not object file. */ \
                if (!fSkipSeg && uEffFileType != MH_OBJECT) \
                { \
                    cbStringPool += RTStrNLen(&pSrcSeg->segname[0], sizeof(pSrcSeg->segname)) + 1; \
                    cSegments++; \
                    if (cSegments == 1) /* The link address is set by the first segment. */  \
                        *pLinkAddress = pSrcSeg->vmaddr; \
                } \
            } while (0)


        /* Validation code shared with the 64-bit variant. */
        #define VALIDATE_AND_ADD_SECTION(a_cBits) \
            do { \
                int fFileBits; \
                \
                /* validate */ \
                if (uEffFileType != MH_OBJECT) \
                    RTLDRMODMACHO_CHECK_RETURN(!strcmp(pSect->segname, pSrcSeg->segname),\
                                              VERR_LDRMACHO_BAD_SECTION); \
                \
                switch (pSect->flags & SECTION_TYPE) \
                { \
                    case S_ZEROFILL: \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved1, VERR_LDRMACHO_BAD_SECTION); \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved2, VERR_LDRMACHO_BAD_SECTION); \
                        fFileBits = 0; \
                        break; \
                    case S_REGULAR: \
                    case S_CSTRING_LITERALS: \
                    case S_COALESCED: \
                    case S_4BYTE_LITERALS: \
                    case S_8BYTE_LITERALS: \
                    case S_16BYTE_LITERALS: \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved1, VERR_LDRMACHO_BAD_SECTION); \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved2, VERR_LDRMACHO_BAD_SECTION); \
                        fFileBits = 1; \
                        break; \
                    \
                    case S_SYMBOL_STUBS: \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved1, VERR_LDRMACHO_BAD_SECTION); \
                        /* reserved2 == stub size. 0 has been seen (corecrypto.kext) */ \
                        RTLDRMODMACHO_CHECK_RETURN(pSect->reserved2 < 64, VERR_LDRMACHO_BAD_SECTION); \
                        fFileBits = 1; \
                        break; \
                    \
                    case S_NON_LAZY_SYMBOL_POINTERS: \
                    case S_LAZY_SYMBOL_POINTERS: \
                    case S_LAZY_DYLIB_SYMBOL_POINTERS: \
                        /* (reserved 1 = is indirect symbol table index) */ \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved2, VERR_LDRMACHO_BAD_SECTION); \
                        Log(("ldrMachO: Can't load because of section flags: %#x\n", pSect->flags & SECTION_TYPE)); \
                        *pfCanLoad = false; \
                        fFileBits = -1; /* __DATA.__got in the 64-bit mach_kernel has bits, any things without bits? */ \
                        break; \
                    \
                    case S_MOD_INIT_FUNC_POINTERS: \
                        /** @todo this requires a query API or flag... (e.g. C++ constructors) */ \
                        RTLDRMODMACHO_CHECK_RETURN(fOpenFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION), \
                                                  VERR_LDRMACHO_UNSUPPORTED_INIT_SECTION); \
                        RT_FALL_THRU(); \
                    case S_MOD_TERM_FUNC_POINTERS: \
                        /** @todo this requires a query API or flag... (e.g. C++ destructors) */ \
                        RTLDRMODMACHO_CHECK_RETURN(fOpenFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION), \
                                                  VERR_LDRMACHO_UNSUPPORTED_TERM_SECTION); \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved1, VERR_LDRMACHO_BAD_SECTION); \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved2, VERR_LDRMACHO_BAD_SECTION); \
                        fFileBits = 1; \
                        break; /* ignored */ \
                    \
                    case S_LITERAL_POINTERS: \
                    case S_DTRACE_DOF: \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved1, VERR_LDRMACHO_BAD_SECTION); \
                        RTLDRMODMACHO_CHECK_RETURN(!pSect->reserved2, VERR_LDRMACHO_BAD_SECTION); \
                        fFileBits = 1; \
                        break; \
                    \
                    case S_INTERPOSING: \
                    case S_GB_ZEROFILL: \
                        RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_UNSUPPORTED_SECTION); \
                    \
                    default: \
                        RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_UNKNOWN_SECTION); \
                } \
                RTLDRMODMACHO_CHECK_RETURN(!(pSect->flags & ~(  S_ATTR_PURE_INSTRUCTIONS | S_ATTR_NO_TOC | S_ATTR_STRIP_STATIC_SYMS \
                                                             | S_ATTR_NO_DEAD_STRIP | S_ATTR_LIVE_SUPPORT | S_ATTR_SELF_MODIFYING_CODE \
                                                             | S_ATTR_DEBUG | S_ATTR_SOME_INSTRUCTIONS | S_ATTR_EXT_RELOC \
                                                             | S_ATTR_LOC_RELOC | SECTION_TYPE)), \
                                          VERR_LDRMACHO_BAD_SECTION); \
                RTLDRMODMACHO_CHECK_RETURN((pSect->flags & S_ATTR_DEBUG) == (pFirstSect->flags & S_ATTR_DEBUG), \
                                          VERR_LDRMACHO_MIXED_DEBUG_SECTION_FLAGS); \
                \
                RTLDRMODMACHO_CHECK_RETURN(pSect->addr - pSrcSeg->vmaddr <= pSrcSeg->vmsize, \
                                          VERR_LDRMACHO_BAD_SECTION); \
                RTLDRMODMACHO_CHECK_RETURN(   pSect->addr - pSrcSeg->vmaddr + pSect->size <= pSrcSeg->vmsize \
                                          || !strcmp(pSrcSeg->segname, "__CTF") /* see above */, \
                                          VERR_LDRMACHO_BAD_SECTION); \
                RTLDRMODMACHO_CHECK_RETURN(pSect->align < 31, \
                                          VERR_LDRMACHO_BAD_SECTION); \
                /* Workaround for buggy ld64 (or as, llvm, ++) that produces a misaligned __TEXT.__unwind_info. */ \
                /* Seen: pSect->align = 4, pSect->addr = 0x5ebe14.  Just adjust the alignment down. */ \
                if (   ((RT_BIT_32(pSect->align) - UINT32_C(1)) & pSect->addr) \
                    && pSect->align == 4 \
                    && strcmp(pSect->sectname, "__unwind_info") == 0) \
                    pSect->align = 2; \
                RTLDRMODMACHO_CHECK_RETURN(!((RT_BIT_32(pSect->align) - UINT32_C(1)) & pSect->addr), \
                                          VERR_LDRMACHO_BAD_SECTION); \
                RTLDRMODMACHO_CHECK_RETURN(!((RT_BIT_32(pSect->align) - UINT32_C(1)) & pSrcSeg->vmaddr), \
                                          VERR_LDRMACHO_BAD_SECTION); \
                \
                /* Adjust the section offset before we check file offset. */ \
                offSect = (offSect + RT_BIT_64(pSect->align) - UINT64_C(1)) & ~(RT_BIT_64(pSect->align) - UINT64_C(1)); \
                if (pSect->addr) \
                { \
                    RTLDRMODMACHO_CHECK_RETURN(offSect <= pSect->addr - pSrcSeg->vmaddr, VERR_LDRMACHO_BAD_SECTION); \
                    if (offSect < pSect->addr - pSrcSeg->vmaddr) \
                        offSect = pSect->addr - pSrcSeg->vmaddr; \
                } \
                \
                if (fFileBits && pSect->offset == 0 && pSrcSeg->fileoff == 0 && pHdr->filetype == MH_DSYM) \
                    fFileBits = 0; \
                if (fFileBits) \
                { \
                    if (uEffFileType != MH_OBJECT) \
                    { \
                        RTLDRMODMACHO_CHECK_RETURN(pSect->offset == pSrcSeg->fileoff + offSect, \
                                                  VERR_LDRMACHO_NON_CONT_SEG_BITS); \
                        RTLDRMODMACHO_CHECK_RETURN(pSect->offset - pSrcSeg->fileoff <= pSrcSeg->filesize, \
                                                  VERR_LDRMACHO_BAD_SECTION); \
                    } \
                    RTLDRMODMACHO_CHECK_RETURN(pSect->offset <= cbFile, \
                                              VERR_LDRMACHO_BAD_SECTION); \
                    RTLDRMODMACHO_CHECK_RETURN((uint64_t)pSect->offset + pSect->size <= cbFile, \
                                              VERR_LDRMACHO_BAD_SECTION); \
                } \
                else \
                    RTLDRMODMACHO_CHECK_RETURN(pSect->offset == 0, VERR_LDRMACHO_BAD_SECTION); \
                \
                if (!pSect->nreloc) \
                    RTLDRMODMACHO_CHECK_RETURN(!pSect->reloff, \
                                              VERR_LDRMACHO_BAD_SECTION); \
                else \
                { \
                    RTLDRMODMACHO_CHECK_RETURN(pSect->reloff <= cbFile, \
                                              VERR_LDRMACHO_BAD_SECTION); \
                    RTLDRMODMACHO_CHECK_RETURN(     (uint64_t)pSect->reloff \
                                                 + (RTFOFF)pSect->nreloc * sizeof(macho_relocation_info_t) \
                                              <= cbFile, \
                                              VERR_LDRMACHO_BAD_SECTION); \
                    cSectionsWithRelocs++; \
                } \
                \
                /* Validate against file type (pointless?) and count the section, for object files add segment. */ \
                switch (uEffFileType) \
                { \
                    case MH_OBJECT: \
                        if (   !(pSect->flags & S_ATTR_DEBUG) \
                            && strcmp(pSect->segname, "__DWARF")) \
                        { \
                            cbStringPool += RTStrNLen(&pSect->segname[0], sizeof(pSect->segname)) + 1; \
                            cbStringPool += RTStrNLen(&pSect->sectname[0], sizeof(pSect->sectname)) + 1; \
                            cSegments++; \
                            if (cSegments == 1) /* The link address is set by the first segment. */  \
                                *pLinkAddress = pSect->addr; \
                        } \
                        RT_FALL_THRU(); \
                    case MH_EXECUTE: \
                    case MH_DYLIB: \
                    case MH_BUNDLE: \
                    case MH_DSYM: \
                    case MH_KEXT_BUNDLE: \
                        cSections++; \
                        break; \
                    default: \
                        RTLDRMODMACHO_FAILED_RETURN(VERR_INVALID_PARAMETER); \
                } \
                \
                /* Advance the section offset, since we're also aligning it. */ \
                offSect += pSect->size; \
            } while (0) /* VALIDATE_AND_ADD_SECTION */

        /*
         * Convert endian if needed, parse and validate the command.
         */
        switch (u.pLoadCmd->cmd)
        {
            case LC_SEGMENT_32:
            {
                segment_command_32_t *pSrcSeg = (segment_command_32_t *)u.pLoadCmd;
                section_32_t   *pFirstSect    = (section_32_t *)(pSrcSeg + 1);
                section_32_t   *pSect         = pFirstSect;
                uint32_t        cSectionsLeft = pSrcSeg->nsects;
                uint64_t        offSect       = 0;

                /* Convert and verify the segment. */
                RTLDRMODMACHO_CHECK_RETURN(u.pLoadCmd->cmdsize >= sizeof(segment_command_32_t), VERR_LDRMACHO_BAD_LOAD_COMMAND);
                RTLDRMODMACHO_CHECK_RETURN(   pHdr->magic == IMAGE_MACHO32_SIGNATURE_OE
                                           || pHdr->magic == IMAGE_MACHO32_SIGNATURE, VERR_LDRMACHO_BIT_MIX);
                if (fConvertEndian)
                {
                    pSrcSeg->vmaddr   = RT_BSWAP_U32(pSrcSeg->vmaddr);
                    pSrcSeg->vmsize   = RT_BSWAP_U32(pSrcSeg->vmsize);
                    pSrcSeg->fileoff  = RT_BSWAP_U32(pSrcSeg->fileoff);
                    pSrcSeg->filesize = RT_BSWAP_U32(pSrcSeg->filesize);
                    pSrcSeg->maxprot  = RT_BSWAP_U32(pSrcSeg->maxprot);
                    pSrcSeg->initprot = RT_BSWAP_U32(pSrcSeg->initprot);
                    pSrcSeg->nsects   = RT_BSWAP_U32(pSrcSeg->nsects);
                    pSrcSeg->flags    = RT_BSWAP_U32(pSrcSeg->flags);
                }

                VALIDATE_AND_ADD_SEGMENT(32);


                /*
                 * Convert, validate and parse the sections.
                 */
                cSectionsLeft = pSrcSeg->nsects;
                pFirstSect = pSect = (section_32_t *)(pSrcSeg + 1);
                while (cSectionsLeft-- > 0)
                {
                    if (fConvertEndian)
                    {
                        pSect->addr      = RT_BSWAP_U32(pSect->addr);
                        pSect->size      = RT_BSWAP_U32(pSect->size);
                        pSect->offset    = RT_BSWAP_U32(pSect->offset);
                        pSect->align     = RT_BSWAP_U32(pSect->align);
                        pSect->reloff    = RT_BSWAP_U32(pSect->reloff);
                        pSect->nreloc    = RT_BSWAP_U32(pSect->nreloc);
                        pSect->flags     = RT_BSWAP_U32(pSect->flags);
                        pSect->reserved1 = RT_BSWAP_U32(pSect->reserved1);
                        pSect->reserved2 = RT_BSWAP_U32(pSect->reserved2);
                    }

                    VALIDATE_AND_ADD_SECTION(32);

                    /* next */
                    pSect++;
                }
                break;
            }

            case LC_SEGMENT_64:
            {
                segment_command_64_t *pSrcSeg = (segment_command_64_t *)u.pLoadCmd;
                section_64_t   *pFirstSect    = (section_64_t *)(pSrcSeg + 1);
                section_64_t   *pSect         = pFirstSect;
                uint32_t        cSectionsLeft = pSrcSeg->nsects;
                uint64_t            offSect       = 0;

                /* Convert and verify the segment. */
                RTLDRMODMACHO_CHECK_RETURN(u.pLoadCmd->cmdsize >= sizeof(segment_command_64_t), VERR_LDRMACHO_BAD_LOAD_COMMAND);
                RTLDRMODMACHO_CHECK_RETURN(   pHdr->magic == IMAGE_MACHO64_SIGNATURE_OE
                                           || pHdr->magic == IMAGE_MACHO64_SIGNATURE, VERR_LDRMACHO_BIT_MIX);
                if (fConvertEndian)
                {
                    pSrcSeg->vmaddr   = RT_BSWAP_U64(pSrcSeg->vmaddr);
                    pSrcSeg->vmsize   = RT_BSWAP_U64(pSrcSeg->vmsize);
                    pSrcSeg->fileoff  = RT_BSWAP_U64(pSrcSeg->fileoff);
                    pSrcSeg->filesize = RT_BSWAP_U64(pSrcSeg->filesize);
                    pSrcSeg->maxprot  = RT_BSWAP_U32(pSrcSeg->maxprot);
                    pSrcSeg->initprot = RT_BSWAP_U32(pSrcSeg->initprot);
                    pSrcSeg->nsects   = RT_BSWAP_U32(pSrcSeg->nsects);
                    pSrcSeg->flags    = RT_BSWAP_U32(pSrcSeg->flags);
                }

                VALIDATE_AND_ADD_SEGMENT(64);

                /*
                 * Convert, validate and parse the sections.
                 */
                while (cSectionsLeft-- > 0)
                {
                    if (fConvertEndian)
                    {
                        pSect->addr      = RT_BSWAP_U64(pSect->addr);
                        pSect->size      = RT_BSWAP_U64(pSect->size);
                        pSect->offset    = RT_BSWAP_U32(pSect->offset);
                        pSect->align     = RT_BSWAP_U32(pSect->align);
                        pSect->reloff    = RT_BSWAP_U32(pSect->reloff);
                        pSect->nreloc    = RT_BSWAP_U32(pSect->nreloc);
                        pSect->flags     = RT_BSWAP_U32(pSect->flags);
                        pSect->reserved1 = RT_BSWAP_U32(pSect->reserved1);
                        pSect->reserved2 = RT_BSWAP_U32(pSect->reserved2);
                    }

                    VALIDATE_AND_ADD_SECTION(64);

                    /* next */
                    pSect++;
                }
                break;
            } /* LC_SEGMENT_64 */


            case LC_SYMTAB:
            {
                size_t cbSym;
                if (fConvertEndian)
                {
                    u.pSymTab->symoff  = RT_BSWAP_U32(u.pSymTab->symoff);
                    u.pSymTab->nsyms   = RT_BSWAP_U32(u.pSymTab->nsyms);
                    u.pSymTab->stroff  = RT_BSWAP_U32(u.pSymTab->stroff);
                    u.pSymTab->strsize = RT_BSWAP_U32(u.pSymTab->strsize);
                }

                /* verify */
                cbSym = pHdr->magic == IMAGE_MACHO32_SIGNATURE
                     || pHdr->magic == IMAGE_MACHO32_SIGNATURE_OE
                      ? sizeof(macho_nlist_32_t)
                      : sizeof(macho_nlist_64_t);
                if (    u.pSymTab->symoff >= cbFile
                    ||  (uint64_t)u.pSymTab->symoff + u.pSymTab->nsyms * cbSym > cbFile)
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_LOAD_COMMAND);
                if (    u.pSymTab->stroff >= cbFile
                    ||  (uint64_t)u.pSymTab->stroff + u.pSymTab->strsize > cbFile)
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_LOAD_COMMAND);

                /* Only one object table, please. */
                cSymbolTabs++;
                if (cSymbolTabs != 1)
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_OBJECT_FILE);

                cSymbols = u.pSymTab->nsyms;
                break;
            }

            case LC_DYSYMTAB:
            {
                if (pHdr->filetype == MH_OBJECT)
                    RTLDRMODMACHO_FAILED_RETURN(RTErrInfoSet(pErrInfo, VERR_LDRMACHO_BAD_OBJECT_FILE,
                                                             "Not expecting LC_DYSYMTAB in MH_OBJECT"));
                if (fConvertEndian)
                {
                    u.pDySymTab->ilocalsym       = RT_BSWAP_U32(u.pDySymTab->ilocalsym);
                    u.pDySymTab->nlocalsym       = RT_BSWAP_U32(u.pDySymTab->nlocalsym);
                    u.pDySymTab->iextdefsym      = RT_BSWAP_U32(u.pDySymTab->iextdefsym);
                    u.pDySymTab->nextdefsym      = RT_BSWAP_U32(u.pDySymTab->nextdefsym);
                    u.pDySymTab->iundefsym       = RT_BSWAP_U32(u.pDySymTab->iundefsym);
                    u.pDySymTab->nundefsym       = RT_BSWAP_U32(u.pDySymTab->nundefsym);
                    u.pDySymTab->tocoff          = RT_BSWAP_U32(u.pDySymTab->tocoff);
                    u.pDySymTab->ntoc            = RT_BSWAP_U32(u.pDySymTab->ntoc);
                    u.pDySymTab->modtaboff       = RT_BSWAP_U32(u.pDySymTab->modtaboff);
                    u.pDySymTab->nmodtab         = RT_BSWAP_U32(u.pDySymTab->nmodtab);
                    u.pDySymTab->extrefsymoff    = RT_BSWAP_U32(u.pDySymTab->extrefsymoff);
                    u.pDySymTab->nextrefsym      = RT_BSWAP_U32(u.pDySymTab->nextrefsym);
                    u.pDySymTab->indirectsymboff = RT_BSWAP_U32(u.pDySymTab->indirectsymboff);
                    u.pDySymTab->nindirectsymb   = RT_BSWAP_U32(u.pDySymTab->nindirectsymb);
                    u.pDySymTab->extreloff       = RT_BSWAP_U32(u.pDySymTab->extreloff);
                    u.pDySymTab->nextrel         = RT_BSWAP_U32(u.pDySymTab->nextrel);
                    u.pDySymTab->locreloff       = RT_BSWAP_U32(u.pDySymTab->locreloff);
                    u.pDySymTab->nlocrel         = RT_BSWAP_U32(u.pDySymTab->nlocrel);
                }

                /* verify */
                RTLDRMODMACHO_CHECK_RETURN((uint64_t)u.pDySymTab->ilocalsym + u.pDySymTab->nlocalsym <= cSymbols,
                                           RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                         "ilocalsym=%#x + nlocalsym=%#x vs cSymbols=%#x",
                                                         u.pDySymTab->ilocalsym, u.pDySymTab->nlocalsym, cSymbols));
                RTLDRMODMACHO_CHECK_RETURN((uint64_t)u.pDySymTab->iextdefsym + u.pDySymTab->nextdefsym <= cSymbols,
                                           RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                         "iextdefsym=%#x + nextdefsym=%#x vs cSymbols=%#x",
                                                         u.pDySymTab->iextdefsym, u.pDySymTab->nextdefsym, cSymbols));
                RTLDRMODMACHO_CHECK_RETURN((uint64_t)u.pDySymTab->iundefsym + u.pDySymTab->nundefsym <= cSymbols,
                                           RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                         "iundefsym=%#x + nundefsym=%#x vs cSymbols=%#x",
                                                         u.pDySymTab->iundefsym, u.pDySymTab->nundefsym, cSymbols));
                RTLDRMODMACHO_CHECK_RETURN(   (uint64_t)u.pDySymTab->tocoff + u.pDySymTab->ntoc * sizeof(dylib_table_of_contents_t)
                                           <= cbFile,
                                           RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                         "tocoff=%#x + ntoc=%#x vs cbFile=%#RX64",
                                                         u.pDySymTab->tocoff, u.pDySymTab->ntoc, cbFile));
                const uint32_t cbModTabEntry = pHdr->magic == IMAGE_MACHO32_SIGNATURE
                                            || pHdr->magic == IMAGE_MACHO32_SIGNATURE_OE
                                             ? sizeof(dylib_module_32_t) : sizeof(dylib_module_64_t);
                RTLDRMODMACHO_CHECK_RETURN((uint64_t)u.pDySymTab->modtaboff + u.pDySymTab->nmodtab * cbModTabEntry <= cbFile,
                                           RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                         "modtaboff=%#x + nmodtab=%#x cbModTabEntry=%#x vs cbFile=%#RX64",
                                                         u.pDySymTab->modtaboff, u.pDySymTab->nmodtab, cbModTabEntry, cbFile));
                RTLDRMODMACHO_CHECK_RETURN(   (uint64_t)u.pDySymTab->extrefsymoff + u.pDySymTab->nextrefsym * sizeof(dylib_reference_t)
                                           <= cbFile,
                                           RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                         "extrefsymoff=%#x + nextrefsym=%#x vs cbFile=%#RX64",
                                                         u.pDySymTab->extrefsymoff, u.pDySymTab->nextrefsym, cbFile));
                RTLDRMODMACHO_CHECK_RETURN(   (uint64_t)u.pDySymTab->indirectsymboff + u.pDySymTab->nindirectsymb * sizeof(uint32_t)
                                           <= cbFile,
                                           RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                         "indirectsymboff=%#x + nindirectsymb=%#x vs cbFile=%#RX64",
                                                         u.pDySymTab->indirectsymboff, u.pDySymTab->nindirectsymb, cbFile));
                RTLDRMODMACHO_CHECK_RETURN((uint64_t)u.pDySymTab->extreloff + u.pDySymTab->nextrel * sizeof(macho_relocation_info_t) <= cbFile,
                                           RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                         "extreloff=%#x + nextrel=%#x vs cbFile=%#RX64",
                                                         u.pDySymTab->extreloff, u.pDySymTab->nextrel, cbFile));
                RTLDRMODMACHO_CHECK_RETURN((uint64_t)u.pDySymTab->locreloff + u.pDySymTab->nlocrel * sizeof(macho_relocation_info_t) <= cbFile,
                                           RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                         "locreloff=%#x + nlocrel=%#x vs cbFile=%#RX64",
                                                         u.pDySymTab->locreloff, u.pDySymTab->nlocrel, cbFile));
                cDySymbolTabs++;
                fDySymbolTabWithRelocs |= (u.pDySymTab->nlocrel + u.pDySymTab->nextrel) != 0;
                break;
            }

            case LC_THREAD:
            case LC_UNIXTHREAD:
            {
                uint32_t *pu32 = (uint32_t *)(u.pb + sizeof(load_command_t));
                uint32_t cItemsLeft = (u.pThread->cmdsize - sizeof(load_command_t)) / sizeof(uint32_t);
                while (cItemsLeft)
                {
                    /* convert & verify header items ([0] == flavor, [1] == uint32_t count). */
                    if (cItemsLeft < 2)
                        RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_LOAD_COMMAND);
                    if (fConvertEndian)
                    {
                        pu32[0] = RT_BSWAP_U32(pu32[0]);
                        pu32[1] = RT_BSWAP_U32(pu32[1]);
                    }
                    if (pu32[1] + 2 > cItemsLeft)
                        RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_LOAD_COMMAND);

                    /* convert & verify according to flavor. */
                    switch (pu32[0])
                    {
                        /** @todo */
                        default:
                            break;
                    }

                    /* next */
                    cItemsLeft -= pu32[1] + 2;
                    pu32 += pu32[1] + 2;
                }
                break;
            }

            case LC_UUID:
                if (u.pUuid->cmdsize != sizeof(uuid_command_t))
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_LOAD_COMMAND);
                /** @todo Check anything here need converting? */
                break;

            case LC_CODE_SIGNATURE:
                if (u.pUuid->cmdsize != sizeof(linkedit_data_command_t))
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_LOAD_COMMAND);
                break;

            case LC_VERSION_MIN_MACOSX:
            case LC_VERSION_MIN_IPHONEOS:
                if (u.pUuid->cmdsize != sizeof(version_min_command_t))
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_LOAD_COMMAND);
                break;

            case LC_SOURCE_VERSION:     /* Harmless. It just gives a clue regarding the source code revision/version. */
            case LC_BUILD_VERSION:      /* Harmless. It just gives a clue regarding the tool/sdk versions. */
            case LC_DATA_IN_CODE:       /* Ignore */
            case LC_DYLIB_CODE_SIGN_DRS:/* Ignore */
                /** @todo valid command size. */
                break;

            case LC_FUNCTION_STARTS:    /** @todo dylib++ */
                /* Ignore for now. */
                break;
            case LC_ID_DYLIB:           /** @todo dylib */
            case LC_LOAD_DYLIB:         /** @todo dylib */
            case LC_LOAD_DYLINKER:      /** @todo dylib */
            case LC_TWOLEVEL_HINTS:     /** @todo dylib */
            case LC_LOAD_WEAK_DYLIB:    /** @todo dylib */
            case LC_ID_DYLINKER:        /** @todo dylib */
            case LC_RPATH:              /** @todo dylib */
            case LC_SEGMENT_SPLIT_INFO: /** @todo dylib++ */
            case LC_REEXPORT_DYLIB:     /** @todo dylib */
            case LC_DYLD_INFO:          /** @todo dylib */
            case LC_DYLD_INFO_ONLY:     /** @todo dylib */
            case LC_LOAD_UPWARD_DYLIB:  /** @todo dylib */
            case LC_DYLD_ENVIRONMENT:   /** @todo dylib */
            case LC_MAIN: /** @todo parse this and find and entry point or smth. */
                /** @todo valid command size. */
                if (!(fOpenFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION)))
                    RTLDRMODMACHO_FAILED_RETURN(RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_UNSUPPORTED_LOAD_COMMAND,
                                                             "cmd=%#x", u.pLoadCmd->cmd));
                Log(("ldrMachO: Can't load because of load command: %#x\n", u.pLoadCmd->cmd));
                *pfCanLoad = false;
                break;

            case LC_LOADFVMLIB:
            case LC_IDFVMLIB:
            case LC_IDENT:
            case LC_FVMFILE:
            case LC_PREPAGE:
            case LC_PREBOUND_DYLIB:
            case LC_ROUTINES:
            case LC_ROUTINES_64:
            case LC_SUB_FRAMEWORK:
            case LC_SUB_UMBRELLA:
            case LC_SUB_CLIENT:
            case LC_SUB_LIBRARY:
            case LC_PREBIND_CKSUM:
            case LC_SYMSEG:
                RTLDRMODMACHO_FAILED_RETURN(RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_UNSUPPORTED_LOAD_COMMAND,
                                                         "cmd=%#x", u.pLoadCmd->cmd));

            default:
                RTLDRMODMACHO_FAILED_RETURN(RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_UNKNOWN_LOAD_COMMAND,
                                                         "cmd=%#x", u.pLoadCmd->cmd));
        }
    }

    /* be strict. */
    if (cbLeft)
        RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_LOAD_COMMAND);

    RTLDRMODMACHO_CHECK_RETURN(cDySymbolTabs <= 1,
                               RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                             "More than one LC_DYSYMTAB command: %u", cDySymbolTabs));
    RTLDRMODMACHO_CHECK_RETURN(!fDySymbolTabWithRelocs || cSectionsWithRelocs == 0,
                               RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                             "Have relocations both in sections and LC_DYSYMTAB"));
    if (!cSegments)
        RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_OBJECT_FILE);

    switch (uEffFileType)
    {
        case MH_OBJECT:
        case MH_EXECUTE:
            RTLDRMODMACHO_CHECK_RETURN(!fDySymbolTabWithRelocs || (fOpenFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION)),
                                       RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                     "Did not expect relocations in LC_DYSYMTAB (file type %u)", uEffFileType));
            break;

        case MH_DYLIB:
        case MH_BUNDLE:
        case MH_KEXT_BUNDLE:
            RTLDRMODMACHO_CHECK_RETURN(cDySymbolTabs > 0,
                                       RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                     "No LC_DYSYMTAB command (file type %u)", uEffFileType));
            RTLDRMODMACHO_CHECK_RETURN(fDySymbolTabWithRelocs || cSectionsWithRelocs == 0,
                                       RTErrInfoSetF(pErrInfo, VERR_LDRMACHO_BAD_LOAD_COMMAND,
                                                     "Expected relocations in LC_DYSYMTAB (file type %u)", uEffFileType));
            break;

        case MH_DSYM:
            break;
    }

    /*
     * Set return values and return.
     */
    *pcSegments = cSegments;
    *pcSections = cSections;
    *pcbStringPool = (uint32_t)cbStringPool;

    return VINF_SUCCESS;
}


/**
 * Parses the load commands after we've carved out the module instance.
 *
 * This fills in the segment table and perhaps some other properties.
 *
 * @returns IPRT status code.
 * @param   pThis           The module.
 * @param   pbStringPool    The string pool
 * @param   cbStringPool    The size of the string pool.
 */
static int  kldrModMachOParseLoadCommands(PRTLDRMODMACHO pThis, char *pbStringPool, uint32_t cbStringPool)
{
    union
    {
        const uint8_t                 *pb;
        const load_command_t          *pLoadCmd;
        const segment_command_32_t    *pSeg32;
        const segment_command_64_t    *pSeg64;
        const symtab_command_t        *pSymTab;
        const uuid_command_t          *pUuid;
        const linkedit_data_command_t *pData;
    } u;
    uint32_t cLeft = pThis->Hdr.ncmds;
    uint32_t cbLeft = pThis->Hdr.sizeofcmds;
    const uint8_t *pb = pThis->pbLoadCommands;
    PRTLDRMODMACHOSEG pDstSeg = &pThis->aSegments[0];
    PRTLDRMODMACHOSECT pSectExtra = pThis->paSections;
    const uint32_t cSegments = pThis->cSegments;
    PRTLDRMODMACHOSEG pSegItr;
    bool fFirstSeg = true;
    RT_NOREF(cbStringPool);

    while (cLeft-- > 0)
    {
        u.pb = pb;
        cbLeft -= u.pLoadCmd->cmdsize;
        pb += u.pLoadCmd->cmdsize;

        /*
         * Convert endian if needed, parse and validate the command.
         */
        switch (u.pLoadCmd->cmd)
        {
            case LC_SEGMENT_32:
            {
                const segment_command_32_t *pSrcSeg = (const segment_command_32_t *)u.pLoadCmd;
                section_32_t   *pFirstSect    = (section_32_t *)(pSrcSeg + 1);
                section_32_t   *pSect         = pFirstSect;
                uint32_t        cSectionsLeft = pSrcSeg->nsects;

                /* Adds a segment, used by the macro below and thus shared with the 64-bit segment variant. */
#define NEW_SEGMENT(a_cBits, a_achName1, a_fObjFile, a_achName2, a_SegAddr, a_cbSeg, a_fFileBits, a_offFile, a_cbFile) \
    do { \
        pDstSeg->SegInfo.pszName = pbStringPool; \
        pDstSeg->SegInfo.cchName = (uint32_t)RTStrNLen(a_achName1, sizeof(a_achName1)); \
        memcpy(pbStringPool, a_achName1, pDstSeg->SegInfo.cchName); \
        pbStringPool += pDstSeg->SegInfo.cchName; \
        if (a_fObjFile) \
        {   /* MH_OBJECT: Add '.sectname' - sections aren't sorted by segments. */ \
            size_t cchName2 = RTStrNLen(a_achName2, sizeof(a_achName2)); \
            *pbStringPool++ = '.'; \
            memcpy(pbStringPool, a_achName2, cchName2); \
            pbStringPool += cchName2; \
            pDstSeg->SegInfo.cchName += (uint32_t)cchName2; \
        } \
        *pbStringPool++ = '\0'; \
        pDstSeg->SegInfo.SelFlat = 0; \
        pDstSeg->SegInfo.Sel16bit = 0; \
        pDstSeg->SegInfo.fFlags = 0; \
        pDstSeg->SegInfo.fProt = RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC; /** @todo fixme! */ \
        pDstSeg->SegInfo.cb = (a_cbSeg); \
        pDstSeg->SegInfo.Alignment = 1; /* updated while parsing sections. */ \
        pDstSeg->SegInfo.LinkAddress = (a_SegAddr); \
        if (a_fFileBits) \
        { \
            pDstSeg->SegInfo.offFile = (RTFOFF)((a_offFile) + pThis->offImage); \
            pDstSeg->SegInfo.cbFile  = (RTFOFF)(a_cbFile); \
        } \
        else \
        { \
            pDstSeg->SegInfo.offFile = -1; \
            pDstSeg->SegInfo.cbFile  = -1; \
        } \
        pDstSeg->SegInfo.RVA = (a_SegAddr) - pThis->LinkAddress; \
        pDstSeg->SegInfo.cbMapped = 0; \
        \
        pDstSeg->iOrgSegNo = (uint32_t)(pDstSeg - &pThis->aSegments[0]); \
        pDstSeg->cSections = 0; \
        pDstSeg->paSections = pSectExtra; \
    } while (0)

    /* Closes the new segment - part of NEW_SEGMENT. */
#define CLOSE_SEGMENT() \
    do { \
        pDstSeg->cSections = (uint32_t)(pSectExtra - pDstSeg->paSections); \
        pDstSeg++; \
    } while (0)


    /* Shared with the 64-bit variant. */
#define ADD_SEGMENT_AND_ITS_SECTIONS(a_cBits) \
    do { \
        bool fAddSegOuter = false; \
        \
        /* \
         * Check that the segment name is unique.  We couldn't do that \
         * in the preparsing stage. \
         */ \
        if (pThis->uEffFileType != MH_OBJECT) \
            for (pSegItr = &pThis->aSegments[0]; pSegItr != pDstSeg; pSegItr++) \
                if (!strncmp(pSegItr->SegInfo.pszName, pSrcSeg->segname, sizeof(pSrcSeg->segname))) \
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDR_DUPLICATE_SEGMENT_NAME); \
        \
        /* \
         * Create a new segment, unless we're supposed to skip this one. \
         */ \
        if (   pThis->uEffFileType != MH_OBJECT \
            && (cSectionsLeft == 0 || !(pFirstSect->flags & S_ATTR_DEBUG)) \
            && strcmp(pSrcSeg->segname, "__DWARF") \
            && strcmp(pSrcSeg->segname, "__CTF") ) \
        { \
            NEW_SEGMENT(a_cBits, pSrcSeg->segname, false /*a_fObjFile*/, "" /*a_achName2*/, \
                        pSrcSeg->vmaddr, pSrcSeg->vmsize, \
                        pSrcSeg->filesize != 0, pSrcSeg->fileoff, pSrcSeg->filesize); \
            fAddSegOuter = true; \
        } \
        \
        /* \
         * Convert and parse the sections. \
         */ \
        while (cSectionsLeft-- > 0) \
        { \
            /* New segment if object file. */ \
            bool fAddSegInner = false; \
            if (   pThis->uEffFileType == MH_OBJECT \
                && !(pSect->flags & S_ATTR_DEBUG) \
                && strcmp(pSrcSeg->segname, "__DWARF") \
                && strcmp(pSrcSeg->segname, "__CTF") ) \
            { \
                Assert(!fAddSegOuter); \
                NEW_SEGMENT(a_cBits, pSect->segname, true /*a_fObjFile*/, pSect->sectname, \
                            pSect->addr, pSect->size, \
                            pSect->offset != 0, pSect->offset, pSect->size); \
                fAddSegInner = true; \
            } \
            \
            /* Section data extract. */ \
            pSectExtra->cb = pSect->size; \
            pSectExtra->RVA = pSect->addr - pDstSeg->SegInfo.LinkAddress; \
            pSectExtra->LinkAddress = pSect->addr; \
            if (pSect->offset) \
                pSectExtra->offFile = pSect->offset + pThis->offImage; \
            else \
                pSectExtra->offFile = -1; \
            pSectExtra->cFixups = pSect->nreloc; \
            pSectExtra->paFixups = NULL; \
            pSectExtra->pauFixupVirginData = NULL; \
            if (pSect->nreloc) \
                pSectExtra->offFixups = pSect->reloff + pThis->offImage; \
            else \
                pSectExtra->offFixups = -1; \
            pSectExtra->fFlags = pSect->flags; \
            pSectExtra->iSegment = (uint32_t)(pDstSeg - &pThis->aSegments[0]); \
            pSectExtra->pvMachoSection = pSect; \
            \
            /* Update the segment alignment, if we're not skipping it. */ \
            if (   (fAddSegOuter || fAddSegInner) \
                && pDstSeg->SegInfo.Alignment < ((RTLDRADDR)1 << pSect->align)) \
                pDstSeg->SegInfo.Alignment = (RTLDRADDR)1 << pSect->align; \
            \
            /* Next section, and if object file next segment. */ \
            pSectExtra++; \
            pSect++; \
            if (fAddSegInner) \
                CLOSE_SEGMENT(); \
        } \
        \
        /* Close the segment and advance. */ \
        if (fAddSegOuter) \
            CLOSE_SEGMENT(); \
        \
        /* Take down 'execSeg' info for signing */ \
        if (fFirstSeg) \
        { \
            fFirstSeg = false; \
            pThis->offSeg0ForCodeSign = pSrcSeg->fileoff; \
            pThis->cbSeg0ForCodeSign  = pSrcSeg->filesize; /** @todo file or vm size? */ \
            pThis->fSeg0ForCodeSign   = pSrcSeg->flags; \
        } \
    } while (0) /* ADD_SEGMENT_AND_ITS_SECTIONS */

                ADD_SEGMENT_AND_ITS_SECTIONS(32);
                break;
            }

            case LC_SEGMENT_64:
            {
                const segment_command_64_t *pSrcSeg = (const segment_command_64_t *)u.pLoadCmd;
                section_64_t   *pFirstSect    = (section_64_t *)(pSrcSeg + 1);
                section_64_t   *pSect         = pFirstSect;
                uint32_t        cSectionsLeft = pSrcSeg->nsects;

                ADD_SEGMENT_AND_ITS_SECTIONS(64);
                break;
            }

            case LC_SYMTAB:
                switch (pThis->uEffFileType)
                {
                    case MH_OBJECT:
                    case MH_EXECUTE:
                    case MH_DYLIB:
                    case MH_BUNDLE:
                    case MH_DSYM:
                    case MH_KEXT_BUNDLE:
                        pThis->offSymbols = u.pSymTab->symoff + pThis->offImage;
                        pThis->cSymbols = u.pSymTab->nsyms;
                        pThis->offStrings = u.pSymTab->stroff + pThis->offImage;
                        pThis->cchStrings = u.pSymTab->strsize;
                        break;
                }
                break;

            case LC_DYSYMTAB:
                pThis->pDySymTab = (dysymtab_command_t *)u.pb;
                break;

            case LC_UUID:
                memcpy(pThis->abImageUuid, u.pUuid->uuid, sizeof(pThis->abImageUuid));
                break;

            case LC_CODE_SIGNATURE:
                pThis->offCodeSignature = u.pData->dataoff;
                pThis->cbCodeSignature  = u.pData->datasize;
                break;

            default:
                break;
        } /* command switch */
    } /* while more commands */

    Assert(pDstSeg == &pThis->aSegments[cSegments - pThis->fMakeGot]);

    /*
     * Adjust mapping addresses calculating the image size.
     */
    {
        bool                fLoadLinkEdit = RT_BOOL(pThis->fOpenFlags & RTLDR_O_MACHO_LOAD_LINKEDIT);
        PRTLDRMODMACHOSECT  pSectExtraItr;
        RTLDRADDR           uNextRVA = 0;
        RTLDRADDR           cb;
        uint32_t            cSegmentsToAdjust = cSegments - pThis->fMakeGot;
        uint32_t            c;

        for (;;)
        {
            /* Check if there is __DWARF segment at the end and make sure it's left
               out of the RVA negotiations and image loading. */
            if (   cSegmentsToAdjust > 0
                && !strcmp(pThis->aSegments[cSegmentsToAdjust - 1].SegInfo.pszName, "__DWARF"))
            {
                cSegmentsToAdjust--;
                pThis->aSegments[cSegmentsToAdjust].SegInfo.RVA = NIL_RTLDRADDR;
                pThis->aSegments[cSegmentsToAdjust].SegInfo.cbMapped = NIL_RTLDRADDR;
                continue;
            }

            /* If we're skipping the __LINKEDIT segment, check for it and adjust
               the number of segments we'll be messing with here.  ASSUMES it's
               last (typcially is, but not always for mach_kernel). */
            if (   !fLoadLinkEdit
                && cSegmentsToAdjust > 0
                && !strcmp(pThis->aSegments[cSegmentsToAdjust - 1].SegInfo.pszName, "__LINKEDIT"))
            {
                cSegmentsToAdjust--;
                pThis->aSegments[cSegmentsToAdjust].SegInfo.RVA      = NIL_RTLDRADDR;
                pThis->aSegments[cSegmentsToAdjust].SegInfo.cbMapped = NIL_RTLDRADDR;
                continue;
            }
            break;
        }

        /* Adjust RVAs. */
        c = cSegmentsToAdjust;
        for (pDstSeg = &pThis->aSegments[0]; c-- > 0; pDstSeg++)
        {
            uNextRVA = RTLDR_ALIGN_ADDR(uNextRVA, pDstSeg->SegInfo.Alignment);
            cb = pDstSeg->SegInfo.RVA - uNextRVA;
            if (cb >= 0x00100000) /* 1MB */
            {
                pDstSeg->SegInfo.RVA = uNextRVA;
                //pThis->pMod->fFlags |= KLDRMOD_FLAGS_NON_CONTIGUOUS_LINK_ADDRS;
            }
            uNextRVA = pDstSeg->SegInfo.RVA + pDstSeg->SegInfo.cb;
        }

        /* Calculate the cbMapping members. */
        c = cSegmentsToAdjust;
        for (pDstSeg = &pThis->aSegments[0]; c-- > 1; pDstSeg++)
        {

            cb = pDstSeg[1].SegInfo.RVA - pDstSeg->SegInfo.RVA;
            pDstSeg->SegInfo.cbMapped = (size_t)cb == cb ? (size_t)cb : ~(size_t)0;
        }

        cb = RTLDR_ALIGN_ADDR(pDstSeg->SegInfo.cb, pDstSeg->SegInfo.Alignment);
        pDstSeg->SegInfo.cbMapped = (size_t)cb == cb ? (size_t)cb : ~(size_t)0;

        /* Set the image size. */
        pThis->cbImage = pDstSeg->SegInfo.RVA + cb;

        /* Fixup the section RVAs (internal). */
        c        = cSegmentsToAdjust;
        uNextRVA = pThis->cbImage;
        pDstSeg  = &pThis->aSegments[0];
        for (pSectExtraItr = pThis->paSections; pSectExtraItr != pSectExtra; pSectExtraItr++)
        {
            if (pSectExtraItr->iSegment < c)
                pSectExtraItr->RVA += pDstSeg[pSectExtraItr->iSegment].SegInfo.RVA;
            else
            {
                pSectExtraItr->RVA = uNextRVA;
                uNextRVA += RTLDR_ALIGN_ADDR(pSectExtraItr->cb, 64);
            }
        }
    }

    /*
     * Make the GOT segment if necessary.
     */
    if (pThis->fMakeGot)
    {
        uint32_t cbPtr = (   pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
                      || pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
                   ? sizeof(uint32_t)
                   : sizeof(uint64_t);
        uint32_t cbGot = pThis->cSymbols * cbPtr;
        uint32_t cbJmpStubs;

        pThis->GotRVA = pThis->cbImage;

        if (pThis->cbJmpStub)
        {
            cbGot = RT_ALIGN_Z(cbGot, 64);
            pThis->JmpStubsRVA = pThis->GotRVA + cbGot;
            cbJmpStubs = pThis->cbJmpStub * pThis->cSymbols;
        }
        else
        {
            pThis->JmpStubsRVA = NIL_RTLDRADDR;
            cbJmpStubs = 0;
        }

        pDstSeg = &pThis->aSegments[cSegments - 1];
        pDstSeg->SegInfo.pszName = "GOT";
        pDstSeg->SegInfo.cchName = 3;
        pDstSeg->SegInfo.SelFlat = 0;
        pDstSeg->SegInfo.Sel16bit = 0;
        pDstSeg->SegInfo.fFlags = 0;
        pDstSeg->SegInfo.fProt = RTMEM_PROT_READ;
        pDstSeg->SegInfo.cb = cbGot + cbJmpStubs;
        pDstSeg->SegInfo.Alignment = 64;
        pDstSeg->SegInfo.LinkAddress = pThis->LinkAddress + pThis->GotRVA;
        pDstSeg->SegInfo.offFile = -1;
        pDstSeg->SegInfo.cbFile  = -1;
        pDstSeg->SegInfo.RVA = pThis->GotRVA;
        pDstSeg->SegInfo.cbMapped = (size_t)RTLDR_ALIGN_ADDR(cbGot + cbJmpStubs, pDstSeg->SegInfo.Alignment);

        pDstSeg->iOrgSegNo = UINT32_MAX;
        pDstSeg->cSections = 0;
        pDstSeg->paSections = NULL;

        pThis->cbImage += pDstSeg->SegInfo.cbMapped;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnClose}
 */
static DECLCALLBACK(int) rtldrMachO_Close(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    RTLDRMODMACHO_ASSERT(!pThis->pvMapping);

    uint32_t i = pThis->cSegments;
    while (i-- > 0)
    {
        uint32_t j = pThis->aSegments[i].cSections;
        while (j-- > 0)
        {
            RTMemFree(pThis->aSegments[i].paSections[j].paFixups);
            pThis->aSegments[i].paSections[j].paFixups = NULL;
            RTMemFree(pThis->aSegments[i].paSections[j].pauFixupVirginData);
            pThis->aSegments[i].paSections[j].pauFixupVirginData = NULL;
        }
    }

    RTMemFree(pThis->pbLoadCommands);
    pThis->pbLoadCommands = NULL;
    RTMemFree(pThis->pchStrings);
    pThis->pchStrings = NULL;
    RTMemFree(pThis->pvaSymbols);
    pThis->pvaSymbols = NULL;
    RTMemFree(pThis->paidxIndirectSymbols);
    pThis->paidxIndirectSymbols = NULL;
    RTMemFree(pThis->paRelocations);
    pThis->paRelocations = NULL;
    RTMemFree(pThis->pauRelocationsVirginData);
    pThis->pauRelocationsVirginData = NULL;
    RTMemFree(pThis->PtrCodeSignature.pb);
    pThis->PtrCodeSignature.pb = NULL;

    return VINF_SUCCESS;
}


/**
 * Gets the right base address.
 *
 * @param   pThis       The interpreter module instance
 * @param   pBaseAddress    The base address, IN & OUT. Optional.
 */
static void kldrModMachOAdjustBaseAddress(PRTLDRMODMACHO pThis, PRTLDRADDR pBaseAddress)
{
    /*
     * Adjust the base address.
     */
    if (*pBaseAddress == RTLDR_BASEADDRESS_LINK)
        *pBaseAddress = pThis->LinkAddress;
}


/**
 * Resolves a linker generated symbol.
 *
 * The Apple linker generates symbols indicating the start and end of sections
 * and segments.  This function checks for these and returns the right value.
 *
 * @returns VINF_SUCCESS or VERR_SYMBOL_NOT_FOUND.
 * @param   pThis               The interpreter module instance.
 * @param   pchSymbol           The symbol.
 * @param   cchSymbol           The length of the symbol.
 * @param   BaseAddress         The base address to apply when calculating the
 *                              value.
 * @param   puValue             Where to return the symbol value.
 */
static int kldrModMachOQueryLinkerSymbol(PRTLDRMODMACHO pThis, const char *pchSymbol, size_t cchSymbol,
                                         RTLDRADDR BaseAddress, PRTLDRADDR puValue)
{
    /*
     * Match possible name prefixes.
     */
    static const struct
    {
        const char *pszPrefix;
        uint32_t    cchPrefix;
        bool        fSection;
        bool        fStart;
    }   s_aPrefixes[] =
    {
        { "section$start$",  (uint8_t)sizeof("section$start$") - 1,   true,  true },
        { "section$end$",    (uint8_t)sizeof("section$end$") - 1,     true,  false},
        { "segment$start$",  (uint8_t)sizeof("segment$start$") - 1,   false, true },
        { "segment$end$",    (uint8_t)sizeof("segment$end$") - 1,     false, false},
    };
    size_t      cchSectName = 0;
    const char *pchSectName = "";
    size_t      cchSegName  = 0;
    const char *pchSegName  = NULL;
    uint32_t    iPrefix     = RT_ELEMENTS(s_aPrefixes) - 1;
    uint32_t    iSeg;
    RTLDRADDR   uValue;

    for (;;)
    {
        uint8_t const cchPrefix = s_aPrefixes[iPrefix].cchPrefix;
        if (   cchSymbol > cchPrefix
            && strncmp(pchSymbol, s_aPrefixes[iPrefix].pszPrefix, cchPrefix) == 0)
        {
            pchSegName = pchSymbol + cchPrefix;
            cchSegName = cchSymbol - cchPrefix;
            break;
        }

        /* next */
        if (!iPrefix)
            return VERR_SYMBOL_NOT_FOUND;
        iPrefix--;
    }

    /*
     * Split the remainder into segment and section name, if necessary.
     */
    if (s_aPrefixes[iPrefix].fSection)
    {
        pchSectName = (const char *)memchr(pchSegName, '$', cchSegName);
        if (!pchSectName)
            return VERR_SYMBOL_NOT_FOUND;
        cchSegName  = pchSectName - pchSegName;
        pchSectName++;
        cchSectName = cchSymbol - (pchSectName - pchSymbol);
    }

    /*
     * Locate the segment.
     */
    if (!pThis->cSegments)
        return VERR_SYMBOL_NOT_FOUND;
    for (iSeg = 0; iSeg < pThis->cSegments; iSeg++)
    {
        if (   pThis->aSegments[iSeg].SegInfo.cchName >= cchSegName
            && memcmp(pThis->aSegments[iSeg].SegInfo.pszName, pchSegName, cchSegName) == 0)
        {
            section_32_t const *pSect;
            if (   pThis->aSegments[iSeg].SegInfo.cchName == cchSegName
                && pThis->Hdr.filetype != MH_OBJECT /* Good enough for __DWARF segs in MH_DHSYM, I hope. */)
                break;

            pSect = (section_32_t *)pThis->aSegments[iSeg].paSections[0].pvMachoSection;
            if (   pThis->uEffFileType == MH_OBJECT
                && pThis->aSegments[iSeg].SegInfo.cchName > cchSegName + 1
                && pThis->aSegments[iSeg].SegInfo.pszName[cchSegName] == '.'
                && strncmp(&pThis->aSegments[iSeg].SegInfo.pszName[cchSegName + 1], pSect->sectname, sizeof(pSect->sectname)) == 0
                && pThis->aSegments[iSeg].SegInfo.cchName - cchSegName - 1 <= sizeof(pSect->sectname) )
                break;
        }
    }
    if (iSeg >= pThis->cSegments)
        return VERR_SYMBOL_NOT_FOUND;

    if (!s_aPrefixes[iPrefix].fSection)
    {
        /*
         * Calculate the segment start/end address.
         */
        uValue = pThis->aSegments[iSeg].SegInfo.RVA;
        if (!s_aPrefixes[iPrefix].fStart)
            uValue += pThis->aSegments[iSeg].SegInfo.cb;
    }
    else
    {
        /*
         * Locate the section.
         */
        uint32_t iSect = pThis->aSegments[iSeg].cSections;
        if (!iSect)
            return VERR_SYMBOL_NOT_FOUND;
        for (;;)
        {
            section_32_t *pSect = (section_32_t *)pThis->aSegments[iSeg].paSections[iSect].pvMachoSection;
            if (   cchSectName <= sizeof(pSect->sectname)
                && memcmp(pSect->sectname, pchSectName, cchSectName) == 0
                && (   cchSectName == sizeof(pSect->sectname)
                    || pSect->sectname[cchSectName] == '\0') )
                break;
            /* next */
            if (!iSect)
                return VERR_SYMBOL_NOT_FOUND;
            iSect--;
        }

        uValue = pThis->aSegments[iSeg].paSections[iSect].RVA;
        if (!s_aPrefixes[iPrefix].fStart)
            uValue += pThis->aSegments[iSeg].paSections[iSect].cb;
    }

    /*
     * Convert from RVA to load address.
     */
    uValue += BaseAddress;
    if (puValue)
        *puValue = uValue;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnGetSymbolEx}
 */
static DECLCALLBACK(int) rtldrMachO_GetSymbolEx(PRTLDRMODINTERNAL pMod, const void *pvBits, RTUINTPTR BaseAddress,
                                                uint32_t iOrdinal, const char *pszSymbol, RTUINTPTR *pValue)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    RT_NOREF(pvBits);
    //RT_NOREF(pszVersion);
    //RT_NOREF(pfnGetForwarder);
    //RT_NOREF(pvUser);
    uint32_t fKind = RTLDRSYMKIND_REQ_FLAT;
    uint32_t *pfKind = &fKind;
    size_t cchSymbol = pszSymbol ? strlen(pszSymbol) : 0;

    /*
     * Resolve defaults.
     */
    kldrModMachOAdjustBaseAddress(pThis, &BaseAddress);

    /*
     * Refuse segmented requests for now.
     */
    RTLDRMODMACHO_CHECK_RETURN(   !pfKind
                              || (*pfKind & RTLDRSYMKIND_REQ_TYPE_MASK) == RTLDRSYMKIND_REQ_FLAT,
                              VERR_LDRMACHO_TODO);

    /*
     * Take action according to file type.
     */
    int rc;
    if (   pThis->Hdr.filetype == MH_OBJECT
        || pThis->Hdr.filetype == MH_EXECUTE /** @todo dylib, execute, dsym: symbols */
        || pThis->Hdr.filetype == MH_DYLIB
        || pThis->Hdr.filetype == MH_BUNDLE
        || pThis->Hdr.filetype == MH_DSYM
        || pThis->Hdr.filetype == MH_KEXT_BUNDLE)
    {
        rc = kldrModMachOLoadObjSymTab(pThis);
        if (RT_SUCCESS(rc))
        {
            if (    pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
                ||  pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
                rc = kldrModMachODoQuerySymbol32Bit(pThis, (macho_nlist_32_t *)pThis->pvaSymbols, pThis->cSymbols,
                                                    pThis->pchStrings, pThis->cchStrings, BaseAddress, iOrdinal, pszSymbol,
                                                    (uint32_t)cchSymbol, pValue, pfKind);
            else
                rc = kldrModMachODoQuerySymbol64Bit(pThis, (macho_nlist_64_t *)pThis->pvaSymbols, pThis->cSymbols,
                                                    pThis->pchStrings, pThis->cchStrings, BaseAddress, iOrdinal, pszSymbol,
                                                    (uint32_t)cchSymbol, pValue, pfKind);
        }

        /*
         * Check for link-editor generated symbols and supply what we can.
         *
         * As small service to clients that insists on adding a '_' prefix
         * before querying symbols, we will ignore the prefix.
         */
        if (  rc == VERR_SYMBOL_NOT_FOUND
            && cchSymbol > sizeof("section$end$") - 1
            && (    pszSymbol[0] == 's'
                || (pszSymbol[1] == 's' && pszSymbol[0] == '_') )
            && memchr(pszSymbol, '$', cchSymbol) )
        {
            if (pszSymbol[0] == '_')
                rc = kldrModMachOQueryLinkerSymbol(pThis, pszSymbol + 1, cchSymbol - 1, BaseAddress, pValue);
            else
                rc = kldrModMachOQueryLinkerSymbol(pThis, pszSymbol, cchSymbol, BaseAddress, pValue);
        }
    }
    else
        rc = VERR_LDRMACHO_TODO;

    return rc;
}


/**
 * Lookup a symbol in a 32-bit symbol table.
 *
 * @returns IPRT status code.
 * @param   pThis
 * @param   paSyms      Pointer to the symbol table.
 * @param   cSyms       Number of symbols in the table.
 * @param   pchStrings  Pointer to the string table.
 * @param   cchStrings  Size of the string table.
 * @param   BaseAddress Adjusted base address, see kLdrModQuerySymbol.
 * @param   iSymbol     See kLdrModQuerySymbol.
 * @param   pchSymbol   See kLdrModQuerySymbol.
 * @param   cchSymbol   See kLdrModQuerySymbol.
 * @param   puValue     See kLdrModQuerySymbol.
 * @param   pfKind      See kLdrModQuerySymbol.
 */
static int kldrModMachODoQuerySymbol32Bit(PRTLDRMODMACHO pThis, const macho_nlist_32_t *paSyms, uint32_t cSyms,
                                          const char *pchStrings, uint32_t cchStrings, RTLDRADDR BaseAddress, uint32_t iSymbol,
                                          const char *pchSymbol, uint32_t cchSymbol, PRTLDRADDR puValue, uint32_t *pfKind)
{
    /*
     * Find a valid symbol matching the search criteria.
     */
    if (iSymbol == UINT32_MAX)
    {
        /* simplify validation. */
        /** @todo figure out a better way to deal with underscore prefixes. sigh. */
        if (cchStrings <= cchSymbol + 1)
            return VERR_SYMBOL_NOT_FOUND;
        cchStrings -= cchSymbol + 1;

        /* external symbols are usually at the end, so search the other way. */
        for (iSymbol = cSyms - 1; iSymbol != UINT32_MAX; iSymbol--)
        {
            const char *psz;

            /* Skip irrellevant and non-public symbols. */
            if (paSyms[iSymbol].n_type & MACHO_N_STAB)
                continue;
            if ((paSyms[iSymbol].n_type & MACHO_N_TYPE) == MACHO_N_UNDF)
                continue;
            if (!(paSyms[iSymbol].n_type & MACHO_N_EXT)) /*??*/
                continue;
            if (paSyms[iSymbol].n_type & MACHO_N_PEXT) /*??*/
                continue;

            /* get name */
            if (!paSyms[iSymbol].n_un.n_strx)
                continue;
            if ((uint32_t)paSyms[iSymbol].n_un.n_strx >= cchStrings)
                continue;
            psz = &pchStrings[paSyms[iSymbol].n_un.n_strx];
            if (psz[cchSymbol + 1])
                continue;
            /** @todo figure out a better way to deal with underscore prefixes. sigh. */
            if (*psz != '_' || memcmp(psz + 1, pchSymbol, cchSymbol))
                continue;

            /* match! */
            break;
        }
        if (iSymbol == UINT32_MAX)
            return VERR_SYMBOL_NOT_FOUND;
    }
    else
    {
        if (iSymbol >= cSyms)
            return VERR_SYMBOL_NOT_FOUND;
        if (paSyms[iSymbol].n_type & MACHO_N_STAB)
            return VERR_SYMBOL_NOT_FOUND;
        if ((paSyms[iSymbol].n_type & MACHO_N_TYPE) == MACHO_N_UNDF)
            return VERR_SYMBOL_NOT_FOUND;
    }

    /*
     * Calc the return values.
     */
    if (pfKind)
    {
        if (    pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
            ||  pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
            *pfKind = RTLDRSYMKIND_32BIT | RTLDRSYMKIND_NO_TYPE;
        else
            *pfKind = RTLDRSYMKIND_64BIT | RTLDRSYMKIND_NO_TYPE;
        if (paSyms[iSymbol].n_desc & N_WEAK_DEF)
            *pfKind |= RTLDRSYMKIND_WEAK;
    }

    switch (paSyms[iSymbol].n_type & MACHO_N_TYPE)
    {
        case MACHO_N_SECT:
        {
            PRTLDRMODMACHOSECT pSect;
            RTLDRADDR offSect;
            RTLDRMODMACHO_CHECK_RETURN((uint32_t)(paSyms[iSymbol].n_sect - 1) < pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
            pSect = &pThis->paSections[paSyms[iSymbol].n_sect - 1];

            offSect = paSyms[iSymbol].n_value - pSect->LinkAddress;
            RTLDRMODMACHO_CHECK_RETURN(   offSect <= pSect->cb
                                      || (   paSyms[iSymbol].n_sect == 1 /* special hack for __mh_execute_header */
                                          && offSect == 0U - pSect->RVA
                                          && pThis->uEffFileType != MH_OBJECT),
                                      VERR_LDRMACHO_BAD_SYMBOL);
            if (puValue)
                *puValue = BaseAddress + pSect->RVA + offSect;

            if (    pfKind
                &&  (pSect->fFlags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SELF_MODIFYING_CODE)))
                *pfKind = (*pfKind & ~RTLDRSYMKIND_TYPE_MASK) | RTLDRSYMKIND_CODE;
            break;
        }

        case MACHO_N_ABS:
            if (puValue)
                *puValue = paSyms[iSymbol].n_value;
            /*if (pfKind)
                pfKind |= RTLDRSYMKIND_ABS;*/
            break;

        case MACHO_N_PBUD:
        case MACHO_N_INDR:
            /** @todo implement indirect and prebound symbols. */
        default:
            RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
    }

    return VINF_SUCCESS;
}


/**
 * Lookup a symbol in a 64-bit symbol table.
 *
 * @returns IPRT status code.
 * @param   pThis
 * @param   paSyms      Pointer to the symbol table.
 * @param   cSyms       Number of symbols in the table.
 * @param   pchStrings  Pointer to the string table.
 * @param   cchStrings  Size of the string table.
 * @param   BaseAddress Adjusted base address, see kLdrModQuerySymbol.
 * @param   iSymbol     See kLdrModQuerySymbol.
 * @param   pchSymbol   See kLdrModQuerySymbol.
 * @param   cchSymbol   See kLdrModQuerySymbol.
 * @param   puValue     See kLdrModQuerySymbol.
 * @param   pfKind      See kLdrModQuerySymbol.
 */
static int kldrModMachODoQuerySymbol64Bit(PRTLDRMODMACHO pThis, const macho_nlist_64_t *paSyms, uint32_t cSyms,
                                          const char *pchStrings, uint32_t cchStrings, RTLDRADDR BaseAddress, uint32_t iSymbol,
                                          const char *pchSymbol, uint32_t cchSymbol, PRTLDRADDR puValue, uint32_t *pfKind)
{
    /*
     * Find a valid symbol matching the search criteria.
     */
    if (iSymbol == UINT32_MAX)
    {
        /* simplify validation. */
        /** @todo figure out a better way to deal with underscore prefixes. sigh. */
        if (cchStrings <= cchSymbol + 1)
            return VERR_SYMBOL_NOT_FOUND;
        cchStrings -= cchSymbol + 1;

        /* external symbols are usually at the end, so search the other way. */
        for (iSymbol = cSyms - 1; iSymbol != UINT32_MAX; iSymbol--)
        {
            const char *psz;

            /* Skip irrellevant and non-public symbols. */
            if (paSyms[iSymbol].n_type & MACHO_N_STAB)
                continue;
            if ((paSyms[iSymbol].n_type & MACHO_N_TYPE) == MACHO_N_UNDF)
                continue;
            if (!(paSyms[iSymbol].n_type & MACHO_N_EXT)) /*??*/
                continue;
            if (paSyms[iSymbol].n_type & MACHO_N_PEXT) /*??*/
                continue;

            /* get name */
            if (!paSyms[iSymbol].n_un.n_strx)
                continue;
            if ((uint32_t)paSyms[iSymbol].n_un.n_strx >= cchStrings)
                continue;
            psz = &pchStrings[paSyms[iSymbol].n_un.n_strx];
            if (psz[cchSymbol + 1])
                continue;
            if (*psz != '_' || memcmp(psz + 1, pchSymbol, cchSymbol))
                continue;

            /* match! */
            break;
        }
        if (iSymbol == UINT32_MAX)
            return VERR_SYMBOL_NOT_FOUND;
    }
    else
    {
        if (iSymbol >= cSyms)
            return VERR_SYMBOL_NOT_FOUND;
        if (paSyms[iSymbol].n_type & MACHO_N_STAB)
            return VERR_SYMBOL_NOT_FOUND;
        if ((paSyms[iSymbol].n_type & MACHO_N_TYPE) == MACHO_N_UNDF)
            return VERR_SYMBOL_NOT_FOUND;
    }

    /*
     * Calc the return values.
     */
    if (pfKind)
    {
        if (    pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
            ||  pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
            *pfKind = RTLDRSYMKIND_32BIT | RTLDRSYMKIND_NO_TYPE;
        else
            *pfKind = RTLDRSYMKIND_64BIT | RTLDRSYMKIND_NO_TYPE;
        if (paSyms[iSymbol].n_desc & N_WEAK_DEF)
            *pfKind |= RTLDRSYMKIND_WEAK;
    }

    switch (paSyms[iSymbol].n_type & MACHO_N_TYPE)
    {
        case MACHO_N_SECT:
        {
            PRTLDRMODMACHOSECT pSect;
            RTLDRADDR offSect;
            RTLDRMODMACHO_CHECK_RETURN((uint32_t)(paSyms[iSymbol].n_sect - 1) < pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
            pSect = &pThis->paSections[paSyms[iSymbol].n_sect - 1];

            offSect = paSyms[iSymbol].n_value - pSect->LinkAddress;
            RTLDRMODMACHO_CHECK_RETURN(   offSect <= pSect->cb
                                      || (   paSyms[iSymbol].n_sect == 1 /* special hack for __mh_execute_header */
                                          && offSect == 0U - pSect->RVA
                                          && pThis->uEffFileType != MH_OBJECT),
                                      VERR_LDRMACHO_BAD_SYMBOL);
            if (puValue)
                *puValue = BaseAddress + pSect->RVA + offSect;

            if (    pfKind
                &&  (pSect->fFlags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SELF_MODIFYING_CODE)))
                *pfKind = (*pfKind & ~RTLDRSYMKIND_TYPE_MASK) | RTLDRSYMKIND_CODE;
            break;
        }

        case MACHO_N_ABS:
            if (puValue)
                *puValue = paSyms[iSymbol].n_value;
            /*if (pfKind)
                pfKind |= RTLDRSYMKIND_ABS;*/
            break;

        case MACHO_N_PBUD:
        case MACHO_N_INDR:
            /** @todo implement indirect and prebound symbols. */
        default:
            RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnEnumSymbols}
 */
static DECLCALLBACK(int) rtldrMachO_EnumSymbols(PRTLDRMODINTERNAL pMod, unsigned fFlags, const void *pvBits,
                                                RTUINTPTR BaseAddress, PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    RT_NOREF(pvBits);

    /*
     * Resolve defaults.
     */
    kldrModMachOAdjustBaseAddress(pThis, &BaseAddress);

    /*
     * Take action according to file type.
     */
    int rc;
    if (   pThis->Hdr.filetype == MH_OBJECT
        || pThis->Hdr.filetype == MH_EXECUTE /** @todo dylib, execute, dsym: symbols */
        || pThis->Hdr.filetype == MH_DYLIB
        || pThis->Hdr.filetype == MH_BUNDLE
        || pThis->Hdr.filetype == MH_DSYM
        || pThis->Hdr.filetype == MH_KEXT_BUNDLE)
    {
        rc = kldrModMachOLoadObjSymTab(pThis);
        if (RT_SUCCESS(rc))
        {
            if (    pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
                ||  pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
                rc = kldrModMachODoEnumSymbols32Bit(pThis, (macho_nlist_32_t *)pThis->pvaSymbols, pThis->cSymbols,
                                                    pThis->pchStrings, pThis->cchStrings, BaseAddress,
                                                    fFlags, pfnCallback, pvUser);
            else
                rc = kldrModMachODoEnumSymbols64Bit(pThis, (macho_nlist_64_t *)pThis->pvaSymbols, pThis->cSymbols,
                                                    pThis->pchStrings, pThis->cchStrings, BaseAddress,
                                                    fFlags, pfnCallback, pvUser);
        }
    }
    else
        RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);

    return rc;
}


/**
 * Enum a 32-bit symbol table.
 *
 * @returns IPRT status code.
 * @param   pThis
 * @param   paSyms      Pointer to the symbol table.
 * @param   cSyms       Number of symbols in the table.
 * @param   pchStrings  Pointer to the string table.
 * @param   cchStrings  Size of the string table.
 * @param   BaseAddress Adjusted base address, see kLdrModEnumSymbols.
 * @param   fFlags      See kLdrModEnumSymbols.
 * @param   pfnCallback See kLdrModEnumSymbols.
 * @param   pvUser      See kLdrModEnumSymbols.
 */
static int kldrModMachODoEnumSymbols32Bit(PRTLDRMODMACHO pThis, const macho_nlist_32_t *paSyms, uint32_t cSyms,
                                          const char *pchStrings, uint32_t cchStrings, RTLDRADDR BaseAddress,
                                          uint32_t fFlags, PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    const uint32_t fKindBase =    pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
                           || pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE
                         ? RTLDRSYMKIND_32BIT : RTLDRSYMKIND_64BIT;
    uint32_t iSym;
    int rc;

    /*
     * Iterate the symbol table.
     */
    for (iSym = 0; iSym < cSyms; iSym++)
    {
        uint32_t fKind;
        RTLDRADDR uValue;
        const char *psz;
        size_t cch;

        /* Skip debug symbols and undefined symbols. */
        if (paSyms[iSym].n_type & MACHO_N_STAB)
            continue;
        if ((paSyms[iSym].n_type & MACHO_N_TYPE) == MACHO_N_UNDF)
            continue;

        /* Skip non-public symbols unless they are requested explicitly. */
        if (!(fFlags & RTLDR_ENUM_SYMBOL_FLAGS_ALL))
        {
            if (!(paSyms[iSym].n_type & MACHO_N_EXT)) /*??*/
                continue;
            if (paSyms[iSym].n_type & MACHO_N_PEXT) /*??*/
                continue;
            if (!paSyms[iSym].n_un.n_strx)
                continue;
        }

        /*
         * Gather symbol info
         */

        /* name */
        RTLDRMODMACHO_CHECK_RETURN((uint32_t)paSyms[iSym].n_un.n_strx < cchStrings, VERR_LDRMACHO_BAD_SYMBOL);
        psz = &pchStrings[paSyms[iSym].n_un.n_strx];
        cch = strlen(psz);
        if (!cch)
            psz = NULL;

        /* kind & value */
        fKind = fKindBase;
        if (paSyms[iSym].n_desc & N_WEAK_DEF)
            fKind |= RTLDRSYMKIND_WEAK;
        switch (paSyms[iSym].n_type & MACHO_N_TYPE)
        {
            case MACHO_N_SECT:
            {
                PRTLDRMODMACHOSECT pSect;
                RTLDRMODMACHO_CHECK_RETURN((uint32_t)(paSyms[iSym].n_sect - 1) < pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
                pSect = &pThis->paSections[paSyms[iSym].n_sect - 1];

                uValue = paSyms[iSym].n_value - pSect->LinkAddress;
                RTLDRMODMACHO_CHECK_RETURN(   uValue <= pSect->cb
                                          || (   paSyms[iSym].n_sect == 1 /* special hack for __mh_execute_header */
                                              && uValue == 0U - pSect->RVA
                                              && pThis->uEffFileType != MH_OBJECT),
                                          VERR_LDRMACHO_BAD_SYMBOL);
                uValue += BaseAddress + pSect->RVA;

                if (pSect->fFlags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SELF_MODIFYING_CODE))
                    fKind |= RTLDRSYMKIND_CODE;
                else
                    fKind |= RTLDRSYMKIND_NO_TYPE;
                break;
            }

            case MACHO_N_ABS:
                uValue = paSyms[iSym].n_value;
                fKind |= RTLDRSYMKIND_NO_TYPE /*RTLDRSYMKIND_ABS*/;
                break;

            case MACHO_N_PBUD:
            case MACHO_N_INDR:
                /** @todo implement indirect and prebound symbols. */
            default:
                RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
        }

        /*
         * Do callback.
         */
        /** @todo figure out a better way to deal with underscore prefixes. sigh. */
        if (cch > 1 && *psz == '_')
            psz++;
        rc = pfnCallback(&pThis->Core, psz, iSym, uValue/*, fKind*/, pvUser);
        if (rc != VINF_SUCCESS)
            return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Enum a 64-bit symbol table.
 *
 * @returns IPRT status code.
 * @param   pThis
 * @param   paSyms      Pointer to the symbol table.
 * @param   cSyms       Number of symbols in the table.
 * @param   pchStrings  Pointer to the string table.
 * @param   cchStrings  Size of the string table.
 * @param   BaseAddress Adjusted base address, see kLdrModEnumSymbols.
 * @param   fFlags      See kLdrModEnumSymbols.
 * @param   pfnCallback See kLdrModEnumSymbols.
 * @param   pvUser      See kLdrModEnumSymbols.
 */
static int kldrModMachODoEnumSymbols64Bit(PRTLDRMODMACHO pThis, const macho_nlist_64_t *paSyms, uint32_t cSyms,
                                          const char *pchStrings, uint32_t cchStrings, RTLDRADDR BaseAddress,
                                          uint32_t fFlags, PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    const uint32_t fKindBase =    pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE
                           || pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE_OE
                         ? RTLDRSYMKIND_64BIT : RTLDRSYMKIND_32BIT;
    uint32_t iSym;
    int rc;

    /*
     * Iterate the symbol table.
     */
    for (iSym = 0; iSym < cSyms; iSym++)
    {
        uint32_t fKind;
        RTLDRADDR uValue;
        const char *psz;
        size_t cch;

        /* Skip debug symbols and undefined symbols. */
        if (paSyms[iSym].n_type & MACHO_N_STAB)
            continue;
        if ((paSyms[iSym].n_type & MACHO_N_TYPE) == MACHO_N_UNDF)
            continue;

        /* Skip non-public symbols unless they are requested explicitly. */
        if (!(fFlags & RTLDR_ENUM_SYMBOL_FLAGS_ALL))
        {
            if (!(paSyms[iSym].n_type & MACHO_N_EXT)) /*??*/
                continue;
            if (paSyms[iSym].n_type & MACHO_N_PEXT) /*??*/
                continue;
            if (!paSyms[iSym].n_un.n_strx)
                continue;
        }

        /*
         * Gather symbol info
         */

        /* name */
        RTLDRMODMACHO_CHECK_RETURN((uint32_t)paSyms[iSym].n_un.n_strx < cchStrings, VERR_LDRMACHO_BAD_SYMBOL);
        psz = &pchStrings[paSyms[iSym].n_un.n_strx];
        cch = strlen(psz);
        if (!cch)
            psz = NULL;

        /* kind & value */
        fKind = fKindBase;
        if (paSyms[iSym].n_desc & N_WEAK_DEF)
            fKind |= RTLDRSYMKIND_WEAK;
        switch (paSyms[iSym].n_type & MACHO_N_TYPE)
        {
            case MACHO_N_SECT:
            {
                PRTLDRMODMACHOSECT pSect;
                RTLDRMODMACHO_CHECK_RETURN((uint32_t)(paSyms[iSym].n_sect - 1) < pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
                pSect = &pThis->paSections[paSyms[iSym].n_sect - 1];

                uValue = paSyms[iSym].n_value - pSect->LinkAddress;
                RTLDRMODMACHO_CHECK_RETURN(   uValue <= pSect->cb
                                          || (   paSyms[iSym].n_sect == 1 /* special hack for __mh_execute_header */
                                              && uValue == 0U - pSect->RVA
                                              && pThis->uEffFileType != MH_OBJECT),
                                          VERR_LDRMACHO_BAD_SYMBOL);
                uValue += BaseAddress + pSect->RVA;

                if (pSect->fFlags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SELF_MODIFYING_CODE))
                    fKind |= RTLDRSYMKIND_CODE;
                else
                    fKind |= RTLDRSYMKIND_NO_TYPE;
                break;
            }

            case MACHO_N_ABS:
                uValue = paSyms[iSym].n_value;
                fKind |= RTLDRSYMKIND_NO_TYPE /*RTLDRSYMKIND_ABS*/;
                break;

            case MACHO_N_PBUD:
            case MACHO_N_INDR:
                /** @todo implement indirect and prebound symbols. */
            default:
                RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
        }

        /*
         * Do callback.
         */
        /** @todo figure out a better way to deal with underscore prefixes. sigh. */
        if (cch > 1 && *psz == '_')
            psz++;
        rc = pfnCallback(&pThis->Core, psz, iSym, uValue/*, fKind*/, pvUser);
        if (rc != VINF_SUCCESS)
            return rc;
    }
    return VINF_SUCCESS;
}

#if 0

/** @copydoc kLdrModGetImport */
static int kldrModMachOGetImport(PRTLDRMODINTERNAL pMod, const void *pvBits, uint32_t iImport, char *pszName, size_t cchName)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    RT_NOREF(pvBits);
    RT_NOREF(iImport);
    RT_NOREF(pszName);
    RT_NOREF(cchName);

    if (pThis->Hdr.filetype == MH_OBJECT)
        return KLDR_ERR_IMPORT_ORDINAL_OUT_OF_BOUNDS;

    /* later */
    return KLDR_ERR_IMPORT_ORDINAL_OUT_OF_BOUNDS;
}



/** @copydoc kLdrModNumberOfImports */
static int32_t kldrModMachONumberOfImports(PRTLDRMODINTERNAL pMod, const void *pvBits)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    RT_NOREF(pvBits);

    if (pThis->Hdr.filetype == MH_OBJECT)
        return VINF_SUCCESS;

    /* later */
    return VINF_SUCCESS;
}


/** @copydoc kLdrModGetStackInfo */
static int kldrModMachOGetStackInfo(PRTLDRMODINTERNAL pMod, const void *pvBits, RTLDRADDR BaseAddress, PKLDRSTACKINFO pStackInfo)
{
    /*PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);*/
    RT_NOREF(pMod);
    RT_NOREF(pvBits);
    RT_NOREF(BaseAddress);

    pStackInfo->Address = NIL_RTLDRADDR;
    pStackInfo->LinkAddress = NIL_RTLDRADDR;
    pStackInfo->cbStack = pStackInfo->cbStackThread = 0;
    /* later */

    return VINF_SUCCESS;
}


/** @copydoc kLdrModQueryMainEntrypoint */
static int kldrModMachOQueryMainEntrypoint(PRTLDRMODINTERNAL pMod, const void *pvBits, RTLDRADDR BaseAddress, PRTLDRADDR pMainEPAddress)
{
#if 0
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    int rc;

    /*
     * Resolve base address alias if any.
     */
    rc = kldrModMachOBitsAndBaseAddress(pThis, NULL, &BaseAddress);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Convert the address from the header.
     */
    *pMainEPAddress = pThis->Hdrs.OptionalHeader.AddressOfEntryPoint
        ? BaseAddress + pThis->Hdrs.OptionalHeader.AddressOfEntryPoint
        : NIL_RTLDRADDR;
#else
    *pMainEPAddress = NIL_RTLDRADDR;
    RT_NOREF(pvBits);
    RT_NOREF(BaseAddress);
    RT_NOREF(pMod);
#endif
    return VINF_SUCCESS;
}

#endif


/**
 * @interface_method_impl{RTLDROPS,pfnEnumDbgInfo}
 */
static DECLCALLBACK(int) rtldrMachO_EnumDbgInfo(PRTLDRMODINTERNAL pMod, const void *pvBits, PFNRTLDRENUMDBG pfnCallback, void *pvUser)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    int rc = VINF_SUCCESS;
    uint32_t iSect;
    RT_NOREF(pvBits);

    for (iSect = 0; iSect < pThis->cSections; iSect++)
    {
        /* (32-bit & 64-bit starts the same way) */
        section_32_t *pMachOSect = (section_32_t *)pThis->paSections[iSect].pvMachoSection;
        char          szTmp[sizeof(pMachOSect->sectname) + 1];

        if (strcmp(pMachOSect->segname, "__DWARF"))
            continue;

        memcpy(szTmp, pMachOSect->sectname, sizeof(pMachOSect->sectname));
        szTmp[sizeof(pMachOSect->sectname)] = '\0';

        RTLDRDBGINFO DbgInfo;
        DbgInfo.enmType            = RTLDRDBGINFOTYPE_DWARF;
        DbgInfo.iDbgInfo           = iSect;
        DbgInfo.LinkAddress        = pThis->paSections[iSect].LinkAddress;
        DbgInfo.cb                 = pThis->paSections[iSect].cb;
        DbgInfo.pszExtFile         = NULL;
        DbgInfo.u.Dwarf.pszSection = szTmp;
        rc = pfnCallback(&pThis->Core, &DbgInfo, pvUser);
        if (rc != VINF_SUCCESS)
            break;
    }

    return rc;
}

#if 0

/** @copydoc kLdrModHasDbgInfo */
static int kldrModMachOHasDbgInfo(PRTLDRMODINTERNAL pMod, const void *pvBits)
{
    /*PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);*/

#if 0
    /*
     * Base this entirely on the presence of a debug directory.
     */
    if (    pThis->Hdrs.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size
            < sizeof(IMAGE_DEBUG_DIRECTORY) /* screw borland linkers */
        ||  !pThis->Hdrs.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress)
        return KLDR_ERR_NO_DEBUG_INFO;
    return VINF_SUCCESS;
#else
    RT_NOREF(pMod);
    RT_NOREF(pvBits);
    return VERR_LDR_NO_DEBUG_INFO;
#endif
}


/** @copydoc kLdrModMap */
static int kldrModMachOMap(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    unsigned fFixed;
    uint32_t i;
    void *pvBase;
    int rc;

    if (!pThis->fCanLoad)
        return VERR_LDRMACHO_TODO;

    /*
     * Already mapped?
     */
    if (pThis->pvMapping)
        return KLDR_ERR_ALREADY_MAPPED;

    /*
     * Map it.
     */
    /* fixed image? */
    fFixed = pMod->enmType == KLDRTYPE_EXECUTABLE_FIXED
          || pMod->enmType == KLDRTYPE_SHARED_LIBRARY_FIXED;
    if (!fFixed)
        pvBase = NULL;
    else
    {
        pvBase = (void *)(uintptr_t)pMod->aSegments[0].LinkAddress;
        if ((uintptr_t)pvBase != pMod->aSegments[0].LinkAddress)
            return VERR_LDR_ADDRESS_OVERFLOW;
    }

    /* try do the prepare */
    rc = kRdrMap(pMod->pRdr, &pvBase, pMod->cSegments, pMod->aSegments, fFixed);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Update the segments with their map addresses.
     */
    for (i = 0; i < pMod->cSegments; i++)
    {
        if (pMod->aSegments[i].RVA != NIL_RTLDRADDR)
            pMod->aSegments[i].MapAddress = (uintptr_t)pvBase + (uintptr_t)pMod->aSegments[i].RVA;
    }
    pThis->pvMapping = pvBase;

    return VINF_SUCCESS;
}


/** @copydoc kLdrModUnmap */
static int kldrModMachOUnmap(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    uint32_t i;
    int rc;

    /*
     * Mapped?
     */
    if (!pThis->pvMapping)
        return KLDR_ERR_NOT_MAPPED;

    /*
     * Try unmap the image.
     */
    rc = kRdrUnmap(pMod->pRdr, pThis->pvMapping, pMod->cSegments, pMod->aSegments);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Update the segments to reflect that they aren't mapped any longer.
     */
    pThis->pvMapping = NULL;
    for (i = 0; i < pMod->cSegments; i++)
        pMod->aSegments[i].MapAddress = 0;

    return VINF_SUCCESS;
}


/** @copydoc kLdrModAllocTLS */
static int kldrModMachOAllocTLS(PRTLDRMODINTERNAL pMod, void *pvMapping)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);

    /*
     * Mapped?
     */
    if (   pvMapping == KLDRMOD_INT_MAP
        && !pThis->pvMapping )
        return KLDR_ERR_NOT_MAPPED;
    return VINF_SUCCESS;
}


/** @copydoc kLdrModFreeTLS */
static void kldrModMachOFreeTLS(PRTLDRMODINTERNAL pMod, void *pvMapping)
{
    RT_NOREF(pMod);
    RT_NOREF(pvMapping);
}



/** @copydoc kLdrModReload */
static int kldrModMachOReload(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);

    /*
     * Mapped?
     */
    if (!pThis->pvMapping)
        return KLDR_ERR_NOT_MAPPED;

    /* the file provider does it all */
    return kRdrRefresh(pMod->pRdr, pThis->pvMapping, pMod->cSegments, pMod->aSegments);
}


/** @copydoc kLdrModFixupMapping */
static int kldrModMachOFixupMapping(PRTLDRMODINTERNAL pMod, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    int rc, rc2;

    /*
     * Mapped?
     */
    if (!pThis->pvMapping)
        return KLDR_ERR_NOT_MAPPED;

    /*
     * Before doing anything we'll have to make all pages writable.
     */
    rc = kRdrProtect(pMod->pRdr, pThis->pvMapping, pMod->cSegments, pMod->aSegments, 1 /* unprotect */);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Resolve imports and apply base relocations.
     */
    rc = rtldrMachO_RelocateBits(pMod, pThis->pvMapping, (uintptr_t)pThis->pvMapping, pThis->LinkAddress,
                                      pfnGetImport, pvUser);

    /*
     * Restore protection.
     */
    rc2 = kRdrProtect(pMod->pRdr, pThis->pvMapping, pMod->cSegments, pMod->aSegments, 0 /* protect */);
    if (RT_SUCCESS(rc) && RT_FAILURE(rc2)
        rc = rc2;
    return rc;
}

#endif


/**
 * Worker for resolving an undefined 32-bit symbol table entry.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   pSym            The symbol table entry.
 * @param   BaseAddress     The module base address.
 * @param   pfnGetImport    The callback for resolving an imported symbol.
 * @param   pvUser          User argument to the callback.
 */
DECLINLINE(int) rtdlrModMachOHandleUndefinedSymbol32(PRTLDRMODMACHO pThis, macho_nlist_32_t *pSym,
                                                     RTLDRADDR BaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    RTLDRADDR Value = NIL_RTLDRADDR;

    /** @todo Implement N_REF_TO_WEAK. */
    RTLDRMODMACHO_CHECK_RETURN(!(pSym->n_desc & N_REF_TO_WEAK), VERR_LDRMACHO_TODO);

    /* Get the symbol name. */
    RTLDRMODMACHO_CHECK_RETURN((uint32_t)pSym->n_un.n_strx < pThis->cchStrings, VERR_LDRMACHO_BAD_SYMBOL);
    const char *pszSymbol = &pThis->pchStrings[pSym->n_un.n_strx];
    size_t      cchSymbol = strlen(pszSymbol);

    /* Check for linker defined symbols relating to sections and segments. */
    int rc;
    if (   cchSymbol <= sizeof("section$end$") - 1
        ||  *pszSymbol != 's'
        || memchr(pszSymbol, '$', cchSymbol) == NULL)
        rc = VERR_SYMBOL_NOT_FOUND;
    else
        rc = kldrModMachOQueryLinkerSymbol(pThis, pszSymbol, cchSymbol, BaseAddress, &Value);

    /* Ask the user for an address to the symbol. */
    //uint32_t fKind = RTLDRSYMKIND_REQ_FLAT;
    /** @todo figure out a better way to deal with underscore prefixes. sigh. */
    if (RT_FAILURE_NP(rc))
        rc = pfnGetImport(&pThis->Core, NULL /*pszModule*/, pszSymbol + (pszSymbol[0] == '_'),
                          UINT32_MAX, &Value/*, &fKind*/, pvUser);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    /* If weak reference we can continue, otherwise fail? */
    else if (pSym->n_desc & N_WEAK_REF)
        Value = 0;
    else
        return rc;

    /* Update the symbol. */
    pSym->n_value = (uint32_t)Value;
    if (pSym->n_value == Value)
        return VINF_SUCCESS;
    return VERR_LDR_ADDRESS_OVERFLOW;
}


/**
 * Worker for resolving an undefined 64-bit symbol table entry.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   pSym            The symbol table entry.
 * @param   BaseAddress     The module base address.
 * @param   pfnGetImport    The callback for resolving an imported symbol.
 * @param   pvUser          User argument to the callback.
 */
DECLINLINE(int) rtdlrModMachOHandleUndefinedSymbol64(PRTLDRMODMACHO pThis, macho_nlist_64_t *pSym,
                                                     RTLDRADDR BaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    RTLDRADDR Value = NIL_RTLDRADDR;

    /** @todo Implement N_REF_TO_WEAK. */
    RTLDRMODMACHO_CHECK_RETURN(!(pSym->n_desc & N_REF_TO_WEAK), VERR_LDRMACHO_TODO);

    /* Get the symbol name. */
    RTLDRMODMACHO_CHECK_RETURN((uint32_t)pSym->n_un.n_strx < pThis->cchStrings, VERR_LDRMACHO_BAD_SYMBOL);
    const char *pszSymbol = &pThis->pchStrings[pSym->n_un.n_strx];
    size_t      cchSymbol = strlen(pszSymbol);

    /* Check for linker defined symbols relating to sections and segments. */
    int rc;
    if (   cchSymbol <= sizeof("section$end$") - 1
        ||  *pszSymbol != 's'
        || memchr(pszSymbol, '$', cchSymbol) == NULL)
        rc = VERR_SYMBOL_NOT_FOUND;
    else
        rc = kldrModMachOQueryLinkerSymbol(pThis, pszSymbol, cchSymbol, BaseAddress, &Value);

    /* Ask the user for an address to the symbol. */
    //uint32_t fKind = RTLDRSYMKIND_REQ_FLAT;
    /** @todo figure out a better way to deal with underscore prefixes. sigh. */
    if (RT_FAILURE_NP(rc))
        rc = pfnGetImport(&pThis->Core, NULL /*pszModule*/, pszSymbol + (pszSymbol[0] == '_'),
                          UINT32_MAX, &Value/*, &fKind*/, pvUser);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    /* If weak reference we can continue, otherwise fail? */
    else if (pSym->n_desc & N_WEAK_REF)
        Value = 0;
    else
        return rc;

    /* Update the symbol. */
    pSym->n_value = (uint64_t)Value;
    if (pSym->n_value == Value)
        return VINF_SUCCESS;
    return VERR_LDR_ADDRESS_OVERFLOW;
}


/**
 * MH_OBJECT: Resolves undefined symbols (imports).
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   BaseAddress     The module base address.
 * @param   pfnGetImport    The callback for resolving an imported symbol.
 * @param   pvUser          User argument to the callback.
 */
static int  kldrModMachOObjDoImports(PRTLDRMODMACHO pThis, RTLDRADDR BaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{

    /*
     * Ensure that we've got the symbol table.
     */
    int rc = kldrModMachOLoadObjSymTab(pThis);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Iterate the symbol table and resolve undefined symbols.
     * We currently ignore REFERENCE_TYPE.
     */
    const uint32_t cSyms = pThis->cSymbols;
    if (    pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
        ||  pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
    {
        macho_nlist_32_t *paSyms = (macho_nlist_32_t *)pThis->pvaSymbols;
        for (uint32_t iSym = 0; iSym < cSyms; iSym++)
        {
            /* skip stabs */
            if (paSyms[iSym].n_type & MACHO_N_STAB)
                continue;

            if ((paSyms[iSym].n_type & MACHO_N_TYPE) == MACHO_N_UNDF)
            {
                rc = rtdlrModMachOHandleUndefinedSymbol32(pThis, &paSyms[iSym], BaseAddress, pfnGetImport, pvUser);
                if (RT_FAILURE(rc))
                    break;
            }
            else if (paSyms[iSym].n_desc & N_WEAK_DEF)
            {
                /** @todo implement weak symbols. */
                /*return VERR_LDRMACHO_TODO; - ignored for now. */
            }
        }
    }
    else
    {
        /* (Identical to the 32-bit code, just different paSym type. (and n_strx is unsigned)) */
        macho_nlist_64_t *paSyms = (macho_nlist_64_t *)pThis->pvaSymbols;
        for (uint32_t iSym = 0; iSym < cSyms; iSym++)
        {
            /* skip stabs */
            if (paSyms[iSym].n_type & MACHO_N_STAB)
                continue;

            if ((paSyms[iSym].n_type & MACHO_N_TYPE) == MACHO_N_UNDF)
            {
                rc = rtdlrModMachOHandleUndefinedSymbol64(pThis, &paSyms[iSym], BaseAddress, pfnGetImport, pvUser);
                if (RT_FAILURE(rc))
                    break;
            }
            else if (paSyms[iSym].n_desc & N_WEAK_DEF)
            {
                /** @todo implement weak symbols. */
                /*return VERR_LDRMACHO_TODO; - ignored for now. */
            }
        }
    }

    return rc;
}


/**
 * Dylib: Resolves undefined symbols (imports).
 *
 * This is conceptually identically to kldrModMachOObjDoImports, only
 * LC_DYSYMTAB helps us avoid working over the whole symbol table.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   BaseAddress     The module base address.
 * @param   pfnGetImport    The callback for resolving an imported symbol.
 * @param   pvUser          User argument to the callback.
 */
static int  kldrModMachODylibDoImports(PRTLDRMODMACHO pThis, RTLDRADDR BaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    /*
     * There must be a LC_DYSYMTAB.
     * We might be lucky, though, and not have any imports.
     */
    dysymtab_command_t const *pDySymTab = pThis->pDySymTab;
    AssertReturn(pDySymTab, VERR_INTERNAL_ERROR_2);
    if (pDySymTab->nundefsym == 0)
        return VINF_SUCCESS;

    /*
     * Ensure that we've got the symbol table.
     */
    int rc = kldrModMachOLoadObjSymTab(pThis);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Iterate the give symbol table section containing undefined symbols and resolve them.
     */
    uint32_t const cSyms = pDySymTab->iundefsym + pDySymTab->nundefsym;
    if (   pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
        || pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
    {
        macho_nlist_32_t *paSyms = (macho_nlist_32_t *)pThis->pvaSymbols;
        for (uint32_t iSym = pDySymTab->iundefsym; RT_SUCCESS(rc) && iSym < cSyms; iSym++)
        {
            AssertContinue((paSyms[iSym].n_type & (MACHO_N_TYPE | MACHO_N_STAB)) == MACHO_N_UNDF);
            rc = rtdlrModMachOHandleUndefinedSymbol32(pThis, &paSyms[iSym], BaseAddress, pfnGetImport, pvUser);
        }
    }
    else
    {
        /* (Identical to the 32-bit code, just different paSym type. (and n_strx is unsigned)) */
        macho_nlist_64_t *paSyms = (macho_nlist_64_t *)pThis->pvaSymbols;
        for (uint32_t iSym = pDySymTab->iundefsym; RT_SUCCESS(rc) && iSym < cSyms; iSym++)
        {
            AssertContinue((paSyms[iSym].n_type & (MACHO_N_TYPE | MACHO_N_STAB)) == MACHO_N_UNDF);
            rc = rtdlrModMachOHandleUndefinedSymbol64(pThis, &paSyms[iSym], BaseAddress, pfnGetImport, pvUser);
        }
    }

    return rc;
}


static int kldrModMachODylibDoIndirectSymbols(PRTLDRMODMACHO pThis, void *pvBits, RTLDRADDR offDelta)
{
    /*
     * There must be a LC_DYSYMTAB.
     * We might be lucky, though, and not have any imports.
     */
    dysymtab_command_t const *pDySymTab = pThis->pDySymTab;
    AssertReturn(pDySymTab, VERR_INTERNAL_ERROR_2);
    uint32_t const cIndirectSymbols = pDySymTab->nindirectsymb;
    if (cIndirectSymbols == 0)
        return VINF_SUCCESS;

    /*
     * Ensure that we've got the symbol table.
     */
    int rc = kldrModMachOLoadObjSymTab(pThis);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Load the indirect symbol table.
     */
    if (!pThis->paidxIndirectSymbols)
    {
        uint32_t *paidxIndirectSymbols = (uint32_t *)RTMemAlloc(cIndirectSymbols * sizeof(uint32_t));
        if (!paidxIndirectSymbols)
            return VERR_NO_MEMORY;
        rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, paidxIndirectSymbols, cIndirectSymbols * sizeof(uint32_t),
                                          pDySymTab->indirectsymboff);
        if (RT_SUCCESS(rc))
            pThis->paidxIndirectSymbols = paidxIndirectSymbols;
        else
        {
            RTMemFree(paidxIndirectSymbols);
            return rc;
        }

        /* Byte swap if needed. */
        if (   pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE
            || pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE_OE)
            for (uint32_t i = 0; i < cIndirectSymbols; i++)
                paidxIndirectSymbols[i] = RT_BSWAP_U32(paidxIndirectSymbols[i]);
    }
    uint32_t const *paidxIndirectSymbols = pThis->paidxIndirectSymbols;

    /*
     * Process the sections using indirect symbols.
     */
    const uint32_t cSymbols = pThis->cSymbols;
    if (   pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
        || pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
    {
        macho_nlist_32_t const *paSymbols = (macho_nlist_32_t *)pThis->pvaSymbols;
        for (uint32_t iSect = 0; iSect < pThis->cSections; iSect++)
        {
            section_32_t const *pSect = (section_32_t const *)pThis->paSections[iSect].pvMachoSection;
            switch (pSect->flags & SECTION_TYPE)
            {
                case S_NON_LAZY_SYMBOL_POINTERS:
                case S_LAZY_SYMBOL_POINTERS:
                {
                    uint32_t       *pauDstPtrs = (uint32_t *)((uintptr_t)pvBits + (uintptr_t)pThis->paSections[iSect].RVA);
                    uint32_t  const cDstPtrs   = pThis->paSections[iSect].cb / sizeof(pauDstPtrs[0]);
                    uint32_t  const idxSrcSkip = pSect->reserved1;
                    if ((uint64_t)idxSrcSkip + cDstPtrs > cIndirectSymbols)
                        return VERR_BAD_EXE_FORMAT; /// @todo better error code.

                    for (uint32_t i = 0; i < cDstPtrs; i++)
                    {
                        uint32_t const idxSym = paidxIndirectSymbols[idxSrcSkip + i];
                        if (idxSym == INDIRECT_SYMBOL_LOCAL)
                            pauDstPtrs[i] += (int32_t)offDelta;
                        else if (idxSym != INDIRECT_SYMBOL_ABS)
                        {
                            AssertMsgReturn(idxSym < cSymbols,
                                            ("i=%#x idxSym=%#x cSymbols=%#x iSect=%#x\n", i, idxSym, cSymbols, iSect),
                                            VERR_BAD_EXE_FORMAT); /// @todo better error code.
                            pauDstPtrs[i] = paSymbols[idxSym].n_value;
                        }
                    }
                    break;
                }

                case S_SYMBOL_STUBS:
                    if (   pThis->Core.enmArch == RTLDRARCH_X86_32
                        && (pSect->flags & S_ATTR_SELF_MODIFYING_CODE)
                        && pSect->reserved2 == 5)
                    {
                        uint32_t        uDstRva    = pThis->paSections[iSect].RVA;
                        uint8_t        *pbDst      = (uint8_t *)((uintptr_t)pvBits + uDstRva);
                        uint32_t  const cDstPtrs   = pThis->paSections[iSect].cb / 5;
                        uint32_t  const idxSrcSkip = pSect->reserved1;
                        if ((uint64_t)idxSrcSkip + cDstPtrs > cIndirectSymbols)
                            return VERR_BAD_EXE_FORMAT; /// @todo better error code.

                        for (uint32_t i = 0; i < cDstPtrs; i++, uDstRva += 5, pbDst += 5)
                        {
                            uint32_t const idxSym = paidxIndirectSymbols[idxSrcSkip + i];
                            if (idxSym != INDIRECT_SYMBOL_ABS && idxSym != INDIRECT_SYMBOL_LOCAL)
                            {
                                AssertMsgReturn(idxSym < cSymbols,
                                                ("i=%#x idxSym=%#x cSymbols=%#x iSect=%#x\n", i, idxSym, cSymbols, iSect),
                                                VERR_BAD_EXE_FORMAT); /// @todo better error code.
                                pbDst[0] = 0xeb; /* JMP rel32 */
                                uint32_t offDisp = paSymbols[idxSym].n_value - (uint32_t)uDstRva - 5;
                                pbDst[1] = (uint8_t)offDisp;
                                offDisp >>= 8;
                                pbDst[2] = (uint8_t)offDisp;
                                offDisp >>= 8;
                                pbDst[3] = (uint8_t)offDisp;
                                offDisp >>= 8;
                                pbDst[4] = (uint8_t)offDisp;
                            }
                        }
                        break;
                    }
                    break;
            }

        }
    }
    else
    {
        /* Exact like for 32-bit, except for 64-bit symbol table, 64-bit addresses and no need to process S_SYMBOL_STUBS. */
        macho_nlist_64_t const *paSymbols = (macho_nlist_64_t *)pThis->pvaSymbols;
        for (uint32_t iSect = 0; iSect < pThis->cSections; iSect++)
        {
            section_64_t const *pSect = (section_64_t const *)pThis->paSections[iSect].pvMachoSection;
            switch (pSect->flags & SECTION_TYPE)
            {
                case S_NON_LAZY_SYMBOL_POINTERS:
                case S_LAZY_SYMBOL_POINTERS:
                {
                    uint64_t       *pauDstPtrs = (uint64_t *)((uintptr_t)pvBits + (uintptr_t)pThis->paSections[iSect].RVA);
                    uint32_t  const cDstPtrs   = pThis->paSections[iSect].cb / sizeof(pauDstPtrs[0]);
                    uint32_t  const idxSrcSkip = pSect->reserved1;
                    if ((uint64_t)idxSrcSkip + cDstPtrs > cIndirectSymbols)
                        return VERR_BAD_EXE_FORMAT; /// @todo better error code.

                    for (uint32_t i = 0; i < cDstPtrs; i++)
                    {
                        uint32_t const idxSym = paidxIndirectSymbols[idxSrcSkip + i];
                        if (idxSym == INDIRECT_SYMBOL_LOCAL)
                            pauDstPtrs[i] += (int64_t)offDelta;
                        else if (idxSym != INDIRECT_SYMBOL_ABS)
                        {
                            AssertMsgReturn(idxSym < cSymbols,
                                            ("i=%#x idxSym=%#x cSymbols=%#x iSect=%#x\n", i, idxSym, cSymbols, iSect),
                                            VERR_BAD_EXE_FORMAT); /// @todo better error code.
                            pauDstPtrs[i] = paSymbols[idxSym].n_value;
                        }
                    }
                    break;
                }

                case S_SYMBOL_STUBS:
                    if (   pThis->Core.enmArch == RTLDRARCH_X86_32
                        && (pSect->flags & S_ATTR_SELF_MODIFYING_CODE)
                        && pSect->reserved2 == 5)
                        return VERR_BAD_EXE_FORMAT;
                    break;
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * MH_OBJECT: Applies base relocations to an (unprotected) image mapping.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   pvMapping       The mapping to fixup.
 * @param   NewBaseAddress  The address to fixup the mapping to.
 */
static int  kldrModMachOObjDoFixups(PRTLDRMODMACHO pThis, void *pvMapping, RTLDRADDR NewBaseAddress)
{
    /*
     * Ensure that we've got the symbol table.
     */
    int rc = kldrModMachOLoadObjSymTab(pThis);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Iterate over the segments and their sections and apply fixups.
     */
    rc = VINF_SUCCESS;
    for (uint32_t iSeg = 0; RT_SUCCESS(rc) && iSeg < pThis->cSegments; iSeg++)
    {
        PRTLDRMODMACHOSEG pSeg = &pThis->aSegments[iSeg];
        for (uint32_t iSect = 0; iSect < pSeg->cSections; iSect++)
        {
            PRTLDRMODMACHOSECT pSect = &pSeg->paSections[iSect];

            /* skip sections without fixups. */
            if (!pSect->cFixups)
                continue;
            AssertReturn(pSect->paFixups, VERR_INTERNAL_ERROR_4);
            AssertReturn(pSect->pauFixupVirginData, VERR_INTERNAL_ERROR_4);

            /*
             * Apply the fixups.
             */
            uint8_t *pbSectBits = (uint8_t *)pvMapping + (uintptr_t)pSect->RVA;
            if (pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE) /** @todo this aint right. */
                rc = kldrModMachOApplyFixupsGeneric32Bit(pThis, pbSectBits, (size_t)pSect->cb, pSect->RVA, pSect->LinkAddress,
                                                         pSect->paFixups, pSect->cFixups, pSect->pauFixupVirginData,
                                                         (macho_nlist_32_t *)pThis->pvaSymbols, pThis->cSymbols, NewBaseAddress);
            else if (   pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE
                     && pThis->Hdr.cputype == CPU_TYPE_X86_64)
                rc = kldrModMachOApplyFixupsAMD64(pThis, pbSectBits, (size_t)pSect->cb, pSect->RVA,
                                                  pSect->paFixups, pSect->cFixups, pSect->pauFixupVirginData,
                                                  (macho_nlist_64_t *)pThis->pvaSymbols, pThis->cSymbols, NewBaseAddress);
            else
                RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
            if (RT_FAILURE(rc))
                break;
        }
    }

    return rc;
}


/**
 * Dylib: Applies base relocations to an (unprotected) image mapping.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   pvMapping       The mapping to fixup.
 * @param   NewBaseAddress  The address to fixup the mapping to.
 */
static int  kldrModMachODylibDoFixups(PRTLDRMODMACHO pThis, void *pvMapping, RTLDRADDR NewBaseAddress)
{
    /*
     * There must be a LC_DYSYMTAB.
     * We might be lucky, though, and not have any imports.
     */
    dysymtab_command_t const *pDySymTab = pThis->pDySymTab;
    AssertReturn(pDySymTab, VERR_INTERNAL_ERROR_2);
    uint32_t cRelocations = pDySymTab->nlocrel + pDySymTab->nextrel;
    if (cRelocations == 0)
        return VINF_SUCCESS;

    /*
     * Ensure that we've got the symbol table.
     */
    int rc = kldrModMachOLoadObjSymTab(pThis);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Load the relocations if needed.
     */
    macho_relocation_union_t const *paRelocations = pThis->paRelocations;
    if (!paRelocations)
    {
        uint32_t *paRawRelocs = (uint32_t *)RTMemAlloc(cRelocations * sizeof(macho_relocation_union_t));
        if (!paRawRelocs)
            return VERR_NO_MEMORY;
        if (pDySymTab->nextrel)
            rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, paRawRelocs, pDySymTab->nextrel * sizeof(macho_relocation_union_t),
                                              pDySymTab->extreloff);
        if (pDySymTab->nlocrel && RT_SUCCESS(rc))
            rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader,
                                              (uint8_t *)paRawRelocs + pDySymTab->nextrel * sizeof(macho_relocation_union_t),
                                              pDySymTab->nlocrel * sizeof(macho_relocation_union_t), pDySymTab->locreloff);
        if (RT_SUCCESS(rc))
            pThis->paRelocations = (macho_relocation_union_t *)paRawRelocs;
        else
        {
            RTMemFree(paRawRelocs);
            return rc;
        }

        /* Byte swap if needed. */
        if (   pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE
            || pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE_OE)
        {
            for (uint32_t i = 0; i < cRelocations; i++)
            {
                paRawRelocs[i * 2]     = RT_BSWAP_U32(paRawRelocs[i * 2]);
                paRawRelocs[i * 2 + 1] = RT_BSWAP_U32(paRawRelocs[i * 2 + 1]);
            }
            ASMCompilerBarrier();
        }

        paRelocations = pThis->paRelocations;
    }

    /*
     * Apply the fixups.
     */
    if (pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE) /** @todo this aint right. */
        return kldrModMachOApplyFixupsGeneric32Bit(pThis, (uint8_t *)pvMapping, (size_t)pThis->cbImage, 0, pThis->LinkAddress,
                                                   paRelocations, cRelocations, pThis->pauRelocationsVirginData,
                                                   (macho_nlist_32_t *)pThis->pvaSymbols, pThis->cSymbols, NewBaseAddress);
    if (   pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE
        && pThis->Hdr.cputype == CPU_TYPE_X86_64)
        return kldrModMachOApplyFixupsAMD64(pThis, (uint8_t *)pvMapping, (size_t)pThis->cbImage, 0,
                                            paRelocations, cRelocations, pThis->pauRelocationsVirginData,
                                            (macho_nlist_64_t *)pThis->pvaSymbols, pThis->cSymbols, NewBaseAddress);
    RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
}


/**
 * Applies generic fixups to a section in an image of the same endian-ness
 * as the host CPU.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   pbBits          Pointer to the bits to fix up.
 * @param   cbBits          Size of the bits to fix up.
 * @param   uBitsRva        The RVA of the bits.
 * @param   uBitsLinkAddr   The link address of the bits.
 * @param   paFixups        The fixups.
 * @param   cFixups         Number of fixups.
 * @param   pauVirginData   The virgin data / addends.  Parallel to paFixups.
 * @param   paSyms          Pointer to the symbol table.
 * @param   cSyms           Number of symbols.
 * @param   NewBaseAddress  The new base image address.
 */
static int  kldrModMachOApplyFixupsGeneric32Bit(PRTLDRMODMACHO pThis, uint8_t *pbBits, size_t cbBits, RTLDRADDR uBitsRva,
                                                RTLDRADDR uBitsLinkAddr, const macho_relocation_union_t *paFixups,
                                                const uint32_t cFixups, PCRTUINT64U const pauVirginData,
                                                macho_nlist_32_t *paSyms, uint32_t cSyms, RTLDRADDR NewBaseAddress)
{
    /*
     * Iterate the fixups and apply them.
     */
    for (uint32_t iFixup = 0; iFixup < cFixups; iFixup++)
    {
        macho_relocation_union_t Fixup   = paFixups[iFixup];
        RTLDRADDR                SymAddr = ~(RTLDRADDR)0;
        RTPTRUNION               uFix;

        if (!(Fixup.r.r_address & R_SCATTERED))
        {
            /* sanity */
            RTLDRMODMACHO_CHECK_RETURN((uint32_t)Fixup.r.r_address + RT_BIT_32(Fixup.r.r_length) <= cbBits, VERR_LDR_BAD_FIXUP);

            /* Calc the fixup address. */
            uFix.pv = pbBits + Fixup.r.r_address;

            /*
             * Calc the symbol value.
             */
            /* Calc the linked symbol address / addend. */
            switch (Fixup.r.r_length)
            {
                case 0: SymAddr = (int8_t)pauVirginData[iFixup].au8[0]; break;
                case 1: SymAddr = (int16_t)pauVirginData[iFixup].au16[0]; break;
                case 2: SymAddr = (int32_t)pauVirginData[iFixup].au32[0]; break;
                case 3: SymAddr = (int64_t)pauVirginData[iFixup].u; break;
                default: RTLDRMODMACHO_FAILED_RETURN(VERR_LDR_BAD_FIXUP);
            }
            if (Fixup.r.r_pcrel)
                SymAddr += Fixup.r.r_address + uBitsLinkAddr;

            /* Add symbol / section address. */
            if (Fixup.r.r_extern)
            {
                const macho_nlist_32_t *pSym;
                if (Fixup.r.r_symbolnum >= cSyms)
                    return VERR_LDR_BAD_FIXUP;
                pSym = &paSyms[Fixup.r.r_symbolnum];

                if (pSym->n_type & MACHO_N_STAB)
                    return VERR_LDR_BAD_FIXUP;

                switch (pSym->n_type & MACHO_N_TYPE)
                {
                    case MACHO_N_SECT:
                    {
                        PRTLDRMODMACHOSECT pSymSect;
                        RTLDRMODMACHO_CHECK_RETURN((uint32_t)pSym->n_sect - 1 <= pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
                        pSymSect = &pThis->paSections[pSym->n_sect - 1];

                        SymAddr += pSym->n_value - pSymSect->LinkAddress + pSymSect->RVA + NewBaseAddress;
                        break;
                    }

                    case MACHO_N_UNDF:
                    case MACHO_N_ABS:
                        SymAddr += pSym->n_value;
                        break;

                    case MACHO_N_INDR:
                    case MACHO_N_PBUD:
                        RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
                    default:
                        RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_SYMBOL);
                }
            }
            else if (Fixup.r.r_symbolnum != R_ABS)
            {
                PRTLDRMODMACHOSECT pSymSect;
                if (Fixup.r.r_symbolnum > pThis->cSections)
                    return VERR_LDR_BAD_FIXUP;
                pSymSect = &pThis->paSections[Fixup.r.r_symbolnum - 1];

                SymAddr -= pSymSect->LinkAddress;
                SymAddr += pSymSect->RVA + NewBaseAddress;
            }

            /* adjust for PC relative */
            if (Fixup.r.r_pcrel)
                SymAddr -= Fixup.r.r_address + uBitsRva + NewBaseAddress;
        }
        else
        {
            PRTLDRMODMACHOSECT pSymSect;
            uint32_t iSymSect;
            RTLDRADDR Value;

            /* sanity */
            RTLDRMODMACHO_ASSERT(Fixup.s.r_scattered);
            RTLDRMODMACHO_CHECK_RETURN((uint32_t)Fixup.s.r_address + RT_BIT_32(Fixup.s.r_length) <= cbBits, VERR_LDR_BAD_FIXUP);

            /* Calc the fixup address. */
            uFix.pv = pbBits + Fixup.s.r_address;

            /*
             * Calc the symbol value.
             */
            /* The addend is stored in the code. */
            switch (Fixup.s.r_length)
            {
                case 0: SymAddr = (int8_t)pauVirginData[iFixup].au8[0]; break;
                case 1: SymAddr = (int16_t)pauVirginData[iFixup].au16[0]; break;
                case 2: SymAddr = (int32_t)pauVirginData[iFixup].au32[0]; break;
                case 3: SymAddr = (int64_t)pauVirginData[iFixup].u; break;
                default: RTLDRMODMACHO_FAILED_RETURN(VERR_LDR_BAD_FIXUP);
            }
            if (Fixup.s.r_pcrel)
                SymAddr += Fixup.s.r_address;
            Value = Fixup.s.r_value;
            SymAddr -= Value;                   /* (-> addend only) */

            /* Find the section number from the r_value. */
            pSymSect = NULL;
            for (iSymSect = 0; iSymSect < pThis->cSections; iSymSect++)
            {
                RTLDRADDR off = Value - pThis->paSections[iSymSect].LinkAddress;
                if (off < pThis->paSections[iSymSect].cb)
                {
                    pSymSect = &pThis->paSections[iSymSect];
                    break;
                }
                else if (off == pThis->paSections[iSymSect].cb) /* edge case */
                    pSymSect = &pThis->paSections[iSymSect];
            }
            if (!pSymSect)
                return VERR_LDR_BAD_FIXUP;

            /* Calc the symbol address. */
            SymAddr += Value - pSymSect->LinkAddress + pSymSect->RVA + NewBaseAddress;
            if (Fixup.s.r_pcrel)
                SymAddr -= Fixup.s.r_address + uBitsRva + NewBaseAddress;

            Fixup.r.r_length = ((scattered_relocation_info_t *)&paFixups[iFixup])->r_length;
            Fixup.r.r_type   = ((scattered_relocation_info_t *)&paFixups[iFixup])->r_type;
        }

        /*
         * Write back the fixed up value.
         */
        if (Fixup.r.r_type == GENERIC_RELOC_VANILLA)
        {
            switch (Fixup.r.r_length)
            {
                case 0: *uFix.pu8  = (uint8_t)SymAddr; break;
                case 1: *uFix.pu16 = (uint16_t)SymAddr; break;
                case 2: *uFix.pu32 = (uint32_t)SymAddr; break;
                case 3: *uFix.pu64 = (uint64_t)SymAddr; break;
            }
        }
        else if (Fixup.r.r_type <= GENERIC_RELOC_LOCAL_SECTDIFF)
            return VERR_LDRMACHO_UNSUPPORTED_FIXUP_TYPE;
        else
            return VERR_LDR_BAD_FIXUP;
    }

    return VINF_SUCCESS;
}


/**
 * Applies AMD64 fixups to a section.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   pbBits          Pointer to the section bits.
 * @param   cbBits          Size of the bits to fix up.
 * @param   uBitsRva        The RVA of the bits.
 * @param   paFixups        The fixups.
 * @param   cFixups         Number of fixups.
 * @param   pauVirginData   The virgin data / addends.  Parallel to paFixups.
 * @param   paSyms          Pointer to the symbol table.
 * @param   cSyms           Number of symbols.
 * @param   NewBaseAddress  The new base image address.
 */
static int  kldrModMachOApplyFixupsAMD64(PRTLDRMODMACHO pThis, uint8_t *pbBits, size_t cbBits, RTLDRADDR uBitsRva,
                                         const macho_relocation_union_t *paFixups,
                                         const uint32_t cFixups, PCRTUINT64U const pauVirginData,
                                         macho_nlist_64_t *paSyms, uint32_t cSyms, RTLDRADDR NewBaseAddress)
{
    /*
     * Iterate the fixups and apply them.
     */
    for (uint32_t iFixup = 0; iFixup < cFixups; iFixup++)
    {
        macho_relocation_union_t Fixup = paFixups[iFixup];

        /* AMD64 doesn't use scattered fixups. */
        RTLDRMODMACHO_CHECK_RETURN(!(Fixup.r.r_address & R_SCATTERED), VERR_LDR_BAD_FIXUP);

        /* sanity */
        RTLDRMODMACHO_CHECK_RETURN((uint32_t)Fixup.r.r_address + RT_BIT_32(Fixup.r.r_length) <= cbBits, VERR_LDR_BAD_FIXUP);

        /* calc fixup addresses. */
        RTPTRUNION uFix;
        uFix.pv = pbBits + Fixup.r.r_address;

        /*
         * Calc the symbol value.
         */
        /* Calc the linked symbol address / addend. */
        RTLDRADDR SymAddr;
        switch (Fixup.r.r_length)
        {
            case 2: SymAddr = (int32_t)pauVirginData[iFixup].au32[0]; break;
            case 3: SymAddr = (int64_t)pauVirginData[iFixup].u; break;
            default:
                RTLDRMODMACHO_FAILED_RETURN(VERR_LDR_BAD_FIXUP);
        }

        /* Add symbol / section address. */
        if (Fixup.r.r_extern)
        {
            const macho_nlist_64_t *pSym;

            RTLDRMODMACHO_CHECK_RETURN(Fixup.r.r_symbolnum < cSyms, VERR_LDR_BAD_FIXUP);
            pSym = &paSyms[Fixup.r.r_symbolnum];
            RTLDRMODMACHO_CHECK_RETURN(!(pSym->n_type & MACHO_N_STAB), VERR_LDR_BAD_FIXUP);

            switch (Fixup.r.r_type)
            {
                /* GOT references just needs to have their symbol verified.
                   Later, we'll optimize GOT building here using a parallel sym->got array. */
                case X86_64_RELOC_GOT_LOAD:
                case X86_64_RELOC_GOT:
                    switch (pSym->n_type & MACHO_N_TYPE)
                    {
                        case MACHO_N_SECT:
                        case MACHO_N_UNDF:
                        case MACHO_N_ABS:
                            break;
                        case MACHO_N_INDR:
                        case MACHO_N_PBUD:
                            RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
                        default:
                            RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_SYMBOL);
                    }
                    SymAddr = sizeof(uint64_t) * Fixup.r.r_symbolnum + pThis->GotRVA + NewBaseAddress;
                    RTLDRMODMACHO_CHECK_RETURN(Fixup.r.r_length == 2, VERR_LDR_BAD_FIXUP);
                    SymAddr -= 4;
                    break;

                /* Verify the r_pcrel field for signed fixups on the way into the default case. */
                case X86_64_RELOC_BRANCH:
                case X86_64_RELOC_SIGNED:
                case X86_64_RELOC_SIGNED_1:
                case X86_64_RELOC_SIGNED_2:
                case X86_64_RELOC_SIGNED_4:
                    RTLDRMODMACHO_CHECK_RETURN(Fixup.r.r_pcrel, VERR_LDR_BAD_FIXUP);
                    RT_FALL_THRU();
                default:
                {
                    /* Adjust with fixup specific addend and verify unsigned/r_pcrel. */
                    switch (Fixup.r.r_type)
                    {
                        case X86_64_RELOC_UNSIGNED:
                            RTLDRMODMACHO_CHECK_RETURN(!Fixup.r.r_pcrel, VERR_LDR_BAD_FIXUP);
                            break;
                        case X86_64_RELOC_BRANCH:
                            RTLDRMODMACHO_CHECK_RETURN(Fixup.r.r_length == 2, VERR_LDR_BAD_FIXUP);
                            SymAddr -= 4;
                            break;
                        case X86_64_RELOC_SIGNED:
                        case X86_64_RELOC_SIGNED_1:
                        case X86_64_RELOC_SIGNED_2:
                        case X86_64_RELOC_SIGNED_4:
                            SymAddr -= 4;
                            break;
                        default:
                            RTLDRMODMACHO_FAILED_RETURN(VERR_LDR_BAD_FIXUP);
                    }

                    switch (pSym->n_type & MACHO_N_TYPE)
                    {
                        case MACHO_N_SECT:
                        {
                            PRTLDRMODMACHOSECT pSymSect;
                            RTLDRMODMACHO_CHECK_RETURN((uint32_t)pSym->n_sect - 1 <= pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
                            pSymSect = &pThis->paSections[pSym->n_sect - 1];
                            SymAddr += pSym->n_value - pSymSect->LinkAddress + pSymSect->RVA + NewBaseAddress;
                            break;
                        }

                        case MACHO_N_UNDF:
                            /* branch to an external symbol may have to take a short detour. */
                            if (   Fixup.r.r_type == X86_64_RELOC_BRANCH
                                &&       SymAddr + Fixup.r.r_address + uBitsRva + NewBaseAddress
                                       - pSym->n_value
                                       + UINT64_C(0x80000000)
                                    >= UINT64_C(0xffffff20))
                            {
                                RTLDRMODMACHO_CHECK_RETURN(pThis->JmpStubsRVA != NIL_RTLDRADDR, VERR_LDR_ADDRESS_OVERFLOW);
                                SymAddr += pThis->cbJmpStub * Fixup.r.r_symbolnum + pThis->JmpStubsRVA + NewBaseAddress;
                            }
                            else
                                SymAddr += pSym->n_value;
                            break;

                        case MACHO_N_ABS:
                            SymAddr += pSym->n_value;
                            break;

                        case MACHO_N_INDR:
                        case MACHO_N_PBUD:
                            RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
                        default:
                            RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_SYMBOL);
                    }
                    break;
                }

                /*
                 * This is a weird customer, it will always be follows by an UNSIGNED fixup.
                 * The value is calculated: target - pair_target.
                 * Note! The linker generally eliminate these when linking modules rather
                 *       than objects (-r).
                 */
                case X86_64_RELOC_SUBTRACTOR:
                {
                    /* Deal with the SUBTRACT symbol first, by subtracting it from SymAddr. */
                    switch (pSym->n_type & MACHO_N_TYPE)
                    {
                        case MACHO_N_SECT:
                        {
                            PRTLDRMODMACHOSECT pSymSect;
                            RTLDRMODMACHO_CHECK_RETURN((uint32_t)pSym->n_sect - 1 <= pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
                            pSymSect = &pThis->paSections[pSym->n_sect - 1];
                            SymAddr -= pSym->n_value - pSymSect->LinkAddress + pSymSect->RVA + NewBaseAddress;
                            break;
                        }

                        case MACHO_N_UNDF:
                        case MACHO_N_ABS:
                            SymAddr -= pSym->n_value;
                            break;

                        case MACHO_N_INDR:
                        case MACHO_N_PBUD:
                            RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
                        default:
                            RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_SYMBOL);
                    }

                    /* Load the 2nd fixup, check sanity. */
                    iFixup++;
                    RTLDRMODMACHO_CHECK_RETURN(!Fixup.r.r_pcrel && iFixup < cFixups, VERR_LDR_BAD_FIXUP);
                    macho_relocation_info_t const Fixup2 = paFixups[iFixup].r;
                    RTLDRMODMACHO_CHECK_RETURN(   Fixup2.r_address == Fixup.r.r_address
                                              && Fixup2.r_length == Fixup.r.r_length
                                              && Fixup2.r_type == X86_64_RELOC_UNSIGNED
                                              && !Fixup2.r_pcrel
                                              && Fixup2.r_symbolnum < cSyms,
                                              VERR_LDR_BAD_FIXUP);

                    if (Fixup2.r_extern)
                    {
                        RTLDRMODMACHO_CHECK_RETURN(Fixup2.r_symbolnum < cSyms, VERR_LDR_BAD_FIXUP);
                        pSym = &paSyms[Fixup2.r_symbolnum];
                        RTLDRMODMACHO_CHECK_RETURN(!(pSym->n_type & MACHO_N_STAB), VERR_LDR_BAD_FIXUP);

                        /* Add its value to SymAddr. */
                        switch (pSym->n_type & MACHO_N_TYPE)
                        {
                            case MACHO_N_SECT:
                            {
                                PRTLDRMODMACHOSECT pSymSect;
                                RTLDRMODMACHO_CHECK_RETURN((uint32_t)pSym->n_sect - 1 <= pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
                                pSymSect = &pThis->paSections[pSym->n_sect - 1];
                                SymAddr += pSym->n_value - pSymSect->LinkAddress + pSymSect->RVA + NewBaseAddress;
                                break;
                            }

                            case MACHO_N_UNDF:
                            case MACHO_N_ABS:
                                SymAddr += pSym->n_value;
                                break;

                            case MACHO_N_INDR:
                            case MACHO_N_PBUD:
                                RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
                            default:
                                RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_BAD_SYMBOL);
                        }
                    }
                    else if (Fixup2.r_symbolnum != R_ABS)
                    {
                        PRTLDRMODMACHOSECT pSymSect;
                        RTLDRMODMACHO_CHECK_RETURN(Fixup2.r_symbolnum <= pThis->cSections, VERR_LDR_BAD_FIXUP);
                        pSymSect = &pThis->paSections[Fixup2.r_symbolnum - 1];
                        SymAddr += pSymSect->RVA - pSymSect->LinkAddress + NewBaseAddress;
                    }
                    else
                        RTLDRMODMACHO_FAILED_RETURN(VERR_LDR_BAD_FIXUP);
                }
                break;
            }
        }
        else
        {
            /* verify against fixup type and make adjustments */
            switch (Fixup.r.r_type)
            {
                case X86_64_RELOC_UNSIGNED:
                    RTLDRMODMACHO_CHECK_RETURN(!Fixup.r.r_pcrel, VERR_LDR_BAD_FIXUP);
                    break;
                case X86_64_RELOC_BRANCH:
                    RTLDRMODMACHO_CHECK_RETURN(Fixup.r.r_pcrel, VERR_LDR_BAD_FIXUP);
                    SymAddr += 4; /* dunno what the assmbler/linker really is doing here... */
                    break;
                case X86_64_RELOC_SIGNED:
                case X86_64_RELOC_SIGNED_1:
                case X86_64_RELOC_SIGNED_2:
                case X86_64_RELOC_SIGNED_4:
                    RTLDRMODMACHO_CHECK_RETURN(Fixup.r.r_pcrel, VERR_LDR_BAD_FIXUP);
                    break;
                /*case X86_64_RELOC_GOT_LOAD:*/
                /*case X86_64_RELOC_GOT: */
                /*case X86_64_RELOC_SUBTRACTOR: - must be r_extern=1 says as. */
                default:
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDR_BAD_FIXUP);
            }
            if (Fixup.r.r_symbolnum != R_ABS)
            {
                PRTLDRMODMACHOSECT pSymSect;
                RTLDRMODMACHO_CHECK_RETURN(Fixup.r.r_symbolnum <= pThis->cSections, VERR_LDR_BAD_FIXUP);
                pSymSect = &pThis->paSections[Fixup.r.r_symbolnum - 1];

                SymAddr -= pSymSect->LinkAddress;
                SymAddr += pSymSect->RVA + NewBaseAddress;
                if (Fixup.r.r_pcrel)
                    SymAddr += Fixup.r.r_address;
            }
        }

        /* adjust for PC relative */
        if (Fixup.r.r_pcrel)
            SymAddr -= Fixup.r.r_address + uBitsRva + NewBaseAddress;

        /*
         * Write back the fixed up value.
         */
        switch (Fixup.r.r_length)
        {
            case 3:
                *uFix.pu64 = (uint64_t)SymAddr;
                break;
            case 2:
                RTLDRMODMACHO_CHECK_RETURN(Fixup.r.r_pcrel || Fixup.r.r_type == X86_64_RELOC_SUBTRACTOR, VERR_LDR_BAD_FIXUP);
                RTLDRMODMACHO_CHECK_RETURN((int32_t)SymAddr == (int64_t)SymAddr, VERR_LDR_ADDRESS_OVERFLOW);
                *uFix.pu32 = (uint32_t)SymAddr;
                break;
            default:
                RTLDRMODMACHO_FAILED_RETURN(VERR_LDR_BAD_FIXUP);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Loads the symbol table (LC_SYMTAB).
 *
 * The symbol table is pointed to by RTLDRMODMACHO::pvaSymbols.
 *
 * @returns IPRT status code.
 * @param   pThis       The Mach-O module interpreter instance.
 */
static int  kldrModMachOLoadObjSymTab(PRTLDRMODMACHO pThis)
{
    int rc = VINF_SUCCESS;

    if (    !pThis->pvaSymbols
        &&  pThis->cSymbols)
    {
        size_t cbSyms;
        size_t cbSym;
        void *pvSyms;
        void *pvStrings;

        /* sanity */
        RTLDRMODMACHO_CHECK_RETURN(   pThis->offSymbols
                                  && (!pThis->cchStrings || pThis->offStrings),
                                  VERR_LDRMACHO_BAD_OBJECT_FILE);

        /* allocate */
        cbSym = pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
             || pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE
             ? sizeof(macho_nlist_32_t)
             : sizeof(macho_nlist_64_t);
        cbSyms = pThis->cSymbols * cbSym;
        RTLDRMODMACHO_CHECK_RETURN(cbSyms / cbSym == pThis->cSymbols, VERR_LDRMACHO_BAD_SYMTAB_SIZE);
        rc = VERR_NO_MEMORY;
        pvSyms = RTMemAlloc(cbSyms);
        if (pvSyms)
        {
            if (pThis->cchStrings)
                pvStrings = RTMemAlloc(pThis->cchStrings);
            else
                pvStrings = RTMemAllocZ(4);
            if (pvStrings)
            {
                /* read */
                rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, pvSyms, cbSyms, pThis->offSymbols);
                if (RT_SUCCESS(rc) && pThis->cchStrings)
                    rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, pvStrings, pThis->cchStrings, pThis->offStrings);
                if (RT_SUCCESS(rc))
                {
                    pThis->pvaSymbols = pvSyms;
                    pThis->pchStrings = (char *)pvStrings;

                    /* perform endian conversion? */
                    if (pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
                    {
                        uint32_t cLeft = pThis->cSymbols;
                        macho_nlist_32_t *pSym = (macho_nlist_32_t *)pvSyms;
                        while (cLeft-- > 0)
                        {
                            pSym->n_un.n_strx = RT_BSWAP_U32(pSym->n_un.n_strx);
                            pSym->n_desc = (int16_t)RT_BSWAP_U16(pSym->n_desc);
                            pSym->n_value = RT_BSWAP_U32(pSym->n_value);
                            pSym++;
                        }
                    }
                    else if (pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE_OE)
                    {
                        uint32_t cLeft = pThis->cSymbols;
                        macho_nlist_64_t *pSym = (macho_nlist_64_t *)pvSyms;
                        while (cLeft-- > 0)
                        {
                            pSym->n_un.n_strx = RT_BSWAP_U32(pSym->n_un.n_strx);
                            pSym->n_desc = (int16_t)RT_BSWAP_U16(pSym->n_desc);
                            pSym->n_value = RT_BSWAP_U64(pSym->n_value);
                            pSym++;
                        }
                    }

                    return VINF_SUCCESS;
                }
                RTMemFree(pvStrings);
            }
            RTMemFree(pvSyms);
        }
    }
    else
        RTLDRMODMACHO_ASSERT(pThis->pchStrings || pThis->Hdr.filetype == MH_DSYM);

    return rc;
}


/**
 * Loads the fixups at the given address and performs endian
 * conversion if necessary.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   offFixups       The file offset of the fixups.
 * @param   cFixups         The number of fixups to load.
 * @param   ppaFixups       Where to put the pointer to the allocated fixup array.
 */
static int kldrModMachOLoadFixups(PRTLDRMODMACHO pThis, RTFOFF offFixups, uint32_t cFixups, macho_relocation_union_t **ppaFixups)
{
    macho_relocation_union_t *paFixups;
    size_t cbFixups;

    /* allocate the memory. */
    cbFixups = cFixups * sizeof(*paFixups);
    RTLDRMODMACHO_CHECK_RETURN(cbFixups / sizeof(*paFixups) == cFixups, VERR_LDRMACHO_BAD_SYMTAB_SIZE);
    paFixups = (macho_relocation_union_t *)RTMemAlloc(cbFixups);
    if (!paFixups)
        return VERR_NO_MEMORY;

    /* read the fixups. */
    int rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, paFixups, cbFixups, offFixups);
    if (RT_SUCCESS(rc))
    {
        *ppaFixups = paFixups;

        /* do endian conversion if necessary. */
        if (   pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE
            || pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE_OE)
        {
            uint32_t iFixup;
            for (iFixup = 0; iFixup < cFixups; iFixup++)
            {
                uint32_t *pu32 = (uint32_t *)&paFixups[iFixup];
                pu32[0] = RT_BSWAP_U32(pu32[0]);
                pu32[1] = RT_BSWAP_U32(pu32[1]);
            }
        }
    }
    else
        RTMemFree(paFixups);
    return rc;
}


/**
 * Loads virgin data (addends) for an array of fixups.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   pbBits          The virgin bits to lift the data from
 * @param   cbBits          The number of virgin bytes.
 * @param   paFixups        The fixups.
 * @param   cFixups         Number of fixups
 * @param   pszName         Name for logging.
 * @param   ppauVirginData  Where to return the virgin data.
 */
static int rtldrMachOLoadVirginData(PRTLDRMODMACHO pThis, uint8_t const *pbBits, size_t cbBits,
                                    macho_relocation_union_t const *paFixups, uint32_t cFixups, const char *pszName,
                                    PRTUINT64U *ppauVirginData)
{
    /*
     * In case we jettisoned the fixups, we will leave virgin data.
     */
    if (*ppauVirginData)
        return VINF_SUCCESS;

#ifdef LOG_ENABLED
    /*
     * Ensure that we've got the symbol table if we're logging fixups.
     */
    if (LogIs5Enabled())
    {
        int rc = kldrModMachOLoadObjSymTab(pThis);
        if (RT_FAILURE(rc))
            return rc;
    }
#endif


    /*
     * Allocate memory and iterate the fixups to get the data.
     */
    PRTUINT64U pauVirginData = *ppauVirginData = (PRTUINT64U)RTMemAllocZ(sizeof(uint64_t) * cFixups);
    if (pauVirginData)
    {
        Log5(("Fixups for %s: (%u)\n", pszName, cFixups));
        for (uint32_t i = 0; i < cFixups; i++)
        {
            uint32_t off;
            uint32_t cShift;
            if (!paFixups[i].s.r_scattered)
            {
                off    = paFixups[i].r.r_address;
                cShift = paFixups[i].r.r_length;
            }
            else
            {
                off    = paFixups[i].s.r_address;
                cShift = paFixups[i].s.r_length;
            }
            RTLDRMODMACHO_CHECK_RETURN(off + RT_BIT_32(cShift) <= cbBits, VERR_LDR_BAD_FIXUP);

            /** @todo This ASSUMES same endian in the image and on the host.  Would need
             * to check target cpu (pThis->Core.enmArch) endianness against host to get
             * it right... (outside the loop, obviously) */
            switch (cShift)
            {
                case 3:
                    pauVirginData[i].u = RT_MAKE_U64_FROM_U8(pbBits[off],     pbBits[off + 1], pbBits[off + 2], pbBits[off + 3],
                                                             pbBits[off + 4], pbBits[off + 5], pbBits[off + 6], pbBits[off + 7]);
                    break;
                case 2:
                    pauVirginData[i].u = (int32_t)RT_MAKE_U32_FROM_U8(pbBits[off], pbBits[off + 1], pbBits[off + 2], pbBits[off + 3]);
                    break;
                case 1:
                    pauVirginData[i].u = (int16_t)RT_MAKE_U16(pbBits[off], pbBits[off + 1]);
                    break;
                case 0:
                    pauVirginData[i].u = (int8_t)pbBits[off];
                    break;
                default:
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDR_BAD_FIXUP);
            }

#ifdef LOG_ENABLED
            if (LogIs5Enabled())
            {
                if (!paFixups[i].s.r_scattered)
                {
                    Log5((" #%06x: %#08x LB %u: t=%#x pc=%u ex=%u sym=%#010x add=%#RX64\n",
                          i, off, RT_BIT_32(cShift), paFixups[i].r.r_type, paFixups[i].r.r_pcrel, paFixups[i].r.r_extern,
                          paFixups[i].r.r_symbolnum, pauVirginData[i].u));
                    if (paFixups[i].r.r_symbolnum < pThis->cSymbols)
                    {
                        if (pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE || pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE_OE)
                        {
                            macho_nlist_64_t const *pSym = (macho_nlist_64_t const *)pThis->pvaSymbols + paFixups[i].r.r_symbolnum;
                            Log5(("     sym: %#04x:%#018RX64 t=%#04x d=%#06x %s\n",
                                  pSym->n_sect, pSym->n_value, pSym->n_type, pSym->n_desc,
                                  pSym->n_un.n_strx < pThis->cchStrings ? &pThis->pchStrings[pSym->n_un.n_strx] : ""));

                        }
                        else
                        {
                            macho_nlist_32_t const *pSym = (macho_nlist_32_t const *)pThis->pvaSymbols + paFixups[i].r.r_symbolnum;
                            Log5(("     sym: %#04x:%#010RX32 t=%#04x d=%#06x %s\n",
                                  pSym->n_sect, pSym->n_value, pSym->n_type, pSym->n_desc,
                                  (uint32_t)pSym->n_un.n_strx < pThis->cchStrings ? &pThis->pchStrings[pSym->n_un.n_strx] : ""));
                        }
                    }
                }
                else
                    Log5((" #%06x: %#08x LB %u: t=%#x pc=%u  val=%#010x add=%#RX64\n", i, off, RT_BIT_32(cShift),
                          paFixups[i].s.r_type, paFixups[i].s.r_pcrel, paFixups[i].s.r_value, pauVirginData[i].u));
            }
#endif
        }
        return VINF_SUCCESS;
    }
    RT_NOREF(pThis, pszName);
    return VERR_NO_MEMORY;
}


/**
 * MH_OBJECT: Loads fixups and addends for each section.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   pbBits          The image bits.  First time we're called, these are
 *                          ASSUMED to be in virgin state and suitable for
 *                          saving addends.
 */
static int rtldrMachOObjLoadFixupsAndVirginData(PRTLDRMODMACHO pThis, uint8_t const *pbBits)
{
    PRTLDRMODMACHOSECT pSect = pThis->paSections;
    for (uint32_t i = 0; i < pThis->cSections; i++, pSect++)
        if (   !pSect->paFixups
            && pSect->cFixups > 0)
        {
            /*
             * Load and endian convert the fixups.
             */
            int rc = kldrModMachOLoadFixups(pThis, pSect->offFixups, pSect->cFixups, &pSect->paFixups);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Save virgin data (addends) for each fixup.
                 */
                rc = rtldrMachOLoadVirginData(pThis, &pbBits[(size_t)pSect->RVA], (size_t)pSect->cb, pSect->paFixups, pSect->cFixups,
                                              pThis->aSegments[pSect->iSegment].SegInfo.pszName, &pSect->pauFixupVirginData);
                if (RT_SUCCESS(rc))
                    continue;

                RTMemFree(pSect->pauFixupVirginData);
                pSect->pauFixupVirginData = NULL;
                RTMemFree(pSect->paFixups);
                pSect->paFixups = NULL;
            }
            return rc;
        }

    return VINF_SUCCESS;
}


/**
 * Dylib: Loads fixups and addends.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module interpreter instance.
 * @param   pbBits          The image bits.  First time we're called, these are
 *                          ASSUMED to be in virgin state and suitable for
 *                          saving addends.
 */
static int rtldrMachODylibLoadFixupsAndVirginData(PRTLDRMODMACHO pThis, uint8_t const *pbBits)
{
    /*
     * Don't do it again if we already loaded them.
     */
    if (pThis->paRelocations)
    {
        Assert(pThis->pauRelocationsVirginData);
        return VINF_SUCCESS;
    }

    /*
     * There must be a LC_DYSYMTAB.  Fixups are optionals.
     */
    dysymtab_command_t const *pDySymTab = pThis->pDySymTab;
    AssertReturn(pDySymTab, VERR_INTERNAL_ERROR_2);
    uint32_t cRelocations = pDySymTab->nlocrel + pDySymTab->nextrel;
    if (cRelocations == 0)
        return VINF_SUCCESS;

    /*
     * Load fixups.
     */
    int rc = VINF_SUCCESS;
    uint32_t *paRawRelocs = (uint32_t *)RTMemAlloc(cRelocations * sizeof(macho_relocation_union_t));
    if (paRawRelocs)
    {
        pThis->paRelocations = (macho_relocation_union_t *)paRawRelocs;

        if (pDySymTab->nextrel)
            rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, paRawRelocs,
                                              pDySymTab->nextrel * sizeof(macho_relocation_union_t), pDySymTab->extreloff);
        if (pDySymTab->nlocrel && RT_SUCCESS(rc))
            rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader,
                                              (uint8_t *)paRawRelocs + pDySymTab->nextrel * sizeof(macho_relocation_union_t),
                                              pDySymTab->nlocrel * sizeof(macho_relocation_union_t), pDySymTab->locreloff);
        if (RT_SUCCESS(rc))
        {
            /* Byte swap if needed. */
            if (   pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE
                || pThis->Hdr.magic == IMAGE_MACHO64_SIGNATURE_OE)
            {
                for (uint32_t i = 0; i < cRelocations; i++)
                {
                    paRawRelocs[i * 2]     = RT_BSWAP_U32(paRawRelocs[i * 2]);
                    paRawRelocs[i * 2 + 1] = RT_BSWAP_U32(paRawRelocs[i * 2 + 1]);
                }
                ASMCompilerBarrier();
            }

            /*
             * Load virgin data (addends).
             */
            rc = rtldrMachOLoadVirginData(pThis, pbBits, (size_t)pThis->cbImage, pThis->paRelocations, cRelocations,
                                          "whole-image", &pThis->pauRelocationsVirginData);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            RTMemFree(pThis->pauRelocationsVirginData);
            pThis->pauRelocationsVirginData = NULL;
        }
        RTMemFree(pThis->paRelocations);
        pThis->paRelocations = NULL;
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}

#if 0

/** @copydoc kLdrModCallInit */
static int kldrModMachOCallInit(PRTLDRMODINTERNAL pMod, void *pvMapping, uintptr_t uHandle)
{
    /* later */
    RT_NOREF(pMod);
    RT_NOREF(pvMapping);
    RT_NOREF(uHandle);
    return VINF_SUCCESS;
}


/** @copydoc kLdrModCallTerm */
static int kldrModMachOCallTerm(PRTLDRMODINTERNAL pMod, void *pvMapping, uintptr_t uHandle)
{
    /* later */
    RT_NOREF(pMod);
    RT_NOREF(pvMapping);
    RT_NOREF(uHandle);
    return VINF_SUCCESS;
}


/** @copydoc kLdrModCallThread */
static int kldrModMachOCallThread(PRTLDRMODINTERNAL pMod, void *pvMapping, uintptr_t uHandle, unsigned fAttachingOrDetaching)
{
    /* Relevant for Mach-O? */
    RT_NOREF(pMod);
    RT_NOREF(pvMapping);
    RT_NOREF(uHandle);
    RT_NOREF(fAttachingOrDetaching);
    return VINF_SUCCESS;
}

#endif

/**
 * @interface_method_impl{RTLDROPS,pfnGetImageSize}
 */
static DECLCALLBACK(size_t) rtldrMachO_GetImageSize(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    return pThis->cbImage;
}


/**
 * @interface_method_impl{RTLDROPS,pfnGetBits}
 */
static DECLCALLBACK(int) rtldrMachO_GetBits(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR BaseAddress,
                                            PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);

    if (!pThis->fCanLoad)
        return VERR_LDRMACHO_TODO;

    /*
     * Zero the entire buffer first to simplify things.
     */
    memset(pvBits, 0, (size_t)pThis->cbImage);

    /*
     * When possible use the segment table to load the data.
     */
    for (uint32_t i = 0; i < pThis->cSegments; i++)
    {
        /* skip it? */
        if (   pThis->aSegments[i].SegInfo.cbFile == -1
            || pThis->aSegments[i].SegInfo.offFile == -1
            || pThis->aSegments[i].SegInfo.RVA == NIL_RTLDRADDR
            || pThis->aSegments[i].SegInfo.cbMapped == 0
            || !pThis->aSegments[i].SegInfo.Alignment)
            continue;
        int rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader,
                                              (uint8_t *)pvBits + pThis->aSegments[i].SegInfo.RVA,
                                              pThis->aSegments[i].SegInfo.cbFile,
                                              pThis->aSegments[i].SegInfo.offFile);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Perform relocations.
     */
    return rtldrMachO_RelocateBits(pMod, pvBits, BaseAddress, pThis->LinkAddress, pfnGetImport, pvUser);
}


/**
 * @interface_method_impl{RTLDROPS,pfnRelocate}
 */
static DECLCALLBACK(int) rtldrMachO_RelocateBits(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR NewBaseAddress,
                                                 RTUINTPTR OldBaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    int rc;

    /*
     * Call workers to do the jobs.
     */
    if (pThis->Hdr.filetype == MH_OBJECT)
    {
        rc = rtldrMachOObjLoadFixupsAndVirginData(pThis, (uint8_t const *)pvBits);
        if (RT_SUCCESS(rc))
            rc = kldrModMachOObjDoImports(pThis, NewBaseAddress, pfnGetImport, pvUser);
        if (RT_SUCCESS(rc))
            rc = kldrModMachOObjDoFixups(pThis, pvBits, NewBaseAddress);

    }
    else
    {
        rc = rtldrMachODylibLoadFixupsAndVirginData(pThis, (uint8_t const *)pvBits);
        if (RT_SUCCESS(rc))
            rc = kldrModMachODylibDoImports(pThis, NewBaseAddress, pfnGetImport, pvUser);
        if (RT_SUCCESS(rc))
            rc = kldrModMachODylibDoIndirectSymbols(pThis, pvBits, NewBaseAddress - OldBaseAddress);
        if (RT_SUCCESS(rc))
            rc = kldrModMachODylibDoFixups(pThis, pvBits, NewBaseAddress);
    }

    /*
     * Construct the global offset table if necessary, it's always the last
     * segment when present.
     */
    if (RT_SUCCESS(rc) && pThis->fMakeGot)
        rc = kldrModMachOMakeGOT(pThis, pvBits, NewBaseAddress);

    return rc;
}


/**
 * Builds the GOT.
 *
 * Assumes the symbol table has all external symbols resolved correctly and that
 * the bits has been cleared up front.
 */
static int kldrModMachOMakeGOT(PRTLDRMODMACHO pThis, void *pvBits, RTLDRADDR NewBaseAddress)
{
    uint32_t  iSym = pThis->cSymbols;
    if (    pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE
        ||  pThis->Hdr.magic == IMAGE_MACHO32_SIGNATURE_OE)
    {
        macho_nlist_32_t const *paSyms = (macho_nlist_32_t const *)pThis->pvaSymbols;
        uint32_t *paGOT = (uint32_t *)((uint8_t *)pvBits + pThis->GotRVA);
        while (iSym-- > 0)
            switch (paSyms[iSym].n_type & MACHO_N_TYPE)
            {
                case MACHO_N_SECT:
                {
                    PRTLDRMODMACHOSECT pSymSect;
                    RTLDRMODMACHO_CHECK_RETURN((uint32_t)paSyms[iSym].n_sect - 1 <= pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
                    pSymSect = &pThis->paSections[paSyms[iSym].n_sect - 1];
                    paGOT[iSym] = (uint32_t)(paSyms[iSym].n_value - pSymSect->LinkAddress + pSymSect->RVA + NewBaseAddress);
                    break;
                }

                case MACHO_N_UNDF:
                case MACHO_N_ABS:
                    paGOT[iSym] = paSyms[iSym].n_value;
                    break;
            }
    }
    else
    {
        macho_nlist_64_t const *paSyms = (macho_nlist_64_t const *)pThis->pvaSymbols;
        uint64_t *paGOT = (uint64_t *)((uint8_t *)pvBits + pThis->GotRVA);
        while (iSym-- > 0)
        {
            switch (paSyms[iSym].n_type & MACHO_N_TYPE)
            {
                case MACHO_N_SECT:
                {
                    PRTLDRMODMACHOSECT pSymSect;
                    RTLDRMODMACHO_CHECK_RETURN((uint32_t)paSyms[iSym].n_sect - 1 <= pThis->cSections, VERR_LDRMACHO_BAD_SYMBOL);
                    pSymSect = &pThis->paSections[paSyms[iSym].n_sect - 1];
                    paGOT[iSym] = paSyms[iSym].n_value - pSymSect->LinkAddress + pSymSect->RVA + NewBaseAddress;
                    break;
                }

                case MACHO_N_UNDF:
                case MACHO_N_ABS:
                    paGOT[iSym] = paSyms[iSym].n_value;
                    break;
            }
        }

        if (pThis->JmpStubsRVA != NIL_RTLDRADDR)
        {
            iSym = pThis->cSymbols;
            switch (pThis->Hdr.cputype)
            {
                /*
                 * AMD64 is simple since the GOT and the indirect jmps are parallel
                 * arrays with entries of the same size. The relative offset will
                 * be the the same for each entry, kind of nice. :-)
                 */
                case CPU_TYPE_X86_64:
                {
                    uint64_t   *paJmps = (uint64_t *)((uint8_t *)pvBits + pThis->JmpStubsRVA);
                    int32_t     off;
                    uint64_t    u64Tmpl;
                    union
                    {
                        uint8_t     ab[8];
                        uint64_t    u64;
                    }       Tmpl;

                    /* create the template. */
                    off = (int32_t)(pThis->GotRVA - (pThis->JmpStubsRVA + 6));
                    Tmpl.ab[0] = 0xff; /* jmp [GOT-entry wrt RIP] */
                    Tmpl.ab[1] = 0x25;
                    Tmpl.ab[2] =  off        & 0xff;
                    Tmpl.ab[3] = (off >>  8) & 0xff;
                    Tmpl.ab[4] = (off >> 16) & 0xff;
                    Tmpl.ab[5] = (off >> 24) & 0xff;
                    Tmpl.ab[6] = 0xcc;
                    Tmpl.ab[7] = 0xcc;
                    u64Tmpl = Tmpl.u64;

                    /* copy the template to every jmp table entry. */
                    while (iSym-- > 0)
                        paJmps[iSym] = u64Tmpl;
                    break;
                }

                default:
                    RTLDRMODMACHO_FAILED_RETURN(VERR_LDRMACHO_TODO);
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnEnumSegments}
 */
static DECLCALLBACK(int) rtldrMachO_EnumSegments(PRTLDRMODINTERNAL pMod, PFNRTLDRENUMSEGS pfnCallback, void *pvUser)
{
    PRTLDRMODMACHO  pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    uint32_t const cSegments  = pThis->cSegments;
    for (uint32_t iSeg = 0; iSeg < cSegments; iSeg++)
    {
        int rc = pfnCallback(pMod, &pThis->aSegments[iSeg].SegInfo, pvUser);
        if (rc != VINF_SUCCESS)
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnLinkAddressToSegOffset}
 */
static DECLCALLBACK(int) rtldrMachO_LinkAddressToSegOffset(PRTLDRMODINTERNAL pMod, RTLDRADDR LinkAddress,
                                                           uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    PRTLDRMODMACHO  pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    uint32_t const cSegments  = pThis->cSegments;
    for (uint32_t iSeg = 0; iSeg < cSegments; iSeg++)
        if (pThis->aSegments[iSeg].SegInfo.RVA != NIL_RTLDRADDR)
        {
            Assert(pThis->aSegments[iSeg].SegInfo.cbMapped != NIL_RTLDRADDR);
            RTLDRADDR offSeg = LinkAddress - pThis->aSegments[iSeg].SegInfo.LinkAddress;
            if (   offSeg < pThis->aSegments[iSeg].SegInfo.cbMapped
                || offSeg < pThis->aSegments[iSeg].SegInfo.cb)
            {
                *piSeg = iSeg;
                *poffSeg = offSeg;
                return VINF_SUCCESS;
            }
        }

    return VERR_LDR_INVALID_LINK_ADDRESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnLinkAddressToRva}
 */
static DECLCALLBACK(int) rtldrMachO_LinkAddressToRva(PRTLDRMODINTERNAL pMod, RTLDRADDR LinkAddress, PRTLDRADDR pRva)
{
    PRTLDRMODMACHO  pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    uint32_t const cSegments  = pThis->cSegments;
    for (uint32_t iSeg = 0; iSeg < cSegments; iSeg++)
        if (pThis->aSegments[iSeg].SegInfo.RVA != NIL_RTLDRADDR)
        {
            Assert(pThis->aSegments[iSeg].SegInfo.cbMapped != NIL_RTLDRADDR);
            RTLDRADDR offSeg = LinkAddress - pThis->aSegments[iSeg].SegInfo.LinkAddress;
            if (   offSeg < pThis->aSegments[iSeg].SegInfo.cbMapped
                || offSeg < pThis->aSegments[iSeg].SegInfo.cb)
            {
                *pRva = pThis->aSegments[iSeg].SegInfo.RVA + offSeg;
                return VINF_SUCCESS;
            }
        }

    return VERR_LDR_INVALID_RVA;
}


/**
 * @interface_method_impl{RTLDROPS,pfnSegOffsetToRva}
 */
static DECLCALLBACK(int) rtldrMachO_SegOffsetToRva(PRTLDRMODINTERNAL pMod, uint32_t iSeg, RTLDRADDR offSeg, PRTLDRADDR pRva)
{
    PRTLDRMODMACHO  pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);

    if (iSeg >= pThis->cSegments)
        return VERR_LDR_INVALID_SEG_OFFSET;
    RTLDRMODMACHOSEG const *pSegment = &pThis->aSegments[iSeg];

    if (pSegment->SegInfo.RVA == NIL_RTLDRADDR)
        return VERR_LDR_INVALID_SEG_OFFSET;

    if (   offSeg > pSegment->SegInfo.cbMapped
        && offSeg > pSegment->SegInfo.cb
        && (    pSegment->SegInfo.cbFile < 0
            ||  offSeg > (uint64_t)pSegment->SegInfo.cbFile))
        return VERR_LDR_INVALID_SEG_OFFSET;

    *pRva = pSegment->SegInfo.RVA + offSeg;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnRvaToSegOffset}
 */
static DECLCALLBACK(int) rtldrMachO_RvaToSegOffset(PRTLDRMODINTERNAL pMod, RTLDRADDR Rva, uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    PRTLDRMODMACHO  pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    uint32_t const cSegments  = pThis->cSegments;
    for (uint32_t iSeg = 0; iSeg < cSegments; iSeg++)
        if (pThis->aSegments[iSeg].SegInfo.RVA != NIL_RTLDRADDR)
        {
            Assert(pThis->aSegments[iSeg].SegInfo.cbMapped != NIL_RTLDRADDR);
            RTLDRADDR offSeg = Rva - pThis->aSegments[iSeg].SegInfo.RVA;
            if (   offSeg < pThis->aSegments[iSeg].SegInfo.cbMapped
                || offSeg < pThis->aSegments[iSeg].SegInfo.cb)
            {
                *piSeg = iSeg;
                *poffSeg = offSeg;
                return VINF_SUCCESS;
            }
        }

    return VERR_LDR_INVALID_RVA;
}


/**
 * @interface_method_impl{RTLDROPS,pfnReadDbgInfo}
 */
static DECLCALLBACK(int) rtldrMachO_ReadDbgInfo(PRTLDRMODINTERNAL pMod, uint32_t iDbgInfo, RTFOFF off, size_t cb, void *pvBuf)
{
    PRTLDRMODMACHO  pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);

    /** @todo May have to apply fixups here. */
    if (iDbgInfo < pThis->cSections)
        return pThis->Core.pReader->pfnRead(pThis->Core.pReader, pvBuf, cb, off);
    return VERR_OUT_OF_RANGE;
}


/**
 * Loads the code signing blob if necessary (RTLDRMODMACHO::PtrCodeSignature).
 *
 * @returns IPRT status code.
 * @param   pThis               The mach-o instance.
 */
static int rtldrMachO_LoadSignatureBlob(PRTLDRMODMACHO pThis)
{
    Assert(pThis->cbCodeSignature > 0);
    if (pThis->PtrCodeSignature.pb != NULL)
        return VINF_SUCCESS;

    if (   pThis->cbCodeSignature > sizeof(RTCRAPLCSHDR)
        && pThis->cbCodeSignature <= _1M)
    {
        /* Allocate and read. */
        void *pv = RTMemAllocZ(RT_ALIGN_Z(pThis->cbCodeSignature, 16));
        AssertReturn(pv, VERR_NO_MEMORY);
        int rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, pv, pThis->cbCodeSignature,
                                              pThis->offImage + pThis->offCodeSignature);
        if (RT_SUCCESS(rc))
        {
            /* Check blob signature. */
            PCRTCRAPLCSSUPERBLOB pSuper = (PCRTCRAPLCSSUPERBLOB)pv;
            if (pSuper->Hdr.uMagic == RTCRAPLCS_MAGIC_EMBEDDED_SIGNATURE)
            {
                uint32_t cbHdr  = RT_BE2H_U32(pSuper->Hdr.cb);
                uint32_t cSlots = RT_BE2H_U32(pSuper->cSlots);
                if (   cbHdr  <= pThis->cbCodeSignature
                    && cbHdr  >  RT_UOFFSETOF(RTCRAPLCSSUPERBLOB, aSlots)
                    && cSlots >  0
                    && cSlots <  128
                    && RT_UOFFSETOF_DYN(RTCRAPLCSSUPERBLOB, aSlots[cSlots]) <= cbHdr)
                {
                    pThis->PtrCodeSignature.pSuper = pSuper;
                    return VINF_SUCCESS;
                }
                rc = VERR_LDRVI_BAD_CERT_HDR_LENGTH;
            }
            else
                rc = VERR_LDRVI_BAD_CERT_HDR_TYPE;
        }
        RTMemFree(pv);
        return rc;
    }
    return VERR_LDRVI_INVALID_SECURITY_DIR_ENTRY;
}


/**
 * Handles a RTLDRPROP_PKCS7_SIGNED_DATA query.
 */
static int rtldrMachO_QueryPkcs7SignedData(PRTLDRMODMACHO pThis, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    int rc = rtldrMachO_LoadSignatureBlob(pThis);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the signature slot.
         */
        uint32_t            iSlot = RT_BE2H_U32(pThis->PtrCodeSignature.pSuper->cSlots);
        PCRTCRAPLCSBLOBSLOT pSlot = &pThis->PtrCodeSignature.pSuper->aSlots[iSlot];
        while (iSlot-- > 0)
        {
            pSlot--;
            if (pSlot->uType == RTCRAPLCS_SLOT_SIGNATURE)
            {
                /*
                 * Validate the data offset.
                 */
                uint32_t offData = RT_BE2H_U32(pSlot->offData);
                if (   offData < pThis->cbCodeSignature - sizeof(RTCRAPLCSHDR)
                    || !(offData & 3) )
                {
                    /*
                     * The data is prefixed by a header with magic set to blob wrapper.
                     * Check that the size is within the bounds of the code signing blob.
                     */
                    PCRTCRAPLCSHDR pHdr = (PCRTCRAPLCSHDR)&pThis->PtrCodeSignature.pb[offData];
                    if (pHdr->uMagic == RTCRAPLCS_MAGIC_BLOBWRAPPER)
                    {
                        uint32_t cbData = RT_BE2H_U32(pHdr->cb);
                        uint32_t cbMax  = pThis->cbCodeSignature - offData ;
                        if (   cbData <= cbMax
                            && cbData > sizeof(RTCRAPLCSHDR))
                        {
                            /*
                             * Copy out the requested data.
                             */
                            *pcbRet = cbData;
                            if (cbData <= cbBuf)
                            {
                                memcpy(pvBuf, pHdr + 1, cbData);
                                return VINF_SUCCESS;
                            }
                            memcpy(pvBuf, pHdr + 1, cbBuf);
                            return VERR_BUFFER_OVERFLOW;
                        }
                    }
                }
                return VERR_LDRVI_BAD_CERT_FORMAT;
            }
        }
        rc = VERR_NOT_FOUND;
    }
    return rc;
}


/** @interface_method_impl{RTLDROPS,pfnQueryProp} */
static DECLCALLBACK(int) rtldrMachO_QueryProp(PRTLDRMODINTERNAL pMod, RTLDRPROP enmProp, void const *pvBits,
                                              void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    PRTLDRMODMACHO  pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    int             rc    = VERR_NOT_FOUND;
    switch (enmProp)
    {
        case RTLDRPROP_UUID:
            Assert(cbBuf >= sizeof(pThis->abImageUuid));
            if (!ASMMemIsZero(pThis->abImageUuid, sizeof(pThis->abImageUuid)))
            {
                *pcbRet = sizeof(pThis->abImageUuid);
                memcpy(pvBuf, pThis->abImageUuid, sizeof(pThis->abImageUuid));
                return VINF_SUCCESS;
            }
            break;

        case RTLDRPROP_FILE_OFF_HEADER:
            Assert(cbBuf == sizeof(uint32_t) || cbBuf == sizeof(uint64_t));
            if (cbBuf == sizeof(uint32_t))
                *(uint32_t *)pvBuf = pThis->offImage;
            else
                *(uint64_t *)pvBuf = pThis->offImage;
            return VINF_SUCCESS;

        case RTLDRPROP_IS_SIGNED:
            Assert(cbBuf == sizeof(bool));
            Assert(*pcbRet == cbBuf);
            *(bool *)pvBuf = pThis->cbCodeSignature > 0;
            return VINF_SUCCESS;

        case RTLDRPROP_PKCS7_SIGNED_DATA:
            if (pThis->cbCodeSignature > 0)
                return rtldrMachO_QueryPkcs7SignedData(pThis, pvBuf, cbBuf, pcbRet);
            break;


#if 0 /** @todo return LC_ID_DYLIB */
        case RTLDRPROP_INTERNAL_NAME:
#endif

        default:
            break;
    }
    NOREF(cbBuf);
    RT_NOREF_PV(pvBits);
    return rc;
}


#ifndef IPRT_WITHOUT_LDR_VERIFY

/**
 * Decodes the signature blob at RTLDRMODMACHO::PtrCodeSignature.
 *
 * @returns IPRT status code.
 * @param   pThis               The Mach-O module instance.
 * @param   ppSignature         Where to return the decoded signature data.
 * @param   pErrInfo            Where to supply extra error details. Optional.
 */
static int rtldrMachO_VerifySignatureDecode(PRTLDRMODMACHO pThis, PRTLDRMACHOSIGNATURE *ppSignature, PRTERRINFO pErrInfo)
{
    Assert(pThis->PtrCodeSignature.pSuper != NULL);

    /*
     * Allocate and init decoded signature data structure.
     */
    PRTLDRMACHOSIGNATURE pSignature = (PRTLDRMACHOSIGNATURE)RTMemTmpAllocZ(sizeof(*pSignature));
    *ppSignature = pSignature;
    if (!pSignature)
        return VERR_NO_TMP_MEMORY;
    pSignature->idxPkcs7 = UINT32_MAX;

    /*
     * Parse the slots, validating the slot headers.
     */
    PCRTCRAPLCSSUPERBLOB pSuper     = pThis->PtrCodeSignature.pSuper;
    uint32_t const       cSlots     = RT_BE2H_U32(pSuper->cSlots);
    uint32_t const       offFirst   = RT_UOFFSETOF_DYN(RTCRAPLCSSUPERBLOB, aSlots[cSlots]);
    uint32_t const       cbBlob     = RT_BE2H_U32(pSuper->Hdr.cb);
    for (uint32_t iSlot = 0; iSlot < cSlots; iSlot++)
    {
        /*
         * Check that the data offset is valid.  There appears to be no alignment
         * requirements here, which is a little weird consindering the PPC heritage.
         */
        uint32_t const offData = RT_BE2H_U32(pSuper->aSlots[iSlot].offData);
        if (   offData < offFirst
            || offData > cbBlob - sizeof(RTCRAPLCSHDR))
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                 "Slot #%u has an invalid data offset: %#x (min %#x, max %#x-4)",
                                 iSlot, offData, offFirst, cbBlob);
        uint32_t const cbMaxData = cbBlob - offData;

        /*
         * PKCS#7/CMS signature.
         */
        if (pSuper->aSlots[iSlot].uType == RTCRAPLCS_SLOT_SIGNATURE)
        {
            if (pSignature->idxPkcs7 != UINT32_MAX)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Already have PKCS#7 data in slot %#u", iSlot, pSignature->idxPkcs7);
            PCRTCRAPLCSHDR pHdr = (PCRTCRAPLCSHDR)&pThis->PtrCodeSignature.pb[offData];
            if (pHdr->uMagic != RTCRAPLCS_MAGIC_BLOBWRAPPER)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Invalid PKCS#7 wrapper magic: %#x", iSlot, RT_BE2H_U32(pHdr->uMagic));
            uint32_t const cb = RT_BE2H_U32(pHdr->cb);
            if (cb > cbMaxData || cb < sizeof(*pHdr) + 2U)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Invalid PKCS#7 size is out of bound: %#x (min %#x, max %#x)",
                                     iSlot, cb, sizeof(*pHdr) + 2, cbMaxData);
            pSignature->idxPkcs7 = iSlot;
            pSignature->pbPkcs7  = (uint8_t const *)(pHdr + 1);
            pSignature->cbPkcs7  = cb - sizeof(*pHdr);
        }
        /*
         * Code directories.
         */
        else if (   pSuper->aSlots[iSlot].uType == RTCRAPLCS_SLOT_CODEDIRECTORY
                 || (  RT_BE2H_U32(pSuper->aSlots[iSlot].uType) - RT_BE2H_U32_C(RTCRAPLCS_SLOT_ALTERNATE_CODEDIRECTORIES)
                     < RTCRAPLCS_SLOT_ALTERNATE_CODEDIRECTORIES_COUNT))
        {
            /* Make sure we don't get too many code directories and that the first one is a regular one. */
            if (pSignature->cCodeDirs >= RT_ELEMENTS(pSignature->aCodeDirs))
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Too many code directory slots (%u found thus far)",
                                     iSlot, pSignature->cCodeDirs + 1);
            if (   pSuper->aSlots[iSlot].uType == RTCRAPLCS_SLOT_CODEDIRECTORY
                && pSignature->cCodeDirs > 0)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Already have primary code directory in slot #%u",
                                     iSlot, pSignature->aCodeDirs[0].uSlot);
            if (   pSuper->aSlots[iSlot].uType != RTCRAPLCS_SLOT_CODEDIRECTORY /* lazy bird */
                && pSignature->cCodeDirs == 0)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Expected alternative code directory after the primary one", iSlot);

            /* Check data header: */
            if (cbMaxData < RT_UOFFSETOF(RTCRAPLCSCODEDIRECTORY, uUnused1))
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Insufficient data vailable for code directory (max %#x)", iSlot, cbMaxData);

            PCRTCRAPLCSCODEDIRECTORY pCodeDir = (PCRTCRAPLCSCODEDIRECTORY)&pThis->PtrCodeSignature.pb[offData];
            if (pCodeDir->Hdr.uMagic != RTCRAPLCS_MAGIC_CODEDIRECTORY)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Invalid code directory magic: %#x", iSlot, RT_BE2H_U32(pCodeDir->Hdr.uMagic));
            uint32_t const cbCodeDir = RT_BE2H_U32(pCodeDir->Hdr.cb);
            if (cbCodeDir > cbMaxData || cbCodeDir < RT_UOFFSETOF(RTCRAPLCSCODEDIRECTORY, offScatter))
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Code directory size is out of bound: %#x (min %#x, max %#x)",
                                     iSlot, cbCodeDir, RT_UOFFSETOF(RTCRAPLCSCODEDIRECTORY, offScatter), cbMaxData);
            pSignature->aCodeDirs[pSignature->cCodeDirs].pCodeDir = pCodeDir;
            pSignature->aCodeDirs[pSignature->cCodeDirs].cb       = cbCodeDir;

            /* Check Version: */
            uint32_t const uVersion = RT_BE2H_U32(pCodeDir->uVersion);
            if (   uVersion < RTCRAPLCS_VER_2_0
                || uVersion >= RT_MAKE_U32(0, 3))
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Code directory version is out of bounds: %#07x", iSlot, uVersion);
            uint32_t cbSelf = uVersion >= RTCRAPLCS_VER_SUPPORTS_EXEC_SEG      ? RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, fExecSeg)
                            : uVersion >= RTCRAPLCS_VER_SUPPORTS_CODE_LIMIT_64 ? RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, cbCodeLimit64)
                            : uVersion >= RTCRAPLCS_VER_SUPPORTS_TEAMID        ? RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, offTeamId)
                            : uVersion >= RTCRAPLCS_VER_SUPPORTS_SCATTER       ? RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, offScatter)
                            :                                                    RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, uUnused1);
            if (cbSelf > cbCodeDir)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Code directory size is out of bound: %#x (min %#x, max %#x)",
                                     iSlot, cbCodeDir, cbSelf, cbCodeDir);

            /* hash type and size. */
            uint8_t      cbHash;
            RTDIGESTTYPE enmDigest;
            switch (pCodeDir->bHashType)
            {
                case RTCRAPLCS_HASHTYPE_SHA1:
                    enmDigest = RTDIGESTTYPE_SHA1;
                    cbHash    = RTSHA1_HASH_SIZE;
                    break;
                case RTCRAPLCS_HASHTYPE_SHA256:
                    enmDigest = RTDIGESTTYPE_SHA256;
                    cbHash    = RTSHA256_HASH_SIZE;
                    break;
                case RTCRAPLCS_HASHTYPE_SHA256_TRUNCATED:
                    enmDigest = RTDIGESTTYPE_SHA256;
                    cbHash    = RTSHA1_HASH_SIZE; /* truncated to SHA-1 size. */
                    break;
                case RTCRAPLCS_HASHTYPE_SHA384:
                    enmDigest = RTDIGESTTYPE_SHA384;
                    cbHash    = RTSHA384_HASH_SIZE;
                    break;
                default:
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, "Slot #%u: Unknown hash type %#x (LB %#x)",
                                         iSlot, pCodeDir->bHashType, pCodeDir->cbHash);
            }
            pSignature->aCodeDirs[pSignature->cCodeDirs].enmDigest = enmDigest;
            if (pCodeDir->cbHash != cbHash)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Unexpected hash size for %s: %#x, expected %#x",
                                     iSlot, RTCrDigestTypeToName(enmDigest), pCodeDir->cbHash, cbHash);

            /* Hash slot offset and counts. Special slots are counted backwards from offHashSlots. */
            uint32_t const cSpecialSlots = RT_BE2H_U32(pCodeDir->cSpecialSlots);
            if (cSpecialSlots > 256)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Too many special slots: %#x", iSlot, cSpecialSlots);
            uint32_t const cCodeSlots = RT_BE2H_U32(pCodeDir->cCodeSlots);
            if (   cCodeSlots >= UINT32_MAX / 2
                || cCodeSlots + cSpecialSlots > (cbCodeDir - cbHash) / cbHash)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, "Slot #%u: Too many code slots: %#x + %#x (max %#x)",
                                     iSlot, cCodeSlots, cSpecialSlots, (cbCodeDir - cbHash) / cbHash);
            uint32_t const offHashSlots = RT_BE2H_U32(pCodeDir->offHashSlots);
            if (   offHashSlots > cbCodeDir - cCodeSlots * cbHash
                || offHashSlots < cbSelf + cSpecialSlots * cbHash)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Code directory hash offset is out of bounds: %#x (min: %#x, max: %#x)",
                                     iSlot, offHashSlots, cbSelf + cSpecialSlots * cbHash, cbCodeDir - cCodeSlots * cbHash);

            /* page shift */
            if (pCodeDir->cPageShift == 0)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Unsupported page shift of zero in code directory", iSlot);
            uint32_t cMaxPageShift;
            if (   pThis->Core.enmArch == RTLDRARCH_AMD64
                || pThis->Core.enmArch == RTLDRARCH_X86_32
                || pThis->Core.enmArch == RTLDRARCH_ARM32)
                cMaxPageShift = 12;
            else if (pThis->Core.enmArch == RTLDRARCH_ARM64)
                cMaxPageShift = 16; /* 16KB */
            else
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, "Unsupported architecture: %d", pThis->Core.enmArch);
            if (   pCodeDir->cPageShift < 12 /* */
                || pCodeDir->cPageShift > cMaxPageShift)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Page shift in code directory is out of range: %d (min: 12, max: %d)",
                                     iSlot, pCodeDir->cPageShift, cMaxPageShift);

            /* code limit vs page shift and code hash slots */
            uint32_t const cbCodeLimit32       = RT_BE2H_U32(pCodeDir->cbCodeLimit32);
            uint32_t const cExpectedCodeHashes = pCodeDir->cPageShift == 0 ? 1
                                               : (cbCodeLimit32 + RT_BIT_32(pCodeDir->cPageShift) - 1) >> pCodeDir->cPageShift;
            if (cExpectedCodeHashes != cCodeSlots)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Code limit and page shift value does not match code hash slots: cbCodeLimit32=%#x cPageShift=%u -> %#x; cCodeSlots=%#x",
                                     iSlot, cbCodeLimit32, pCodeDir->cPageShift, cExpectedCodeHashes, cCodeSlots);

            /* Identifier offset: */
            if (pCodeDir->offIdentifier)
            {
                uint32_t const offIdentifier = RT_BE2H_U32(pCodeDir->offIdentifier);
                if (   offIdentifier < cbSelf
                    || offIdentifier >= cbCodeDir)
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                         "Slot #%u: Identifier offset is out of bounds: %#x (min: %#x, max: %#x)",
                                         iSlot, offIdentifier, cbSelf, cbCodeDir - 1);
                int rc = RTStrValidateEncodingEx((char const *)pCodeDir + offIdentifier, cbCodeDir - offIdentifier,
                                                 RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
                if (RT_FAILURE(rc))
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                         "Slot #%u: Malformed identifier string: %Rrc", iSlot, rc);
            }

            /* Team identifier: */
            if (cbSelf >= RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, offTeamId) && pCodeDir->offTeamId)
            {
                uint32_t const offTeamId = RT_BE2H_U32(pCodeDir->offTeamId);
                if (   offTeamId < cbSelf
                    || offTeamId >= cbCodeDir)
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                         "Slot #%u: Team identifier offset is out of bounds: %#x (min: %#x, max: %#x)",
                                         iSlot, offTeamId, cbSelf, cbCodeDir - 1);
                int rc = RTStrValidateEncodingEx((char const *)pCodeDir + offTeamId, cbCodeDir - offTeamId,
                                                 RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
                if (RT_FAILURE(rc))
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                         "Slot #%u: Malformed team identifier string: %Rrc", iSlot, rc);
            }

            /* We don't support scatter. */
            if (cbSelf >= RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, offScatter) && pCodeDir->offScatter)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Scatter not supported.", iSlot);

            /* We don't really support the 64-bit code limit either: */
            if (   cbSelf >= RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, cbCodeLimit64)
                && pCodeDir->cbCodeLimit64
                && RT_BE2H_U64(pCodeDir->cbCodeLimit64) != cbCodeLimit32)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: 64-bit code limit does not match 32-bit: %#RX64 vs %#RX32",
                                     iSlot, RT_BE2H_U64(pCodeDir->cbCodeLimit64), cbCodeLimit32);

            /* Check executable segment info if present: */
            if (   cbSelf >= RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, fExecSeg)
                && (   pThis->offSeg0ForCodeSign != RT_BE2H_U64(pCodeDir->offExecSeg)
                    || pThis->cbSeg0ForCodeSign != RT_BE2H_U64(pCodeDir->cbExecSeg)
                    || pThis->fSeg0ForCodeSign != RT_BE2H_U64(pCodeDir->fExecSeg)) )
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Segment #0 info mismatch: @%#RX64 LB %#RX64 flags=%#RX64; expected @%#RX64 LB %#RX64 flags=%#RX64",
                                     iSlot, RT_BE2H_U64(pCodeDir->offExecSeg), RT_BE2H_U64(pCodeDir->cbExecSeg),
                                     RT_BE2H_U64(pCodeDir->fExecSeg), pThis->offSeg0ForCodeSign, pThis->cbSeg0ForCodeSign,
                                     pThis->fSeg0ForCodeSign);

            /* Check fields that must be zero (don't want anyone to use them to counter changes): */
            if (pCodeDir->uUnused1 != 0)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Unused field #1 is non-zero: %#x", iSlot, RT_BE2H_U32(pCodeDir->uUnused1));
            if (   cbSelf >= RT_UOFFSET_AFTER(RTCRAPLCSCODEDIRECTORY, uUnused2)
                && pCodeDir->uUnused2 != 0)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Slot #%u: Unused field #2 is non-zero: %#x", iSlot, RT_BE2H_U32(pCodeDir->uUnused2));

            /** @todo idPlatform values.   */
            /** @todo Check for gaps if we know the version number?  Alignment?  */

            /* If first code directory, check that the code limit covers the whole image up to the signature data. */
            if (pSignature->cCodeDirs == 0)
            {
                /** @todo verify the that the signature data is at the very end... */
                if (cbCodeLimit32 != pThis->offCodeSignature)
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                         "Slot #%u: Unexpected code limit: %#x, expected %#x",
                                         iSlot, cbCodeLimit32, pThis->offCodeSignature);
            }
            /* Otherwise, check that the code limit matches the previous directories. */
            else
                for (uint32_t i = 0; i < pSignature->cCodeDirs; i++)
                    if (pSignature->aCodeDirs[i].pCodeDir->cbCodeLimit32 != pCodeDir->cbCodeLimit32)
                        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                             "Slot #%u: Code limit differs from previous directory: %#x, expected %#x",
                                             iSlot, cbCodeLimit32, RT_BE2H_U32(pSignature->aCodeDirs[i].pCodeDir->cbCodeLimit32));

            /* Commit the code dir entry: */
            pSignature->aCodeDirs[pSignature->cCodeDirs++].uSlot = iSlot;
        }
    }

    /*
     * Check that we've got at least one code directory and one PKCS#7 signature.
     */
    if (pSignature->cCodeDirs == 0)
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, "No code directory slot in the code signature");
    if (pSignature->idxPkcs7 == UINT32_MAX)
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, "No PKCS#7 slot in the code signature");

    /*
     * Decode the PKCS#7 signature.
     */
    RTASN1CURSORPRIMARY PrimaryCursor;
    RTAsn1CursorInitPrimary(&PrimaryCursor, pSignature->pbPkcs7, pSignature->cbPkcs7,
                            pErrInfo, &g_RTAsn1DefaultAllocator, 0, "Mach-O-BLOB");
    int rc = RTCrPkcs7ContentInfo_DecodeAsn1(&PrimaryCursor.Cursor, 0, &pSignature->ContentInfo, "CI");
    if (RT_SUCCESS(rc))
    {
        if (RTCrPkcs7ContentInfo_IsSignedData(&pSignature->ContentInfo))
        {
            pSignature->pSignedData = pSignature->ContentInfo.u.pSignedData;

            /*
             * Check that the signedData stuff adds up.
             */
            if (!strcmp(pSignature->pSignedData->ContentInfo.ContentType.szObjId, RTCR_PKCS7_DATA_OID))
            {
                rc = RTCrPkcs7SignedData_CheckSanity(pSignature->pSignedData,
                                                     RTCRPKCS7SIGNEDDATA_SANITY_F_AUTHENTICODE /** @todo consider not piggy-backing on auth-code */
                                                     | RTCRPKCS7SIGNEDDATA_SANITY_F_ONLY_KNOWN_HASH
                                                     | RTCRPKCS7SIGNEDDATA_SANITY_F_SIGNING_CERT_PRESENT,
                                                     pErrInfo, "SD");
                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;
            }
            else
                rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_EXPECTED_INDIRECT_DATA_CONTENT_OID,
                                   "Unexpected pSignedData.ContentInfo.ContentType.szObjId value: %s (expected %s)",
                                   pSignature->pSignedData->ContentInfo.ContentType.szObjId, RTCR_PKCS7_DATA_OID);
        }
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_EXPECTED_INDIRECT_DATA_CONTENT_OID, /** @todo error code*/
                               "PKCS#7 is not 'signedData': %s", pSignature->ContentInfo.ContentType.szObjId);
    }
    return rc;
}

/**
 * Destroys the decoded signature data structure.
 *
 * @param   pSignature      The decoded signature data.  Can be NULL.
 */
static void rtldrMachO_VerifySignatureDestroy(PRTLDRMACHOSIGNATURE pSignature)
{
    if (pSignature)
    {
        RTCrPkcs7ContentInfo_Delete(&pSignature->ContentInfo);
        RTMemTmpFree(pSignature);
    }
}


/**
 * Worker for rtldrMachO_VerifySignatureValidatePkcs7Hashes that handles plists
 * with code directory hashes inside them.
 *
 * It is assumed that these plist files was invented to handle alternative code
 * directories.
 *
 * @note    Putting an XML plist into the authenticated attribute list was
 *          probably not such a great idea, given all the optional and
 *          adjustable white-space padding.  We should probably validate
 *          everything very strictly, limiting the elements, require certain
 *          attribute lists and even have strict expectations about the
 *          white-space, but right now let just make sure it's xml and get the
 *          data in the cdhashes array.
 *
 * @todo    The code here is a little braindead and bulky.  It should be
 *          possible to describe the expected XML structure using a tables.
 */
static int rtldrMachO_VerifySignatureValidateCdHashesPlist(PRTLDRMACHOSIGNATURE pSignature, char *pszPlist,
                                                           uint8_t *pbHash, uint32_t cbHash, PRTERRINFO pErrInfo)
{
    const char * const pszStart = pszPlist;
#define CHECK_ISTR_AND_SKIP_OR_RETURN(a_szLead) \
    do { \
        if (!RTStrNICmp(pszPlist, RT_STR_TUPLE(a_szLead))) \
            pszPlist += sizeof(a_szLead) - 1; \
        else return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, \
                                  "Expected '%s' found '%.16s...' at %#zu in plist", a_szLead, pszPlist, pszPlist - pszStart); \
    } while (0)
#define CHECK_STR_AND_SKIP_OR_RETURN(a_szLead) \
    do { \
        if (!RTStrNCmp(pszPlist, RT_STR_TUPLE(a_szLead))) \
            pszPlist += sizeof(a_szLead) - 1; \
        else return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, \
                                  "Expected '%s' found '%.16s...' at %#zu in plist", a_szLead, pszPlist, pszPlist - pszStart); \
    } while (0)
#define SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN() \
    do { /* currently only permitting spaces, tabs and newline, following char must be '<'. */ \
        char chMacro; \
        while ((chMacro = *pszPlist) == ' ' || chMacro == '\n' || chMacro == '\t') \
            pszPlist++; \
        if (chMacro == '<') { /* likely */ } \
        else return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, \
                                  "Expected '<' found '%.16s...' at %#zu in plist", pszPlist, pszPlist - pszStart); \
    } while (0)
#define SKIP_SPACE_BEFORE_VALUE() \
    do { /* currently only permitting spaces, tabs and newline. */ \
        char chMacro; \
        while ((chMacro = *pszPlist) == ' ' || chMacro == '\n' || chMacro == '\t') \
            pszPlist++; \
    } while (0)
#define SKIP_REQUIRED_SPACE_BETWEEN_ATTRIBUTES_OR_RETURN() \
    do { /* currently only permitting a single space */ \
        if (pszPlist[0] == ' ' && pszPlist[1] != ' ') \
            pszPlist++; \
        else return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, \
                                  "Expected ' ' found '%.16s...' at %#zu in plist", pszPlist, pszPlist - pszStart); \
    } while (0)

    /* Example:
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>cdhashes</key>
  <array>
          <data>
          hul2SSkDQFRXbGlt3AmCp25MU0Y=
          </data>
          <data>
          N0kvxg0CJBNuZTq135PntAaRczw=
          </data>
  </array>
</dict>
</plist>
    */

    /* <?xml version="1.0" encoding="UTF-8"?> */
    CHECK_STR_AND_SKIP_OR_RETURN("<?xml");
    SKIP_REQUIRED_SPACE_BETWEEN_ATTRIBUTES_OR_RETURN();
    CHECK_STR_AND_SKIP_OR_RETURN("version=\"1.0\"");
    SKIP_REQUIRED_SPACE_BETWEEN_ATTRIBUTES_OR_RETURN();
    CHECK_STR_AND_SKIP_OR_RETURN("encoding=\"UTF-8\"");
    CHECK_STR_AND_SKIP_OR_RETURN("?>");
    SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();

    /* <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"> */
    CHECK_STR_AND_SKIP_OR_RETURN("<!DOCTYPE");
    SKIP_REQUIRED_SPACE_BETWEEN_ATTRIBUTES_OR_RETURN();
    CHECK_STR_AND_SKIP_OR_RETURN("plist");
    SKIP_REQUIRED_SPACE_BETWEEN_ATTRIBUTES_OR_RETURN();
    CHECK_STR_AND_SKIP_OR_RETURN("PUBLIC");
    SKIP_REQUIRED_SPACE_BETWEEN_ATTRIBUTES_OR_RETURN();
    CHECK_STR_AND_SKIP_OR_RETURN("\"-//Apple//DTD PLIST 1.0//EN\"");
    SKIP_REQUIRED_SPACE_BETWEEN_ATTRIBUTES_OR_RETURN();
    CHECK_STR_AND_SKIP_OR_RETURN("\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"");
    CHECK_STR_AND_SKIP_OR_RETURN(">");
    SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();

    /* <plist version="1.0"> */
    CHECK_STR_AND_SKIP_OR_RETURN("<plist");
    SKIP_REQUIRED_SPACE_BETWEEN_ATTRIBUTES_OR_RETURN();
    CHECK_STR_AND_SKIP_OR_RETURN("version=\"1.0\"");
    CHECK_STR_AND_SKIP_OR_RETURN(">");
    SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();

    /* <dict> */
    CHECK_STR_AND_SKIP_OR_RETURN("<dict>");
    SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();

    /* <key>cdhashes</key> */
    CHECK_STR_AND_SKIP_OR_RETURN("<key>cdhashes</key>");
    SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();

    /* <array> */
    CHECK_STR_AND_SKIP_OR_RETURN("<array>");
    SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();

    /*
     * Repeated: <data>hul2SSkDQFRXbGlt3AmCp25MU0Y=</data>
     */
    uint32_t iCodeDir = 0;
    for (;;)
    {
        /* Decode the binary data (base64) and skip it. */
        CHECK_STR_AND_SKIP_OR_RETURN("<data>");
        SKIP_SPACE_BEFORE_VALUE();

        char ch;
        size_t cchBase64 = 0;
        while (RT_C_IS_ALNUM(ch = pszPlist[cchBase64]) || ch == '+' || ch == '/' || ch == '=')
            cchBase64++;
        size_t cbActualHash = cbHash;
        char *pszEnd = NULL;
        int rc = RTBase64DecodeEx(pszPlist, cchBase64, pbHash, cbHash, &cbActualHash, &pszEnd);
        if (RT_FAILURE(rc))
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                 "Failed to decode hash #%u in authenticated plist attribute: %Rrc (%.*s)",
                                 iCodeDir, rc, cchBase64, pszPlist);
        pszPlist += cchBase64;
        AssertReturn(pszPlist == pszEnd, VERR_INTERNAL_ERROR_2);
        SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();

        /* The binary hash data must be exactly the size of SHA1, larger
           hash like SHA-256 and SHA-384 are truncated for some reason. */
        if (cbActualHash != RTSHA1_HASH_SIZE)
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                 "Hash #%u in authenticated plist attribute has the wrong length: %u, exepcted %u",
                                 iCodeDir, cbActualHash, RTSHA1_HASH_SIZE);

        /* Skip closing tag. */
        CHECK_STR_AND_SKIP_OR_RETURN("</data>");
        SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();


        /* Calculate the hash and compare. */
        RTCRDIGEST hDigest;
        rc = RTCrDigestCreateByType(&hDigest, pSignature->aCodeDirs[iCodeDir].enmDigest);
        if (RT_SUCCESS(rc))
        {
            rc = RTCrDigestUpdate(hDigest, pSignature->aCodeDirs[iCodeDir].pCodeDir, pSignature->aCodeDirs[iCodeDir].cb);
            if (RT_SUCCESS(rc))
            {
                if (memcmp(pbHash, RTCrDigestGetHash(hDigest), cbActualHash) == 0)
                    rc = VINF_SUCCESS;
                else
                    rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_IMAGE_HASH_MISMATCH,
                                       "Code directory #%u hash mismatch (plist):\n"
                                       "signed: %.*Rhxs\n"
                                       "our:    %.*Rhxs\n",
                                       iCodeDir, cbActualHash, pbHash,
                                       RTCrDigestGetHashSize(hDigest), RTCrDigestGetHash(hDigest));
            }
            else
                rc = RTErrInfoSetF(pErrInfo, rc, "RTCrDigestUpdate failed: %Rrc", rc);
            RTCrDigestRelease(hDigest);
        }
        else
            rc = RTErrInfoSetF(pErrInfo, rc, "Failed to create a digest of type %u verifying code dir #%u: %Rrc",
                               pSignature->aCodeDirs[iCodeDir].enmDigest, iCodeDir, rc);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Advance.
         */
        iCodeDir++;
        SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();
        if (RTStrNCmp(pszPlist, RT_STR_TUPLE("<data>")) == 0)
        {
            if (iCodeDir >= pSignature->cCodeDirs)
                return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                     "Authenticated plist attribute has too many code directories (%u in blob)",
                                     pSignature->cCodeDirs);
        }
        else if (iCodeDir == pSignature->cCodeDirs)
            break;
        else
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                 "Authenticated plist attribute does not include all code directors: %u out of %u",
                                 iCodeDir, pSignature->cCodeDirs);
    }

    /*</array>*/
    CHECK_STR_AND_SKIP_OR_RETURN("</array>");
    SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();

    /*</dict>*/
    CHECK_STR_AND_SKIP_OR_RETURN("</dict>");
    SKIP_SPACE_BETWEEN_ELEMENTS_OR_RETURN();

    /*</plist>*/
    CHECK_STR_AND_SKIP_OR_RETURN("</plist>");
    SKIP_SPACE_BEFORE_VALUE();

    if (*pszPlist == '\0')
        return VINF_SUCCESS;
    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                         "Authenticated plist attribute has unexpected trailing content: %.32s", pszPlist);
}


/**
 * Verifies the code directory hashes embedded in the PKCS\#7 data.
 *
 * @returns IPRT status code.
 * @param   pSignature      The decoded signature data.
 * @param   pErrInfo        Where to supply extra error details. Optional.
 */
static int rtldrMachO_VerifySignatureValidatePkcs7Hashes(PRTLDRMACHOSIGNATURE pSignature, PRTERRINFO pErrInfo)
{
    /*
     * Look thru the authenticated attributes in the signer info array.
     */
    PRTCRPKCS7SIGNEDDATA pSignedData = pSignature->pSignedData;
    for (uint32_t iSignerInfo = 0; iSignerInfo < pSignedData->SignerInfos.cItems; iSignerInfo++)
    {
        PCRTCRPKCS7SIGNERINFO pSignerInfo = pSignedData->SignerInfos.papItems[iSignerInfo];
        bool                  fMsgDigest  = false;
        bool                  fPlist      = false;
        for (uint32_t iAttrib = 0; iAttrib < pSignerInfo->AuthenticatedAttributes.cItems; iAttrib++)
        {
            PCRTCRPKCS7ATTRIBUTE pAttrib = pSignerInfo->AuthenticatedAttributes.papItems[iAttrib];
            if (RTAsn1ObjId_CompareWithString(&pAttrib->Type, RTCR_PKCS9_ID_MESSAGE_DIGEST_OID) == 0)
            {
                /*
                 * Validate the message digest while we're here.
                 */
                AssertReturn(pAttrib->uValues.pOctetStrings && pAttrib->uValues.pOctetStrings->cItems == 1, VERR_INTERNAL_ERROR_5);

                RTCRDIGEST hDigest;
                int rc = RTCrDigestCreateByObjId(&hDigest, &pSignerInfo->DigestAlgorithm.Algorithm);
                if (RT_SUCCESS(rc))
                {
                    rc = RTCrDigestUpdate(hDigest, pSignature->aCodeDirs[0].pCodeDir, pSignature->aCodeDirs[0].cb);
                    if (RT_SUCCESS(rc))
                    {
                        if (!RTCrDigestMatch(hDigest,
                                             pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.uData.pv,
                                             pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.cb))
                            rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_MESSAGE_DIGEST_ATTRIB_MISMATCH,
                                               "Authenticated message-digest attribute mismatch:\n"
                                               "signed: %.*Rhxs\n"
                                               "our:    %.*Rhxs\n",
                                               pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.cb,
                                               pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.uData.pv,
                                               RTCrDigestGetHashSize(hDigest), RTCrDigestGetHash(hDigest));
                    }
                    else
                        rc = RTErrInfoSetF(pErrInfo, rc, "RTCrDigestUpdate failed: %Rrc", rc);
                    RTCrDigestRelease(hDigest);
                }
                else
                    rc = RTErrInfoSetF(pErrInfo, rc, "Failed to create a digest for OID %s: %Rrc",
                                       pSignerInfo->DigestAlgorithm.Algorithm.szObjId, rc);
                if (RT_FAILURE(rc))
                    return rc;
                fMsgDigest = true;
            }
            else if (pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_APPLE_MULTI_CD_PLIST)
            {
                /*
                 * An XML (better be) property list with code directory hashes in it.
                 */
                if (!pAttrib->uValues.pOctetStrings || pAttrib->uValues.pOctetStrings->cItems != 1)
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, "Bad authenticated plist attribute");

                uint32_t    cch = pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.cb;
                char const *pch = pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.uData.pch;
                int rc = RTStrValidateEncodingEx(pch, cch, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
                if (RT_FAILURE(rc))
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                         "Authenticated plist attribute is not valid UTF-8: %Rrc", rc);
                uint32_t const cchMin = sizeof("<?xml?><plist><dict><key>cdhashes</key><array><data>hul2SSkDQFRXbGlt3AmCp25MU0Y=</data></array></dict></plist>") - 1;
                if (cch < cchMin)
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                         "Authenticated plist attribute is too short: %#x, min: %#x", cch, cchMin);
                if (cch > _64K)
                    return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT,
                                         "Authenticated plist attribute is too long: %#x, max: 64KB", cch);

                /* Copy the plist into a buffer and zero terminate it.  Also allocate room for decoding a hash. */
                const uint32_t cbMaxHash = 128;
                char *pszTmp = (char *)RTMemTmpAlloc(cbMaxHash + cch + 3);
                if (!pszTmp)
                    return VERR_NO_TMP_MEMORY;
                pszTmp[cbMaxHash + cch] = '\0';
                pszTmp[cbMaxHash + cch + 1] = '\0';
                pszTmp[cbMaxHash + cch + 2] = '\0';
                rc = rtldrMachO_VerifySignatureValidateCdHashesPlist(pSignature, (char *)memcpy(pszTmp + cbMaxHash, pch, cch),
                                                                     (uint8_t *)pszTmp, cbMaxHash, pErrInfo);
                RTMemTmpFree(pszTmp);
                if (RT_FAILURE(rc))
                    return rc;
                fPlist = true;
            }
        }
        if (!fMsgDigest && pSignature->cCodeDirs > 1)
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, "Missing authenticated message-digest attribute");
        if (!fPlist && pSignature->cCodeDirs > 1)
            return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, "Missing authenticated code directory hash plist attribute");
    }
    if (pSignedData->SignerInfos.cItems < 1)
        return RTErrInfoSetF(pErrInfo, VERR_LDRVI_BAD_CERT_FORMAT, "PKCS#7 signed data contains no signatures");

    return VINF_SUCCESS;
}


/**
 * Verifies the page hashes of the given code directory.
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module instance.
 * @param   pEntry          The data entry for the code directory to validate.
 * @param   pbBuf           Read buffer.
 * @param   cbBuf           Buffer size.
 * @param   pErrInfo        Where to supply extra error details. Optional.
 */
static int rtldrMachO_VerifySignatureValidateCodeDir(PRTLDRMODMACHO pThis, PRTLDRMACHCODEDIR pEntry,
                                                     uint8_t *pbBuf, uint32_t cbBuf, PRTERRINFO pErrInfo)
{
    RTCRDIGEST hDigest;
    int rc = RTCrDigestCreateByType(&hDigest, pEntry->enmDigest);
    if (RT_SUCCESS(rc))
    {
        PCRTCRAPLCSCODEDIRECTORY pCodeDir    = pEntry->pCodeDir;
        PRTLDRREADER const       pRdr        = pThis->Core.pReader;
        uint32_t                 cbCodeLimit = RT_BE2H_U32(pCodeDir->cbCodeLimit32);
        uint32_t const           cbPage      = RT_BIT_32(pCodeDir->cPageShift);
        uint32_t const           cHashes     = RT_BE2H_U32(pCodeDir->cCodeSlots);
        uint8_t const            cbHash      = pCodeDir->cbHash;
        uint8_t const           *pbHash      = (uint8_t const *)pCodeDir + RT_BE2H_U32(pCodeDir->offHashSlots);
        RTFOFF                   offFile     = pThis->offImage;
        if (   RT_BE2H_U32(pCodeDir->uVersion) < RTCRAPLCS_VER_SUPPORTS_SCATTER
            || pCodeDir->offScatter == 0)
        {
            /*
             * Work the image in linear fashion.
             */
            for (uint32_t iHash = 0; iHash < cHashes; iHash++, pbHash += cbHash, cbCodeLimit -= cbPage)
            {
                RTFOFF const offPage = offFile;

                /*
                 * Read and digest the data for the current hash page.
                 */
                rc = RTCrDigestReset(hDigest);
                AssertRCBreak(rc);
                Assert(cbCodeLimit > cbPage || iHash + 1 == cHashes);
                uint32_t cbLeft = iHash + 1 < cHashes ? cbPage : cbCodeLimit;
                while (cbLeft > 0)
                {
                    uint32_t const cbToRead = RT_MIN(cbBuf, cbLeft);
                    rc = pRdr->pfnRead(pRdr, pbBuf, cbToRead, offFile);
                    AssertRCBreak(rc);

                    rc = RTCrDigestUpdate(hDigest, pbBuf, cbToRead);
                    AssertRCBreak(rc);

                    offFile += cbToRead;
                    cbLeft  -= cbToRead;
                }
                AssertRCBreak(rc);
                rc = RTCrDigestFinal(hDigest, NULL, 0);
                AssertRCBreak(rc);

                /*
                 * Compare it.
                 * Note! Don't use RTCrDigestMatch here as there is a truncated SHA-256 variant.
                 */
                if (memcmp(pbHash, RTCrDigestGetHash(hDigest), cbHash) != 0)
                {
                    rc = RTErrInfoSetF(pErrInfo, VERR_LDRVI_PAGE_HASH_MISMATCH,
                                       "Hash #%u (@%RX64 LB %#x) mismatch in code dir #%u: %.*Rhxs, expected %.*Rhxs",
                                       iHash, offPage, cbPage, pEntry->uSlot, (int)cbHash, pbHash,
                                       (int)cbHash, RTCrDigestGetHash(hDigest));
                    break;
                }

            }
        }
        /*
         * Work the image in scattered fashion.
         */
        else
            rc = VERR_INTERNAL_ERROR_4;

        RTCrDigestRelease(hDigest);
    }
    return rc;
}


/**
 * Verifies the page hashes of all the code directories
 *
 * @returns IPRT status code.
 * @param   pThis           The Mach-O module instance.
 * @param   pSignature      The decoded signature data.
 * @param   pErrInfo        Where to supply extra error details. Optional.
 */
static int rtldrMachO_VerifySignatureValidateCodeDirs(PRTLDRMODMACHO pThis, PRTLDRMACHOSIGNATURE pSignature, PRTERRINFO pErrInfo)
{
    void *pvBuf = RTMemTmpAllocZ(_4K);
    if (pvBuf)
    {
        int rc = VERR_INTERNAL_ERROR_3;
        for (uint32_t i = 0; i < pSignature->cCodeDirs; i++)
        {
            rc = rtldrMachO_VerifySignatureValidateCodeDir(pThis, &pSignature->aCodeDirs[i], (uint8_t *)pvBuf, _4K, pErrInfo);
            if (RT_FAILURE(rc))
                break;
        }
        RTMemTmpFree(pvBuf);
        return rc;
    }
    return VERR_NO_TMP_MEMORY;
}

#endif /* !IPRT_WITHOUT_LDR_VERIFY*/

/**
 * @interface_method_impl{RTLDROPS,pfnVerifySignature}
 */
static DECLCALLBACK(int)
rtldrMachO_VerifySignature(PRTLDRMODINTERNAL pMod, PFNRTLDRVALIDATESIGNEDDATA pfnCallback, void *pvUser, PRTERRINFO pErrInfo)
{
#ifndef IPRT_WITHOUT_LDR_VERIFY
    PRTLDRMODMACHO pThis = RT_FROM_MEMBER(pMod, RTLDRMODMACHO, Core);
    int rc = rtldrMachO_LoadSignatureBlob(pThis);
    if (RT_SUCCESS(rc))
    {
        PRTLDRMACHOSIGNATURE pSignature = NULL;
        rc = rtldrMachO_VerifySignatureDecode(pThis, &pSignature, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            rc = rtldrMachO_VerifySignatureValidatePkcs7Hashes(pSignature, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                rc = rtldrMachO_VerifySignatureValidateCodeDirs(pThis, pSignature, pErrInfo);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Finally, let the caller verify the certificate chain for the PKCS#7 bit.
                     */
                    RTLDRSIGNATUREINFO Info;
                    Info.iSignature     = 0;
                    Info.cSignatures    = 1;
                    Info.enmType        = RTLDRSIGNATURETYPE_PKCS7_SIGNED_DATA;
                    Info.pvSignature    = &pSignature->ContentInfo;
                    Info.cbSignature    = sizeof(pSignature->ContentInfo);
                    Info.pvExternalData = pSignature->aCodeDirs[0].pCodeDir;
                    Info.cbExternalData = pSignature->aCodeDirs[0].cb;
                    rc = pfnCallback(&pThis->Core, &Info, pErrInfo, pvUser);
                }
            }
        }
        rtldrMachO_VerifySignatureDestroy(pSignature);
    }
    return rc;
#else
    RT_NOREF_PV(pMod); RT_NOREF_PV(pfnCallback); RT_NOREF_PV(pvUser); RT_NOREF_PV(pErrInfo);
    return VERR_NOT_SUPPORTED;
#endif
}


/**
 * Operations for a Mach-O module interpreter.
 */
static const RTLDROPS s_rtldrMachOOps=
{
    "mach-o",
    rtldrMachO_Close,
    NULL,
    NULL /*pfnDone*/,
    rtldrMachO_EnumSymbols,
    /* ext */
    rtldrMachO_GetImageSize,
    rtldrMachO_GetBits,
    rtldrMachO_RelocateBits,
    rtldrMachO_GetSymbolEx,
    NULL /*pfnQueryForwarderInfo*/,
    rtldrMachO_EnumDbgInfo,
    rtldrMachO_EnumSegments,
    rtldrMachO_LinkAddressToSegOffset,
    rtldrMachO_LinkAddressToRva,
    rtldrMachO_SegOffsetToRva,
    rtldrMachO_RvaToSegOffset,
    rtldrMachO_ReadDbgInfo,
    rtldrMachO_QueryProp,
    rtldrMachO_VerifySignature,
    NULL /*pfnHashImage*/,
    NULL /*pfnUnwindFrame*/,
    42
};


/**
 * Handles opening Mach-O images (non-fat).
 */
DECLHIDDEN(int) rtldrMachOOpen(PRTLDRREADER pReader, uint32_t fFlags, RTLDRARCH enmArch, RTFOFF offImage,
                               PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo)
{

    /*
     * Create the instance data and do a minimal header validation.
     */
    PRTLDRMODMACHO pThis = NULL;
    int rc = kldrModMachODoCreate(pReader, offImage, fFlags, &pThis, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Match up against the requested CPU architecture.
         */
        if (   enmArch == RTLDRARCH_WHATEVER
            || pThis->Core.enmArch == enmArch)
        {
            pThis->Core.pOps     = &s_rtldrMachOOps;
            pThis->Core.u32Magic = RTLDRMOD_MAGIC;
            *phLdrMod = &pThis->Core;
            return VINF_SUCCESS;
        }
        rc = VERR_LDR_ARCH_MISMATCH;
    }
    if (pThis)
    {
        RTMemFree(pThis->pbLoadCommands);
        RTMemFree(pThis);
    }
    return rc;

}


/**
 * Handles opening FAT Mach-O image.
 */
DECLHIDDEN(int) rtldrFatOpen(PRTLDRREADER pReader, uint32_t fFlags, RTLDRARCH enmArch, PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo)
{
    fat_header_t FatHdr;
    int rc = pReader->pfnRead(pReader, &FatHdr, sizeof(FatHdr), 0);
    if (RT_FAILURE(rc))
        return RTErrInfoSetF(pErrInfo, rc, "Read error at offset 0: %Rrc", rc);

    if (FatHdr.magic == IMAGE_FAT_SIGNATURE)
    { /* likely */ }
    else if (FatHdr.magic == IMAGE_FAT_SIGNATURE_OE)
        FatHdr.nfat_arch = RT_BSWAP_U32(FatHdr.nfat_arch);
    else
        return RTErrInfoSetF(pErrInfo, VERR_INVALID_EXE_SIGNATURE, "magic=%#x", FatHdr.magic);
    if (FatHdr.nfat_arch < 64)
        return RTErrInfoSetF(pErrInfo, VERR_INVALID_EXE_SIGNATURE, "Bad nfat_arch value: %#x", FatHdr.nfat_arch);

    uint32_t offEntry = sizeof(FatHdr);
    for (uint32_t i = 0; i < FatHdr.nfat_arch; i++, offEntry += sizeof(fat_arch_t))
    {
        fat_arch_t FatEntry;
        rc = pReader->pfnRead(pReader, &FatEntry, sizeof(FatEntry), offEntry);
        if (RT_FAILURE(rc))
            return RTErrInfoSetF(pErrInfo, rc, "Read error at offset 0: %Rrc", rc);
        if (FatHdr.magic == IMAGE_FAT_SIGNATURE_OE)
        {
            FatEntry.cputype    = (int32_t)RT_BSWAP_U32((uint32_t)FatEntry.cputype);
            //FatEntry.cpusubtype = (int32_t)RT_BSWAP_U32((uint32_t)FatEntry.cpusubtype);
            FatEntry.offset     = RT_BSWAP_U32(FatEntry.offset);
            //FatEntry.size       = RT_BSWAP_U32(FatEntry.size);
            //FatEntry.align      = RT_BSWAP_U32(FatEntry.align);
        }

        /*
         * Match enmArch.
         */
        bool fMatch = false;
        switch (enmArch)
        {
            case RTLDRARCH_WHATEVER:
                fMatch = true;
                break;

            case RTLDRARCH_X86_32:
                fMatch = FatEntry.cputype == CPU_TYPE_X86;
                break;

            case RTLDRARCH_AMD64:
                fMatch = FatEntry.cputype == CPU_TYPE_X86_64;
                break;

            case RTLDRARCH_ARM32:
                fMatch = FatEntry.cputype == CPU_TYPE_ARM32;
                break;

            case RTLDRARCH_ARM64:
                fMatch = FatEntry.cputype == CPU_TYPE_ARM64;
                break;

            case RTLDRARCH_X86_16:
                fMatch = false;
                break;

            case RTLDRARCH_INVALID:
            case RTLDRARCH_HOST:
            case RTLDRARCH_END:
            case RTLDRARCH_32BIT_HACK:
                AssertFailedReturn(VERR_INVALID_PARAMETER);
        }
        if (fMatch)
            return rtldrMachOOpen(pReader, fFlags, enmArch, FatEntry.offset, phLdrMod, pErrInfo);
    }

    return VERR_LDR_ARCH_MISMATCH;

}

