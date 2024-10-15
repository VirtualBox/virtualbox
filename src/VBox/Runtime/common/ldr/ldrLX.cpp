/* $Id: ldrLX.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * kLdr - The Module Interpreter for the Linear eXecutable (LX) Format.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * This code is based on: kLdr/kLdrModLX.c from kStuff r113.
 *
 * Copyright (c) 2006-2007 Knut St. Osmundsen <bird-kStuff-spamix@anduin.net>
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
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include <iprt/formats/lx.h>
#include <iprt/formats/pecoff.h>
#include <iprt/formats/codeview.h>
#include <iprt/formats/elf32.h>
#include "internal/ldr.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def KLDRMODLX_STRICT
 * Define KLDRMODLX_STRICT to enabled strict checks in KLDRMODLX. */
#define KLDRMODLX_STRICT 1

/** @def KLDRMODLX_ASSERT
 * Assert that an expression is true when KLDR_STRICT is defined.
 */
#ifdef KLDRMODLX_STRICT
# define KLDRMODLX_ASSERT(expr)  Assert(expr)
#else
# define KLDRMODLX_ASSERT(expr)  do {} while (0)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Instance data for the LX module interpreter.
 */
typedef struct KLDRMODLX
{
    /** Core module structure. */
    RTLDRMODINTERNAL        Core;

    /** Pointer to the user mapping. */
    const void             *pvMapping;
    /** The size of the mapped LX image. */
    size_t                  cbMapped;
    /** Reserved flags. */
    uint32_t                f32Reserved;

    /** The offset of the LX header. */
    RTFOFF                  offHdr;
    /** Copy of the LX header. */
    struct e32_exe          Hdr;

    /** Pointer to the loader section.
     * Allocated together with this strcture. */
    const uint8_t          *pbLoaderSection;
    /** Pointer to the last byte in the loader section. */
    const uint8_t          *pbLoaderSectionLast;
    /** Pointer to the object table in the loader section. */
    const struct o32_obj   *paObjs;
    /** Pointer to the object page map table in the loader section. */
    const struct o32_map   *paPageMappings;
    /** Pointer to the resource table in the loader section. */
    const struct rsrc32    *paRsrcs;
    /** Pointer to the resident name table in the loader section. */
    const uint8_t          *pbResNameTab;
    /** Pointer to the entry table in the loader section. */
    const uint8_t          *pbEntryTab;

    /** Pointer to the non-resident name table. */
    uint8_t                *pbNonResNameTab;
    /** Pointer to the last byte in the non-resident name table. */
    const uint8_t          *pbNonResNameTabLast;

    /** Pointer to the fixup section. */
    uint8_t                *pbFixupSection;
    /** Pointer to the last byte in the fixup section. */
    const uint8_t          *pbFixupSectionLast;
    /** Pointer to the fixup page table within pvFixupSection. */
    const uint32_t         *paoffPageFixups;
    /** Pointer to the fixup record table within pvFixupSection. */
    const uint8_t          *pbFixupRecs;
    /** Pointer to the import module name table within pvFixupSection. */
    const uint8_t          *pbImportMods;
    /** Pointer to the import module name table within pvFixupSection. */
    const uint8_t          *pbImportProcs;

    /** Pointer to the module name (in the resident name table). */
    const char             *pszName;
    /** The name length. */
    size_t                  cchName;

    /** The target CPU. */
    RTLDRCPU                enmCpu;
    /** Number of segments in aSegments. */
    uint32_t                cSegments;
    /** Segment info. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RTLDRSEG                aSegments[RT_FLEXIBLE_ARRAY];
} KLDRMODLX, *PKLDRMODLX;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int kldrModLXHasDbgInfo(PRTLDRMODINTERNAL pMod, const void *pvBits);
static DECLCALLBACK(int) rtldrLX_RelocateBits(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR NewBaseAddress,
                                              RTUINTPTR OldBaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser);
static const uint8_t *kldrModLXDoNameTableLookupByOrdinal(const uint8_t *pbNameTable, ssize_t cbNameTable, uint32_t iOrdinal);
static int kldrModLXDoNameLookup(PKLDRMODLX pModLX, const char *pchSymbol, size_t cchSymbol, uint32_t *piSymbol);
static const uint8_t *kldrModLXDoNameTableLookupByName(const uint8_t *pbNameTable, ssize_t cbNameTable,
                                                       const char *pchSymbol, size_t cchSymbol);
static int kldrModLXGetImport(PKLDRMODLX pThis, const void *pvBits, uint32_t iImport,
                              char *pszName, size_t cchName, size_t *pcbNeeded);
static int kldrModLXDoLoadBits(PKLDRMODLX pModLX, void *pvBits);
static int kldrModLXDoIterDataUnpacking(uint8_t *pbDst, const uint8_t *pbSrc, int cbSrc);
static int kldrModLXDoIterData2Unpacking(uint8_t *pbDst, const uint8_t *pbSrc, int cbSrc);
static void kLdrModLXMemCopyW(uint8_t *pbDst, const uint8_t *pbSrc, int cb);
static int kldrModLXDoForwarderQuery(PKLDRMODLX pModLX, const struct e32_entry *pEntry,
                                     PFNRTLDRIMPORT pfnGetForwarder, void *pvUser, PRTLDRADDR puValue, uint32_t *pfKind);
#if 0
static int kldrModLXDoProtect(PKLDRMODLX pModLX, void *pvBits, unsigned fUnprotectOrProtect);
static int kldrModLXDoCallDLL(PKLDRMODLX pModLX, void *pvMapping, unsigned uOp, uintptr_t uHandle);
static int32_t kldrModLXDoCall(uintptr_t uEntrypoint, uintptr_t uHandle, uint32_t uOp, void *pvReserved);
#endif
static int kldrModLXDoLoadFixupSection(PKLDRMODLX pModLX);
static int kldrModLXDoReloc(uint8_t *pbPage, int off, RTLDRADDR PageAddress, const struct r32_rlc *prlc,
                            int iSelector, RTLDRADDR uValue, uint32_t fKind);


/**
 * Separate function for reading creating the LX module instance to
 * simplify cleanup on failure.
 */
static int kldrModLXDoCreate(PRTLDRREADER pRdr, RTFOFF offNewHdr, uint32_t fFlags, PKLDRMODLX *ppModLX, PRTERRINFO pErrInfo)
{
    struct e32_exe Hdr;
    PKLDRMODLX pModLX;
    uint32_t off, offEnd;
    uint32_t i;
    int fCanOptimizeMapping;
    uint32_t NextRVA;

    RT_NOREF(fFlags);
    *ppModLX = NULL;

    /*
     * Read the signature and file header.
     */
    int rc = pRdr->pfnRead(pRdr, &Hdr, sizeof(Hdr), offNewHdr > 0 ? offNewHdr : 0);
    if (RT_FAILURE(rc))
        return RTErrInfoSetF(pErrInfo, rc, "Error reading LX header at %RTfoff: %Rrc", offNewHdr, rc);
    if (    Hdr.e32_magic[0] != E32MAGIC1
        ||  Hdr.e32_magic[1] != E32MAGIC2)
        return RTErrInfoSetF(pErrInfo, VERR_INVALID_EXE_SIGNATURE, "Not LX magic: %02x %02x", Hdr.e32_magic[0], Hdr.e32_magic[1]);

    /* We're not interested in anything but x86 images. */
    if (    Hdr.e32_level != E32LEVEL
        ||  Hdr.e32_border != E32LEBO
        ||  Hdr.e32_worder != E32LEWO
        ||  Hdr.e32_cpu < E32CPU286
        ||  Hdr.e32_cpu > E32CPU486
        ||  Hdr.e32_pagesize != OBJPAGELEN
        )
        return VERR_LDRLX_BAD_HEADER;

    /* Some rough sanity checks. */
    offEnd = pRdr->pfnSize(pRdr) >= (uint64_t)~(uint32_t)16 ? ~(uint32_t)16 : (uint32_t)pRdr->pfnSize(pRdr);
    if (    Hdr.e32_itermap > offEnd
        ||  Hdr.e32_datapage > offEnd
        ||  Hdr.e32_nrestab > offEnd
        ||  Hdr.e32_nrestab + Hdr.e32_cbnrestab > offEnd
        ||  Hdr.e32_ldrsize > offEnd - offNewHdr - sizeof(Hdr)
        ||  Hdr.e32_fixupsize > offEnd - offNewHdr - sizeof(Hdr)
        ||  Hdr.e32_fixupsize + Hdr.e32_ldrsize > offEnd - offNewHdr - sizeof(Hdr))
        return VERR_LDRLX_BAD_HEADER;

    /* Verify the loader section. */
    offEnd = Hdr.e32_objtab + Hdr.e32_ldrsize;
    if (Hdr.e32_objtab < sizeof(Hdr) && Hdr.e32_objcnt)
        return RTErrInfoSetF(pErrInfo, VERR_LDRLX_BAD_LOADER_SECTION,
                             "Object table is inside the header: %#x", Hdr.e32_objtab);
    off = Hdr.e32_objtab + sizeof(struct o32_obj) * Hdr.e32_objcnt;
    if (off > offEnd)
        return RTErrInfoSetF(pErrInfo, VERR_LDRLX_BAD_LOADER_SECTION,
                             "Object table spans beyond the executable: e32_objcnt=%u", Hdr.e32_objcnt);
    if (Hdr.e32_objcnt >= _32K)
        return RTErrInfoSetF(pErrInfo, VERR_LDRLX_BAD_LOADER_SECTION, "Too many segments: %#x\n", Hdr.e32_objcnt);
    if (    Hdr.e32_objmap
        &&  (Hdr.e32_objmap < off || Hdr.e32_objmap > offEnd))
        return RTErrInfoSetF(pErrInfo, VERR_LDRLX_BAD_LOADER_SECTION,
                             "Bad object page map table offset: %#x", Hdr.e32_objmap);
    if (    Hdr.e32_rsrccnt
        && (   Hdr.e32_rsrctab < off
            || Hdr.e32_rsrctab > offEnd
            || Hdr.e32_rsrctab + sizeof(struct rsrc32) * Hdr.e32_rsrccnt > offEnd))
        return RTErrInfoSetF(pErrInfo, VERR_LDRLX_BAD_LOADER_SECTION,
                             "Resource table is out of bounds: %#x entries at %#x", Hdr.e32_rsrccnt, Hdr.e32_rsrctab);
    if (    Hdr.e32_restab
        &&  (Hdr.e32_restab < off || Hdr.e32_restab > offEnd - 2))
        return VERR_LDRLX_BAD_LOADER_SECTION;
    if (    Hdr.e32_enttab
        &&  (Hdr.e32_enttab < off || Hdr.e32_enttab >= offEnd))
        return VERR_LDRLX_BAD_LOADER_SECTION;
    if (    Hdr.e32_dircnt
        && (Hdr.e32_dirtab < off || Hdr.e32_dirtab > offEnd - 2))
        return VERR_LDRLX_BAD_LOADER_SECTION;

    /* Verify the fixup section. */
    off = offEnd;
    offEnd = off + Hdr.e32_fixupsize;
    if (    Hdr.e32_fpagetab
        &&  (Hdr.e32_fpagetab < off || Hdr.e32_fpagetab > offEnd))
    {
        /*
         * wlink mixes the fixup section and the loader section.
         */
        off = Hdr.e32_fpagetab;
        offEnd = off + Hdr.e32_fixupsize;
        Hdr.e32_ldrsize = off - Hdr.e32_objtab;
    }
    if (    Hdr.e32_frectab
        &&  (Hdr.e32_frectab < off || Hdr.e32_frectab > offEnd))
        return VERR_LDRLX_BAD_FIXUP_SECTION;
    if (    Hdr.e32_impmod
        &&  (Hdr.e32_impmod < off || Hdr.e32_impmod > offEnd || Hdr.e32_impmod + Hdr.e32_impmodcnt > offEnd))
        return VERR_LDRLX_BAD_FIXUP_SECTION;
    if (    Hdr.e32_impproc
        &&  (Hdr.e32_impproc < off || Hdr.e32_impproc > offEnd))
        return VERR_LDRLX_BAD_FIXUP_SECTION;

    /*
     * Calc the instance size, allocate and initialize it.
     */
    size_t cbModLXAndSegments = RT_ALIGN_Z(RT_UOFFSETOF_DYN(KLDRMODLX, aSegments[Hdr.e32_objcnt + 1]), 8);
    cbModLXAndSegments += sizeof("segXXXXX") * (Hdr.e32_objcnt + 1);

    pModLX = (PKLDRMODLX)RTMemAlloc(cbModLXAndSegments + Hdr.e32_ldrsize + 2 /*for two extra zeros*/);
    if (!pModLX)
        return VERR_NO_MEMORY;
    *ppModLX = pModLX;

    /* Core & CPU. */
    pModLX->Core.u32Magic   = 0;      /* set by caller. */
    pModLX->Core.eState     = LDR_STATE_OPENED;
    pModLX->Core.pOps       = NULL;   /* set by caller. */
    pModLX->Core.pReader    = pRdr;
    switch (Hdr.e32_cpu)
    {
        case E32CPU286:
            pModLX->enmCpu = RTLDRCPU_I80286;
            pModLX->Core.enmArch = RTLDRARCH_X86_16;
            break;
        case E32CPU386:
            pModLX->enmCpu = RTLDRCPU_I386;
            pModLX->Core.enmArch = RTLDRARCH_X86_32;
            break;
        case E32CPU486:
            pModLX->enmCpu = RTLDRCPU_I486;
            pModLX->Core.enmArch = RTLDRARCH_X86_32;
            break;
    }
    pModLX->Core.enmEndian = RTLDRENDIAN_LITTLE;
    pModLX->Core.enmFormat = RTLDRFMT_LX;
    switch (Hdr.e32_mflags & E32MODMASK)
    {
        case E32MODEXE:
            pModLX->Core.enmType = !(Hdr.e32_mflags & E32NOINTFIX)
                                 ? RTLDRTYPE_EXECUTABLE_RELOCATABLE
                                 : RTLDRTYPE_EXECUTABLE_FIXED;
            break;

        case E32MODDLL:
        case E32PROTDLL:
        case E32MODPROTDLL:
            pModLX->Core.enmType = !(Hdr.e32_mflags & E32SYSDLL)
                                 ? RTLDRTYPE_SHARED_LIBRARY_RELOCATABLE
                                 : RTLDRTYPE_SHARED_LIBRARY_FIXED;
            break;

        case E32MODPDEV:
        case E32MODVDEV:
            pModLX->Core.enmType = RTLDRTYPE_SHARED_LIBRARY_RELOCATABLE;
            break;
    }

    /* KLDRMODLX */
    pModLX->cSegments = Hdr.e32_objcnt;
    pModLX->pszName = NULL; /* finalized further down */
    pModLX->cchName = 0;
    pModLX->pvMapping = 0;
    pModLX->cbMapped = 0;
    pModLX->f32Reserved = 0;

    pModLX->offHdr = offNewHdr >= 0 ? offNewHdr : 0;
    memcpy(&pModLX->Hdr, &Hdr, sizeof(Hdr));

    pModLX->pbLoaderSection = (uint8_t *)pModLX + cbModLXAndSegments;
    pModLX->pbLoaderSectionLast = pModLX->pbLoaderSection + pModLX->Hdr.e32_ldrsize - 1;
    pModLX->paObjs = NULL;
    pModLX->paPageMappings = NULL;
    pModLX->paRsrcs = NULL;
    pModLX->pbResNameTab = NULL;
    pModLX->pbEntryTab = NULL;

    pModLX->pbNonResNameTab = NULL;
    pModLX->pbNonResNameTabLast = NULL;

    pModLX->pbFixupSection = NULL;
    pModLX->pbFixupSectionLast = NULL;
    pModLX->paoffPageFixups = NULL;
    pModLX->pbFixupRecs = NULL;
    pModLX->pbImportMods = NULL;
    pModLX->pbImportProcs = NULL;

    /*
     * Read the loader data.
     */
    rc = pRdr->pfnRead(pRdr, (void *)pModLX->pbLoaderSection, pModLX->Hdr.e32_ldrsize, pModLX->Hdr.e32_objtab + pModLX->offHdr);
    if (RT_FAILURE(rc))
        return rc;
    ((uint8_t *)pModLX->pbLoaderSectionLast)[1] = 0;
    ((uint8_t *)pModLX->pbLoaderSectionLast)[2] = 0;
    if (pModLX->Hdr.e32_objcnt)
        pModLX->paObjs = (const struct o32_obj *)pModLX->pbLoaderSection;
    if (pModLX->Hdr.e32_objmap)
        pModLX->paPageMappings = (const struct o32_map *)(pModLX->pbLoaderSection + pModLX->Hdr.e32_objmap - pModLX->Hdr.e32_objtab);
    if (pModLX->Hdr.e32_rsrccnt)
        pModLX->paRsrcs = (const struct rsrc32 *)(pModLX->pbLoaderSection + pModLX->Hdr.e32_rsrctab - pModLX->Hdr.e32_objtab);
    if (pModLX->Hdr.e32_restab)
        pModLX->pbResNameTab = pModLX->pbLoaderSection + pModLX->Hdr.e32_restab - pModLX->Hdr.e32_objtab;
    if (pModLX->Hdr.e32_enttab)
        pModLX->pbEntryTab = pModLX->pbLoaderSection + pModLX->Hdr.e32_enttab - pModLX->Hdr.e32_objtab;

    /*
     * Get the soname from the resident name table.
     * Very convenient that it's the 0 ordinal, because then we get a
     * free string terminator.
     * (The table entry consists of a pascal string followed by a 16-bit ordinal.)
     */
    if (pModLX->pbResNameTab)
        pModLX->pszName = (const char *)kldrModLXDoNameTableLookupByOrdinal(pModLX->pbResNameTab,
                                                                            pModLX->pbLoaderSectionLast - pModLX->pbResNameTab + 1,
                                                                            0);
    if (!pModLX->pszName)
        return VERR_LDRLX_NO_SONAME;
    pModLX->cchName = *(const uint8_t *)pModLX->pszName++;
    if (   pModLX->pszName[pModLX->cchName] != '\0'
        || pModLX->cchName != strlen(pModLX->pszName))
        return VERR_LDRLX_BAD_SONAME;

    /*
     * Quick validation of the object table.
     */
    for (i = 0; i < pModLX->cSegments; i++)
    {
        if (pModLX->paObjs[i].o32_base & (OBJPAGELEN - 1))
            return VERR_LDRLX_BAD_OBJECT_TABLE;
        if (pModLX->paObjs[i].o32_base + pModLX->paObjs[i].o32_size <= pModLX->paObjs[i].o32_base)
            return VERR_LDRLX_BAD_OBJECT_TABLE;
        if (pModLX->paObjs[i].o32_mapsize > (pModLX->paObjs[i].o32_size + (OBJPAGELEN - 1)))
            return VERR_LDRLX_BAD_OBJECT_TABLE;
        if (    pModLX->paObjs[i].o32_mapsize
            &&  (   (uint8_t *)&pModLX->paPageMappings[pModLX->paObjs[i].o32_pagemap] > pModLX->pbLoaderSectionLast
                 || (uint8_t *)&pModLX->paPageMappings[pModLX->paObjs[i].o32_pagemap + pModLX->paObjs[i].o32_mapsize]
                     > pModLX->pbLoaderSectionLast))
            return VERR_LDRLX_BAD_OBJECT_TABLE;
        if (i > 0 && !(pModLX->paObjs[i].o32_flags & OBJRSRC))
        {
            if (pModLX->paObjs[i].o32_base <= pModLX->paObjs[i - 1].o32_base)
                return VERR_LDRLX_BAD_OBJECT_TABLE;
            if (pModLX->paObjs[i].o32_base < pModLX->paObjs[i - 1].o32_base + pModLX->paObjs[i - 1].o32_mapsize)
                return VERR_LDRLX_BAD_OBJECT_TABLE;
        }
    }

    /*
     * Check if we can optimize the mapping by using a different
     * object alignment. The linker typically uses 64KB alignment,
     * we can easily get away with page alignment in most cases.
     *
     * However, this screws up DwARF debug info, let's not do this
     * when the purpose is reading debug info.
     */
    /** @todo Add flag for enabling this optimization. */
    fCanOptimizeMapping = !(Hdr.e32_mflags & (E32NOINTFIX | E32SYSDLL))
                       && !(fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION));
    NextRVA = 0;

    /*
     * Setup the KLDRMOD segment array.
     */
    char *pszSegNm = (char *)&pModLX->aSegments[pModLX->cSegments];
    for (i = 0; i < pModLX->cSegments; i++)
    {
        /* dummy segment name */
        pModLX->aSegments[i].pszName    = pszSegNm;
        size_t cchName = RTStrPrintf(pszSegNm, sizeof("segXXXXX"), "seg%u", i);
        pszSegNm += cchName + 1;
        pModLX->aSegments[i].cchName    = (uint32_t)cchName;

        /* unused */
        pModLX->aSegments[i].offFile    = -1;
        pModLX->aSegments[i].cbFile     = -1;
        pModLX->aSegments[i].SelFlat    = 0;
        pModLX->aSegments[i].Sel16bit   = 0;

        /* flags */
        pModLX->aSegments[i].fFlags = 0;
        if (!(pModLX->paObjs[i].o32_flags & OBJBIGDEF))
            pModLX->aSegments[i].fFlags |= RTLDRSEG_FLAG_16BIT;
        if (pModLX->paObjs[i].o32_flags & OBJALIAS16)
            pModLX->aSegments[i].fFlags |= RTLDRSEG_FLAG_OS2_ALIAS16;
        if (pModLX->paObjs[i].o32_flags & OBJCONFORM)
            pModLX->aSegments[i].fFlags |= RTLDRSEG_FLAG_OS2_CONFORM;
        if (pModLX->paObjs[i].o32_flags & OBJIOPL)
            pModLX->aSegments[i].fFlags |= RTLDRSEG_FLAG_OS2_IOPL;

        /* size and addresses */
        pModLX->aSegments[i].Alignment   = OBJPAGELEN;
        pModLX->aSegments[i].cb          = pModLX->paObjs[i].o32_size;
        pModLX->aSegments[i].LinkAddress = pModLX->paObjs[i].o32_base;
        pModLX->aSegments[i].RVA         = NextRVA;
        if (    fCanOptimizeMapping
            ||  i + 1 >= pModLX->cSegments
            ||  (pModLX->paObjs[i].o32_flags & OBJRSRC)
            ||  (pModLX->paObjs[i + 1].o32_flags & OBJRSRC))
            pModLX->aSegments[i].cbMapped = RT_ALIGN_Z(pModLX->paObjs[i].o32_size, OBJPAGELEN);
        else
            pModLX->aSegments[i].cbMapped = pModLX->paObjs[i + 1].o32_base - pModLX->paObjs[i].o32_base;
        /** @todo Above probably doesn't work for os2krnl and other images
         *        non-sequential virtual address assignments. */
        NextRVA += (uint32_t)pModLX->aSegments[i].cbMapped;

        /* protection */
        switch (  pModLX->paObjs[i].o32_flags
                & (OBJSHARED | OBJREAD | OBJWRITE | OBJEXEC))
        {
            case 0:
            case OBJSHARED:
                pModLX->aSegments[i].fProt = 0;
                break;
            case OBJREAD:
            case OBJREAD | OBJSHARED:
                pModLX->aSegments[i].fProt = RTMEM_PROT_READ;
                break;
            case OBJWRITE:
            case OBJWRITE | OBJREAD:
                pModLX->aSegments[i].fProt = RTMEM_PROT_READ | RTMEM_PROT_WRITECOPY;
                break;
            case OBJWRITE | OBJSHARED:
            case OBJWRITE | OBJSHARED | OBJREAD:
                pModLX->aSegments[i].fProt = RTMEM_PROT_READ | RTMEM_PROT_WRITE;
                break;
            case OBJEXEC:
            case OBJEXEC | OBJSHARED:
                pModLX->aSegments[i].fProt = RTMEM_PROT_EXEC;
                break;
            case OBJEXEC | OBJREAD:
            case OBJEXEC | OBJREAD | OBJSHARED:
                pModLX->aSegments[i].fProt = RTMEM_PROT_EXEC | RTMEM_PROT_READ;
                break;
            case OBJEXEC | OBJWRITE:
            case OBJEXEC | OBJWRITE | OBJREAD:
                pModLX->aSegments[i].fProt = RTMEM_PROT_EXEC | RTMEM_PROT_READ | RTMEM_PROT_WRITECOPY;
                break;
            case OBJEXEC | OBJWRITE | OBJSHARED:
            case OBJEXEC | OBJWRITE | OBJSHARED | OBJREAD:
                pModLX->aSegments[i].fProt = RTMEM_PROT_EXEC | RTMEM_PROT_READ | RTMEM_PROT_WRITE;
                break;
        }
        if ((pModLX->paObjs[i].o32_flags & (OBJREAD | OBJWRITE | OBJEXEC | OBJRSRC)) == OBJRSRC)
            pModLX->aSegments[i].fProt = RTMEM_PROT_READ;
        /*pModLX->aSegments[i].f16bit = !(pModLX->paObjs[i].o32_flags & OBJBIGDEF)
        pModLX->aSegments[i].fIOPL = !(pModLX->paObjs[i].o32_flags & OBJIOPL)
        pModLX->aSegments[i].fConforming = !(pModLX->paObjs[i].o32_flags & OBJCONFORM) */
    }

    /* set the mapping size */
    pModLX->cbMapped = NextRVA;

    /*
     * We're done.
     */
    *ppModLX = pModLX;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnClose}
 */
