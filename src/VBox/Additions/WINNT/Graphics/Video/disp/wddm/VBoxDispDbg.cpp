/* $Id: VBoxDispDbg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo Display D3D User mode dll
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include "VBoxDispD3DCmn.h"

#ifdef VBOXWDDMDISP_DEBUG_VEHANDLER
# include <Psapi.h>
#endif

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/process.h>
#include <iprt/string.h>

#ifndef IPRT_NO_CRT
# include <stdio.h>
#endif


#if defined(VBOXWDDMDISP_DEBUG) || defined(LOG_TO_BACKDOOR_DRV)
static const char *vboxVDbgDoGetExeName(void)
{
# ifdef IPRT_NO_CRT
    /** @todo use RTProcShortName instead?   */
    return RTProcExecutablePath(); /* should've been initialized by nocrt-startup-dll-win.cpp already */
# else
    static bool volatile s_fInitialized = false;
    static char          s_szExePath[MAX_PATH];
    if (!s_fInitialized)
    {
        DWORD cName = GetModuleFileNameA(NULL, s_szExePath, RT_ELEMENTS(s_szExePath));
        if (!cName)
        {
#  ifdef LOG_ENABLED
            DWORD winEr = GetLastError();
#  endif
            WARN(("GetModuleFileNameA failed, winEr %d", winEr));
            return NULL;
        }
        s_fInitialized = TRUE;
    }
    return s_szExePath;
#endif /* !IPRT_NO_CRT */
}

static void vboxDispLogDbgFormatStringV(char *pszBuffer, uint32_t cbBuffer, const char *pszFormat, va_list va)
{
# ifdef IPRT_NO_CRT
    va_list vaCopy;
    va_copy(vaCopy, va); /* The &va for a %N must not be taken from an parameter list, so copy it onto the stack. */
    RTStrPrintf(pszBuffer, cbBuffer, "['%s' 0x%lx.0x%lx] Disp: %N",
                vboxVDbgDoGetExeName(), GetCurrentProcessId(), GetCurrentThreadId(), pszFormat, &vaCopy);
    va_end(vaCopy);
# else
    int cch = _snprintf(pszBuffer, cbBuffer, "['%s' 0x%lx.0x%lx] Disp: ",
                        vboxVDbgDoGetExeName(), GetCurrentProcessId(), GetCurrentThreadId());
    AssertReturnVoid(cch > 0);
    AssertReturnVoid((unsigned)cch + 2 <= cbBuffer);
    cbBuffer  -= (unsigned)cch;
    pszBuffer += (unsigned)cch;

    cch = _vsnprintf(pszBuffer, cbBuffer, pszFormat, va);
    pszBuffer[cbBuffer - 1] = '\0'; /* Don't trust _vsnprintf to terminate the output. */
# endif
}

#endif /* VBOXWDDMDISP_DEBUG || LOG_TO_BACKDOOR_DRV */
#if defined(VBOXWDDMDISP_DEBUG)
LONG g_VBoxVDbgFIsDwm = -1;

DWORD g_VBoxVDbgPid = 0;

DWORD g_VBoxVDbgFLogRel = 1;
# if !defined(VBOXWDDMDISP_DEBUG)
DWORD g_VBoxVDbgFLog = 0;
# else
DWORD g_VBoxVDbgFLog = 1;
# endif
DWORD g_VBoxVDbgFLogFlow = 0;

#endif

#ifdef VBOXWDDMDISP_DEBUG

#define VBOXWDDMDISP_DEBUG_DUMP_DEFAULT 0
DWORD g_VBoxVDbgFDumpSetTexture = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpDrawPrim = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpTexBlt = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpBlt = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpRtSynch = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpFlush = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpShared = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpLock = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpUnlock = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpPresentEnter = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpPresentLeave = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFDumpScSync = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;

DWORD g_VBoxVDbgFBreakShared = VBOXWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_VBoxVDbgFBreakDdi = 0;

DWORD g_VBoxVDbgFCheckSysMemSync = 0;
DWORD g_VBoxVDbgFCheckBlt = 0;
DWORD g_VBoxVDbgFCheckTexBlt = 0;
DWORD g_VBoxVDbgFCheckScSync = 0;

DWORD g_VBoxVDbgFSkipCheckTexBltDwmWndUpdate = 1;

DWORD g_VBoxVDbgCfgMaxDirectRts = 3;
DWORD g_VBoxVDbgCfgForceDummyDevCreate = 0;

