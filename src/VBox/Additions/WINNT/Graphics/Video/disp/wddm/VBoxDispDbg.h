/* $Id: VBoxDispDbg.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispDbg_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispDbg_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define VBOX_VIDEO_LOG_NAME "VBoxD3D"
#define VBOX_VIDEO_LOG_LOGGER vboxVDbgInternalLogLogger
#define VBOX_VIDEO_LOGREL_LOGGER vboxVDbgInternalLogRelLogger
#define VBOX_VIDEO_LOGFLOW_LOGGER vboxVDbgInternalLogFlowLogger
#define VBOX_VIDEO_LOG_FN_FMT "%s"

#include "../../common/VBoxVideoLog.h"

#ifdef DEBUG
/* debugging configuration flags */

/* Adds vectored exception handler to be able to catch non-debug UM exceptions in kernel debugger. */
#define VBOXWDDMDISP_DEBUG_VEHANDLER

/* generic debugging facilities & extra data checks */
# define VBOXWDDMDISP_DEBUG
/* for some reason when debugging with VirtualKD, user-mode DbgPrint's are discarded
 * the workaround so far is to pass the log info to the kernel driver and DbgPrint'ed from there,
 * which is enabled by this define */
//#  define VBOXWDDMDISP_DEBUG_PRINTDRV

/* Uncomment to use OutputDebugString */
//#define VBOXWDDMDISP_DEBUG_PRINT

/* disable shared resource creation with wine */
//#  define VBOXWDDMDISP_DEBUG_NOSHARED

//#  define VBOXWDDMDISP_DEBUG_PRINT_SHARED_CREATE
//#  define VBOXWDDMDISP_DEBUG_TIMER

/* debug config vars */
extern DWORD g_VBoxVDbgFDumpSetTexture;
extern DWORD g_VBoxVDbgFDumpDrawPrim;
extern DWORD g_VBoxVDbgFDumpTexBlt;
extern DWORD g_VBoxVDbgFDumpBlt;
extern DWORD g_VBoxVDbgFDumpRtSynch;
extern DWORD g_VBoxVDbgFDumpFlush;
extern DWORD g_VBoxVDbgFDumpShared;
extern DWORD g_VBoxVDbgFDumpLock;
extern DWORD g_VBoxVDbgFDumpUnlock;
extern DWORD g_VBoxVDbgFDumpPresentEnter;
extern DWORD g_VBoxVDbgFDumpPresentLeave;
extern DWORD g_VBoxVDbgFDumpScSync;

extern DWORD g_VBoxVDbgFBreakShared;
extern DWORD g_VBoxVDbgFBreakDdi;

extern DWORD g_VBoxVDbgFCheckSysMemSync;
extern DWORD g_VBoxVDbgFCheckBlt;
extern DWORD g_VBoxVDbgFCheckTexBlt;
extern DWORD g_VBoxVDbgFCheckScSync;

extern DWORD g_VBoxVDbgFSkipCheckTexBltDwmWndUpdate;

extern DWORD g_VBoxVDbgCfgMaxDirectRts;
extern DWORD g_VBoxVDbgCfgForceDummyDevCreate;

extern struct VBOXWDDMDISP_DEVICE *g_VBoxVDbgInternalDevice;
extern struct VBOXWDDMDISP_RESOURCE *g_VBoxVDbgInternalRc;

#endif

#if defined(VBOXWDDMDISP_DEBUG)
/* log enable flags */
extern DWORD g_VBoxVDbgFLogRel;
extern DWORD g_VBoxVDbgFLog;
extern DWORD g_VBoxVDbgFLogFlow;
#endif

#ifdef VBOXWDDMDISP_DEBUG_VEHANDLER
void vboxVDbgVEHandlerRegister();
void vboxVDbgVEHandlerUnregister();
#endif

#if defined(LOG_TO_BACKDOOR_DRV) || defined(VBOXWDDMDISP_DEBUG_PRINTDRV)
# define DbgPrintDrv(_m) do { vboxDispLogDrvF _m; } while (0)
# define DbgPrintDrvRel(_m) do { vboxDispLogDrvF _m; } while (0)
# define DbgPrintDrvFlow(_m) do { vboxDispLogDrvF _m; } while (0)
#else
# define DbgPrintDrv(_m) do { } while (0)
# define DbgPrintDrvRel(_m) do { } while (0)
# define DbgPrintDrvFlow(_m) do { } while (0)
#endif