static DECLCALLBACK(int) rtldrLX_Close(PRTLDRMODINTERNAL pMod)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    KLDRMODLX_ASSERT(!pModLX->pvMapping);

    if (pModLX->pbNonResNameTab)
    {
        RTMemFree(pModLX->pbNonResNameTab);
        pModLX->pbNonResNameTab = NULL;
    }
    if (pModLX->pbFixupSection)
    {
        RTMemFree(pModLX->pbFixupSection);
        pModLX->pbFixupSection = NULL;
    }
    return VINF_SUCCESS;
}


/**
 * Resolved base address aliases.
 *
 * @param   pModLX          The interpreter module instance
 * @param   pBaseAddress    The base address, IN & OUT.
 */
static void kldrModLXResolveBaseAddress(PKLDRMODLX pModLX, PRTLDRADDR pBaseAddress)
{
    if (*pBaseAddress == RTLDR_BASEADDRESS_LINK)
        *pBaseAddress = pModLX->aSegments[0].LinkAddress;
}


static int kldrModLXQuerySymbol(PRTLDRMODINTERNAL pMod, const void *pvBits, RTLDRADDR BaseAddress, uint32_t iSymbol,
                                const char *pchSymbol, size_t cchSymbol, const char *pszVersion,
                                PFNRTLDRIMPORT pfnGetForwarder, void *pvUser, PRTLDRADDR puValue, uint32_t *pfKind)
{
    PKLDRMODLX                  pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    uint32_t                    iOrdinal;
    int                         rc;
    const struct b32_bundle    *pBundle;
    RT_NOREF(pvBits);
    RT_NOREF(pszVersion);

    /*
     * Give up at once if there is no entry table.
     */
    if (!pModLX->Hdr.e32_enttab)
        return VERR_SYMBOL_NOT_FOUND;

    /*
     * Translate the symbol name into an ordinal.
     */
    if (pchSymbol)
    {
        rc = kldrModLXDoNameLookup(pModLX, pchSymbol, cchSymbol, &iSymbol);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Iterate the entry table.
     * (The entry table is made up of bundles of similar exports.)
     */
    iOrdinal = 1;
    pBundle = (const struct b32_bundle *)pModLX->pbEntryTab;
    while (pBundle->b32_cnt && iOrdinal <= iSymbol)
    {
        static const size_t s_cbEntry[] = { 0, 3, 5, 5, 7 };

        /*
         * Check for a hit first.
         */
        iOrdinal += pBundle->b32_cnt;
        if (iSymbol < iOrdinal)
        {
            uint32_t offObject;
            const struct e32_entry *pEntry = (const struct e32_entry *)((uintptr_t)(pBundle + 1)
                                                                        +   (iSymbol - (iOrdinal - pBundle->b32_cnt))
                                                                          * s_cbEntry[pBundle->b32_type]);

            /*
             * Calculate the return address.
             */
            kldrModLXResolveBaseAddress(pModLX, &BaseAddress);
            switch (pBundle->b32_type)
            {
                /* empty bundles are place holders unused ordinal ranges. */
                case EMPTY:
                    return VERR_SYMBOL_NOT_FOUND;

                /* e32_flags + a 16-bit offset. */
                case ENTRY16:
                    offObject = pEntry->e32_variant.e32_offset.offset16;
                    if (pfKind)
                        *pfKind = RTLDRSYMKIND_16BIT | RTLDRSYMKIND_NO_TYPE;
                    break;

                /* e32_flags + a 16-bit offset + a 16-bit callgate selector. */
                case GATE16:
                    offObject = pEntry->e32_variant.e32_callgate.offset;
                    if (pfKind)
                        *pfKind = RTLDRSYMKIND_16BIT | RTLDRSYMKIND_CODE;
                    break;

                /* e32_flags + a 32-bit offset. */
                case ENTRY32:
                    offObject = pEntry->e32_variant.e32_offset.offset32;
                    if (pfKind)
                        *pfKind = RTLDRSYMKIND_32BIT;
                    break;

                /* e32_flags + 16-bit import module ordinal + a 32-bit procname or ordinal. */
                case ENTRYFWD:
                    return kldrModLXDoForwarderQuery(pModLX, pEntry, pfnGetForwarder, pvUser, puValue, pfKind);

                default:
                    /* anyone actually using TYPEINFO will end up here. */
                    KLDRMODLX_ASSERT(!"Bad bundle type");
                    return VERR_LDRLX_BAD_BUNDLE;
            }

            /*
             * Validate the object number and calc the return address.
             */
            if (    pBundle->b32_obj <= 0
                ||  pBundle->b32_obj > pModLX->cSegments)
                return VERR_LDRLX_BAD_BUNDLE;
            if (puValue)
                *puValue = BaseAddress
                         + offObject
                         + pModLX->aSegments[pBundle->b32_obj - 1].RVA;
            return VINF_SUCCESS;
        }

        /*
         * Skip the bundle.
         */
        if (pBundle->b32_type > ENTRYFWD)
        {
            KLDRMODLX_ASSERT(!"Bad type"); /** @todo figure out TYPEINFO. */
            return VERR_LDRLX_BAD_BUNDLE;
        }
        if (pBundle->b32_type == 0)
            pBundle = (const struct b32_bundle *)((const uint8_t *)pBundle + 2);
        else
            pBundle = (const struct b32_bundle *)((const uint8_t *)(pBundle + 1) + s_cbEntry[pBundle->b32_type] * pBundle->b32_cnt);
    }

    return VERR_SYMBOL_NOT_FOUND;
}


/**
 * @interface_method_impl{RTLDROPS,pfnGetSymbolEx}
 */
static DECLCALLBACK(int) rtldrLX_GetSymbolEx(PRTLDRMODINTERNAL pMod, const void *pvBits, RTUINTPTR BaseAddress,
                                             uint32_t iOrdinal, const char *pszSymbol, RTUINTPTR *pValue)
{
    uint32_t fKind = RTLDRSYMKIND_REQ_FLAT;
    return kldrModLXQuerySymbol(pMod, pvBits, BaseAddress, iOrdinal, pszSymbol, pszSymbol ? strlen(pszSymbol) : 0,
                                NULL, NULL, NULL, pValue, &fKind);
}


/**
 * Do name lookup.
 *
 * @returns IPRT status code.
 * @param   pModLX      The module to lookup the symbol in.
 * @param   pchSymbol   The symbol to lookup.
 * @param   cchSymbol   The symbol name length.
 * @param   piSymbol    Where to store the symbol ordinal.
 */
static int kldrModLXDoNameLookup(PKLDRMODLX pModLX, const char *pchSymbol, size_t cchSymbol, uint32_t *piSymbol)
{

    /*
     * First do a hash table lookup.
     */
    /** @todo hash name table for speed. */

    /*
     * Search the name tables.
     */
    const uint8_t *pbName = kldrModLXDoNameTableLookupByName(pModLX->pbResNameTab,
                                                         pModLX->pbLoaderSectionLast - pModLX->pbResNameTab + 1,
                                                         pchSymbol, cchSymbol);
    if (!pbName)
    {
        if (!pModLX->pbNonResNameTab)
        {
            /* lazy load it */
            /** @todo non-resident name table. */
        }
        if (pModLX->pbNonResNameTab)
            pbName = kldrModLXDoNameTableLookupByName(pModLX->pbResNameTab,
                                                      pModLX->pbNonResNameTabLast - pModLX->pbResNameTab + 1,
                                                      pchSymbol, cchSymbol);
    }
    if (!pbName)
        return VERR_SYMBOL_NOT_FOUND;

    *piSymbol = *(const uint16_t *)(pbName + 1 + *pbName);
    return VINF_SUCCESS;
}


/**
 * Lookup a name table entry by name.
 *
 * @returns Pointer to the name table entry if found.
 * @returns NULL if not found.
 * @param   pbNameTable     Pointer to the name table that should be searched.
 * @param   cbNameTable     The size of the name table.
 * @param   pchSymbol       The name of the symbol we're looking for.
 * @param   cchSymbol       The length of the symbol name.
 */
static const uint8_t *kldrModLXDoNameTableLookupByName(const uint8_t *pbNameTable, ssize_t cbNameTable,
                                                       const char *pchSymbol, size_t cchSymbol)
{
    /*
     * Determin the namelength up front so we can skip anything which doesn't matches the length.
     */
    uint8_t cbSymbol8Bit = (uint8_t)cchSymbol;
    if (cbSymbol8Bit != cchSymbol)
        return NULL; /* too long. */

    /*
     * Walk the name table.
     */
    while (*pbNameTable != 0 && cbNameTable > 0)
    {
        const uint8_t cbName = *pbNameTable;

        cbNameTable -= cbName + 1 + 2;
        if (cbNameTable < 0)
            break;

        if (    cbName == cbSymbol8Bit
            &&  !memcmp(pbNameTable + 1, pchSymbol, cbName))
            return pbNameTable;

        /* next entry */
        pbNameTable += cbName + 1 + 2;
    }

    return NULL;
}


/**
 * Deal with a forwarder entry.
 *
 * @returns IPRT status code.
 * @param   pModLX          The PE module interpreter instance.
 * @param   pEntry          The forwarder entry.
 * @param   pfnGetForwarder The callback for resolving forwarder symbols. (optional)
 * @param   pvUser          The user argument for the callback.
 * @param   puValue         Where to put the value. (optional)
 * @param   pfKind          Where to put the symbol kind. (optional)
 */
static int kldrModLXDoForwarderQuery(PKLDRMODLX pModLX, const struct e32_entry *pEntry,
                                     PFNRTLDRIMPORT pfnGetForwarder, void *pvUser, PRTLDRADDR puValue, uint32_t *pfKind)
{
    if (!pfnGetForwarder)
        return VERR_LDR_FORWARDER;

    /*
     * Validate the entry import module ordinal.
     */
    if (    !pEntry->e32_variant.e32_fwd.modord
        ||  pEntry->e32_variant.e32_fwd.modord > pModLX->Hdr.e32_impmodcnt)
        return VERR_LDRLX_BAD_FORWARDER;

    char szImpModule[256];
    int rc = kldrModLXGetImport(pModLX, NULL, pEntry->e32_variant.e32_fwd.modord - 1, szImpModule, sizeof(szImpModule), NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Figure out the parameters.
     */
    uint32_t    iSymbol;
    const char *pszSymbol;
    char        szSymbol[256];
    if (pEntry->e32_flags & FWD_ORDINAL)
    {
        iSymbol = pEntry->e32_variant.e32_fwd.value;
        pszSymbol = NULL;                   /* no symbol name. */
    }
    else
    {
        const uint8_t *pbName;

        /* load the fixup section if necessary. */
        if (!pModLX->pbImportProcs)
        {
            rc = kldrModLXDoLoadFixupSection(pModLX);
            if (RT_FAILURE(rc))
                return rc;
        }

        /* Make name pointer. */
        pbName = pModLX->pbImportProcs + pEntry->e32_variant.e32_fwd.value;
        if (    pbName >= pModLX->pbFixupSectionLast
            ||  pbName < pModLX->pbFixupSection
            || !*pbName)
            return VERR_LDRLX_BAD_FORWARDER;


        /* check for '#' name. */
        if (pbName[1] == '#')
        {
            uint8_t         cbLeft = *pbName;
            const uint8_t  *pb = pbName + 1;
            unsigned    uBase;

            /* base detection */
            uBase = 10;
            if (    cbLeft > 1
                &&  pb[1] == '0'
                &&  (pb[2] == 'x' || pb[2] == 'X'))
            {
                uBase = 16;
                pb += 2;
                cbLeft -= 2;
            }

            /* ascii to integer */
            iSymbol = 0;
            while (cbLeft-- > 0)
            {
                /* convert char to digit. */
                unsigned uDigit = *pb++;
                if (uDigit >= '0' && uDigit <= '9')
                    uDigit -= '0';
                else if (uDigit >= 'a' && uDigit <= 'z')
                    uDigit -= 'a' + 10;
                else if (uDigit >= 'A' && uDigit <= 'Z')
                    uDigit -= 'A' + 10;
                else if (!uDigit)
                    break;
                else
                    return VERR_LDRLX_BAD_FORWARDER;
                if (uDigit >= uBase)
                    return VERR_LDRLX_BAD_FORWARDER;

                /* insert the digit */
                iSymbol *= uBase;
                iSymbol += uDigit;
            }
            if (!iSymbol)
                return VERR_LDRLX_BAD_FORWARDER;

            pszSymbol = NULL;               /* no symbol name. */
        }
        else
        {
            memcpy(szSymbol, pbName + 1, *pbName);
            szSymbol[*pbName] = '\0';
            pszSymbol = szSymbol;
            iSymbol = UINT32_MAX;
        }
    }

    /*
     * Resolve the forwarder.
     */
    rc = pfnGetForwarder(&pModLX->Core, szImpModule, pszSymbol, iSymbol, puValue, /*pfKind, */pvUser);
    if (RT_SUCCESS(rc) && pfKind)
        *pfKind |= RTLDRSYMKIND_FORWARDER;
    return rc;
}


/**
 * Loads the fixup section from the executable image.
 *
 * The fixup section isn't loaded until it's accessed. It's also freed by kLdrModDone().
 *
 * @returns IPRT status code.
 * @param   pModLX          The PE module interpreter instance.
 */
static int kldrModLXDoLoadFixupSection(PKLDRMODLX pModLX)
{
    void *pv = RTMemAlloc(pModLX->Hdr.e32_fixupsize);
    if (!pv)
        return VERR_NO_MEMORY;

    uint32_t off = pModLX->Hdr.e32_objtab + pModLX->Hdr.e32_ldrsize;
    int rc = pModLX->Core.pReader->pfnRead(pModLX->Core.pReader, pv, pModLX->Hdr.e32_fixupsize,
                                           off + pModLX->offHdr);
    if (RT_SUCCESS(rc))
    {
        pModLX->pbFixupSection = (uint8_t *)pv;
        pModLX->pbFixupSectionLast = pModLX->pbFixupSection + pModLX->Hdr.e32_fixupsize;
        KLDRMODLX_ASSERT(!pModLX->paoffPageFixups);
        if (pModLX->Hdr.e32_fpagetab)
            pModLX->paoffPageFixups = (const uint32_t *)(pModLX->pbFixupSection + pModLX->Hdr.e32_fpagetab - off);
        KLDRMODLX_ASSERT(!pModLX->pbFixupRecs);
        if (pModLX->Hdr.e32_frectab)
            pModLX->pbFixupRecs = pModLX->pbFixupSection + pModLX->Hdr.e32_frectab - off;
        KLDRMODLX_ASSERT(!pModLX->pbImportMods);
        if (pModLX->Hdr.e32_impmod)
            pModLX->pbImportMods = pModLX->pbFixupSection + pModLX->Hdr.e32_impmod - off;
        KLDRMODLX_ASSERT(!pModLX->pbImportProcs);
        if (pModLX->Hdr.e32_impproc)
            pModLX->pbImportProcs = pModLX->pbFixupSection + pModLX->Hdr.e32_impproc - off;
    }
    else
        RTMemFree(pv);
    return rc;
}


/**
 * @interface_method_impl{RTLDROPS,pfnEnumSymbols}
 */
static DECLCALLBACK(int) rtldrLX_EnumSymbols(PRTLDRMODINTERNAL pMod, unsigned fFlags, const void *pvBits,
                                             RTUINTPTR BaseAddress, PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    RT_NOREF(pvBits);
    RT_NOREF(fFlags);

    kldrModLXResolveBaseAddress(pModLX, &BaseAddress);

    /*
     * Enumerate the entry table.
     * (The entry table is made up of bundles of similar exports.)
     */
    int                      rc       = VINF_SUCCESS;
    uint32_t                 iOrdinal = 1;
    const struct b32_bundle *pBundle  = (const struct b32_bundle *)pModLX->pbEntryTab;
    while (pBundle->b32_cnt && iOrdinal)
    {
        static const size_t s_cbEntry[] = { 0, 3, 5, 5, 7 };

        /*
         * Enum the entries in the bundle.
         */
        if (pBundle->b32_type != EMPTY)
        {
            const struct e32_entry *pEntry;
            size_t cbEntry;
            RTLDRADDR BundleRVA;
            unsigned cLeft;


            /* Validate the bundle. */
            switch (pBundle->b32_type)
            {
                case ENTRY16:
                case GATE16:
                case ENTRY32:
                    if (    pBundle->b32_obj <= 0
                        ||  pBundle->b32_obj > pModLX->cSegments)
                        return VERR_LDRLX_BAD_BUNDLE;
                    BundleRVA = pModLX->aSegments[pBundle->b32_obj - 1].RVA;
                    break;

                case ENTRYFWD:
                    BundleRVA = 0;
                    break;

                default:
                    /* anyone actually using TYPEINFO will end up here. */
                    KLDRMODLX_ASSERT(!"Bad bundle type");
                    return VERR_LDRLX_BAD_BUNDLE;
            }

            /* iterate the bundle entries. */
            cbEntry = s_cbEntry[pBundle->b32_type];
            pEntry = (const struct e32_entry *)(pBundle + 1);
            cLeft = pBundle->b32_cnt;
            while (cLeft-- > 0)
            {
                RTLDRADDR uValue;
                uint32_t fKind;
                int fFoundName;
                const uint8_t *pbName;

                /*
                 * Calc the symbol value and kind.
                 */
                switch (pBundle->b32_type)
                {
                    /* e32_flags + a 16-bit offset. */
                    case ENTRY16:
                        uValue = BaseAddress + BundleRVA + pEntry->e32_variant.e32_offset.offset16;
                        fKind = RTLDRSYMKIND_16BIT | RTLDRSYMKIND_NO_TYPE;
                        break;

                    /* e32_flags + a 16-bit offset + a 16-bit callgate selector. */
                    case GATE16:
                        uValue = BaseAddress + BundleRVA + pEntry->e32_variant.e32_callgate.offset;
                        fKind = RTLDRSYMKIND_16BIT | RTLDRSYMKIND_CODE;
                        break;

                    /* e32_flags + a 32-bit offset. */
                    case ENTRY32:
                        uValue = BaseAddress + BundleRVA + pEntry->e32_variant.e32_offset.offset32;
                        fKind = RTLDRSYMKIND_32BIT;
                        break;

                    /* e32_flags + 16-bit import module ordinal + a 32-bit procname or ordinal. */
                    case ENTRYFWD:
                        uValue = 0; /** @todo implement enumeration of forwarders properly. */
                        fKind = RTLDRSYMKIND_FORWARDER;
                        break;

                    default: /* shut up gcc. */
                        uValue = 0;
                        fKind = RTLDRSYMKIND_NO_BIT | RTLDRSYMKIND_NO_TYPE;
                        break;
                }

                /*
                 * Any symbol names?
                 */
                fFoundName = 0;
                char szName[256];

                /* resident name table. */
                pbName = pModLX->pbResNameTab;
                if (pbName)
                {
                    do
                    {
                        pbName = kldrModLXDoNameTableLookupByOrdinal(pbName, pModLX->pbLoaderSectionLast - pbName + 1, iOrdinal);
                        if (!pbName)
                            break;
                        fFoundName = 1;
                        memcpy(szName, (const char *)pbName + 1, *pbName);
                        szName[*pbName] = '\0';
                        rc = pfnCallback(pMod, szName, iOrdinal, uValue, /*fKind,*/ pvUser);
                        if (rc != VINF_SUCCESS)
                            return rc;

                        /* skip to the next entry */
                        pbName += 1 + *pbName + 2;
                    } while (pbName < pModLX->pbLoaderSectionLast);
                }

                /* resident name table. */
                pbName = pModLX->pbNonResNameTab;
                /** @todo lazy load the non-resident name table. */
                if (pbName)
                {
                    do
                    {
                        pbName = kldrModLXDoNameTableLookupByOrdinal(pbName, pModLX->pbNonResNameTabLast - pbName + 1, iOrdinal);
                        if (!pbName)
                            break;
                        fFoundName = 1;
                        memcpy(szName, (const char *)pbName + 1, *pbName);
                        szName[*pbName] = '\0';
                        rc = pfnCallback(pMod, szName, iOrdinal, uValue, /*fKind,*/ pvUser);
                        if (rc != VINF_SUCCESS)
                            return rc;

                        /* skip to the next entry */
                        pbName += 1 + *pbName + 2;
                    } while (pbName < pModLX->pbLoaderSectionLast);
                }

                /*
                 * If no names, call once with the ordinal only.
                 */
                if (!fFoundName)
                {
                    RT_NOREF(fKind);
                    rc = pfnCallback(pMod, NULL /*pszName*/, iOrdinal, uValue, /*fKind,*/ pvUser);
                    if (rc != VINF_SUCCESS)
                        return rc;
                }

                /* next */
                iOrdinal++;
                pEntry = (const struct e32_entry *)((uintptr_t)pEntry + cbEntry);
            }
        }

        /*
         * The next bundle.
         */
        if (pBundle->b32_type > ENTRYFWD)
        {
            KLDRMODLX_ASSERT(!"Bad type"); /** @todo figure out TYPEINFO. */
            return VERR_LDRLX_BAD_BUNDLE;
        }
        if (pBundle->b32_type == 0)
            pBundle = (const struct b32_bundle *)((const uint8_t *)pBundle + 2);
        else
            pBundle = (const struct b32_bundle *)((const uint8_t *)(pBundle + 1) + s_cbEntry[pBundle->b32_type] * pBundle->b32_cnt);
    }

    return VINF_SUCCESS;
}


/**
 * Lookup a name table entry by ordinal.
 *
 * @returns Pointer to the name table entry if found.
 * @returns NULL if not found.
 * @param   pbNameTable Pointer to the name table that should be searched.
 * @param   cbNameTable The size of the name table.
 * @param   iOrdinal    The ordinal to search for.
 */
static const uint8_t *kldrModLXDoNameTableLookupByOrdinal(const uint8_t *pbNameTable, ssize_t cbNameTable, uint32_t iOrdinal)
{
    while (*pbNameTable != 0 && cbNameTable > 0)
    {
        const uint8_t   cbName = *pbNameTable;
        uint32_t        iName;

        cbNameTable -= cbName + 1 + 2;
        if (cbNameTable < 0)
            break;

        iName = *(pbNameTable + cbName + 1)
              | ((unsigned)*(pbNameTable + cbName + 2) << 8);
        if (iName == iOrdinal)
            return pbNameTable;

        /* next entry */
        pbNameTable += cbName + 1 + 2;
    }

    return NULL;
}


static int kldrModLXGetImport(PKLDRMODLX pModLX, const void *pvBits, uint32_t iImport, char *pszName, size_t cchName,
                              size_t *pcbNeeded)
{
    const uint8_t *pb;
    int            rc;
    RT_NOREF(pvBits);

    /*
     * Validate
     */
    if (iImport >= pModLX->Hdr.e32_impmodcnt)
        return VERR_LDRLX_IMPORT_ORDINAL_OUT_OF_BOUNDS;

    /*
     * Lazy loading the fixup section.
     */
    if (!pModLX->pbImportMods)
    {
        rc = kldrModLXDoLoadFixupSection(pModLX);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Iterate the module import table until we reach the requested import ordinal.
     */
    pb = pModLX->pbImportMods;
    while (iImport-- > 0)
        pb += *pb + 1;

    /*
     * Copy out the result.
     */
    if (pcbNeeded)
        *pcbNeeded = *pb + 1;
    if (*pb < cchName)
    {
        memcpy(pszName, pb + 1, *pb);
        pszName[*pb] = '\0';
        rc = VINF_SUCCESS;
    }
    else
    {
        memcpy(pszName, pb + 1, cchName);
        if (cchName)
            pszName[cchName - 1] = '\0';
        rc = VERR_BUFFER_OVERFLOW;
    }

    return rc;
}

#if 0

/** @copydoc kLdrModNumberOfImports */
static int32_t kldrModLXNumberOfImports(PRTLDRMODINTERNAL pMod, const void *pvBits)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    RT_NOREF(pvBits);
    return pModLX->Hdr.e32_impmodcnt;
}


/** @copydoc kLdrModGetStackInfo */
static int kldrModLXGetStackInfo(PRTLDRMODINTERNAL pMod, const void *pvBits, RTLDRADDR BaseAddress, PKLDRSTACKINFO pStackInfo)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    const uint32_t i = pModLX->Hdr.e32_stackobj;
    RT_NOREF(pvBits);

    if (    i
        &&  i <= pModLX->cSegments
        &&  pModLX->Hdr.e32_esp <= pModLX->aSegments[i - 1].LinkAddress + pModLX->aSegments[i - 1].cb
        &&  pModLX->Hdr.e32_stacksize
        &&  pModLX->Hdr.e32_esp - pModLX->Hdr.e32_stacksize >= pModLX->aSegments[i - 1].LinkAddress)
    {

        kldrModLXResolveBaseAddress(pModLX, &BaseAddress);
        pStackInfo->LinkAddress = pModLX->Hdr.e32_esp - pModLX->Hdr.e32_stacksize;
        pStackInfo->Address = BaseAddress
                            + pModLX->aSegments[i - 1].RVA
                            + pModLX->Hdr.e32_esp - pModLX->Hdr.e32_stacksize - pModLX->aSegments[i - 1].LinkAddress;
    }
    else
    {
        pSt0ackInfo->Address = NIL_RTLDRADDR;
        pStackInfo->LinkAddress = NIL_RTLDRADDR;
    }
    pStackInfo->cbStack = pModLX->Hdr.e32_stacksize;
    pStackInfo->cbStackThread = 0;

    return VINF_SUCCESS;
}


/** @copydoc kLdrModQueryMainEntrypoint */
static int kldrModLXQueryMainEntrypoint(PRTLDRMODINTERNAL pMod, const void *pvBits, RTLDRADDR BaseAddress, PRTLDRADDR pMainEPAddress)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    RT_NOREF(pvBits);

    /*
     * Convert the address from the header.
     */
    kldrModLXResolveBaseAddress(pModLX, &BaseAddress);
    *pMainEPAddress = pModLX->Hdr.e32_startobj
                   && pModLX->Hdr.e32_startobj <= pModLX->cSegments
                   && pModLX->Hdr.e32_eip < pModLX->aSegments[pModLX->Hdr.e32_startobj - 1].cb
        ? BaseAddress + pModLX->aSegments[pModLX->Hdr.e32_startobj - 1].RVA + pModLX->Hdr.e32_eip
        : NIL_RTLDRADDR;
    return VINF_SUCCESS;
}