PVBOXWDDMDISP_DEVICE g_VBoxVDbgInternalDevice = NULL;
PVBOXWDDMDISP_RESOURCE g_VBoxVDbgInternalRc = NULL;

VOID vboxVDbgDoPrintDmlCmd(const char *pszDesc, const char *pszCmd)
{
#ifdef IPRT_NO_CRT
    /* DML isn't exactly following XML rules, docs + examples are not consistent
       about escaping sequences even. But assume it groks the typical XML
       escape sequences for now. */
    vboxVDbgPrint(("<?dml?><exec cmd=\"%RMas\">%RMes</exec>, ( %s )\n", pszCmd, pszDesc, pszCmd)); /** @todo escape the last pszCmd too? */
#else
    vboxVDbgPrint(("<?dml?><exec cmd=\"%s\">%s</exec>, ( %s )\n", pszCmd, pszDesc, pszCmd));
#endif
}

VOID vboxVDbgDoPrintDumpCmd(const char* pszDesc, const void *pvData, uint32_t width, uint32_t height, uint32_t bpp, uint32_t pitch)
{
    char szCmd[1024];
#ifdef IPRT_NO_CRT
    RTStrPrintf(szCmd, sizeof(szCmd), "!vbvdbg.ms 0x%p 0n%d 0n%d 0n%d 0n%d", pvData, width, height, bpp, pitch);
#else
    _snprintf(szCmd, sizeof(szCmd), "!vbvdbg.ms 0x%p 0n%d 0n%d 0n%d 0n%d", pvData, width, height, bpp, pitch);
    szCmd[sizeof(szCmd) - 1] = '\0';
#endif
    vboxVDbgDoPrintDmlCmd(pszDesc, szCmd);
}

VOID vboxVDbgDoPrintLopLastCmd(const char* pszDesc)
{
    vboxVDbgDoPrintDmlCmd(pszDesc, "ed @@(&vboxVDbgLoop) 0");
}

typedef struct VBOXVDBG_DUMP_INFO
{
    DWORD fFlags;
    const VBOXWDDMDISP_ALLOCATION *pAlloc;
    IDirect3DResource9 *pD3DRc;
    const RECT *pRect;
} VBOXVDBG_DUMP_INFO, *PVBOXVDBG_DUMP_INFO;

typedef DECLCALLBACKTYPE(void, FNVBOXVDBG_CONTENTS_DUMPER,(PVBOXVDBG_DUMP_INFO pInfo, BOOLEAN fBreak, void *pvDumper));
typedef FNVBOXVDBG_CONTENTS_DUMPER *PFNVBOXVDBG_CONTENTS_DUMPER;

static VOID vboxVDbgDoDumpSummary(const char * pPrefix, PVBOXVDBG_DUMP_INFO pInfo, const char * pSuffix)
{
    const VBOXWDDMDISP_ALLOCATION *pAlloc = pInfo->pAlloc;
    IDirect3DResource9 *pD3DRc = pInfo->pD3DRc;
    const RECT * const  pRect  = pInfo->pRect;
    char szRectBuf[24];
    if (pRect)
#ifdef IPRT_NO_CRT
        RTStrPrintf(szRectBuf, sizeof(szRectBuf), "(%ld:%ld);(%ld:%ld)", pRect->left, pRect->top, pRect->right, pRect->bottom);
#else
        _snprintf(szRectBuf, sizeof(szRectBuf), "(%ld:%ld);(%ld:%ld)", pRect->left, pRect->top, pRect->right, pRect->bottom);
#endif
    else
        strcpy(szRectBuf, "n/a");

    vboxVDbgPrint(("%s Sh(0x%p), Rc(0x%p), pAlloc(0x%x), pD3DIf(0x%p), Type(%s), Rect(%s), Locks(%d) %s",
                    pPrefix ? pPrefix : "",
                    pAlloc ? pAlloc->pRc->aAllocations[0].hSharedHandle : NULL,
                    pAlloc ? pAlloc->pRc : NULL,
                    pAlloc,
                    pD3DRc,
                    pD3DRc ? vboxDispLogD3DRcType(pD3DRc->GetType()) : "n/a",
                    szRectBuf,
                    pAlloc ? pAlloc->LockInfo.cLocks : 0,
                    pSuffix ? pSuffix : ""));
}

