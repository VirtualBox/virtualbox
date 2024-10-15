/* $Id: VBoxWddmUmHlp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox WDDM User Mode Driver Helpers
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef GA_INCLUDED_SRC_3D_win_VBoxWddmUmHlp_VBoxWddmUmHlp_h
#define GA_INCLUDED_SRC_3D_win_VBoxWddmUmHlp_VBoxWddmUmHlp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/d3d9.h>
#include <d3dumddi.h>
#include <iprt/win/d3dkmthk.h>

#include <iprt/asm.h>
#include <iprt/cdefs.h>

/* Do not require IPRT library. */
/** @todo r=bird: It is *NOT* okay to redefine Assert* (or Log*) macros!  It
 * causes confusing as the code no longer behaves in the way one expect.  Thus,
 * it is strictly forbidden. */
#ifndef IPRT_NO_CRT
# undef Assert
# undef AssertReturnVoid
# ifdef RT_STRICT
#  define Assert(_e) (void)( (!!(_e)) || (ASMBreakpoint(), 0) )
#  define AssertReturnVoid(a_Expr) do { if (RT_LIKELY(a_Expr)) {} else { ASMBreakpoint(); return; } } while (0)
# else
#  define Assert(_e) (void)( 0 )
#  define AssertReturnVoid(a_Expr) do { if (RT_LIKELY(a_Expr)) {} else return; } while (0)
# endif
#endif

/* Do not require ntstatus.h.
 * D3DKMT functions return NTSTATUS, but the driver code uses it only as a success/failure indicator.
 * Therefore define the success and a failure status here.
 */
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS 0
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)
#endif

RT_C_DECLS_BEGIN

typedef struct VBOXWDDMDLLPROC
{
    char const *pszName;
    FARPROC *ppfn;
} VBOXWDDMDLLPROC;

typedef struct D3DKMTFUNCTIONS
{
    PFND3DKMT_OPENADAPTERFROMHDC         pfnD3DKMTOpenAdapterFromHdc;
    PFND3DKMT_OPENADAPTERFROMDEVICENAME  pfnD3DKMTOpenAdapterFromDeviceName;
    PFND3DKMT_CLOSEADAPTER               pfnD3DKMTCloseAdapter;
    PFND3DKMT_QUERYADAPTERINFO           pfnD3DKMTQueryAdapterInfo;
    PFND3DKMT_ESCAPE                     pfnD3DKMTEscape;
    PFND3DKMT_CREATEDEVICE               pfnD3DKMTCreateDevice;
    PFND3DKMT_DESTROYDEVICE              pfnD3DKMTDestroyDevice;
    PFND3DKMT_CREATECONTEXT              pfnD3DKMTCreateContext;
    PFND3DKMT_DESTROYCONTEXT             pfnD3DKMTDestroyContext;
    PFND3DKMT_CREATEALLOCATION           pfnD3DKMTCreateAllocation;
    PFND3DKMT_DESTROYALLOCATION          pfnD3DKMTDestroyAllocation;
    PFND3DKMT_RENDER                     pfnD3DKMTRender;
    PFND3DKMT_PRESENT                    pfnD3DKMTPresent;
    PFND3DKMT_GETSHAREDPRIMARYHANDLE     pfnD3DKMTGetSharedPrimaryHandle;
    PFND3DKMT_QUERYRESOURCEINFO          pfnD3DKMTQueryResourceInfo;
    PFND3DKMT_OPENRESOURCE               pfnD3DKMTOpenResource;

    /* Win 8+ */
    PFND3DKMT_ENUMADAPTERS               pfnD3DKMTEnumAdapters;
    PFND3DKMT_OPENADAPTERFROMLUID        pfnD3DKMTOpenAdapterFromLuid;
} D3DKMTFUNCTIONS;

DECLCALLBACK(HMODULE) VBoxWddmLoadSystemDll(const char *pszName);
DECLCALLBACK(void) VBoxWddmLoadAdresses(HMODULE hmod, VBOXWDDMDLLPROC *paProcs);

DECLCALLBACK(int) D3DKMTLoad(void);
DECLCALLBACK(D3DKMTFUNCTIONS const *) D3DKMTFunctions(void);

DECLCALLBACK(void) VBoxDispMpLoggerLogF(const char *pszFormat, ...);
DECLCALLBACK(void) VBoxWddmUmLog(const char *pszString);

/** @todo Rename to VBoxWddm* */
NTSTATUS vboxDispKmtOpenAdapter2(D3DKMT_HANDLE *phAdapter, LUID *pLuid);
NTSTATUS vboxDispKmtOpenAdapter(D3DKMT_HANDLE *phAdapter);
NTSTATUS vboxDispKmtCloseAdapter(D3DKMT_HANDLE hAdapter);

RT_C_DECLS_END

#endif /* !GA_INCLUDED_SRC_3D_win_VBoxWddmUmHlp_VBoxWddmUmHlp_h */