#endif

/** Helper for rtldrLX_EnumDbgInfo. */
static int rtldrLx_EnumDbgInfoHelper(PKLDRMODLX pModLX, PFNRTLDRENUMDBG pfnCallback, void *pvUser,
                                     uint8_t *pbBuf, uint32_t cbRead, uint32_t offDbgInfo, bool *pfReturn)
{
    RTLDRDBGINFO DbgInfo;
    uint32_t     iDbgInfo = 0;
    uint32_t     cbDbgInfo = pModLX->Hdr.e32_debuglen;

    /*
     * Recent watcom linkers emit PE style IMAGE_DEBUG_MISC for specifying
     * external file with CV info.
     */
    if (cbRead >= sizeof(IMAGE_DEBUG_MISC))
    {
        PCIMAGE_DEBUG_MISC pMisc = (PCIMAGE_DEBUG_MISC)pbBuf;
        if (   pMisc->DataType    == IMAGE_DEBUG_MISC_EXENAME
            && pMisc->Length      <= cbRead
            && pMisc->Length      >= RT_UOFFSETOF(IMAGE_DEBUG_MISC, Data[4])
            && pMisc->Unicode     == 0
            && pMisc->Reserved[0] == 0
            && pMisc->Reserved[1] == 0
            && pMisc->Reserved[2] == 0
            && pMisc->Data[0]     >= 0x20
            && pMisc->Data[0]     <  0x7f
            && pMisc->Data[1]     >= 0x20
            && pMisc->Data[1]     <  0x7f
            && pMisc->Data[2]     >= 0x20
            && pMisc->Data[2]     <  0x7f )
        {
            uint32_t cchMaxName = pMisc->Length - RT_UOFFSETOF(IMAGE_DEBUG_MISC, Data[0]);
            for (uint32_t cchName = 3; cchName < cchMaxName; cchName++)
            {
                char const ch = pMisc->Data[cchName];
                if (ch == 0)
                {
                    DbgInfo.enmType         = RTLDRDBGINFOTYPE_CODEVIEW;
                    DbgInfo.iDbgInfo        = iDbgInfo;
                    DbgInfo.offFile         = offDbgInfo;
                    DbgInfo.LinkAddress     = NIL_RTLDRADDR;
                    DbgInfo.cb              = pMisc->Length;
                    DbgInfo.pszExtFile      = (char *)&pMisc->Data[0];
                    DbgInfo.u.Cv.cbImage    = pModLX->Hdr.e32_mpages * pModLX->Hdr.e32_pagesize;
                    DbgInfo.u.Cv.uTimestamp = 0;
                    DbgInfo.u.Cv.uMajorVer  = 0;
                    DbgInfo.u.Cv.uMinorVer  = 0;

                    *pfReturn = true;
                    int rc = pfnCallback(&pModLX->Core, &DbgInfo, pvUser);
                    if (rc != VINF_SUCCESS)
                        return rc;
                }
                else if (ch >= 0x30 && ch < 0x7f)
                    continue;
                break;
            }

            /* Skip it. */
            pbBuf      += pMisc->Length;
            cbRead     -= pMisc->Length;
            offDbgInfo += pMisc->Length;
            cbDbgInfo  -= pMisc->Length;
            iDbgInfo++;
        }
    }

    /*
     * Look for codeview signature.
     */
    if (cbRead > sizeof(RTCVHDR))
    {
        RTCVHDR const *pCvHdr = (RTCVHDR const *)pbBuf;
        if (   pCvHdr->off >= sizeof(*pCvHdr)
            && pCvHdr->off < cbDbgInfo)
        {
            switch (pCvHdr->u32Magic)
            {
                case RTCVHDR_MAGIC_NB11:
                case RTCVHDR_MAGIC_NB09:
                case RTCVHDR_MAGIC_NB08:
                case RTCVHDR_MAGIC_NB07:
                case RTCVHDR_MAGIC_NB06:
                case RTCVHDR_MAGIC_NB05:
                case RTCVHDR_MAGIC_NB04:
                case RTCVHDR_MAGIC_NB02:
                case RTCVHDR_MAGIC_NB01:
                case RTCVHDR_MAGIC_NB00:
                    DbgInfo.enmType         = RTLDRDBGINFOTYPE_CODEVIEW;
                    DbgInfo.iDbgInfo        = iDbgInfo;
                    DbgInfo.offFile         = offDbgInfo;
                    DbgInfo.LinkAddress     = NIL_RTLDRADDR;
                    DbgInfo.cb              = cbDbgInfo;
                    DbgInfo.pszExtFile      = NULL;
                    DbgInfo.u.Cv.cbImage    = pModLX->Hdr.e32_mpages * pModLX->Hdr.e32_pagesize;
                    DbgInfo.u.Cv.uTimestamp = 0;
                    DbgInfo.u.Cv.uMajorVer  = 0;
                    DbgInfo.u.Cv.uMinorVer  = 0;

                    *pfReturn = true;
                    return pfnCallback(&pModLX->Core, &DbgInfo, pvUser);
            }
        }
    }

    /*
     * Watcom wraps its DWARF output in an ELF image, so look for and ELF magic.
     */
    if (cbRead >= sizeof(Elf32_Ehdr))
    {
        Elf32_Ehdr const *pElfHdr = (Elf32_Ehdr const *)pbBuf;
        if (   pElfHdr->e_ident[EI_MAG0]    == ELFMAG0
            && pElfHdr->e_ident[EI_MAG1]    == ELFMAG1
            && pElfHdr->e_ident[EI_MAG2]    == ELFMAG2
            && pElfHdr->e_ident[EI_MAG3]    == ELFMAG3
            && pElfHdr->e_ident[EI_CLASS]   == ELFCLASS32
            && pElfHdr->e_ident[EI_DATA]    == ELFDATA2LSB
            && pElfHdr->e_ident[EI_VERSION] == EV_CURRENT
            && pElfHdr->e_shentsize         == sizeof(Elf32_Shdr)
            && pElfHdr->e_shnum             >= 2
            && pElfHdr->e_shnum             <  _32K + 10
            && pElfHdr->e_shstrndx          <= pElfHdr->e_shnum
            && pElfHdr->e_shstrndx          >  0)
        {
            /** @todo try use pBuf for reading into and try to read more at once. */
            uint32_t const offShdrs = pElfHdr->e_shoff + offDbgInfo;
            uint32_t const cShdrs   = pElfHdr->e_shnum;
            uint32_t const cbShdr   = pElfHdr->e_shentsize;
            int            rc       = VINF_SUCCESS;

            /* Read the section string table. */
            Elf32_Shdr Shdr;
            int rc2 = pModLX->Core.pReader->pfnRead(pModLX->Core.pReader, &Shdr, sizeof(Shdr),
                                                    offShdrs + pElfHdr->e_shstrndx * cbShdr);
            if (   RT_SUCCESS(rc2)
                && Shdr.sh_offset > 0
                && Shdr.sh_size > 0
                && Shdr.sh_size < _256K
                && Shdr.sh_type == SHT_STRTAB)
            {
                uint32_t const cbStrTab = Shdr.sh_size;
                char * const   pszStrTab = (char *)RTMemTmpAlloc(cbStrTab + 2);
                if (pszStrTab)
                {
                    rc2 = pModLX->Core.pReader->pfnRead(pModLX->Core.pReader, pszStrTab, Shdr.sh_size,
                                                        offDbgInfo + Shdr.sh_offset);
                    if (RT_SUCCESS(rc2))
                    {
                        pszStrTab[cbStrTab] = '\0';

                        /* Iterate the sections, one by one. */
                        for (uint32_t i = 1; i < cShdrs; i++)
                        {
                            rc = pModLX->Core.pReader->pfnRead(pModLX->Core.pReader, &Shdr, sizeof(Shdr), offShdrs + i * cbShdr);
                            if (   RT_SUCCESS(rc)
                                && Shdr.sh_name < cbStrTab
                                && strncmp(&pszStrTab[Shdr.sh_name], RT_STR_TUPLE(".debug_")) == 0)
                            {
                                DbgInfo.enmType            = RTLDRDBGINFOTYPE_DWARF;
                                DbgInfo.iDbgInfo           = iDbgInfo;
                                DbgInfo.offFile            = offDbgInfo + Shdr.sh_offset;
                                DbgInfo.LinkAddress        = NIL_RTLDRADDR;
                                DbgInfo.cb                 = Shdr.sh_size;
                                DbgInfo.pszExtFile         = NULL;
                                DbgInfo.u.Dwarf.pszSection = &pszStrTab[Shdr.sh_name];

                                *pfReturn = true;
                                rc = pfnCallback(&pModLX->Core, &DbgInfo, pvUser);
                                if (rc != VINF_SUCCESS)
                                    break;
                                iDbgInfo++;
                            }
                        }
                    }
                    RTMemTmpFree(pszStrTab);
                }
            }
            return rc;
        }
    }

    /*
     * Watcom debug info? Don't know how to detect it...
     */

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnEnumDbgInfo}
 */