VOID vboxVDbgDoDumpPerform(const char * pPrefix, PVBOXVDBG_DUMP_INFO pInfo, const char * pSuffix,
        PFNVBOXVDBG_CONTENTS_DUMPER pfnCd, void *pvCd)
{
    DWORD fFlags = pInfo->fFlags;

    if (!VBOXVDBG_DUMP_TYPE_ENABLED_FOR_INFO(pInfo, fFlags))
        return;

    if (!pInfo->pD3DRc && pInfo->pAlloc)
        pInfo->pD3DRc = (IDirect3DResource9*)pInfo->pAlloc->pD3DIf;

    BOOLEAN bLogOnly = VBOXVDBG_DUMP_TYPE_FLOW_ONLY(fFlags);
    if (bLogOnly || !pfnCd)
    {
        vboxVDbgDoDumpSummary(pPrefix, pInfo, pSuffix);
        if (VBOXVDBG_DUMP_FLAGS_IS_SET(fFlags, VBOXVDBG_DUMP_TYPEF_BREAK_ON_FLOW)
                || (!bLogOnly && VBOXVDBG_DUMP_FLAGS_IS_CLEARED(fFlags, VBOXVDBG_DUMP_TYPEF_DONT_BREAK_ON_CONTENTS)))
            Assert(0);
        return;
    }

    vboxVDbgDoDumpSummary(pPrefix, pInfo, NULL);

    pfnCd(pInfo, VBOXVDBG_DUMP_FLAGS_IS_CLEARED(fFlags, VBOXVDBG_DUMP_TYPEF_DONT_BREAK_ON_CONTENTS), pvCd);

    if (pSuffix && pSuffix[0] != '\0')
        vboxVDbgPrint(("%s", pSuffix));
}

static DECLCALLBACK(void) vboxVDbgAllocRectContentsDumperCb(PVBOXVDBG_DUMP_INFO pInfo, BOOLEAN fBreak, void *pvDumper)
{
    RT_NOREF(fBreak, pvDumper);
    const VBOXWDDMDISP_ALLOCATION *pAlloc = pInfo->pAlloc;
    const RECT *pRect = pInfo->pRect;

    Assert(pAlloc->hAllocation);

    D3DDDICB_LOCK LockData;
    LockData.hAllocation = pAlloc->hAllocation;
    LockData.PrivateDriverData = 0;
    LockData.NumPages = 0;
    LockData.pPages = NULL;
    LockData.pData = NULL; /* out */
    LockData.Flags.Value = 0;
    LockData.Flags.LockEntire =1;
    LockData.Flags.ReadOnly = 1;

    PVBOXWDDMDISP_DEVICE pDevice = pAlloc->pRc->pDevice;

    HRESULT hr = pDevice->RtCallbacks.pfnLockCb(pDevice->hDevice, &LockData);
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        UINT bpp = vboxWddmCalcBitsPerPixel(pAlloc->SurfDesc.format);
        vboxVDbgDoPrintDumpCmd("Surf Info", LockData.pData, pAlloc->SurfDesc.d3dWidth, pAlloc->SurfDesc.height, bpp, pAlloc->SurfDesc.pitch);
        if (pRect)
        {
            Assert(pRect->right > pRect->left);
            Assert(pRect->bottom > pRect->top);
            vboxVDbgDoPrintRect("rect: ", pRect, "\n");
            vboxVDbgDoPrintDumpCmd("Rect Info", ((uint8_t*)LockData.pData) + (pRect->top * pAlloc->SurfDesc.pitch) + ((pRect->left * bpp) >> 3),
                    pRect->right - pRect->left, pRect->bottom - pRect->top, bpp, pAlloc->SurfDesc.pitch);
        }
        Assert(0);

        D3DDDICB_UNLOCK DdiUnlock;

        DdiUnlock.NumAllocations = 1;
        DdiUnlock.phAllocations = &pAlloc->hAllocation;

        hr = pDevice->RtCallbacks.pfnUnlockCb(pDevice->hDevice, &DdiUnlock);
        Assert(hr == S_OK);
    }
}

VOID vboxVDbgDoDumpAllocRect(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc, RECT *pRect, const char* pSuffix, DWORD fFlags)
{
    VBOXVDBG_DUMP_INFO Info;
    Info.fFlags = fFlags;
    Info.pAlloc = pAlloc;
    Info.pD3DRc = NULL;
    Info.pRect = pRect;
    vboxVDbgDoDumpPerform(pPrefix, &Info, pSuffix, vboxVDbgAllocRectContentsDumperCb, NULL);
}