#ifdef VBOXWDDMDISP_DEBUG_PRINT
# define DbgPrintUsr(_m) do { vboxDispLogDbgPrintF _m; } while (0)
# define DbgPrintUsrRel(_m) do { vboxDispLogDbgPrintF _m; } while (0)
# define DbgPrintUsrFlow(_m) do { vboxDispLogDbgPrintF _m; } while (0)
#else
# define DbgPrintUsr(_m) do { } while (0)
# define DbgPrintUsrRel(_m) do { } while (0)
# define DbgPrintUsrFlow(_m) do { } while (0)
#endif

#if defined(VBOXWDDMDISP_DEBUG)
#define vboxVDbgInternalLog(_p) if (g_VBoxVDbgFLog) { _p }
#define vboxVDbgInternalLogFlow(_p) if (g_VBoxVDbgFLogFlow) { _p }
#define vboxVDbgInternalLogRel(_p) if (g_VBoxVDbgFLogRel) { _p }
#else
#define vboxVDbgInternalLog(_p) do {} while (0)
#define vboxVDbgInternalLogFlow(_p) do {} while (0)
#define vboxVDbgInternalLogRel(_p) do { _p } while (0)
#endif

/* @todo: remove these from the code and from here */
#define vboxVDbgPrint(_m) LOG_EXACT(_m)
#define vboxVDbgPrintF(_m) LOGF_EXACT(_m)
#define vboxVDbgPrintR(_m)  LOGREL_EXACT(_m)

#define vboxVDbgInternalLogLogger(_m) do { \
        vboxVDbgInternalLog( \
            Log(_m); \
            DbgPrintUsr(_m); \
            DbgPrintDrv(_m); \
        ); \
    } while (0)

#define vboxVDbgInternalLogFlowLogger(_m)  do { \
        vboxVDbgInternalLogFlow( \
            LogFlow(_m); \
            DbgPrintUsrFlow(_m); \
            DbgPrintDrvFlow(_m); \
        ); \
    } while (0)

#define vboxVDbgInternalLogRelLogger(_m)  do { \
        vboxVDbgInternalLogRel( \
            LogRel(_m); \
            DbgPrintUsrRel(_m); \
            DbgPrintDrvRel(_m); \
        ); \
    } while (0)

#if defined(VBOXWDDMDISP_DEBUG)
extern DWORD g_VBoxVDbgPid;
extern LONG g_VBoxVDbgFIsDwm;
#define VBOXVDBG_CHECK_EXE(_pszName) (vboxVDbgDoCheckExe(_pszName))
#define VBOXVDBG_IS_DWM() (!!(g_VBoxVDbgFIsDwm >=0 ? g_VBoxVDbgFIsDwm : (g_VBoxVDbgFIsDwm = VBOXVDBG_CHECK_EXE("dwm.exe"))))
BOOL vboxVDbgDoCheckExe(const char * pszName);
#endif
#if defined(VBOXWDDMDISP_DEBUG) || defined(LOG_TO_BACKDOOR_DRV)

#define VBOXVDBG_STRCASE(_t) \
        case _t: return #_t;
#define VBOXVDBG_STRCASE_UNKNOWN() \
        default: Assert(0); return "Unknown";

DECLINLINE(const char*) vboxDispLogD3DRcType(D3DRESOURCETYPE enmType)
{
    switch (enmType)
    {
        VBOXVDBG_STRCASE(D3DRTYPE_SURFACE);
        VBOXVDBG_STRCASE(D3DRTYPE_VOLUME);
        VBOXVDBG_STRCASE(D3DRTYPE_TEXTURE);
        VBOXVDBG_STRCASE(D3DRTYPE_VOLUMETEXTURE);
        VBOXVDBG_STRCASE(D3DRTYPE_CUBETEXTURE);
        VBOXVDBG_STRCASE(D3DRTYPE_VERTEXBUFFER);
        VBOXVDBG_STRCASE(D3DRTYPE_INDEXBUFFER);
        VBOXVDBG_STRCASE_UNKNOWN();
    }
}

#include <VBoxDispMpLogger.h>

VBOXDISPMPLOGGER_DECL(void) VBoxDispMpLoggerDumpD3DCAPS9(struct _D3DCAPS9 *pCaps);

void vboxDispLogDrvF(const char *pszFormat, ...);