static DECLCALLBACK(int) rtldrLX_EnumDbgInfo(PRTLDRMODINTERNAL pMod, const void *pvBits,
                                             PFNRTLDRENUMDBG pfnCallback, void *pvUser)
{
    /*PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);*/
    RT_NOREF(pfnCallback);
    RT_NOREF(pvUser);

    /*
     * Quit immediately if no debug info.
     */
    if (kldrModLXHasDbgInfo(pMod, pvBits))
        return VINF_SUCCESS;
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);

    /*
     * Read the debug info and look for familiar magics and structures.
     */
    union
    {
        uint8_t             ab[1024];
        IMAGE_DEBUG_MISC    Misc;
        RTCVHDR             CvHdr;
    } uBuf;

    bool fReturn = false;

    /* Try the offset without header displacement first. */
    uint32_t cbToRead = RT_MIN(pModLX->Hdr.e32_debuglen, sizeof(uBuf));
    int rc = pModLX->Core.pReader->pfnRead(pModLX->Core.pReader, &uBuf, cbToRead, pModLX->Hdr.e32_debuginfo);
    if (RT_SUCCESS(rc))
        rc = rtldrLx_EnumDbgInfoHelper(pModLX, pfnCallback, pvUser, &uBuf.ab[0], cbToRead, pModLX->Hdr.e32_debuginfo, &fReturn);

    /* If that didn't yield anything, try displaying it by the header offset. */
    if (!fReturn && pModLX->offHdr > 0)
    {
        rc = pModLX->Core.pReader->pfnRead(pModLX->Core.pReader, &uBuf, cbToRead, pModLX->Hdr.e32_debuginfo + pModLX->offHdr);
        if (RT_SUCCESS(rc))
            rc = rtldrLx_EnumDbgInfoHelper(pModLX, pfnCallback, pvUser, &uBuf.ab[0], cbToRead,
                                           pModLX->Hdr.e32_debuginfo + pModLX->offHdr, &fReturn);
    }
    return rc;
}