static DECLCALLBACK(void) vboxVDbgRcRectContentsDumperCb(PVBOXVDBG_DUMP_INFO pInfo, BOOLEAN fBreak, void *pvDumper)
{
    RT_NOREF(pvDumper);
    const VBOXWDDMDISP_ALLOCATION *pAlloc = pInfo->pAlloc;
    const RECT *pRect = pInfo->pRect;
    IDirect3DSurface9 *pSurf;
    HRESULT hr = VBoxD3DIfSurfGet(pAlloc->pRc, pAlloc->iAlloc, &pSurf);
    if (hr != S_OK)
    {
        WARN(("VBoxD3DIfSurfGet failed, hr 0x%x", hr));
        return;
    }

    D3DSURFACE_DESC Desc;
    hr = pSurf->GetDesc(&Desc);
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        D3DLOCKED_RECT Lr;
        hr = pSurf->LockRect(&Lr, NULL, D3DLOCK_READONLY);
        Assert(hr == S_OK);
        if (hr == S_OK)
        {
            UINT bpp = vboxWddmCalcBitsPerPixel((D3DDDIFORMAT)Desc.Format);
            vboxVDbgDoPrintDumpCmd("Surf Info", Lr.pBits, Desc.Width, Desc.Height, bpp, Lr.Pitch);
            if (pRect)
            {
                Assert(pRect->right > pRect->left);
                Assert(pRect->bottom > pRect->top);
                vboxVDbgDoPrintRect("rect: ", pRect, "\n");
                vboxVDbgDoPrintDumpCmd("Rect Info", ((uint8_t*)Lr.pBits) + (pRect->top * Lr.Pitch) + ((pRect->left * bpp) >> 3),
                        pRect->right - pRect->left, pRect->bottom - pRect->top, bpp, Lr.Pitch);
            }

            if (fBreak)
            {
                Assert(0);
            }
            hr = pSurf->UnlockRect();
            Assert(hr == S_OK);
        }
    }

    pSurf->Release();
}

VOID vboxVDbgDoDumpRcRect(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc,
        IDirect3DResource9 *pD3DRc, RECT *pRect, const char * pSuffix, DWORD fFlags)
{
    VBOXVDBG_DUMP_INFO Info;
    Info.fFlags = fFlags;
    Info.pAlloc = pAlloc;
    Info.pD3DRc = pD3DRc;
    Info.pRect = pRect;
    vboxVDbgDoDumpPerform(pPrefix, &Info, pSuffix, vboxVDbgRcRectContentsDumperCb, NULL);
}


#define VBOXVDBG_STRCASE(_t) \
        case _t: return #_t;
#define VBOXVDBG_STRCASE_UNKNOWN() \
        default: Assert(0); return "Unknown";

const char* vboxVDbgStrCubeFaceType(D3DCUBEMAP_FACES enmFace)
{
    switch (enmFace)
    {
    VBOXVDBG_STRCASE(D3DCUBEMAP_FACE_POSITIVE_X);
    VBOXVDBG_STRCASE(D3DCUBEMAP_FACE_NEGATIVE_X);
    VBOXVDBG_STRCASE(D3DCUBEMAP_FACE_POSITIVE_Y);
    VBOXVDBG_STRCASE(D3DCUBEMAP_FACE_NEGATIVE_Y);
    VBOXVDBG_STRCASE(D3DCUBEMAP_FACE_POSITIVE_Z);
    VBOXVDBG_STRCASE(D3DCUBEMAP_FACE_NEGATIVE_Z);
    VBOXVDBG_STRCASE_UNKNOWN();
    }
}

VOID vboxVDbgDoDumpRt(const char * pPrefix, PVBOXWDDMDISP_DEVICE pDevice, const char * pSuffix, DWORD fFlags)
{
    for (UINT i = 0; i < pDevice->cRTs; ++i)
    {
        IDirect3DSurface9 *pRt;
        PVBOXWDDMDISP_ALLOCATION pAlloc = pDevice->apRTs[i];
        if (!pAlloc) continue;
        IDirect3DDevice9 *pDeviceIf = pDevice->pDevice9If;
        HRESULT hr = pDeviceIf->GetRenderTarget(i, &pRt);
        Assert(hr == S_OK);
        if (hr == S_OK)
        {
//            Assert(pAlloc->pD3DIf == pRt);
            vboxVDbgDoDumpRcRect(pPrefix, pAlloc, NULL, NULL, pSuffix, fFlags);
            pRt->Release();
        }
        else
        {
            vboxVDbgPrint((__FUNCTION__": ERROR getting rt: 0x%x", hr));
        }
    }
}