# define vboxDispDumpD3DCAPS9(_pCaps) do { VBoxDispMpLoggerDumpD3DCAPS9(_pCaps); } while (0)
#else
# define vboxDispDumpD3DCAPS9(_pCaps) do { } while (0)
#endif

#ifdef VBOXWDDMDISP_DEBUG

void vboxDispLogDbgPrintF(const char *pszFormat, ...);

typedef struct VBOXWDDMDISP_ALLOCATION *PVBOXWDDMDISP_ALLOCATION;
typedef struct VBOXWDDMDISP_RESOURCE *PVBOXWDDMDISP_RESOURCE;

#define VBOXVDBG_DUMP_TYPEF_FLOW                   0x00000001
#define VBOXVDBG_DUMP_TYPEF_CONTENTS               0x00000002
#define VBOXVDBG_DUMP_TYPEF_DONT_BREAK_ON_CONTENTS 0x00000004
#define VBOXVDBG_DUMP_TYPEF_BREAK_ON_FLOW          0x00000008
#define VBOXVDBG_DUMP_TYPEF_SHARED_ONLY            0x00000010

#define VBOXVDBG_DUMP_FLAGS_IS_SETANY(_fFlags, _Value) (((_fFlags) & (_Value)) != 0)
#define VBOXVDBG_DUMP_FLAGS_IS_SET(_fFlags, _Value) (((_fFlags) & (_Value)) == (_Value))
#define VBOXVDBG_DUMP_FLAGS_IS_CLEARED(_fFlags, _Value) (((_fFlags) & (_Value)) == 0)
#define VBOXVDBG_DUMP_FLAGS_CLEAR(_fFlags, _Value) ((_fFlags) & (~(_Value)))
#define VBOXVDBG_DUMP_FLAGS_SET(_fFlags, _Value) ((_fFlags) | (_Value))

#define VBOXVDBG_DUMP_TYPE_ENABLED(_fFlags) (VBOXVDBG_DUMP_FLAGS_IS_SETANY(_fFlags, VBOXVDBG_DUMP_TYPEF_FLOW | VBOXVDBG_DUMP_TYPEF_CONTENTS))
#define VBOXVDBG_DUMP_TYPE_ENABLED_FOR_INFO(_pInfo, _fFlags) ( \
        VBOXVDBG_DUMP_TYPE_ENABLED(_fFlags) \
        && ( \
                VBOXVDBG_DUMP_FLAGS_IS_CLEARED(_fFlags, VBOXVDBG_DUMP_TYPEF_SHARED_ONLY) \
                || ((_pInfo)->pAlloc && (_pInfo)->pAlloc->pRc->aAllocations[0].hSharedHandle) \
            ))

#define VBOXVDBG_DUMP_TYPE_FLOW_ONLY(_fFlags) (VBOXVDBG_DUMP_FLAGS_IS_SET(_fFlags, VBOXVDBG_DUMP_TYPEF_FLOW) \
        && VBOXVDBG_DUMP_FLAGS_IS_CLEARED(_fFlags, VBOXVDBG_DUMP_TYPEF_CONTENTS))
#define VBOXVDBG_DUMP_TYPE_CONTENTS(_fFlags) (VBOXVDBG_DUMP_FLAGS_IS_SET(_fFlags, VBOXVDBG_DUMP_TYPEF_CONTENTS))
#define VBOXVDBG_DUMP_TYPE_GET_FLOW_ONLY(_fFlags) ( \
        VBOXVDBG_DUMP_FLAGS_SET( \
                VBOXVDBG_DUMP_FLAGS_CLEAR(_fFlags, VBOXVDBG_DUMP_TYPEF_CONTENTS), \
                VBOXVDBG_DUMP_TYPEF_FLOW) \
        )

VOID vboxVDbgDoDumpAllocRect(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc, RECT *pRect, const char* pSuffix, DWORD fFlags);
VOID vboxVDbgDoDumpRcRect(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc, IDirect3DResource9 *pD3DRc, RECT *pRect, const char * pSuffix, DWORD fFlags);
VOID vboxVDbgDoDumpLockUnlockSurfTex(const char * pPrefix, const VBOXWDDMDISP_ALLOCATION *pAlloc, const char * pSuffix, DWORD fFlags);
VOID vboxVDbgDoDumpRt(const char * pPrefix, struct VBOXWDDMDISP_DEVICE *pDevice, const char * pSuffix, DWORD fFlags);
VOID vboxVDbgDoDumpSamplers(const char * pPrefix, struct VBOXWDDMDISP_DEVICE *pDevice, const char * pSuffix, DWORD fFlags);

