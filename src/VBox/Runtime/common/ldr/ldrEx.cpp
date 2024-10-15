/* $Id: ldrEx.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, Extended Features.
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
#include <iprt/dbg.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/md5.h>
#include <iprt/mem.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/formats/mz.h>
#include <iprt/formats/mach-o.h>
#include "internal/ldr.h"

#if defined(LDR_ONLY_PE) || defined(LDR_ONLY_MACHO)
# undef LDR_WITH_PE
# undef LDR_WITH_ELF
# undef LDR_WITH_LX
# undef LDR_WITH_LE
# undef LDR_WITH_MACHO
# undef LDR_WITH_NE
# undef LDR_WITH_MZ
# undef LDR_WITH_AOUT
# ifdef LDR_ONLY_PE
#  define LDR_WITH_PE
# endif
# ifdef LDR_ONLY_MACHO
#  define LDR_WITH_MACHO
# endif
#endif


RTDECL(int) RTLdrOpenWithReader(PRTLDRREADER pReader, uint32_t fFlags, RTLDRARCH enmArch, PRTLDRMOD phMod, PRTERRINFO pErrInfo)
{
    /*
     * Resolve RTLDRARCH_HOST.
     */
    if (enmArch == RTLDRARCH_HOST)
        enmArch = RTLdrGetHostArch();

    /*
     * Read and verify the file signature.
     */
    union
    {
        char        ach[4];
        uint16_t    au16[2];
        uint32_t    u32;
    } uSign;
    int rc = pReader->pfnRead(pReader, &uSign, sizeof(uSign), 0);
    if (RT_FAILURE(rc))
        return rc;
    if (    uSign.au16[0] != IMAGE_DOS_SIGNATURE
        &&  uSign.u32     != IMAGE_NT_SIGNATURE
        &&  uSign.u32     != IMAGE_ELF_SIGNATURE
        &&  uSign.au16[0] != IMAGE_LX_SIGNATURE
        &&  uSign.u32     != IMAGE_MACHO64_SIGNATURE
        &&  uSign.u32     != IMAGE_MACHO64_SIGNATURE_OE
        &&  uSign.u32     != IMAGE_MACHO32_SIGNATURE
        &&  uSign.u32     != IMAGE_MACHO32_SIGNATURE_OE
        &&  uSign.u32     != IMAGE_FAT_SIGNATURE
        &&  uSign.u32     != IMAGE_FAT_SIGNATURE_OE )
    {
        Log(("rtldrOpenWithReader: %s: unknown magic %#x / '%.4s\n", pReader->pfnLogName(pReader), uSign.u32, &uSign.ach[0]));
        return VERR_INVALID_EXE_SIGNATURE;
    }
    uint32_t offHdr = 0;
    if (uSign.au16[0] == IMAGE_DOS_SIGNATURE)
    {
        rc = pReader->pfnRead(pReader, &offHdr, sizeof(offHdr), RT_UOFFSETOF(IMAGE_DOS_HEADER, e_lfanew));
        if (RT_FAILURE(rc))
            return rc;

        if (offHdr <= sizeof(IMAGE_DOS_HEADER))
        {
            Log(("rtldrOpenWithReader: %s: no new header / invalid offset %#RX32\n", pReader->pfnLogName(pReader), offHdr));
            return VERR_INVALID_EXE_SIGNATURE;
        }
        rc = pReader->pfnRead(pReader, &uSign, sizeof(uSign), offHdr);
        if (RT_FAILURE(rc))
            return rc;
        if (    uSign.u32     != IMAGE_NT_SIGNATURE
            &&  uSign.au16[0] != IMAGE_LX_SIGNATURE
            &&  uSign.au16[0] != IMAGE_LE_SIGNATURE
            &&  uSign.au16[0] != IMAGE_NE_SIGNATURE)
        {
            Log(("rtldrOpenWithReader: %s: unknown new magic %#x / '%.4s\n", pReader->pfnLogName(pReader), uSign.u32, &uSign.ach[0]));
            return VERR_INVALID_EXE_SIGNATURE;
        }
    }

    /*
     * Create image interpreter instance depending on the signature.
     */
    if (uSign.u32 == IMAGE_NT_SIGNATURE)