VOID vboxVDbgDoDumpSamplers(const char * pPrefix, PVBOXWDDMDISP_DEVICE pDevice, const char * pSuffix, DWORD fFlags)
{
    for (UINT i = 0, iSampler = 0; iSampler < pDevice->cSamplerTextures; ++i)
    {
        Assert(i < RT_ELEMENTS(pDevice->aSamplerTextures));
        if (!pDevice->aSamplerTextures[i]) continue;
        PVBOXWDDMDISP_RESOURCE pRc = pDevice->aSamplerTextures[i];
        for (UINT j = 0; j < pRc->cAllocations; ++j)
        {
            PVBOXWDDMDISP_ALLOCATION pAlloc = &pRc->aAllocations[j];
            vboxVDbgDoDumpRcRect(pPrefix, pAlloc, NULL, NULL, pSuffix, fFlags);
        }
        ++iSampler;
    }
}

static DECLCALLBACK(void) vboxVDbgLockUnlockSurfTexContentsDumperCb(PVBOXVDBG_DUMP_INFO pInfo, BOOLEAN fBreak, void *pvDumper)
{
    RT_NOREF(pvDumper);
    const VBOXWDDMDISP_ALLOCATION *pAlloc = pInfo->pAlloc;
    const RECT *pRect = pInfo->pRect;
    UINT bpp = vboxWddmCalcBitsPerPixel(pAlloc->SurfDesc.format);
    uint32_t width, height, pitch = 0;
    void *pvData;
    if (pAlloc->LockInfo.fFlags.AreaValid)
    {
        width = pAlloc->LockInfo.Area.left - pAlloc->LockInfo.Area.right;
        height = pAlloc->LockInfo.Area.bottom - pAlloc->LockInfo.Area.top;
    }
    else
    {
        width = pAlloc->SurfDesc.width;
        height = pAlloc->SurfDesc.height;
    }

    if (pAlloc->LockInfo.fFlags.NotifyOnly)
    {
        pitch = pAlloc->SurfDesc.pitch;
        pvData = ((uint8_t*)pAlloc->pvMem) + pitch*pRect->top + ((bpp*pRect->left) >> 3);
    }
    else
    {
        pvData = pAlloc->LockInfo.pvData;
    }

    vboxVDbgDoPrintDumpCmd("Surf Info", pvData, width, height, bpp, pitch);

    if (fBreak)
    {
        Assert(0);
    }
}

VOID vboxVDbgDoDumpLockUnlockSurfTex(const char * pPrefix, const VBOXWDDMDISP_ALLOCATION *pAlloc, const char * pSuffix, DWORD fFlags)
{
    Assert(!pAlloc->hSharedHandle);

    RECT Rect;
    const RECT *pRect;
    Assert(!pAlloc->LockInfo.fFlags.RangeValid);
    Assert(!pAlloc->LockInfo.fFlags.BoxValid);
    if (pAlloc->LockInfo.fFlags.AreaValid)
    {
        pRect = &pAlloc->LockInfo.Area;
    }
    else
    {
        Rect.top = 0;
        Rect.bottom = pAlloc->SurfDesc.height;
        Rect.left = 0;
        Rect.right = pAlloc->SurfDesc.width;
        pRect = &Rect;
    }

    VBOXVDBG_DUMP_INFO Info;
    Info.fFlags = fFlags;
    Info.pAlloc = pAlloc;
    Info.pD3DRc = NULL;
    Info.pRect = pRect;
    vboxVDbgDoDumpPerform(pPrefix, &Info, pSuffix, vboxVDbgLockUnlockSurfTexContentsDumperCb, NULL);
}

VOID vboxVDbgDoDumpLockSurfTex(const char * pPrefix, const D3DDDIARG_LOCK* pData, const char * pSuffix, DWORD fFlags)
{
    const VBOXWDDMDISP_RESOURCE *pRc = (const VBOXWDDMDISP_RESOURCE*)pData->hResource;
    const VBOXWDDMDISP_ALLOCATION *pAlloc = &pRc->aAllocations[pData->SubResourceIndex];
#ifdef VBOXWDDMDISP_DEBUG
    VBOXWDDMDISP_ALLOCATION *pUnconstpAlloc = (VBOXWDDMDISP_ALLOCATION *)pAlloc;
    pUnconstpAlloc->LockInfo.pvData = pData->pSurfData;
#endif
    vboxVDbgDoDumpLockUnlockSurfTex(pPrefix, pAlloc, pSuffix, fFlags);
}