static int kldrModLXHasDbgInfo(PRTLDRMODINTERNAL pMod, const void *pvBits)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    RT_NOREF(pvBits);

    /*
     * Don't currently bother with linkers which doesn't advertise it in the header.
     */
    if (    !pModLX->Hdr.e32_debuginfo
        ||  !pModLX->Hdr.e32_debuglen)
        return VERR_NOT_FOUND;
    return VINF_SUCCESS;
}

#if 0

/** @copydoc kLdrModMap */
static int kldrModLXMap(PRTLDRMODINTERNAL pMod)
{
    PKLDRMODLX  pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    unsigned    fFixed;
    void       *pvBase;
    int         rc;

    /*
     * Already mapped?
     */
    if (pModLX->pvMapping)
        return KLDR_ERR_ALREADY_MAPPED;

    /*
     * Allocate memory for it.
     */
    /* fixed image? */
    fFixed = pModLX->Core.enmType == RTLDRTYPE_EXECUTABLE_FIXED
          || pModLX->Core.enmType == RTLDRTYPE_SHARED_LIBRARY_FIXED;
    if (!fFixed)
        pvBase = NULL;
    else
    {
        pvBase = (void *)(uintptr_t)pModLX->aSegments[0].LinkAddress;
        if ((uintptr_t)pvBase != pModLX->aSegments[0].LinkAddress)
            return KLDR_ERR_ADDRESS_OVERFLOW;
    }
    rc = kHlpPageAlloc(&pvBase, pModLX->cbMapped, KPROT_EXECUTE_READWRITE, fFixed);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Load the bits, apply page protection, and update the segment table.
     */
    rc = kldrModLXDoLoadBits(pModLX, pvBase);
    if (RT_SUCCESS(rc))
        rc = kldrModLXDoProtect(pModLX, pvBase, 0 /* protect */);
    if (RT_SUCCESS(rc))
    {
        uint32_t i;
        for (i = 0; i < pModLX->cSegments; i++)
        {
            if (pModLX->aSegments[i].RVA != NIL_RTLDRADDR)
                pModLX->aSegments[i].MapAddress = (uintptr_t)pvBase + (uintptr_t)pModLX->aSegments[i].RVA;
        }
        pModLX->pvMapping = pvBase;
    }
    else
        kHlpPageFree(pvBase, pModLX->cbMapped);
    return rc;
}

#endif

/**
 * Loads the LX pages into the specified memory mapping.
 *
 * @returns IPRT status code.
 *
 * @param   pModLX  The LX module interpreter instance.
 * @param   pvBits  Where to load the bits.
 */
static int kldrModLXDoLoadBits(PKLDRMODLX pModLX, void *pvBits)
{
    const PRTLDRREADER pRdr = pModLX->Core.pReader;
    uint8_t *pbTmpPage = NULL;
    int rc = VINF_SUCCESS;
    uint32_t i;

    /*
     * Iterate the segments.
     */
    for (i = 0; i < pModLX->Hdr.e32_objcnt; i++)
    {
        const struct o32_obj * const pObj = &pModLX->paObjs[i];
        const uint32_t      cPages = (uint32_t)(pModLX->aSegments[i].cbMapped / OBJPAGELEN);
        uint32_t            iPage;
        uint8_t            *pbPage = (uint8_t *)pvBits + (uintptr_t)pModLX->aSegments[i].RVA;

        /*
         * Iterate the page map pages.
         */
        for (iPage = 0; RT_SUCCESS(rc) && iPage < pObj->o32_mapsize; iPage++, pbPage += OBJPAGELEN)
        {
            const struct o32_map *pMap = &pModLX->paPageMappings[iPage + pObj->o32_pagemap - 1];
            switch (pMap->o32_pageflags)
            {
                case VALID:
                    if (pMap->o32_pagesize == OBJPAGELEN)
                        rc = pRdr->pfnRead(pRdr, pbPage, OBJPAGELEN,
                                           pModLX->Hdr.e32_datapage + (pMap->o32_pagedataoffset << pModLX->Hdr.e32_pageshift));
                    else if (pMap->o32_pagesize < OBJPAGELEN)
                    {
                        rc = pRdr->pfnRead(pRdr, pbPage, pMap->o32_pagesize,
                                           pModLX->Hdr.e32_datapage + (pMap->o32_pagedataoffset << pModLX->Hdr.e32_pageshift));
                        memset(pbPage + pMap->o32_pagesize, 0, OBJPAGELEN - pMap->o32_pagesize);
                    }
                    else
                        rc = VERR_LDRLX_BAD_PAGE_MAP;
                    break;

                case ITERDATA:
                case ITERDATA2:
                    /* make sure we've got a temp page .*/
                    if (!pbTmpPage)
                    {
                        pbTmpPage = (uint8_t *)RTMemAlloc(OBJPAGELEN + 256);
                        if (!pbTmpPage)
                            break;
                    }
                    /* validate the size. */
                    if (pMap->o32_pagesize > OBJPAGELEN + 252)
                    {
                        rc = VERR_LDRLX_BAD_PAGE_MAP;
                        break;
                    }

                    /* read it and ensure 4 extra zero bytes. */
                    rc = pRdr->pfnRead(pRdr, pbTmpPage, pMap->o32_pagesize,
                                       pModLX->Hdr.e32_datapage + (pMap->o32_pagedataoffset << pModLX->Hdr.e32_pageshift));
                    if (RT_FAILURE(rc))
                        break;
                    memset(pbTmpPage + pMap->o32_pagesize, 0, 4);

                    /* unpack it into the image page. */
                    if (pMap->o32_pageflags == ITERDATA2)
                        rc = kldrModLXDoIterData2Unpacking(pbPage, pbTmpPage, pMap->o32_pagesize);
                    else
                        rc = kldrModLXDoIterDataUnpacking(pbPage, pbTmpPage, pMap->o32_pagesize);
                    break;

                case INVALID: /* we're probably not dealing correctly with INVALID pages... */
                case ZEROED:
                    memset(pbPage, 0, OBJPAGELEN);
                    break;

                case RANGE:
                    KLDRMODLX_ASSERT(!"RANGE");
                    RT_FALL_THRU();
                default:
                    rc = VERR_LDRLX_BAD_PAGE_MAP;
                    break;
            }
        }
        if (RT_FAILURE(rc))
            break;

        /*
         * Zero the remaining pages.
         */
        if (iPage < cPages)
            memset(pbPage, 0, (cPages - iPage) * OBJPAGELEN);
    }

    if (pbTmpPage)
        RTMemFree(pbTmpPage);
    return rc;
}


/**
 * Unpacks iterdata (aka EXEPACK).
 *
 * @returns IPRT status code.
 * @param   pbDst       Where to put the uncompressed data. (Assumes OBJPAGELEN size.)
 * @param   pbSrc       The compressed source data.
 * @param   cbSrc       The file size of the compressed data. The source buffer
 *                      contains 4 additional zero bytes.
 */