void vboxVDbgDoPrintRect(const char * pPrefix, const RECT *pRect, const char * pSuffix);
void vboxVDbgDoPrintAlloc(const char * pPrefix, const VBOXWDDMDISP_RESOURCE *pRc, uint32_t iAlloc, const char * pSuffix);

VOID vboxVDbgDoDumpLockSurfTex(const char * pPrefix, const D3DDDIARG_LOCK* pData, const char * pSuffix, DWORD fFlags);
VOID vboxVDbgDoDumpUnlockSurfTex(const char * pPrefix, const D3DDDIARG_UNLOCK* pData, const char * pSuffix, DWORD fFlags);

BOOL vboxVDbgDoCheckRectsMatch(const VBOXWDDMDISP_RESOURCE *pDstRc, uint32_t iDstAlloc,
                            const VBOXWDDMDISP_RESOURCE *pSrcRc, uint32_t iSrcAlloc,
                            const RECT *pDstRect,
                            const RECT *pSrcRect,
                            BOOL fBreakOnMismatch);

VOID vboxVDbgDoPrintLopLastCmd(const char* pszDesc);

HRESULT vboxVDbgTimerStart(HANDLE hTimerQueue, HANDLE *phTimer, DWORD msTimeout);
HRESULT vboxVDbgTimerStop(HANDLE hTimerQueue, HANDLE hTimer);

#define VBOXVDBG_IS_PID(_pid) ((_pid) == (g_VBoxVDbgPid ? g_VBoxVDbgPid : (g_VBoxVDbgPid = GetCurrentProcessId())))
#define VBOXVDBG_IS_DUMP_ALLOWED_PID(_pid) (((int)(_pid)) > 0 ? VBOXVDBG_IS_PID(_pid) : !VBOXVDBG_IS_PID(-((int)(_pid))))

#define VBOXVDBG_ASSERT_IS_DWM(_bDwm) do { \
        Assert((!VBOXVDBG_IS_DWM()) == (!(_bDwm))); \
    } while (0)

#define VBOXVDBG_DUMP_FLAGS_FOR_TYPE(_type) g_VBoxVDbgFDump##_type
#define VBOXVDBG_BREAK_FLAGS_FOR_TYPE(_type) g_VBoxVDbgFBreak##_type
#define VBOXVDBG_CHECK_FLAGS_FOR_TYPE(_type) g_VBoxVDbgFCheck##_type
#define VBOXVDBG_IS_DUMP_ALLOWED(_type) ( VBOXVDBG_DUMP_TYPE_ENABLED(VBOXVDBG_DUMP_FLAGS_FOR_TYPE(_type)) )

#define VBOXVDBG_IS_BREAK_ALLOWED(_type) ( !!VBOXVDBG_BREAK_FLAGS_FOR_TYPE(_type) )

#define VBOXVDBG_IS_CHECK_ALLOWED(_type) ( !!VBOXVDBG_CHECK_FLAGS_FOR_TYPE(_type) )

#define VBOXVDBG_IS_DUMP_SHARED_ALLOWED(_pRc) (\
        (_pRc)->RcDesc.fFlags.SharedResource \
        && VBOXVDBG_IS_DUMP_ALLOWED(Shared) \
        )

#define VBOXVDBG_IS_BREAK_SHARED_ALLOWED(_pRc) (\
        (_pRc)->RcDesc.fFlags.SharedResource \
        && VBOXVDBG_IS_BREAK_ALLOWED(Shared) \
        )

#define VBOXVDBG_BREAK_SHARED(_pRc) do { \
        if (VBOXVDBG_IS_BREAK_SHARED_ALLOWED(_pRc)) { \
            vboxVDbgPrint(("Break on shared access: Rc(0x%p), SharedHandle(0x%p)\n", (_pRc), (_pRc)->aAllocations[0].hSharedHandle)); \
            AssertFailed(); \
        } \
    } while (0)

#define VBOXVDBG_BREAK_DDI() do { \
        if (VBOXVDBG_IS_BREAK_ALLOWED(Ddi)) { \
            AssertFailed(); \
        } \
    } while (0)

#define VBOXVDBG_LOOP_LAST() do { vboxVDbgLoop = 0; } while (0)

#define VBOXVDBG_LOOP(_op) do { \
        DWORD vboxVDbgLoop = 1; \
        do { \
            _op; \
        } while (vboxVDbgLoop); \
    } while (0)