VOID vboxVDbgDoDumpUnlockSurfTex(const char * pPrefix, const D3DDDIARG_UNLOCK* pData, const char * pSuffix, DWORD fFlags)
{
    const VBOXWDDMDISP_RESOURCE *pRc = (const VBOXWDDMDISP_RESOURCE*)pData->hResource;
    const VBOXWDDMDISP_ALLOCATION *pAlloc = &pRc->aAllocations[pData->SubResourceIndex];
    vboxVDbgDoDumpLockUnlockSurfTex(pPrefix, pAlloc, pSuffix, fFlags);
}

BOOL vboxVDbgDoCheckLRects(D3DLOCKED_RECT *pDstLRect, const RECT *pDstRect, D3DLOCKED_RECT *pSrcLRect, const RECT *pSrcRect, DWORD bpp, BOOL fBreakOnMismatch)
{
    LONG DstH, DstW, SrcH, SrcW, DstWBytes;
    BOOL fMatch = FALSE;
    DstH = pDstRect->bottom - pDstRect->top;
    DstW = pDstRect->right - pDstRect->left;
    SrcH = pSrcRect->bottom - pSrcRect->top;
    SrcW = pSrcRect->right - pSrcRect->left;

    DstWBytes = ((DstW * bpp + 7) >> 3);

    if(DstW != SrcW && DstH != SrcH)
    {
        WARN(("stretched comparison not supported!!"));
        return FALSE;
    }

    uint8_t *pDst = (uint8_t*)pDstLRect->pBits;
    uint8_t *pSrc = (uint8_t*)pSrcLRect->pBits;
    for (LONG i = 0; i < DstH; ++i)
    {
        if (!(fMatch = !memcmp(pDst, pSrc, DstWBytes)))
        {
            vboxVDbgPrint(("not match!\n"));
            if (fBreakOnMismatch)
                Assert(0);
            break;
        }
        pDst += pDstLRect->Pitch;
        pSrc += pSrcLRect->Pitch;
    }
    return fMatch;
}

BOOL vboxVDbgDoCheckRectsMatch(const VBOXWDDMDISP_RESOURCE *pDstRc, uint32_t iDstAlloc,
                            const VBOXWDDMDISP_RESOURCE *pSrcRc, uint32_t iSrcAlloc,
                            const RECT *pDstRect,
                            const RECT *pSrcRect,
                            BOOL fBreakOnMismatch)
{
    BOOL fMatch = FALSE;
    RECT DstRect = {0}, SrcRect = {0};
    if (!pDstRect)
    {
        DstRect.left = 0;
        DstRect.right = pDstRc->aAllocations[iDstAlloc].SurfDesc.width;
        DstRect.top = 0;
        DstRect.bottom = pDstRc->aAllocations[iDstAlloc].SurfDesc.height;
        pDstRect = &DstRect;
    }

    if (!pSrcRect)
    {
        SrcRect.left = 0;
        SrcRect.right = pSrcRc->aAllocations[iSrcAlloc].SurfDesc.width;
        SrcRect.top = 0;
        SrcRect.bottom = pSrcRc->aAllocations[iSrcAlloc].SurfDesc.height;
        pSrcRect = &SrcRect;
    }

    if (pDstRc == pSrcRc
            && iDstAlloc == iSrcAlloc)
    {
        if (!memcmp(pDstRect, pSrcRect, sizeof (*pDstRect)))
        {
            vboxVDbgPrint(("matching same rect of one allocation, skipping..\n"));
            return TRUE;
        }
        WARN(("matching different rects of the same allocation, unsupported!"));
        return FALSE;
    }

    if (pDstRc->RcDesc.enmFormat != pSrcRc->RcDesc.enmFormat)
    {
        WARN(("matching different formats, unsupported!"));
        return FALSE;
    }

    DWORD bpp = pDstRc->aAllocations[iDstAlloc].SurfDesc.bpp;
    if (!bpp)
    {
        WARN(("uninited bpp! unsupported!"));
        return FALSE;
    }

    LONG DstH, DstW, SrcH, SrcW;
    DstH = pDstRect->bottom - pDstRect->top;
    DstW = pDstRect->right - pDstRect->left;
    SrcH = pSrcRect->bottom - pSrcRect->top;
    SrcW = pSrcRect->right - pSrcRect->left;

    if(DstW != SrcW && DstH != SrcH)
    {
        WARN(("stretched comparison not supported!!"));
        return FALSE;
    }

    D3DLOCKED_RECT SrcLRect, DstLRect;
    HRESULT hr = VBoxD3DIfLockRect((VBOXWDDMDISP_RESOURCE *)pDstRc, iDstAlloc, &DstLRect, pDstRect, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
        WARN(("VBoxD3DIfLockRect failed, hr(0x%x)", hr));
        return FALSE;
    }

    hr = VBoxD3DIfLockRect((VBOXWDDMDISP_RESOURCE *)pSrcRc, iSrcAlloc, &SrcLRect, pSrcRect, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
        WARN(("VBoxD3DIfLockRect failed, hr(0x%x)", hr));
        hr = VBoxD3DIfUnlockRect((VBOXWDDMDISP_RESOURCE *)pDstRc, iDstAlloc);
        return FALSE;
    }

    fMatch = vboxVDbgDoCheckLRects(&DstLRect, pDstRect, &SrcLRect, pSrcRect, bpp, fBreakOnMismatch);

    hr = VBoxD3DIfUnlockRect((VBOXWDDMDISP_RESOURCE *)pDstRc, iDstAlloc);
    Assert(hr == S_OK);

    hr = VBoxD3DIfUnlockRect((VBOXWDDMDISP_RESOURCE *)pSrcRc, iSrcAlloc);
    Assert(hr == S_OK);

    return fMatch;
}