#ifdef LDR_WITH_PE
        rc = rtldrPEOpen(pReader, fFlags, enmArch, offHdr, phMod, pErrInfo);
#else
        rc = VERR_PE_EXE_NOT_SUPPORTED;
#endif
    else if (uSign.u32 == IMAGE_ELF_SIGNATURE)
#if defined(LDR_WITH_ELF)
        rc = rtldrELFOpen(pReader, fFlags, enmArch, phMod, pErrInfo);
#else
        rc = VERR_ELF_EXE_NOT_SUPPORTED;
#endif
    else if (   uSign.u32 == IMAGE_MACHO64_SIGNATURE
             || uSign.u32 == IMAGE_MACHO64_SIGNATURE_OE
             || uSign.u32 == IMAGE_MACHO32_SIGNATURE
             || uSign.u32 == IMAGE_MACHO32_SIGNATURE_OE)
#if defined(LDR_WITH_MACHO)
        rc = rtldrMachOOpen(pReader, fFlags, enmArch, offHdr, phMod, pErrInfo);
#else
        rc = VERR_INVALID_EXE_SIGNATURE;
#endif
    else if (   uSign.u32 == IMAGE_FAT_SIGNATURE
             || uSign.u32 == IMAGE_FAT_SIGNATURE_OE)
#if defined(LDR_WITH_MACHO)
        rc = rtldrFatOpen(pReader, fFlags, enmArch, phMod, pErrInfo);
#else
        rc = VERR_INVALID_EXE_SIGNATURE;
#endif
    else if (uSign.au16[0] == IMAGE_LX_SIGNATURE)
#ifdef LDR_WITH_LX
        rc = rtldrLXOpen(pReader, fFlags, enmArch, offHdr, phMod, pErrInfo);
#else
        rc = VERR_LX_EXE_NOT_SUPPORTED;
#endif
    else if (uSign.au16[0] == IMAGE_LE_SIGNATURE)
#ifdef LDR_WITH_LE
        rc = rtldrLEOpen(pReader, fFlags, enmArch, phMod, pErrInfo);
#else
        rc = VERR_LE_EXE_NOT_SUPPORTED;
#endif
    else if (uSign.au16[0] == IMAGE_NE_SIGNATURE)
#ifdef LDR_WITH_NE
        rc = rtldrNEOpen(pReader, fFlags, enmArch, phMod, pErrInfo);
#else
        rc = VERR_NE_EXE_NOT_SUPPORTED;
#endif
    else if (uSign.au16[0] == IMAGE_DOS_SIGNATURE)
#ifdef LDR_WITH_MZ
        rc = rtldrMZOpen(pReader, fFlags, enmArch, phMod, pErrInfo);
#else
        rc = VERR_MZ_EXE_NOT_SUPPORTED;
#endif
    else if (/*   uSign.u32 == IMAGE_AOUT_A_SIGNATURE
             || uSign.u32 == IMAGE_AOUT_Z_SIGNATURE*/ /** @todo find the aout magics in emx or binutils. */
             0)
#ifdef LDR_WITH_AOUT
        rc = rtldrAOUTOpen(pReader, fFlags, enmArch, phMod, pErrInfo);
#else
        rc = VERR_AOUT_EXE_NOT_SUPPORTED;
#endif
    else
    {
        Log(("rtldrOpenWithReader: %s: the format isn't implemented %#x / '%.4s\n", pReader->pfnLogName(pReader), uSign.u32, &uSign.ach[0]));
        rc = VERR_INVALID_EXE_SIGNATURE;
    }

    LogFlow(("rtldrOpenWithReader: %s: returns %Rrc *phMod=%p\n", pReader->pfnLogName(pReader), rc, *phMod));
    return rc;
}


RTDECL(size_t) RTLdrSize(RTLDRMOD hLdrMod)
{
    LogFlow(("RTLdrSize: hLdrMod=%RTldrm\n", hLdrMod));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), ~(size_t)0);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), ~(size_t)0);

    /*
     * Do it.
     */
    size_t cb = pMod->pOps->pfnGetImageSize(pMod);
    LogFlow(("RTLdrSize: returns %zu\n", cb));
    return cb;
}
RT_EXPORT_SYMBOL(RTLdrSize);