static int kldrModLXDoIterDataUnpacking(uint8_t *pbDst, const uint8_t *pbSrc, int cbSrc)
{
    const struct LX_Iter   *pIter = (const struct LX_Iter *)pbSrc;
    int                     cbDst = OBJPAGELEN;

    /* Validate size of data. */
    if (cbSrc >= (int)OBJPAGELEN - 2)
        return VERR_LDRLX_BAD_ITERDATA;

    /*
     * Expand the page.
     */
    while (cbSrc > 0 && pIter->LX_nIter)
    {
        if (pIter->LX_nBytes == 1)
        {
            /*
             * Special case - one databyte.
             */
            cbDst -= pIter->LX_nIter;
            if (cbDst < 0)
                return VERR_LDRLX_BAD_ITERDATA;

            cbSrc -= 4 + 1;
            if (cbSrc < -4)
                return VERR_LDRLX_BAD_ITERDATA;

            memset(pbDst, pIter->LX_Iterdata, pIter->LX_nIter);
            pbDst += pIter->LX_nIter;
            pIter++;
        }
        else
        {
            /*
             * General.
             */
            int i;

            cbDst -= pIter->LX_nIter * pIter->LX_nBytes;
            if (cbDst < 0)
                return VERR_LDRLX_BAD_ITERDATA;

            cbSrc -= 4 + pIter->LX_nBytes;
            if (cbSrc < -4)
                return VERR_LDRLX_BAD_ITERDATA;

            for (i = pIter->LX_nIter; i > 0; i--, pbDst += pIter->LX_nBytes)
                memcpy(pbDst, &pIter->LX_Iterdata, pIter->LX_nBytes);
            pIter   = (struct LX_Iter *)((char*)pIter + 4 + pIter->LX_nBytes);
        }
    }

    /*
     * Zero remainder of the page.
     */
    if (cbDst > 0)
        memset(pbDst, 0, cbDst);

    return VINF_SUCCESS;
}


/**
 * Unpacks iterdata (aka EXEPACK).
 *
 * @returns IPRT status code.
 * @param   pbDst       Where to put the uncompressed data. (Assumes OBJPAGELEN size.)
 * @param   pbSrc       The compressed source data.
 * @param   cbSrc       The file size of the compressed data. The source buffer
 *                      contains 4 additional zero bytes.
 */