void vboxVDbgDoPrintAlloc(const char * pPrefix, const VBOXWDDMDISP_RESOURCE *pRc, uint32_t iAlloc, const char * pSuffix)
{
    Assert(pRc->cAllocations > iAlloc);
    const VBOXWDDMDISP_ALLOCATION *pAlloc = &pRc->aAllocations[iAlloc];
    BOOL bPrimary = pRc->RcDesc.fFlags.Primary;
    BOOL bFrontBuf = FALSE;
    vboxVDbgPrint(("%s d3dWidth(%d), width(%d), height(%d), format(%d), usage(%s), %s", pPrefix,
            pAlloc->SurfDesc.d3dWidth, pAlloc->SurfDesc.width, pAlloc->SurfDesc.height, pAlloc->SurfDesc.format,
            bPrimary ?
                    (bFrontBuf ? "Front Buffer" : "Back Buffer")
                    : "?Everage? Alloc",
            pSuffix));
}

void vboxVDbgDoPrintRect(const char * pPrefix, const RECT *pRect, const char * pSuffix)
{
    vboxVDbgPrint(("%s left(%d), top(%d), right(%d), bottom(%d) %s", pPrefix, pRect->left, pRect->top, pRect->right, pRect->bottom, pSuffix));
}

static VOID CALLBACK vboxVDbgTimerCb(__in PVOID lpParameter, __in BOOLEAN TimerOrWaitFired) RT_NOTHROW_DEF
{
    RT_NOREF(lpParameter, TimerOrWaitFired);
    Assert(0);
}

HRESULT vboxVDbgTimerStart(HANDLE hTimerQueue, HANDLE *phTimer, DWORD msTimeout)
{
    if (!CreateTimerQueueTimer(phTimer, hTimerQueue,
                               vboxVDbgTimerCb,
                               NULL,
                               msTimeout, /* ms*/
                               0,
                               WT_EXECUTEONLYONCE))
    {
        DWORD winEr = GetLastError();
        AssertMsgFailed(("CreateTimerQueueTimer failed, winEr (%d)\n", winEr));
        return E_FAIL;
    }
    return S_OK;
}

HRESULT vboxVDbgTimerStop(HANDLE hTimerQueue, HANDLE hTimer)
{
    if (!DeleteTimerQueueTimer(hTimerQueue, hTimer, NULL))
    {
        DWORD winEr = GetLastError();
        AssertMsg(winEr == ERROR_IO_PENDING, ("DeleteTimerQueueTimer failed, winEr (%d)\n", winEr));
    }
    return S_OK;
}
#endif

#if defined(VBOXWDDMDISP_DEBUG)
BOOL vboxVDbgDoCheckExe(const char * pszName)
{
    char const *pszModule = vboxVDbgDoGetExeName();
    if (!pszModule)
        return FALSE;
    size_t cchModule = strlen(pszModule);
    size_t cchName   = strlen(pszName);
    if (cchName > cchModule)
        return FALSE;
# ifdef IPRT_NO_CRT
    if (RTStrICmp(pszName, &pszModule[cchModule - cchName]))
# else
    if (_stricmp(pszName, &pszModule[cchModule - cchName]))
# endif
        return FALSE;
    return TRUE;
}
#endif