RTDECL(int) RTLdrGetBits(RTLDRMOD hLdrMod, void *pvBits, RTLDRADDR BaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    LogFlow(("RTLdrGetBits: hLdrMod=%RTldrm pvBits=%p BaseAddress=%RTptr pfnGetImport=%p pvUser=%p\n",
             hLdrMod, pvBits, BaseAddress, pfnGetImport, pvUser));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrReturn(pvBits, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnGetImport, VERR_INVALID_POINTER);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    /*
     * Do it.
     */
    int rc = pMod->pOps->pfnGetBits(pMod, pvBits, BaseAddress, pfnGetImport, pvUser);
    LogFlow(("RTLdrGetBits: returns %Rrc\n",rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrGetBits);


RTDECL(int) RTLdrRelocate(RTLDRMOD hLdrMod, void *pvBits, RTLDRADDR NewBaseAddress, RTLDRADDR OldBaseAddress,
                          PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    LogFlow(("RTLdrRelocate: hLdrMod=%RTldrm pvBits=%p NewBaseAddress=%RTptr OldBaseAddress=%RTptr pfnGetImport=%p pvUser=%p\n",
             hLdrMod, pvBits, NewBaseAddress, OldBaseAddress, pfnGetImport, pvUser));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrReturn(pvBits, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnGetImport, VERR_INVALID_POINTER);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    /*
     * Do it.
     */
    int rc = pMod->pOps->pfnRelocate(pMod, pvBits, NewBaseAddress, OldBaseAddress, pfnGetImport, pvUser);
    LogFlow(("RTLdrRelocate: returns %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrRelocate);


RTDECL(int) RTLdrGetSymbolEx(RTLDRMOD hLdrMod, const void *pvBits, RTLDRADDR BaseAddress,
                             uint32_t iOrdinal, const char *pszSymbol, PRTLDRADDR pValue)
{
    LogFlow(("RTLdrGetSymbolEx: hLdrMod=%RTldrm pvBits=%p BaseAddress=%RTptr iOrdinal=%#x pszSymbol=%p:{%s} pValue=%p\n",
             hLdrMod, pvBits, BaseAddress, iOrdinal, pszSymbol, pszSymbol, pValue));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pvBits, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pszSymbol, VERR_INVALID_POINTER);
    AssertReturn(pszSymbol || iOrdinal != UINT32_MAX, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pValue, VERR_INVALID_POINTER);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;

    /*
     * Do it.
     */
    int rc;
    if (pMod->pOps->pfnGetSymbolEx)
        rc = pMod->pOps->pfnGetSymbolEx(pMod, pvBits, BaseAddress, iOrdinal, pszSymbol, pValue);
    else if (!BaseAddress && !pvBits && iOrdinal == UINT32_MAX)
    {
        void *pvValue;
        rc = pMod->pOps->pfnGetSymbol(pMod, pszSymbol, &pvValue);
        if (RT_SUCCESS(rc))
            *pValue = (uintptr_t)pvValue;
    }
    else
        AssertMsgFailedReturn(("BaseAddress=%RTptr pvBits=%p\n", BaseAddress, pvBits), VERR_INVALID_FUNCTION);
    LogFlow(("RTLdrGetSymbolEx: returns %Rrc *pValue=%p\n", rc, *pValue));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrGetSymbolEx);


RTDECL(int) RTLdrQueryForwarderInfo(RTLDRMOD hLdrMod, const void *pvBits, uint32_t iOrdinal, const char *pszSymbol,
                                    PRTLDRIMPORTINFO pInfo, size_t cbInfo)
{
    LogFlow(("RTLdrQueryForwarderInfo: hLdrMod=%RTldrm pvBits=%p iOrdinal=%#x pszSymbol=%p:{%s} pInfo=%p cbInfo=%zu\n",
             hLdrMod, pvBits, iOrdinal, pszSymbol, pszSymbol, pInfo, cbInfo));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pvBits, VERR_INVALID_POINTER);
    AssertMsgReturn(pszSymbol, ("pszSymbol=%p\n", pszSymbol), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pInfo, VERR_INVALID_PARAMETER);
    AssertReturn(cbInfo >= sizeof(*pInfo), VERR_INVALID_PARAMETER);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;

    /*
     * Do it.
     */
    int rc;
    if (pMod->pOps->pfnQueryForwarderInfo)
    {
        rc = pMod->pOps->pfnQueryForwarderInfo(pMod, pvBits, iOrdinal, pszSymbol, pInfo, cbInfo);
        if (RT_SUCCESS(rc))
            LogFlow(("RTLdrQueryForwarderInfo: returns %Rrc pInfo={%#x,%#x,%s,%s}\n", rc,
                     pInfo->iSelfOrdinal, pInfo->iOrdinal, pInfo->pszSymbol, pInfo->szModule));
        else
            LogFlow(("RTLdrQueryForwarderInfo: returns %Rrc\n", rc));
    }
    else
    {
        LogFlow(("RTLdrQueryForwarderInfo: returns VERR_NOT_SUPPORTED\n"));
        rc = VERR_NOT_SUPPORTED;
    }
    return rc;

}
RT_EXPORT_SYMBOL(RTLdrQueryForwarderInfo);


RTDECL(int) RTLdrEnumSymbols(RTLDRMOD hLdrMod, unsigned fFlags, const void *pvBits, RTLDRADDR BaseAddress,
                             PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    LogFlow(("RTLdrEnumSymbols: hLdrMod=%RTldrm fFlags=%#x pvBits=%p BaseAddress=%RTptr pfnCallback=%p pvUser=%p\n",
             hLdrMod, fFlags, pvBits, BaseAddress, pfnCallback, pvUser));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pvBits, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    //AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    /*
     * Do it.
     */
    int rc = pMod->pOps->pfnEnumSymbols(pMod, fFlags, pvBits, BaseAddress, pfnCallback, pvUser);
    LogFlow(("RTLdrEnumSymbols: returns %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrEnumSymbols);


RTDECL(int) RTLdrEnumDbgInfo(RTLDRMOD hLdrMod, const void *pvBits, PFNRTLDRENUMDBG pfnCallback, void *pvUser)
{
    LogFlow(("RTLdrEnumDbgInfo: hLdrMod=%RTldrm pvBits=%p pfnCallback=%p pvUser=%p\n",
             hLdrMod, pvBits, pfnCallback, pvUser));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pvBits, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    //AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    /*
     * Do it.
     */
    int rc;
    if (pMod->pOps->pfnEnumDbgInfo)
        rc = pMod->pOps->pfnEnumDbgInfo(pMod, pvBits, pfnCallback, pvUser);
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlow(("RTLdrEnumDbgInfo: returns %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrEnumDbgInfo);


RTDECL(int) RTLdrEnumSegments(RTLDRMOD hLdrMod, PFNRTLDRENUMSEGS pfnCallback, void *pvUser)
{
    LogFlow(("RTLdrEnumSegments: hLdrMod=%RTldrm pfnCallback=%p pvUser=%p\n",
             hLdrMod, pfnCallback, pvUser));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    //AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    /*
     * Do it.
     */
    int rc;
    if (pMod->pOps->pfnEnumSegments)
        rc = pMod->pOps->pfnEnumSegments(pMod, pfnCallback, pvUser);
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlow(("RTLdrEnumSegments: returns %Rrc\n", rc));
    return rc;

}
RT_EXPORT_SYMBOL(RTLdrEnumSegments);


RTDECL(int) RTLdrLinkAddressToSegOffset(RTLDRMOD hLdrMod, RTLDRADDR LinkAddress, uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    LogFlow(("RTLdrLinkAddressToSegOffset: hLdrMod=%RTldrm LinkAddress=%RTptr piSeg=%p poffSeg=%p\n",
             hLdrMod, LinkAddress, piSeg, poffSeg));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrReturn(piSeg, VERR_INVALID_POINTER);
    AssertPtrReturn(poffSeg, VERR_INVALID_POINTER);

    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    //AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    *piSeg   = UINT32_MAX;
    *poffSeg = ~(RTLDRADDR)0;

    /*
     * Do it.
     */
    int rc;
    if (pMod->pOps->pfnLinkAddressToSegOffset)
        rc = pMod->pOps->pfnLinkAddressToSegOffset(pMod, LinkAddress, piSeg, poffSeg);
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlow(("RTLdrLinkAddressToSegOffset: returns %Rrc %#x:%RTptr\n", rc, *piSeg, *poffSeg));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrLinkAddressToSegOffset);


RTDECL(int) RTLdrLinkAddressToRva(RTLDRMOD hLdrMod, RTLDRADDR LinkAddress, PRTLDRADDR pRva)
{
    LogFlow(("RTLdrLinkAddressToRva: hLdrMod=%RTldrm LinkAddress=%RTptr pRva=%p\n",
             hLdrMod, LinkAddress, pRva));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrReturn(pRva, VERR_INVALID_POINTER);

    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    //AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    *pRva = ~(RTLDRADDR)0;

    /*
     * Do it.
     */
    int rc;
    if (pMod->pOps->pfnLinkAddressToRva)
        rc = pMod->pOps->pfnLinkAddressToRva(pMod, LinkAddress, pRva);
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlow(("RTLdrLinkAddressToRva: returns %Rrc %RTptr\n", rc, *pRva));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrLinkAddressToRva);


RTDECL(int) RTLdrSegOffsetToRva(RTLDRMOD hLdrMod, uint32_t iSeg, RTLDRADDR offSeg, PRTLDRADDR pRva)
{
    LogFlow(("RTLdrSegOffsetToRva: hLdrMod=%RTldrm iSeg=%#x offSeg=%RTptr pRva=%p\n", hLdrMod, iSeg, offSeg, pRva));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrReturn(pRva, VERR_INVALID_POINTER);

    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    //AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    *pRva = ~(RTLDRADDR)0;

    /*
     * Do it.
     */
    int rc;
    if (pMod->pOps->pfnSegOffsetToRva)
        rc = pMod->pOps->pfnSegOffsetToRva(pMod, iSeg, offSeg, pRva);
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlow(("RTLdrSegOffsetToRva: returns %Rrc %RTptr\n", rc, *pRva));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrSegOffsetToRva);

RTDECL(int) RTLdrRvaToSegOffset(RTLDRMOD hLdrMod, RTLDRADDR Rva, uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    LogFlow(("RTLdrRvaToSegOffset: hLdrMod=%RTldrm Rva=%RTptr piSeg=%p poffSeg=%p\n",
             hLdrMod, Rva, piSeg, poffSeg));

    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertPtrReturn(piSeg, VERR_INVALID_POINTER);
    AssertPtrReturn(poffSeg, VERR_INVALID_POINTER);

    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    //AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    *piSeg   = UINT32_MAX;
    *poffSeg = ~(RTLDRADDR)0;

    /*
     * Do it.
     */
    int rc;
    if (pMod->pOps->pfnRvaToSegOffset)
        rc = pMod->pOps->pfnRvaToSegOffset(pMod, Rva, piSeg, poffSeg);
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlow(("RTLdrRvaToSegOffset: returns %Rrc %#x:%RTptr\n", rc, *piSeg, *poffSeg));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrRvaToSegOffset);


RTDECL(int) RTLdrQueryProp(RTLDRMOD hLdrMod, RTLDRPROP enmProp, void *pvBuf, size_t cbBuf)
{
    return RTLdrQueryPropEx(hLdrMod, enmProp, NULL /*pvBits*/, pvBuf, cbBuf, NULL);
}
RT_EXPORT_SYMBOL(RTLdrQueryProp);


RTDECL(int) RTLdrQueryPropEx(RTLDRMOD hLdrMod, RTLDRPROP enmProp, void *pvBits, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), RTLDRENDIAN_INVALID);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;

    AssertPtrNullReturn(pcbRet, VERR_INVALID_POINTER);
    size_t cbRet;
    if (!pcbRet)
        pcbRet = &cbRet;

    /*
     * Do some pre screening of the input
     */
    switch (enmProp)
    {
        case RTLDRPROP_UUID:
            *pcbRet = sizeof(RTUUID);
            AssertReturn(cbBuf == sizeof(RTUUID), VERR_INVALID_PARAMETER);
            break;
        case RTLDRPROP_TIMESTAMP_SECONDS:
            *pcbRet = sizeof(int64_t);
            AssertReturn(cbBuf == sizeof(int32_t) || cbBuf == sizeof(int64_t), VERR_INVALID_PARAMETER);
            *pcbRet = cbBuf;
            break;
        case RTLDRPROP_IS_SIGNED:
            *pcbRet = sizeof(bool);
            AssertReturn(cbBuf == sizeof(bool), VERR_INVALID_PARAMETER);
            break;
        case RTLDRPROP_PKCS7_SIGNED_DATA:
        case RTLDRPROP_SHA1_PAGE_HASHES:
        case RTLDRPROP_SHA256_PAGE_HASHES:
            *pcbRet = 0;
            break;
        case RTLDRPROP_HASHABLE_PAGES:
            *pcbRet = sizeof(uint32_t);
            AssertReturn(cbBuf >= sizeof(uint32_t), VERR_INVALID_PARAMETER);
            break;
        case RTLDRPROP_SIGNATURE_CHECKS_ENFORCED:
            *pcbRet = sizeof(bool);
            AssertReturn(cbBuf == sizeof(bool), VERR_INVALID_PARAMETER);
            break;
        case RTLDRPROP_IMPORT_COUNT:
            *pcbRet = sizeof(uint32_t);
            AssertReturn(cbBuf == sizeof(uint32_t), VERR_INVALID_PARAMETER);
            break;
        case RTLDRPROP_IMPORT_MODULE:
            *pcbRet = sizeof(uint32_t);
            AssertReturn(cbBuf >= sizeof(uint32_t), VERR_INVALID_PARAMETER);
            break;
        case RTLDRPROP_FILE_OFF_HEADER:
            *pcbRet = sizeof(uint64_t);
            AssertReturn(cbBuf == sizeof(uint32_t) || cbBuf == sizeof(uint64_t), VERR_INVALID_PARAMETER);
            break;
        case RTLDRPROP_INTERNAL_NAME:
        case RTLDRPROP_UNWIND_TABLE:
            *pcbRet = 0;
            break;

        case RTLDRPROP_UNWIND_INFO:
            AssertReturn(pvBuf, VERR_INVALID_POINTER);
            AssertReturn(cbBuf >= sizeof(uint32_t), VERR_INVALID_PARAMETER);
            *pcbRet = 0;
            break;

        case RTLDRPROP_BUILDID:
            *pcbRet = 0;
            break;

        default:
            AssertFailedReturn(VERR_INVALID_FUNCTION);
    }
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);

    /*
     * Call the image specific worker, if there is one.
     */
    if (!pMod->pOps->pfnQueryProp)
        return VERR_NOT_SUPPORTED;
    return pMod->pOps->pfnQueryProp(pMod, enmProp, pvBits, pvBuf, cbBuf, pcbRet);
}
RT_EXPORT_SYMBOL(RTLdrQueryPropEx);


RTDECL(int) RTLdrVerifySignature(RTLDRMOD hLdrMod, PFNRTLDRVALIDATESIGNEDDATA pfnCallback, void *pvUser, PRTERRINFO pErrInfo)
{
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);

    /*
     * Call the image specific worker, if there is one.
     */
    if (!pMod->pOps->pfnVerifySignature)
        return VERR_NOT_SUPPORTED;
    return pMod->pOps->pfnVerifySignature(pMod, pfnCallback, pvUser, pErrInfo);
}
RT_EXPORT_SYMBOL(RTLdrVerifySignature);


RTDECL(int) RTLdrHashImage(RTLDRMOD hLdrMod, RTDIGESTTYPE enmDigest, uint8_t *pabHash, size_t cbHash)
{
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;

    /*
     * Make sure there is sufficient space for the wanted digest and that
     * it's supported.
     */
    switch (enmDigest)
    {
        case RTDIGESTTYPE_MD5:      AssertReturn(cbHash >= RTMD5_HASH_SIZE,    VERR_BUFFER_OVERFLOW); break;
        case RTDIGESTTYPE_SHA1:     AssertReturn(cbHash >= RTSHA1_HASH_SIZE,   VERR_BUFFER_OVERFLOW); break;
        case RTDIGESTTYPE_SHA256:   AssertReturn(cbHash >= RTSHA256_HASH_SIZE, VERR_BUFFER_OVERFLOW); break;
        case RTDIGESTTYPE_SHA512:   AssertReturn(cbHash >= RTSHA512_HASH_SIZE, VERR_BUFFER_OVERFLOW); break;
        default:
            if (enmDigest > RTDIGESTTYPE_INVALID && enmDigest < RTDIGESTTYPE_END)
                return VERR_NOT_SUPPORTED;
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }
    AssertPtrReturn(pabHash, VERR_INVALID_POINTER);

    /*
     * Call the image specific worker, if there is one.
     */
    if (!pMod->pOps->pfnHashImage)
        return VERR_NOT_SUPPORTED;
    return pMod->pOps->pfnHashImage(pMod, enmDigest, pabHash, cbHash);
}
RT_EXPORT_SYMBOL(RTLdrHashImage);


RTDECL(int) RTLdrUnwindFrame(RTLDRMOD hLdrMod, void const *pvBits, uint32_t iSeg, RTLDRADDR off, PRTDBGUNWINDSTATE pState)
{
    /*
     * Validate.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    AssertPtr(pState);
    AssertReturn(pState->u32Magic == RTDBGUNWINDSTATE_MAGIC, VERR_INVALID_MAGIC);

    /*
     * Pass on the work.
     */
    if (pMod->pOps->pfnUnwindFrame)
        return pMod->pOps->pfnUnwindFrame(pMod, pvBits, iSeg, off, pState);
    return VERR_DBG_NO_UNWIND_INFO;
}
RT_EXPORT_SYMBOL(RTLdrUnwindFrame);


/**
 * Internal method used by the IPRT debug bits.
 *
 * @returns IPRT status code.
 * @param   hLdrMod             The loader handle which executable we wish to
 *                              read from.
 * @param   pvBuf               The output buffer.
 * @param   iDbgInfo            The debug info ordinal number if the request
 *                              corresponds exactly to a debug info part from
 *                              pfnEnumDbgInfo.  Otherwise, pass UINT32_MAX.
 * @param   off                 Where in the executable file to start reading.
 * @param   cb                  The number of bytes to read.
 *
 * @remarks Fixups will only be applied if @a iDbgInfo is specified.
 */
DECLHIDDEN(int) rtLdrReadAt(RTLDRMOD hLdrMod, void *pvBuf, uint32_t iDbgInfo, RTFOFF off, size_t cb)
{
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;

    if (iDbgInfo != UINT32_MAX)
    {
        AssertReturn(pMod->pOps->pfnReadDbgInfo, VERR_NOT_SUPPORTED);
        return pMod->pOps->pfnReadDbgInfo(pMod, iDbgInfo, off, cb, pvBuf);
    }

    AssertReturn(pMod->pReader, VERR_NOT_SUPPORTED);
    return pMod->pReader->pfnRead(pMod->pReader, pvBuf, cb, off);
}


RTDECL(const char *) RTLdrArchName(RTLDRARCH enmArch)
{
    switch (enmArch)
    {
        case RTLDRARCH_INVALID:     return "INVALID";
        case RTLDRARCH_WHATEVER:    return "WHATEVER";
        case RTLDRARCH_HOST:        return "HOST";
        case RTLDRARCH_AMD64:       return "AMD64";
        case RTLDRARCH_X86_16:      return "X86_16";
        case RTLDRARCH_X86_32:      return "X86_32";
        case RTLDRARCH_ARM32:       return "ARM32";
        case RTLDRARCH_ARM64:       return "ARM64";

        case RTLDRARCH_END:
        case RTLDRARCH_32BIT_HACK:
            break;
    }
    return "UNKNOWN";
}
RT_EXPORT_SYMBOL(RTLdrArchName);