#define VBOXVDBG_CHECK_SMSYNC(_pRc) do { \
        if (VBOXVDBG_IS_CHECK_ALLOWED(SysMemSync)) { \
            vboxWddmDbgRcSynchMemCheck((_pRc)); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_RECTS_INIT(_d) DWORD vboxVDbgDumpRects = _d; NOREF(vboxVDbgDumpRects)
#define VBOXVDBG_DUMP_RECTS_FORCE() vboxVDbgDumpRects = 1;
#define VBOXVDBG_DUMP_RECTS_FORCED() (!!vboxVDbgDumpRects)

#define VBOXVDBG_CHECK_RECTS(_opRests, _opDump, _pszOpName, _pDstRc, _iDstAlloc, _pSrcRc, _iSrcAlloc, _pDstRect, _pSrcRect) do { \
        VBOXVDBG_LOOP(\
                VBOXVDBG_DUMP_RECTS_INIT(0); \
                _opRests; \
                if (vboxVDbgDoCheckRectsMatch(_pDstRc, _iDstAlloc, _pSrcRc, _iSrcAlloc, _pDstRect, _pSrcRect, FALSE)) { \
                    VBOXVDBG_LOOP_LAST(); \
                } \
                else \
                { \
                    VBOXVDBG_DUMP_RECTS_FORCE(); \
                    vboxVDbgPrint(("vboxVDbgDoCheckRectsMatch failed! The " _pszOpName " will be re-done so it can be debugged\n")); \
                    vboxVDbgDoPrintLopLastCmd("Don't redo the" _pszOpName); \
                    Assert(0); \
                } \
                _opDump; \
         ); \
    } while (0)

#define VBOXVDBG_DEV_CHECK_SHARED(_pDevice, _pIsShared) do { \
        *(_pIsShared) = FALSE; \
        for (UINT i = 0; i < (_pDevice)->cRTs; ++i) { \
            PVBOXWDDMDISP_ALLOCATION pRtVar = (_pDevice)->apRTs[i]; \
            if (pRtVar && pRtVar->pRc->RcDesc.fFlags.SharedResource) { *(_pIsShared) = TRUE; break; } \
        } \
        if (!*(_pIsShared)) { \
            for (UINT i = 0, iSampler = 0; iSampler < (_pDevice)->cSamplerTextures; ++i) { \
                Assert(i < RT_ELEMENTS((_pDevice)->aSamplerTextures)); \
                if (!(_pDevice)->aSamplerTextures[i]) continue; \
                ++iSampler; \
                if (!(_pDevice)->aSamplerTextures[i]->RcDesc.fFlags.SharedResource) continue; \
                *(_pIsShared) = TRUE; break; \
            } \
        } \
    } while (0)

#define VBOXVDBG_IS_DUMP_SHARED_ALLOWED_DEV(_pDevice, _pIsAllowed) do { \
        VBOXVDBG_DEV_CHECK_SHARED(_pDevice, _pIsAllowed); \
        if (*(_pIsAllowed)) \
        { \
            *(_pIsAllowed) = VBOXVDBG_IS_DUMP_ALLOWED(Shared); \
        } \
    } while (0)

#define VBOXVDBG_IS_BREAK_SHARED_ALLOWED_DEV(_pDevice, _pIsAllowed) do { \
        VBOXVDBG_DEV_CHECK_SHARED(_pDevice, _pIsAllowed); \
        if (*(_pIsAllowed)) \
        { \
            *(_pIsAllowed) = VBOXVDBG_IS_BREAK_ALLOWED(Shared); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_DRAWPRIM_ENTER(_pDevice) do { \
        BOOL fDumpShaded = FALSE; \
        VBOXVDBG_IS_DUMP_SHARED_ALLOWED_DEV(_pDevice, &fDumpShaded); \
        if (fDumpShaded \
                || VBOXVDBG_IS_DUMP_ALLOWED(DrawPrim)) \
        { \
            vboxVDbgDoDumpRt("==>" __FUNCTION__ ": Rt: ", (_pDevice), "", VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(DrawPrim)); \
            vboxVDbgDoDumpSamplers("==>" __FUNCTION__ ": Sl: ", (_pDevice), "", VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(DrawPrim)); \
        }\
    } while (0)

#define VBOXVDBG_DUMP_DRAWPRIM_LEAVE(_pDevice) do { \
        BOOL fDumpShaded = FALSE; \
        VBOXVDBG_IS_DUMP_SHARED_ALLOWED_DEV(_pDevice, &fDumpShaded); \
        if (fDumpShaded \
                || VBOXVDBG_IS_DUMP_ALLOWED(DrawPrim)) \
        { \
            vboxVDbgDoDumpRt("<==" __FUNCTION__ ": Rt: ", (_pDevice), "", VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(DrawPrim)); \
            vboxVDbgDoDumpSamplers("<==" __FUNCTION__ ": Sl: ", (_pDevice), "", VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(DrawPrim)); \
        }\
    } while (0)