#ifdef VBOXWDDMDISP_DEBUG_VEHANDLER

typedef BOOL WINAPI FNGetModuleInformation(HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb);
typedef FNGetModuleInformation *PFNGetModuleInformation;

static PFNGetModuleInformation g_pfnGetModuleInformation = NULL;
static HMODULE g_hModPsapi = NULL;
static PVOID g_VBoxWDbgVEHandler = NULL;

static bool vboxVDbgIsAddressInModule(PVOID pv, const char *pszModuleName)
{
    HMODULE hMod = GetModuleHandleA(pszModuleName);
    if (!hMod)
        return false;

    HANDLE hProcess = GetCurrentProcess();

    if (!g_pfnGetModuleInformation)
        return false;

    MODULEINFO ModuleInfo = {0};
    if (!g_pfnGetModuleInformation(hProcess, hMod, &ModuleInfo, sizeof(ModuleInfo)))
        return false;

    return    (uintptr_t)ModuleInfo.lpBaseOfDll <= (uintptr_t)pv
           && (uintptr_t)pv < (uintptr_t)ModuleInfo.lpBaseOfDll + ModuleInfo.SizeOfImage;
}

static bool vboxVDbgIsExceptionIgnored(PEXCEPTION_RECORD pExceptionRecord)
{
    /* Module (dll) names for GetModuleHandle.
     * Exceptions originated from these modules will be ignored.
     */
    static const char *apszIgnoredModuleNames[] =
    {
        NULL
    };

    int i = 0;
    while (apszIgnoredModuleNames[i])
    {
        if (vboxVDbgIsAddressInModule(pExceptionRecord->ExceptionAddress, apszIgnoredModuleNames[i]))
            return true;

        ++i;
    }

    return false;
}

static LONG WINAPI vboxVDbgVectoredHandler(struct _EXCEPTION_POINTERS *pExceptionInfo) RT_NOTHROW_DEF
{
    static volatile bool g_fAllowIgnore = true; /* Might be changed in kernel debugger. */

    PEXCEPTION_RECORD pExceptionRecord = pExceptionInfo->ExceptionRecord;
    /* PCONTEXT pContextRecord = pExceptionInfo->ContextRecord; */

    switch (pExceptionRecord->ExceptionCode)
    {
        default:
            break;
        case EXCEPTION_BREAKPOINT:
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            if (g_fAllowIgnore && vboxVDbgIsExceptionIgnored(pExceptionRecord))
                break;
            ASMBreakpoint();
            break;
        case 0x40010006: /* OutputDebugStringA? */
        case 0x4001000a: /* OutputDebugStringW? */
            break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void vboxVDbgVEHandlerRegister()
{
    Assert(!g_VBoxWDbgVEHandler);
    g_VBoxWDbgVEHandler = AddVectoredExceptionHandler(1, vboxVDbgVectoredHandler);
    Assert(g_VBoxWDbgVEHandler);

    g_hModPsapi = GetModuleHandleA("Psapi.dll"); /* Usually already loaded. */
    if (g_hModPsapi)
        g_pfnGetModuleInformation = (PFNGetModuleInformation)GetProcAddress(g_hModPsapi, "GetModuleInformation");
}

void vboxVDbgVEHandlerUnregister()
{
    Assert(g_VBoxWDbgVEHandler);
    ULONG uResult = RemoveVectoredExceptionHandler(g_VBoxWDbgVEHandler);
    Assert(uResult); RT_NOREF(uResult);
    g_VBoxWDbgVEHandler = NULL;

    g_hModPsapi = NULL;
    g_pfnGetModuleInformation = NULL;
}

#endif /* VBOXWDDMDISP_DEBUG_VEHANDLER */
#if defined(VBOXWDDMDISP_DEBUG) || defined(LOG_TO_BACKDOOR_DRV)

void vboxDispLogDrvF(char const *pszFormat, ...)
{
    char szBuffer[4096];
    va_list va;
    va_start(va, pszFormat);
    vboxDispLogDbgFormatStringV(szBuffer, sizeof(szBuffer), pszFormat, va);
    va_end(va);

    VBoxDispMpLoggerLog(szBuffer);
}

void vboxDispLogDbgPrintF(const char *pszFormat, ...)
{
    char szBuffer[4096];
    va_list va;
    va_start(va, pszFormat);
    vboxDispLogDbgFormatStringV(szBuffer, sizeof(szBuffer), pszFormat, va);
    va_end(va);

    OutputDebugStringA(szBuffer);
}

#endif