static int kldrModLXDoIterData2Unpacking(uint8_t *pbDst, const uint8_t *pbSrc, int cbSrc)
{
    int cbDst = OBJPAGELEN;

    while (cbSrc > 0)
    {
        /*
         * Bit 0 and 1 is the encoding type.
         */
        switch (*pbSrc & 0x03)
        {
            /*
             *
             *  0  1  2  3  4  5  6  7
             *  type  |              |
             *        ----------------
             *             cb         <cb bytes of data>
             *
             * Bits 2-7 is, if not zero, the length of an uncompressed run
             * starting at the following byte.
             *
             *  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
             *  type  |              |  |                    | |                     |
             *        ----------------  ---------------------- -----------------------
             *             zero                 cb                 char to multiply
             *
             * If the bits are zero, the following two bytes describes a 1 byte interation
             * run. First byte is count, second is the byte to copy. A count of zero is
             * means end of data, and we simply stops. In that case the rest of the data
             * should be zero.
             */
            case 0:
            {
                if (*pbSrc)
                {
                    const int cb = *pbSrc >> 2;
                    cbDst -= cb;
                    if (cbDst < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    cbSrc -= cb + 1;
                    if (cbSrc < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    memcpy(pbDst, ++pbSrc, cb);
                    pbDst += cb;
                    pbSrc += cb;
                }
                else if (cbSrc < 2)
                    return VERR_LDRLX_BAD_ITERDATA2;
                else
                {
                    const int cb = pbSrc[1];
                    if (!cb)
                        goto l_endloop;
                    cbDst -= cb;
                    if (cbDst < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    cbSrc -= 3;
                    if (cbSrc < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    memset(pbDst, pbSrc[2], cb);
                    pbDst += cb;
                    pbSrc += 3;
                }
                break;
            }


            /*
             *  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
             *  type  |  |  |     |  |                       |
             *        ----  -------  -------------------------
             *        cb1   cb2 - 3          offset            <cb1 bytes of data>
             *
             * Two bytes layed out as described above, followed by cb1 bytes of data to be copied.
             * The cb2(+3) and offset describes an amount of data to be copied from the expanded
             * data relative to the current position. The data copied as you would expect it to be.
             */
            case 1:
            {
                cbSrc -= 2;
                if (cbSrc < 0)
                    return VERR_LDRLX_BAD_ITERDATA2;
                else
                {
                    const unsigned  off = ((unsigned)pbSrc[1] << 1) | (*pbSrc >> 7);
                    const int       cb1 = (*pbSrc >> 2) & 3;
                    const int       cb2 = ((*pbSrc >> 4) & 7) + 3;

                    pbSrc += 2;
                    cbSrc -= cb1;
                    if (cbSrc < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    cbDst -= cb1;
                    if (cbDst < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    memcpy(pbDst, pbSrc, cb1);
                    pbDst += cb1;
                    pbSrc += cb1;

                    if (off > OBJPAGELEN - (unsigned)cbDst)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    cbDst -= cb2;
                    if (cbDst < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    memmove(pbDst, pbDst - off, cb2);
                    pbDst += cb2;
                }
                break;
            }


            /*
             *  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
             *  type  |  |  |                                |
             *        ----  ----------------------------------
             *       cb-3               offset
             *
             * Two bytes layed out as described above.
             * The cb(+3) and offset describes an amount of data to be copied from the expanded
             * data relative to the current position.
             *
             * If offset == 1 the data is not copied as expected, but in the memcpyw manner.
             */
            case 2:
            {
                cbSrc -= 2;
                if (cbSrc < 0)
                    return VERR_LDRLX_BAD_ITERDATA2;
                else
                {
                    const unsigned  off = ((unsigned)pbSrc[1] << 4) | (*pbSrc >> 4);
                    const int       cb = ((*pbSrc >> 2) & 3) + 3;

                    pbSrc += 2;
                    if (off > OBJPAGELEN - (unsigned)cbDst)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    cbDst -= cb;
                    if (cbDst < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    kLdrModLXMemCopyW(pbDst, pbDst - off, cb);
                    pbDst += cb;
                }
                break;
            }


            /*
             *  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
             *  type  |        |  |              |  |                                |
             *        ----------  ----------------  ----------------------------------
             *           cb1            cb2                      offset                <cb1 bytes of data>
             *
             * Three bytes layed out as described above, followed by cb1 bytes of data to be copied.
             * The cb2 and offset describes an amount of data to be copied from the expanded
             * data relative to the current position.
             *
             * If offset == 1 the data is not copied as expected, but in the memcpyw manner.
             */
            case 3:
            {
                cbSrc -= 3;
                if (cbSrc < 0)
                    return VERR_LDRLX_BAD_ITERDATA2;
                else
                {
                    const int       cb1 = (*pbSrc >> 2) & 0xf;
                    const int       cb2 = ((pbSrc[1] & 0xf) << 2) | (*pbSrc >> 6);
                    const unsigned  off = ((unsigned)pbSrc[2] << 4) | (pbSrc[1] >> 4);

                    pbSrc += 3;
                    cbSrc -= cb1;
                    if (cbSrc < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    cbDst -= cb1;
                    if (cbDst < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    memcpy(pbDst, pbSrc, cb1);
                    pbDst += cb1;
                    pbSrc += cb1;

                    if (off > OBJPAGELEN - (unsigned)cbDst)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    cbDst -= cb2;
                    if (cbDst < 0)
                        return VERR_LDRLX_BAD_ITERDATA2;
                    kLdrModLXMemCopyW(pbDst, pbDst - off, cb2);
                    pbDst += cb2;
                }
                break;
            }
        } /* type switch. */
    } /* unpack loop */

l_endloop:


    /*
     * Zero remainder of the page.
     */
    if (cbDst > 0)
        memset(pbDst, 0, cbDst);

    return VINF_SUCCESS;
}


/**
 * Special memcpy employed by the iterdata2 algorithm.
 *
 * Emulate a 16-bit memcpy (copying 16-bit at a time) and the effects this
 * has if src is very close to the destination.
 *
 * @param   pbDst   Destination pointer.
 * @param   pbSrc   Source pointer. Will always be <= pbDst.
 * @param   cb      Amount of data to be copied.
 * @remark  This assumes that unaligned word and dword access is fine.
 */
static void kLdrModLXMemCopyW(uint8_t *pbDst, const uint8_t *pbSrc, int cb)
{
    switch (pbDst - pbSrc)
    {
        case 0:
        case 1:
        case 2:
        case 3:
            /* 16-bit copy (unaligned) */
            if (cb & 1)
                *pbDst++ = *pbSrc++;
            for (cb >>= 1; cb > 0; cb--, pbDst += 2, pbSrc += 2)
                *(uint16_t *)pbDst = *(const uint16_t *)pbSrc;
            break;

        default:
            /* 32-bit copy (unaligned) */
            if (cb & 1)
                *pbDst++ = *pbSrc++;
            if (cb & 2)
            {
                *(uint16_t *)pbDst = *(const uint16_t *)pbSrc;
                pbDst += 2;
                pbSrc += 2;
            }
            for (cb >>= 2; cb > 0; cb--, pbDst += 4, pbSrc += 4)
                *(uint32_t *)pbDst = *(const uint32_t *)pbSrc;
            break;
    }
}

#if 0

/**
 * Unprotects or protects the specified image mapping.
 *
 * @returns IPRT status code.
 *
 * @param   pModLX  The LX module interpreter instance.
 * @param   pvBits  The mapping to protect.
 * @param   UnprotectOrProtect  If 1 unprotect (i.e. make all writable), otherwise
 *          protect according to the object table.
 */
static int kldrModLXDoProtect(PKLDRMODLX pModLX, void *pvBits, unsigned fUnprotectOrProtect)
{
    uint32_t i;

    /*
     * Change object protection.
     */
    for (i = 0; i < pModLX->cSegments; i++)
    {
        int rc;
        void *pv;
        KPROT enmProt;

        /* calc new protection. */
        enmProt = pModLX->aSegments[i].enmProt;
        if (fUnprotectOrProtect)
        {
            switch (enmProt)
            {
                case KPROT_NOACCESS:
                case KPROT_READONLY:
                case KPROT_READWRITE:
                case KPROT_WRITECOPY:
                    enmProt = KPROT_READWRITE;
                    break;
                case KPROT_EXECUTE:
                case KPROT_EXECUTE_READ:
                case KPROT_EXECUTE_READWRITE:
                case KPROT_EXECUTE_WRITECOPY:
                    enmProt = KPROT_EXECUTE_READWRITE;
                    break;
                default:
                    KLDRMODLX_ASSERT(!"bad enmProt");
                    return -1;
            }
        }
        else
        {
            /* copy on write -> normal write. */
            if (enmProt == KPROT_EXECUTE_WRITECOPY)
                enmProt = KPROT_EXECUTE_READWRITE;
            else if (enmProt == KPROT_WRITECOPY)
                enmProt = KPROT_READWRITE;
        }


        /* calc the address and set page protection. */
        pv = (uint8_t *)pvBits + pModLX->aSegments[i].RVA;

        rc = kHlpPageProtect(pv, pModLX->aSegments[i].cbMapped, enmProt);
        if (RT_FAILURE(rc))
            break;

        /** @todo the gap page should be marked NOACCESS! */
    }

    return VINF_SUCCESS;
}


/** @copydoc kLdrModUnmap */
static int kldrModLXUnmap(PRTLDRMODINTERNAL pMod)
{
    PKLDRMODLX  pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    uint32_t    i;
    int         rc;

    /*
     * Mapped?
     */
    if (!pModLX->pvMapping)
        return KLDR_ERR_NOT_MAPPED;

    /*
     * Free the mapping and update the segments.
     */
    rc = kHlpPageFree((void *)pModLX->pvMapping, pModLX->cbMapped);
    KLDRMODLX_ASSERT(!rc);
    pModLX->pvMapping = NULL;

    for (i = 0; i < pModLX->cSegments; i++)
        pModLX->aSegments[i].MapAddress = 0;

    return rc;
}


/** @copydoc kLdrModAllocTLS */
static int kldrModLXAllocTLS(PRTLDRMODINTERNAL pMod, void *pvMapping)
{
    PKLDRMODLX  pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);

    /* no tls, just do the error checking. */
    if (   pvMapping == KLDRMOD_INT_MAP
        && pModLX->pvMapping)
        return KLDR_ERR_NOT_MAPPED;
    return VINF_SUCCESS;
}


/** @copydoc kLdrModFreeTLS */
static void kldrModLXFreeTLS(PRTLDRMODINTERNAL pMod, void *pvMapping)
{
    /* no tls. */
    RT_NOREF(pMod);
    RT_NOREF(pvMapping);

}


/** @copydoc kLdrModReload */
static int kldrModLXReload(PRTLDRMODINTERNAL pMod)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    int rc, rc2;

    /*
     * Mapped?
     */
    if (!pModLX->pvMapping)
        return KLDR_ERR_NOT_MAPPED;

    /*
     * Before doing anything we'll have to make all pages writable.
     */
    rc = kldrModLXDoProtect(pModLX, (void *)pModLX->pvMapping, 1 /* unprotect */);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Load the bits again.
     */
    rc = kldrModLXDoLoadBits(pModLX, (void *)pModLX->pvMapping);

    /*
     * Restore protection.
     */
    rc2 = kldrModLXDoProtect(pModLX, (void *)pModLX->pvMapping, 0 /* protect */);
    if (RT_SUCCESS(rc) && RT_FAILURE(rc2))
        rc = rc2;
    return rc;
}


/** @copydoc kLdrModFixupMapping */
static int kldrModLXFixupMapping(PRTLDRMODINTERNAL pMod, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    int rc, rc2;

    /*
     * Mapped?
     */
    if (!pModLX->pvMapping)
        return KLDR_ERR_NOT_MAPPED;

    /*
     * Before doing anything we'll have to make all pages writable.
     */
    rc = kldrModLXDoProtect(pModLX, (void *)pModLX->pvMapping, 1 /* unprotect */);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Apply fixups and resolve imports.
     */
    rc = rtldrLX_RelocateBits(pMod, (void *)pModLX->pvMapping, (uintptr_t)pModLX->pvMapping,
                              pModLX->aSegments[0].LinkAddress, pfnGetImport, pvUser);

    /*
     * Restore protection.
     */
    rc2 = kldrModLXDoProtect(pModLX, (void *)pModLX->pvMapping, 0 /* protect */);
    if (RT_SUCCESS(rc) && RT_FAILURE(rc2))
        rc = rc2;
    return rc;
}


/** @copydoc kLdrModCallInit */
static int kldrModLXCallInit(PRTLDRMODINTERNAL pMod, void *pvMapping, uintptr_t uHandle)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    int rc;

    /*
     * Mapped?
     */
    if (pvMapping == KLDRMOD_INT_MAP)
    {
        pvMapping = (void *)pModLX->pvMapping;
        if (!pvMapping)
            return KLDR_ERR_NOT_MAPPED;
    }

    /*
     * Do TLS callbacks first and then call the init/term function if it's a DLL.
     */
    if ((pModLX->Hdr.e32_mflags & E32MODMASK) == E32MODDLL)
        rc = kldrModLXDoCallDLL(pModLX, pvMapping, 0 /* attach */, uHandle);
    else
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Call the DLL entrypoint.
 *
 * @returns 0 on success.
 * @returns KLDR_ERR_MODULE_INIT_FAILED  or KLDR_ERR_THREAD_ATTACH_FAILED on failure.
 * @param   pModLX          The LX module interpreter instance.
 * @param   pvMapping       The module mapping to use (resolved).
 * @param   uOp             The operation (DLL_*).
 * @param   uHandle         The module handle to present.
 */
static int kldrModLXDoCallDLL(PKLDRMODLX pModLX, void *pvMapping, unsigned uOp, uintptr_t uHandle)
{
    int rc;

    /*
     * If no entrypoint there isn't anything to be done.
     */
    if (    !pModLX->Hdr.e32_startobj
        ||  pModLX->Hdr.e32_startobj > pModLX->Hdr.e32_objcnt)
        return VINF_SUCCESS;

    /*
     * Invoke the entrypoint and convert the boolean result to a kLdr status code.
     */
    rc = kldrModLXDoCall((uintptr_t)pvMapping
                         + (uintptr_t)pModLX->aSegments[pModLX->Hdr.e32_startobj - 1].RVA
                         + pModLX->Hdr.e32_eip,
                         uHandle, uOp, NULL);
    if (rc)
        rc = VINF_SUCCESS;
    else if (uOp == 0 /* attach */)
        rc = KLDR_ERR_MODULE_INIT_FAILED;
    else /* detach: ignore failures */
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Do a 3 parameter callback.
 *
 * @returns 32-bit callback return.
 * @param   uEntrypoint     The address of the function to be called.
 * @param   uHandle         The first argument, the module handle.
 * @param   uOp             The second argumnet, the reason we're calling.
 * @param   pvReserved      The third argument, reserved argument. (figure this one out)
 */
static int32_t kldrModLXDoCall(uintptr_t uEntrypoint, uintptr_t uHandle, uint32_t uOp, void *pvReserved)
{
#if defined(__X86__) || defined(__i386__) || defined(_M_IX86)
    int32_t rc;
/** @todo try/except */

    /*
     * Paranoia.
     */
# ifdef __GNUC__
    __asm__ __volatile__(
        "pushl  %2\n\t"
        "pushl  %1\n\t"
        "pushl  %0\n\t"
        "lea   12(%%esp), %2\n\t"
        "call  *%3\n\t"
        "movl   %2, %%esp\n\t"
        : "=a" (rc)
        : "d" (uOp),
          "S" (0),
          "c" (uEntrypoint),
          "0" (uHandle));
# elif defined(_MSC_VER)
    __asm {
        mov     eax, [uHandle]
        mov     edx, [uOp]
        mov     ecx, 0
        mov     ebx, [uEntrypoint]
        push    edi
        mov     edi, esp
        push    ecx
        push    edx
        push    eax
        call    ebx
        mov     esp, edi
        pop     edi
        mov     [rc], eax
    }
# else
#  error "port me!"
# endif
    RT_NOREF(pvReserved);
    return rc;

#else
    RT_NOREF(uEntrypoint);
    RT_NOREF(uHandle);
    RT_NOREF(uOp);
    RT_NOREF(pvReserved);
    return KCPU_ERR_ARCH_CPU_NOT_COMPATIBLE;
#endif
}


/** @copydoc kLdrModCallTerm */
static int kldrModLXCallTerm(PRTLDRMODINTERNAL pMod, void *pvMapping, uintptr_t uHandle)
{
    PKLDRMODLX  pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);

    /*
     * Mapped?
     */
    if (pvMapping == KLDRMOD_INT_MAP)
    {
        pvMapping = (void *)pModLX->pvMapping;
        if (!pvMapping)
            return KLDR_ERR_NOT_MAPPED;
    }

    /*
     * Do the call.
     */
    if ((pModLX->Hdr.e32_mflags & E32MODMASK) == E32MODDLL)
        kldrModLXDoCallDLL(pModLX, pvMapping, 1 /* detach */, uHandle);

    return VINF_SUCCESS;
}


/** @copydoc kLdrModCallThread */
static int kldrModLXCallThread(PRTLDRMODINTERNAL pMod, void *pvMapping, uintptr_t uHandle, unsigned fAttachingOrDetaching)
{
    /* no thread attach/detach callout. */
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
static DECLCALLBACK(size_t) rtldrLX_GetImageSize(PRTLDRMODINTERNAL pMod)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    return pModLX->cbMapped;
}


/**
 * @interface_method_impl{RTLDROPS,pfnGetBits}
 */
static DECLCALLBACK(int) rtldrLX_GetBits(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR BaseAddress,
                                         PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);

    /*
     * Load the image bits.
     */
    int rc = kldrModLXDoLoadBits(pModLX, pvBits);
    if (RT_SUCCESS(rc))
    {
        /*
         * Perform relocations.
         *
         * We force this to take place by messing with the OldBaseAddress as we
         * have to apply internal relocations even if the load address is the
         * same as the link address.
         */
        rc = rtldrLX_RelocateBits(pMod, pvBits, BaseAddress,
                                  _4K ^ BaseAddress ^ pModLX->aSegments[0].LinkAddress,
                                  pfnGetImport, pvUser);
    }
    return rc;
}


/* GCC goes boinkers if we put this inside the function. */
union RELOC_VISIBILITY_STUPIDITY
{
    const uint8_t          *pb;
    const struct r32_rlc   *prlc;
};

/**
 * @interface_method_impl{RTLDROPS,pfnRelocate}
 */
static DECLCALLBACK(int) rtldrLX_RelocateBits(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR NewBaseAddress,
                                              RTUINTPTR OldBaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PKLDRMODLX pModLX = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    uint32_t iSeg;
    int rc;

    /*
     * Do we need to to *anything*?
     */
    if (    NewBaseAddress == OldBaseAddress
        &&  NewBaseAddress == pModLX->paObjs[0].o32_base
        &&  !pModLX->Hdr.e32_impmodcnt)
        return VINF_SUCCESS;

    /*
     * Load the fixup section.
     */
    if (!pModLX->pbFixupSection)
    {
        rc = kldrModLXDoLoadFixupSection(pModLX);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Iterate the segments.
     */
    for (iSeg = 0; iSeg < pModLX->Hdr.e32_objcnt; iSeg++)
    {
        const struct o32_obj * const pObj = &pModLX->paObjs[iSeg];
        RTLDRADDR           PageAddress = NewBaseAddress + pModLX->aSegments[iSeg].RVA;
        uint32_t            iPage;
        uint8_t            *pbPage = (uint8_t *)pvBits + (uintptr_t)pModLX->aSegments[iSeg].RVA;

        /*
         * Iterate the page map pages.
         */
        for (iPage = 0, rc = VINF_SUCCESS;
             RT_SUCCESS(rc) && iPage < pObj->o32_mapsize;
             iPage++, pbPage += OBJPAGELEN, PageAddress += OBJPAGELEN)
        {
            const uint8_t * const   pbFixupRecEnd = pModLX->pbFixupRecs + pModLX->paoffPageFixups[iPage + pObj->o32_pagemap];
            const uint8_t          *pb            = pModLX->pbFixupRecs + pModLX->paoffPageFixups[iPage + pObj->o32_pagemap - 1];
            RTLDRADDR               uValue        = NIL_RTLDRADDR;
            uint32_t                fKind         = 0;
            int                     iSelector;

            /* sanity */
            if (pbFixupRecEnd < pb)
                return VERR_LDR_BAD_FIXUP;
            if (pbFixupRecEnd - 1 > pModLX->pbFixupSectionLast)
                return VERR_LDR_BAD_FIXUP;
            if (pb < pModLX->pbFixupSection)
                return VERR_LDR_BAD_FIXUP;

            /*
             * Iterate the fixup record.
             */
            while (pb < pbFixupRecEnd)
            {
                union RELOC_VISIBILITY_STUPIDITY u;
                char szImpModule[256];
                u.pb = pb;
                pb += 3 + (u.prlc->nr_stype & NRCHAIN ? 0 : 1); /* place pch at the 4th member. */

                /*
                 * Figure out the target.
                 */
                switch (u.prlc->nr_flags & NRRTYP)
                {
                    /*
                     * Internal fixup.
                     */
                    case NRRINT:
                    {
                        uint16_t iTrgObject;
                        uint32_t offTrgObject;

                        /* the object */
                        if (u.prlc->nr_flags & NR16OBJMOD)
                        {
                            iTrgObject = *(const uint16_t *)pb;
                            pb += 2;
                        }
                        else
                            iTrgObject = *pb++;
                        iTrgObject--;
                        if (iTrgObject >= pModLX->Hdr.e32_objcnt)
                            return VERR_LDR_BAD_FIXUP;

                        /* the target */
                        if ((u.prlc->nr_stype & NRSRCMASK) != NRSSEG)
                        {
                            if (u.prlc->nr_flags & NR32BITOFF)
                            {
                                offTrgObject = *(const uint32_t *)pb;
                                pb += 4;
                            }
                            else
                            {
                                offTrgObject = *(const uint16_t *)pb;
                                pb += 2;
                            }

                            /* calculate the symbol info. */
                            uValue = offTrgObject + NewBaseAddress + pModLX->aSegments[iTrgObject].RVA;
                        }
                        else
                            uValue = NewBaseAddress + pModLX->aSegments[iTrgObject].RVA;
                        if (    (u.prlc->nr_stype & NRALIAS)
                            ||  (pModLX->aSegments[iTrgObject].fFlags & RTLDRSEG_FLAG_16BIT))
                            iSelector = pModLX->aSegments[iTrgObject].Sel16bit;
                        else
                            iSelector = pModLX->aSegments[iTrgObject].SelFlat;
                        fKind = 0;
                        break;
                    }

                    /*
                     * Import by symbol ordinal.
                     */
                    case NRRORD:
                    {
                        uint16_t iModule;
                        uint32_t iSymbol;

                        /* the module ordinal */
                        if (u.prlc->nr_flags & NR16OBJMOD)
                        {
                            iModule = *(const uint16_t *)pb;
                            pb += 2;
                        }
                        else
                            iModule = *pb++;
                        iModule--;
                        if (iModule >= pModLX->Hdr.e32_impmodcnt)
                            return VERR_LDR_BAD_FIXUP;
                        rc = kldrModLXGetImport(pModLX, NULL, iModule, szImpModule, sizeof(szImpModule), NULL);
                        if (RT_FAILURE(rc))
                            return rc;

#if 1
                        if (u.prlc->nr_flags & NRICHAIN)
                            return VERR_LDR_BAD_FIXUP;
#endif

                        /* . */
                        if (u.prlc->nr_flags & NR32BITOFF)
                        {
                            iSymbol = *(const uint32_t *)pb;
                            pb += 4;
                        }
                        else if (!(u.prlc->nr_flags & NR8BITORD))
                        {
                            iSymbol = *(const uint16_t *)pb;
                            pb += 2;
                        }
                        else
                            iSymbol = *pb++;

                        /* resolve it. */
                        rc = pfnGetImport(pMod, szImpModule, NULL, iSymbol, &uValue, /*&fKind,*/ pvUser);
                        if (RT_FAILURE(rc))
                            return rc;
                        iSelector = -1;
                        break;
                    }

                    /*
                     * Import by symbol name.
                     */
                    case NRRNAM:
                    {
                        uint32_t iModule;
                        uint16_t offSymbol;
                        const uint8_t *pbSymbol;

                        /* the module ordinal */
                        if (u.prlc->nr_flags & NR16OBJMOD)
                        {
                            iModule = *(const uint16_t *)pb;
                            pb += 2;
                        }
                        else
                            iModule = *pb++;
                        iModule--;
                        if (iModule >= pModLX->Hdr.e32_impmodcnt)
                            return VERR_LDR_BAD_FIXUP;
                        rc = kldrModLXGetImport(pModLX, NULL, iModule, szImpModule, sizeof(szImpModule), NULL);
                        if (RT_FAILURE(rc))
                            return rc;
#if 1
                        if (u.prlc->nr_flags & NRICHAIN)
                            return VERR_LDR_BAD_FIXUP;
#endif

                        /* . */
                        if (u.prlc->nr_flags & NR32BITOFF)
                        {
                            offSymbol = *(const uint32_t *)pb;
                            pb += 4;
                        }
                        else if (!(u.prlc->nr_flags & NR8BITORD))
                        {
                            offSymbol = *(const uint16_t *)pb;
                            pb += 2;
                        }
                        else
                            offSymbol = *pb++;
                        pbSymbol = pModLX->pbImportProcs + offSymbol;
                        if (    pbSymbol < pModLX->pbImportProcs
                            ||  pbSymbol > pModLX->pbFixupSectionLast)
                            return VERR_LDR_BAD_FIXUP;
                        char szSymbol[256];
                        memcpy(szSymbol, pbSymbol + 1, *pbSymbol);
                        szSymbol[*pbSymbol] = '\0';

                        /* resolve it. */
                        rc = pfnGetImport(pMod, szImpModule, szSymbol, UINT32_MAX, &uValue, /*&fKind,*/ pvUser);
                        if (RT_FAILURE(rc))
                            return rc;
                        iSelector = -1;
                        break;
                    }

                    case NRRENT:
                        KLDRMODLX_ASSERT(!"NRRENT");
                        RT_FALL_THRU();
                    default:
                        iSelector = -1;
                        break;
                }

                /* addend */
                if (u.prlc->nr_flags & NRADD)
                {
                    if (u.prlc->nr_flags & NR32BITADD)
                    {
                        uValue += *(const uint32_t *)pb;
                        pb += 4;
                    }
                    else
                    {
                        uValue += *(const uint16_t *)pb;
                        pb += 2;
                    }
                }


                /*
                 * Deal with the 'source' (i.e. the place that should be modified - very logical).
                 */
                if (!(u.prlc->nr_stype & NRCHAIN))
                {
                    int off = u.prlc->r32_soff;

                    /* common / simple */
                    if (    (u.prlc->nr_stype & NRSRCMASK) == NROFF32
                        &&  off >= 0
                        &&  off <= (int)OBJPAGELEN - 4)
                        *(uint32_t *)&pbPage[off] = (uint32_t)uValue;
                    else if (    (u.prlc->nr_stype & NRSRCMASK) == NRSOFF32
                            &&  off >= 0
                            &&  off <= (int)OBJPAGELEN - 4)
                        *(uint32_t *)&pbPage[off] = (uint32_t)(uValue - (PageAddress + off + 4));
                    else
                    {
                        /* generic */
                        rc = kldrModLXDoReloc(pbPage, off, PageAddress, u.prlc, iSelector, uValue, fKind);
                        if (RT_FAILURE(rc))
                            return rc;
                    }
                }
                else if (!(u.prlc->nr_flags & NRICHAIN))
                {
                    const int16_t *poffSrc = (const int16_t *)pb;
                    uint8_t c = u.pb[2];

                    /* common / simple */
                    if ((u.prlc->nr_stype & NRSRCMASK) == NROFF32)
                    {
                        while (c-- > 0)
                        {
                            int off = *poffSrc++;
                            if (off >= 0 && off <= (int)OBJPAGELEN - 4)
                                *(uint32_t *)&pbPage[off] = (uint32_t)uValue;
                            else
                            {
                                rc = kldrModLXDoReloc(pbPage, off, PageAddress, u.prlc, iSelector, uValue, fKind);
                                if (RT_FAILURE(rc))
                                    return rc;
                            }
                        }
                    }
                    else if ((u.prlc->nr_stype & NRSRCMASK) == NRSOFF32)
                    {
                        while (c-- > 0)
                        {
                            int off = *poffSrc++;
                            if (off >= 0 && off <= (int)OBJPAGELEN - 4)
                                *(uint32_t *)&pbPage[off] = (uint32_t)(uValue - (PageAddress + off + 4));
                            else
                            {
                                rc = kldrModLXDoReloc(pbPage, off, PageAddress, u.prlc, iSelector, uValue, fKind);
                                if (RT_FAILURE(rc))
                                    return rc;
                            }
                        }
                    }
                    else
                    {
                        while (c-- > 0)
                        {
                            rc = kldrModLXDoReloc(pbPage, *poffSrc++, PageAddress, u.prlc, iSelector, uValue, fKind);
                            if (RT_FAILURE(rc))
                                return rc;
                        }
                    }
                    pb = (const uint8_t *)poffSrc;
                }
                else
                {
                    /* This is a pain because it will require virgin pages on a relocation. */
                    KLDRMODLX_ASSERT(!"NRICHAIN");
                    return VERR_LDRLX_NRICHAIN_NOT_SUPPORTED;
                }
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Applies the relocation to one 'source' in a page.
 *
 * This takes care of the more esotic case while the common cases
 * are dealt with seperately.
 *
 * @returns IPRT status code.
 * @param   pbPage      The page in which to apply the fixup.
 * @param   off         Page relative offset of where to apply the offset.
 * @param   PageAddress The page address.
 * @param   prlc        The relocation record.
 * @param   iSelector   Selector value, -1 if flat.
 * @param   uValue      The target value.
 * @param   fKind       The target kind.
 */
static int kldrModLXDoReloc(uint8_t *pbPage, int off, RTLDRADDR PageAddress, const struct r32_rlc *prlc,
                            int iSelector, RTLDRADDR uValue, uint32_t fKind)
{
#pragma pack(1) /* just to be sure */
    union
    {
        uint8_t         ab[6];
        uint32_t        off32;
        uint16_t        off16;
        uint8_t         off8;
        struct
        {
            uint16_t    off;
            uint16_t    Sel;
        }               Far16;
        struct
        {
            uint32_t    off;
            uint16_t    Sel;
        }               Far32;
    }                   uData;
#pragma pack()
    const uint8_t      *pbSrc;
    uint8_t            *pbDst;
    uint8_t             cb;

    RT_NOREF(fKind);

    /*
     * Compose the fixup data.
     */
    switch (prlc->nr_stype & NRSRCMASK)
    {
        case NRSBYT:
            uData.off8 = (uint8_t)uValue;
            cb = 1;
            break;
        case NRSSEG:
            if (iSelector == -1)
            {
                /* fixme */
            }
            uData.off16 = iSelector;
            cb = 2;
            break;
        case NRSPTR:
            if (iSelector == -1)
            {
                /* fixme */
            }
            uData.Far16.off = (uint16_t)uValue;
            uData.Far16.Sel = iSelector;
            cb = 4;
            break;
        case NRSOFF:
            uData.off16 = (uint16_t)uValue;
            cb = 2;
            break;
        case NRPTR48:
            if (iSelector == -1)
            {
                /* fixme */
            }
            uData.Far32.off = (uint32_t)uValue;
            uData.Far32.Sel = iSelector;
            cb = 6;
            break;
        case NROFF32:
            uData.off32 = (uint32_t)uValue;
            cb = 4;
            break;
        case NRSOFF32:
            uData.off32 = (uint32_t)(uValue - (PageAddress + off + 4));
            cb = 4;
            break;
        default:
            return VERR_LDRLX_BAD_FIXUP_SECTION; /** @todo fix error, add more checks! */
    }

    /*
     * Apply it. This is sloooow...
     */
    pbSrc = &uData.ab[0];
    pbDst = pbPage + off;
    while (cb-- > 0)
    {
        if (off > (int)OBJPAGELEN)
            break;
        if (off >= 0)
            *pbDst = *pbSrc;
        pbSrc++;
        pbDst++;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnEnumSegments}
 */
static DECLCALLBACK(int) rtldrLX_EnumSegments(PRTLDRMODINTERNAL pMod, PFNRTLDRENUMSEGS pfnCallback, void *pvUser)
{
    PKLDRMODLX      pThis     = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    uint32_t const  cSegments = pThis->cSegments;
    for (uint32_t iSeg = 0; iSeg < cSegments; iSeg++)
    {
        int rc = pfnCallback(pMod, &pThis->aSegments[iSeg], pvUser);
        if (rc != VINF_SUCCESS)
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnLinkAddressToSegOffset}
 */
static DECLCALLBACK(int) rtldrLX_LinkAddressToSegOffset(PRTLDRMODINTERNAL pMod, RTLDRADDR LinkAddress,
                                                           uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    PKLDRMODLX     pThis     = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    uint32_t const cSegments = pThis->cSegments;
    for (uint32_t iSeg = 0; iSeg < cSegments; iSeg++)
    {
        RTLDRADDR offSeg = LinkAddress - pThis->aSegments[iSeg].LinkAddress;
        if (   offSeg < pThis->aSegments[iSeg].cbMapped
            || offSeg < pThis->aSegments[iSeg].cb)
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
static DECLCALLBACK(int) rtldrLX_LinkAddressToRva(PRTLDRMODINTERNAL pMod, RTLDRADDR LinkAddress, PRTLDRADDR pRva)
{
    PKLDRMODLX     pThis     = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    uint32_t const cSegments = pThis->cSegments;
    for (uint32_t iSeg = 0; iSeg < cSegments; iSeg++)
    {
        RTLDRADDR offSeg = LinkAddress - pThis->aSegments[iSeg].LinkAddress;
        if (   offSeg < pThis->aSegments[iSeg].cbMapped
            || offSeg < pThis->aSegments[iSeg].cb)
        {
            *pRva = pThis->aSegments[iSeg].RVA + offSeg;
            return VINF_SUCCESS;
        }
    }

    return VERR_LDR_INVALID_RVA;
}


/**
 * @interface_method_impl{RTLDROPS,pfnSegOffsetToRva}
 */
static DECLCALLBACK(int) rtldrLX_SegOffsetToRva(PRTLDRMODINTERNAL pMod, uint32_t iSeg, RTLDRADDR offSeg, PRTLDRADDR pRva)
{
    PKLDRMODLX  pThis = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);

    if (iSeg >= pThis->cSegments)
        return VERR_LDR_INVALID_SEG_OFFSET;
    PCRTLDRSEG pSegment = &pThis->aSegments[iSeg];

    if (   offSeg > pSegment->cbMapped
        && offSeg > pSegment->cb
        && (    pSegment->cbFile < 0
            ||  offSeg > (uint64_t)pSegment->cbFile))
        return VERR_LDR_INVALID_SEG_OFFSET;

    *pRva = pSegment->RVA + offSeg;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTLDROPS,pfnRvaToSegOffset}
 */
static DECLCALLBACK(int) rtldrLX_RvaToSegOffset(PRTLDRMODINTERNAL pMod, RTLDRADDR Rva, uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    PKLDRMODLX     pThis     = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    uint32_t const cSegments = pThis->cSegments;
    for (uint32_t iSeg = 0; iSeg < cSegments; iSeg++)
    {
        RTLDRADDR offSeg = Rva - pThis->aSegments[iSeg].RVA;
        if (   offSeg < pThis->aSegments[iSeg].cbMapped
            || offSeg < pThis->aSegments[iSeg].cb)
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
static DECLCALLBACK(int) rtldrLX_ReadDbgInfo(PRTLDRMODINTERNAL pMod, uint32_t iDbgInfo, RTFOFF off, size_t cb, void *pvBuf)
{
    PKLDRMODLX pThis = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    RT_NOREF(iDbgInfo);
    return pThis->Core.pReader->pfnRead(pThis->Core.pReader, pvBuf, cb, off);
}


/**
 * @interface_method_impl{RTLDROPS,pfnQueryProp}
 */
static DECLCALLBACK(int) rtldrLX_QueryProp(PRTLDRMODINTERNAL pMod, RTLDRPROP enmProp, void const *pvBits,
                                          void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    PKLDRMODLX pThis = RT_FROM_MEMBER(pMod, KLDRMODLX, Core);
    int           rc;
    switch (enmProp)
    {
        case RTLDRPROP_IMPORT_COUNT:
            Assert(cbBuf == sizeof(uint32_t));
            Assert(*pcbRet == cbBuf);
            *(uint32_t *)pvBuf = pThis->Hdr.e32_impmodcnt;
            rc = VINF_SUCCESS;
            break;

        case RTLDRPROP_IMPORT_MODULE:
            rc = kldrModLXGetImport(pThis, pvBits, *(uint32_t const *)pvBuf, (char *)pvBuf, cbBuf, pcbRet);
            break;

        case RTLDRPROP_INTERNAL_NAME:
            *pcbRet = pThis->cchName + 1;
            if (cbBuf >= pThis->cchName + 1)
            {
                memcpy(pvBuf, pThis->pszName, pThis->cchName + 1);
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
            break;


        default:
            rc = VERR_NOT_FOUND;
            break;
    }
    RT_NOREF_PV(pvBits);
    return rc;
}


/**
 * Operations for a Mach-O module interpreter.
 */
static const RTLDROPS s_rtldrLXOps=
{
    "LX",
    rtldrLX_Close,
    NULL,
    NULL /*pfnDone*/,
    rtldrLX_EnumSymbols,
    /* ext */
    rtldrLX_GetImageSize,
    rtldrLX_GetBits,
    rtldrLX_RelocateBits,
    rtldrLX_GetSymbolEx,
    NULL /*pfnQueryForwarderInfo*/,
    rtldrLX_EnumDbgInfo,
    rtldrLX_EnumSegments,
    rtldrLX_LinkAddressToSegOffset,
    rtldrLX_LinkAddressToRva,
    rtldrLX_SegOffsetToRva,
    rtldrLX_RvaToSegOffset,
    rtldrLX_ReadDbgInfo,
    rtldrLX_QueryProp,
    NULL /*pfnVerifySignature*/,
    NULL /*pfnHashImage*/,
    NULL /*pfnUnwindFrame*/,
    42
};


/**
 * Handles opening LX images.
 */
DECLHIDDEN(int) rtldrLXOpen(PRTLDRREADER pReader, uint32_t fFlags, RTLDRARCH enmArch, RTFOFF offLxHdr,
                            PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo)
{

    /*
     * Create the instance data and do a minimal header validation.
     */
    PKLDRMODLX pThis = NULL;
    int rc = kldrModLXDoCreate(pReader, offLxHdr, fFlags, &pThis, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Match up against the requested CPU architecture.
         */
        if (   enmArch == RTLDRARCH_WHATEVER
            || pThis->Core.enmArch == enmArch)
        {
            pThis->Core.pOps     = &s_rtldrLXOps;
            pThis->Core.u32Magic = RTLDRMOD_MAGIC;
            *phLdrMod = &pThis->Core;
            return VINF_SUCCESS;
        }
        rc = VERR_LDR_ARCH_MISMATCH;
    }
    if (pThis)
        RTMemFree(pThis);
    return rc;

}


RTDECL(int) RTLdrLxSetSegmentSelectors(RTLDRMOD hLdrMod, uint32_t iSegment, uint16_t Sel16bit, uint16_t SelFlat)
{
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    PKLDRMODLX pThis = RT_FROM_MEMBER(hLdrMod, KLDRMODLX, Core);
    AssertReturn(pThis->Core.pOps == &s_rtldrLXOps, VERR_NOT_SUPPORTED);
    AssertReturn(iSegment < pThis->cSegments, VERR_OUT_OF_RANGE);
    pThis->aSegments[iSegment].Sel16bit = Sel16bit;
    pThis->aSegments[iSegment].SelFlat  = SelFlat;
    return VINF_SUCCESS;
}