#define VBOXVDBG_BREAK_SHARED_DEV(_pDevice)  do { \
        BOOL fBreakShaded = FALSE; \
        VBOXVDBG_IS_BREAK_SHARED_ALLOWED_DEV(_pDevice, &fBreakShaded); \
        if (fBreakShaded) { \
            vboxVDbgPrint((__FUNCTION__"== Break on shared access\n")); \
            AssertFailed(); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_SETTEXTURE(_pRc) do { \
        if (VBOXVDBG_IS_DUMP_ALLOWED(SetTexture) \
                || VBOXVDBG_IS_DUMP_SHARED_ALLOWED(_pRc) \
                ) \
        { \
            vboxVDbgDoDumpRcRect("== " __FUNCTION__ ": ", &(_pRc)->aAllocations[0], NULL, NULL, "", \
                    VBOXVDBG_DUMP_FLAGS_FOR_TYPE(SetTexture) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { \
        if (VBOXVDBG_IS_DUMP_ALLOWED(TexBlt) \
                || VBOXVDBG_IS_DUMP_SHARED_ALLOWED(_pSrcRc) \
                || VBOXVDBG_IS_DUMP_SHARED_ALLOWED(_pDstRc) \
                ) \
        { \
            RECT SrcRect = *(_pSrcRect); \
            RECT _DstRect; \
            vboxWddmRectMoved(&_DstRect, &SrcRect, (_pDstPoint)->x, (_pDstPoint)->y); \
            vboxVDbgDoDumpRcRect("==> " __FUNCTION__ ": Src: ", &(_pSrcRc)->aAllocations[0], NULL, &SrcRect, "", \
                    VBOXVDBG_DUMP_FLAGS_FOR_TYPE(TexBlt) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
            vboxVDbgDoDumpRcRect("==> " __FUNCTION__ ": Dst: ", &(_pDstRc)->aAllocations[0], NULL, &_DstRect, "", \
                    VBOXVDBG_DUMP_FLAGS_FOR_TYPE(TexBlt) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { \
        if (VBOXVDBG_DUMP_RECTS_FORCED() \
                || VBOXVDBG_IS_DUMP_ALLOWED(TexBlt) \
                || VBOXVDBG_IS_DUMP_SHARED_ALLOWED(_pSrcRc) \
                || VBOXVDBG_IS_DUMP_SHARED_ALLOWED(_pDstRc) \
                ) \
        { \
            RECT SrcRect = *(_pSrcRect); \
            RECT _DstRect; \
            vboxWddmRectMoved(&_DstRect, &SrcRect, (_pDstPoint)->x, (_pDstPoint)->y); \
            vboxVDbgDoDumpRcRect("<== " __FUNCTION__ ": Src: ", &(_pSrcRc)->aAllocations[0], NULL, &SrcRect, "", \
                    VBOXVDBG_DUMP_FLAGS_FOR_TYPE(TexBlt) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
            vboxVDbgDoDumpRcRect("<== " __FUNCTION__ ": Dst: ", &(_pDstRc)->aAllocations[0], NULL, &_DstRect, "", \
                    VBOXVDBG_DUMP_FLAGS_FOR_TYPE(TexBlt) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_STRETCH_RECT(_type, _str, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) do { \
        if (VBOXVDBG_IS_DUMP_ALLOWED(_type) \
                || VBOXVDBG_IS_DUMP_SHARED_ALLOWED((_pSrcAlloc)->pRc) \
                || VBOXVDBG_IS_DUMP_SHARED_ALLOWED((_pDstAlloc)->pRc) \
                ) \
        { \
            DWORD fFlags = VBOXVDBG_DUMP_FLAGS_FOR_TYPE(_type) | VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Shared); \
            if (VBOXVDBG_DUMP_TYPE_CONTENTS(fFlags) && \
                    ((_pSrcSurf) == (_pDstSurf) \
                    && ( ((_pSrcRect) && (_pDstRect) && !memcmp((_pSrcRect), (_pDstRect), sizeof (_pDstRect))) \
                            || ((_pSrcRect) == (_pDstRect)) \
                            )) ) \
            { \
                vboxVDbgPrint((_str #_type ": skipping content dump of the same rect for one surfcace\n")); \
                fFlags = VBOXVDBG_DUMP_TYPE_GET_FLOW_ONLY(fFlags); \
            } \
            RECT Rect, *pRect; \
            if (_pSrcRect) \
            { \
                Rect = *((RECT*)(_pSrcRect)); \
                pRect = &Rect; \
            } \
            else \
                pRect = NULL; \
            vboxVDbgDoDumpRcRect(_str __FUNCTION__" Src: ", (_pSrcAlloc), (_pSrcSurf), pRect, "", fFlags); \
            if (_pDstRect) \
            { \
                Rect = *((RECT*)(_pDstRect)); \
                pRect = &Rect; \
            } \
            else \
                pRect = NULL; \
            vboxVDbgDoDumpRcRect(_str __FUNCTION__" Dst: ", (_pDstAlloc), (_pDstSurf), pRect, "", fFlags); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_BLT_ENTER(_pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) \
    VBOXVDBG_DUMP_STRETCH_RECT(Blt, "==>", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect)

#define VBOXVDBG_DUMP_BLT_LEAVE(_pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) \
        VBOXVDBG_DUMP_STRETCH_RECT(Blt, "<==", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect)

#define VBOXVDBG_IS_SKIP_DWM_WND_UPDATE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) ( \
            g_VBoxVDbgFSkipCheckTexBltDwmWndUpdate \
            && ( \
                VBOXVDBG_IS_DWM() \
                && (_pSrcRc)->RcDesc.enmPool == D3DDDIPOOL_SYSTEMMEM \
                && (_pSrcRc)->RcDesc.enmFormat == D3DDDIFMT_A8R8G8B8 \
                && (_pSrcRc)->cAllocations == 1 \
                && (_pDstRc)->RcDesc.enmPool == D3DDDIPOOL_VIDEOMEMORY \
                && (_pDstRc)->RcDesc.enmFormat == D3DDDIFMT_A8R8G8B8 \
                && (_pDstRc)->RcDesc.fFlags.RenderTarget \
                && (_pDstRc)->RcDesc.fFlags.NotLockable \
                && (_pDstRc)->cAllocations == 1 \
                && (_pSrcRc)->aAllocations[0].SurfDesc.width == (_pDstRc)->aAllocations[0].SurfDesc.width \
                && (_pSrcRc)->aAllocations[0].SurfDesc.height == (_pDstRc)->aAllocations[0].SurfDesc.height \
            ) \
        )

#define VBOXVDBG_CHECK_TEXBLT(_opTexBlt, _pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { \
        if (VBOXVDBG_IS_CHECK_ALLOWED(TexBlt)) { \
            if (VBOXVDBG_IS_SKIP_DWM_WND_UPDATE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint)) \
            { \
                vboxVDbgPrint(("TEXBLT: skipping check for dwm wnd update\n")); \
            } \
            else \
            { \
                RECT DstRect; \
                DstRect.left = (_pDstPoint)->x; \
                DstRect.right = (_pDstPoint)->x + (_pSrcRect)->right - (_pSrcRect)->left; \
                DstRect.top = (_pDstPoint)->y; \
                DstRect.bottom = (_pDstPoint)->y + (_pSrcRect)->bottom - (_pSrcRect)->top; \
                VBOXVDBG_CHECK_RECTS(\
                        VBOXVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint); \
                        _opTexBlt ,\
                        VBOXVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint), \
                        "TexBlt", \
                        _pDstRc, 0, _pSrcRc, 0, &DstRect, _pSrcRect); \
                break; \
            } \
        } \
        VBOXVDBG_DUMP_RECTS_INIT(0); \
        VBOXVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint); \
        _opTexBlt;\
        VBOXVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint); \
    } while (0)

#define VBOXVDBG_CHECK_STRETCH_RECT(_type, _op, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) do { \
        if (VBOXVDBG_IS_CHECK_ALLOWED(_type)) { \
            VBOXVDBG_CHECK_RECTS(\
                    VBOXVDBG_DUMP_STRETCH_RECT(_type, "==>", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect); \
                    _op ,\
                    VBOXVDBG_DUMP_STRETCH_RECT(_type, "<==", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect), \
                    #_type , \
                    _pDstAlloc->pRc, _pDstAlloc->iAlloc, _pSrcAlloc->pRc, _pSrcAlloc->iAlloc, _pDstRect, _pSrcRect); \
        } \
        else \
        { \
            VBOXVDBG_DUMP_RECTS_INIT(0); \
            VBOXVDBG_DUMP_STRETCH_RECT(_type, "==>", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect); \
            _op;\
            VBOXVDBG_DUMP_STRETCH_RECT(_type, "<==", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect); \
        } \
    } while (0)

#define VBOXVDBG_CHECK_BLT(_opBlt, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) \
        VBOXVDBG_CHECK_STRETCH_RECT(Blt, _opBlt, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect)

#define VBOXVDBG_DUMP_SYNC_RT(_pBbSurf) do { \
        if (VBOXVDBG_IS_DUMP_ALLOWED(RtSynch)) \
        { \
            vboxVDbgDoDumpRcRect("== " __FUNCTION__ " Bb:\n", NULL, (_pBbSurf), NULL, "", VBOXVDBG_DUMP_FLAGS_FOR_TYPE(RtSynch)); \
        } \
    } while (0)


#define VBOXVDBG_DUMP_FLUSH(_pDevice) do { \
        if (VBOXVDBG_IS_DUMP_ALLOWED(Flush)) \
        { \
            vboxVDbgDoDumpRt("== " __FUNCTION__ ": Rt: ", (_pDevice), "", \
                    VBOXVDBG_DUMP_FLAGS_CLEAR(VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Flush), VBOXVDBG_DUMP_TYPEF_SHARED_ONLY)); \
        }\
    } while (0)

#define VBOXVDBG_DUMP_LOCK_ST(_pData) do { \
        if (VBOXVDBG_IS_DUMP_ALLOWED(Lock) \
                || VBOXVDBG_IS_DUMP_ALLOWED(Unlock) \
                ) \
        { \
            vboxVDbgDoDumpLockSurfTex("== " __FUNCTION__ ": ", (_pData), "", VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Lock)); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_UNLOCK_ST(_pData) do { \
        if (VBOXVDBG_IS_DUMP_ALLOWED(Unlock) \
                ) \
        { \
            vboxVDbgDoDumpUnlockSurfTex("== " __FUNCTION__ ": ", (_pData), "", VBOXVDBG_DUMP_FLAGS_FOR_TYPE(Unlock)); \
        } \
    } while (0)

#else
#define VBOXVDBG_DUMP_DRAWPRIM_ENTER(_pDevice) do { } while (0)
#define VBOXVDBG_DUMP_DRAWPRIM_LEAVE(_pDevice) do { } while (0)
#define VBOXVDBG_DUMP_SETTEXTURE(_pRc) do { } while (0)
#define VBOXVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { } while (0)
#define VBOXVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { } while (0)
#define VBOXVDBG_DUMP_BLT_ENTER(_pSrcRc, _pSrcSurf, _pSrcRect, _pDstRc, _pDstSurf, _pDstRect) do { } while (0)
#define VBOXVDBG_DUMP_BLT_LEAVE(_pSrcRc, _pSrcSurf, _pSrcRect, _pDstRc, _pDstSurf, _pDstRect) do { } while (0)
#define VBOXVDBG_DUMP_SYNC_RT(_pBbSurf) do { } while (0)
#define VBOXVDBG_DUMP_FLUSH(_pDevice) do { } while (0)
#define VBOXVDBG_DUMP_LOCK_ST(_pData) do { } while (0)
#define VBOXVDBG_DUMP_UNLOCK_ST(_pData) do { } while (0)
#define VBOXVDBG_BREAK_SHARED(_pRc) do { } while (0)
#define VBOXVDBG_BREAK_SHARED_DEV(_pDevice) do { } while (0)
#define VBOXVDBG_BREAK_DDI() do { } while (0)
#define VBOXVDBG_CHECK_SMSYNC(_pRc) do { } while (0)
#define VBOXVDBG_CHECK_BLT(_opBlt, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) do { _opBlt; } while (0)
#define VBOXVDBG_CHECK_TEXBLT(_opTexBlt, _pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { _opTexBlt; } while (0)
#define VBOXVDBG_ASSERT_IS_DWM(_bDwm) do { } while (0)
#endif


#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispDbg_h */
