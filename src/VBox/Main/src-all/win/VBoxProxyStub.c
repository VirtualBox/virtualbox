/* $Id: VBoxProxyStub.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxProxyStub - Proxy Stub and Typelib, COM DLL exports and DLL init/term.
 *
 * @remarks This is a C file and not C++ because rpcproxy.h isn't C++ clean,
 *          at least not in SDK v7.1.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN
#define PROXY_DELEGATION                                                                             /* see generated dlldata.c */
#include <iprt/nt/nt-and-windows.h>
#include <rpcproxy.h>
#include <iprt/win/shlwapi.h>
#include <stdio.h>

#include "VirtualBox.h"
#include <VBox/cdefs.h>                 /* for VBOX_STRICT */
#include <VBox/log.h>
#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/initterm.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef DEBUG_bird
# define VBSP_LOG_ENABLED
#endif

#ifdef VBSP_LOG_ENABLED
# define VBSP_LOG_VALUE_CHANGE(a)   RTAssertMsg2 a
#else
# define VBSP_LOG_VALUE_CHANGE(a)   do { } while (0)
#endif

#ifdef VBSP_LOG_ENABLED
# define VBSP_LOG_SET_VALUE(a)      RTAssertMsg2 a
#else
# define VBSP_LOG_SET_VALUE(a)      do { } while (0)
#endif

#ifdef VBSP_LOG_ENABLED
# define VBSP_LOG_NEW_KEY(a)        RTAssertMsg2 a
#else
# define VBSP_LOG_NEW_KEY(a)        do { } while (0)
#endif

#ifdef VBSP_LOG_ENABLED
# define VBSP_LOG_DEL_KEY(a)        RTAssertMsg2 a
#else
# define VBSP_LOG_DEL_KEY(a)        do { } while (0)
#endif

/**
 * Selects the proxy stub DLL based on 32-on-64-bit and host OS version.
 *
 * The legacy DLL covers 64-bit pre Windows 7 versions of Windows. W2K3-amd64
 * has trouble parsing the result when MIDL /target NT51 or higher. Vista and
 * windows server 2008 seems to have trouble with newer IDL compilers.
 */
#if ARCH_BITS == 64 || defined(VBOX_IN_32_ON_64_MAIN_API)
# define VBPS_PROXY_STUB_FILE(a_fIs32On64) ( (a_fIs32On64) ? "x86\\VBoxProxyStub-x86.dll" : VBPS_PROXY_STUB_FILE_SUB() )
#else
# define VBPS_PROXY_STUB_FILE(a_fIs32On64) VBPS_PROXY_STUB_FILE_SUB()
#endif
#define VBPS_PROXY_STUB_FILE_SUB() \
    ( RT_MAKE_U64(((PKUSER_SHARED_DATA)MM_SHARED_USER_DATA_VA)->NtMinorVersion, \
                  ((PKUSER_SHARED_DATA)MM_SHARED_USER_DATA_VA)->NtMajorVersion) >= RT_MAKE_U64(1/*Lo*/,6/*Hi*/) \
      ? "VBoxProxyStub.dll" : "VBoxProxyStubLegacy.dll" )

/** For use with AssertLogRel except a_Expr1 from assertions but not LogRel. */
#ifdef RT_STRICT
# define VBPS_LOGREL_NO_ASSERT(a_Expr) (a_Expr)
#else
# define VBPS_LOGREL_NO_ASSERT(a_Expr) false
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** For NdrXxx. */
CStdPSFactoryBuffer         g_ProxyStubFactory =                                                     /* see generated dlldata.c */
{
    NULL,
    0,
    NULL,
    0
};
/** Reference to VirtualBox_p.c structure. */
EXTERN_PROXY_FILE(VirtualBox)                                                                        /* see generated dlldata.c */
/** For NdrXxx and for returning. */
static const ProxyFileInfo *g_apProxyFiles[] =
{
    REFERENCE_PROXY_FILE(VirtualBox),
    NULL /* terminator */
};
/** The class ID for this proxy stub factory (see Makefile). */
static const CLSID          g_ProxyClsId = PROXY_CLSID_IS;
/** The instance handle of this DLL.  For use in registration routines. */
static HINSTANCE            g_hDllSelf;


/** Type library GUIDs to clean up manually.
 * Must be upper case!  */
static PCRTUTF16 const      g_apwszTypeLibIds[] =
{
    L"{46137EEC-703B-4FE5-AFD4-7C9BBBBA0259}",
    L"{D7569351-1750-46F0-936E-BD127D5BC264}",
};

/** Type library version to clean up manually. */
static PCRTUTF16 const      g_apwszTypelibVersions[] =
{
    L"1.0",
    L"1.3",
};

/** Proxy stub class IDs we wish to clean up manually.
 * Must be upper case!  */
static PCRTUTF16 const      g_apwszProxyStubClsIds[] =
{
    L"{0BB3B78C-1807-4249-5BA5-EA42D66AF0BF}",
    L"{327E3C00-EE61-462F-AED3-0DFF6CBF9904}",
};


/**
 * DLL main function.
 *
 * @returns TRUE (/ FALSE).
 * @param   hInstance           The DLL handle.
 * @param   dwReason            The rason for the call (DLL_XXX).
 * @param   lpReserved          Reserved.
 */
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            /* Save the DLL handle so we can get the path to this DLL during
               registration and updating. */
            g_hDllSelf = hInstance;

            /* We don't need callbacks for thread creation and destruction. */
            DisableThreadLibraryCalls(hInstance);

            /* Init IPRT. */
            RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            Log12(("VBoxProxyStub[%u]/DllMain: DLL_PROCESS_ATTACH\n", GetCurrentProcessId()));

#ifdef VBOX_STRICT
            {
                /*
                 * Check that no interface has more than 256 methods in the stub vtable.
                 */
                const ProxyFileInfo **ppProxyFile = &g_apProxyFiles[0];
                const ProxyFileInfo  *pProxyFile;
                while ((pProxyFile = *ppProxyFile++) != NULL)
                {
                    const PCInterfaceStubVtblList * const   papStubVtbls  = pProxyFile->pStubVtblList;
                    const char * const                     *papszNames    = pProxyFile->pNamesArray;
                    unsigned                                iIf           = pProxyFile->TableSize;
                    AssertStmt(iIf < 1024, iIf = 0);
                    Assert(pProxyFile->TableVersion == 2);

                    while (iIf-- > 0)
                        AssertMsg(papStubVtbls[iIf]->header.DispatchTableCount <= 256,
                                  ("%s: DispatchTableCount=%d\n", papszNames[iIf], papStubVtbls[iIf]->header.DispatchTableCount));
                }
            }
#endif
            break;

        case DLL_PROCESS_DETACH:
            Log12(("VBoxProxyStub[%u]/DllMain: DLL_PROCESS_DETACH\n", GetCurrentProcessId()));
            break;
    }

    NOREF(lpReserved);
    return TRUE;
}


/**
 * RPC entry point returning info about the proxy.
 */
void RPC_ENTRY GetProxyDllInfo(const ProxyFileInfo ***ppapInfo, const CLSID **ppClsid)
{
    *ppapInfo = &g_apProxyFiles[0];
    *ppClsid  = &g_ProxyClsId;
    Log12(("VBoxProxyStub[%u]/GetProxyDllInfo:\n", GetCurrentProcessId()));
}


/**
 * Instantiate the proxy stub class object.
 *
 * @returns COM status code
 * @param   rclsid      Reference to the ID of the call to instantiate (our
 *                      g_ProxyClsId).
 * @param   riid        The interface ID to return (IID_IPSFactoryBuffer).
 * @param   ppv         Where to return the interface pointer on success.
 */
HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    HRESULT hrc;
    Assert(memcmp(rclsid, &g_ProxyClsId, sizeof(g_ProxyClsId)) == 0);

    hrc = NdrDllGetClassObject(rclsid, riid, ppv,                                 /* see DLLGETCLASSOBJECTROUTINE in RpcProxy.h */
                               g_apProxyFiles, &g_ProxyClsId, &g_ProxyStubFactory);

    /*
     * This may fail if the IDL compiler generates code that is incompatible
     * with older windows releases.  Like for instance 64-bit W2K8 SP1 not
     * liking the output of MIDL 7.00.0555 (from the v7.1 SDK), despite
     * /target being set to NT51.
     */
    AssertLogRelMsg(hrc == S_OK, ("%Rhrc\n",  hrc));
    Log12(("VBoxProxyStub[%u]/DllGetClassObject(%RTuuid, %RTuuid, %p): %#x + *ppv=%p\n",
           GetCurrentProcessId(), rclsid, riid, ppv, hrc, ppv ? *ppv : NULL));
    return hrc;
}


/**
 * Checks whether the DLL can be unloaded or not.
 *
 * @returns S_OK if it can be unloaded, S_FALSE if not.
 */
HRESULT STDAPICALLTYPE DllCanUnloadNow(void)
{
    HRESULT hrc = NdrDllCanUnloadNow(&g_ProxyStubFactory);                                 /* see DLLCANUNLOADNOW in RpcProxy.h */
    Log12(("VBoxProxyStub[%u]/DllCanUnloadNow: %Rhrc\n", GetCurrentProcessId(), hrc));
    return hrc;
}



/**
 * Release call that could be referenced by VirtualBox_p.c via
 * CStdStubBuffer_METHODS.
 *
 * @returns New reference count.
 * @param   pThis               Buffer to release.
 */
ULONG STDMETHODCALLTYPE CStdStubBuffer_Release(IRpcStubBuffer *pThis)                /* see CSTDSTUBBUFFERRELEASE in RpcProxy.h */
{
    ULONG cRefs =  NdrCStdStubBuffer_Release(pThis, (IPSFactoryBuffer *)&g_ProxyStubFactory);
    Log12(("VBoxProxyStub[%u]/CStdStubBuffer_Release: %p -> %#x\n", GetCurrentProcessId(), pThis, cRefs));
    return cRefs;
}


/**
 * Release call referenced by VirtualBox_p.c via
 * CStdStubBuffer_DELEGATING_METHODS.
 *
 * @returns New reference count.
 * @param   pThis               Buffer to release.
 */
ULONG WINAPI CStdStubBuffer2_Release(IRpcStubBuffer *pThis)                         /* see CSTDSTUBBUFFER2RELEASE in RpcProxy.h */
{
    ULONG cRefs = NdrCStdStubBuffer2_Release(pThis, (IPSFactoryBuffer *)&g_ProxyStubFactory);
    Log12(("VBoxProxyStub[%u]/CStdStubBuffer2_Release: %p -> %#x\n", GetCurrentProcessId(), pThis, cRefs));
    return cRefs;
}


/**
 * Pure virtual method implementation referenced by VirtualBox_p.c
 */
void __cdecl _purecall(void)                                                              /* see DLLDUMMYPURECALL in RpcProxy.h */
{
    AssertFailed();
}


#ifdef VBSP_LOG_ENABLED
# include <iprt/asm.h>

/** For logging full key names.   */
static PCRTUTF16 vbpsDebugKeyToWSZ(HKEY hKey)
{
    static union
    {
        KEY_NAME_INFORMATION NameInfo;
        WCHAR awchPadding[260];
    }                           s_aBufs[4];
    static uint32_t volatile    iNext = 0;
    uint32_t                    i = ASMAtomicIncU32(&iNext) % RT_ELEMENTS(s_aBufs);
    ULONG                       cbRet = 0;
    NTSTATUS                    rcNt;

    memset(&s_aBufs[i], 0, sizeof(s_aBufs[i]));
    rcNt = NtQueryKey(hKey, KeyNameInformation, &s_aBufs[i], sizeof(s_aBufs[i]) - sizeof(WCHAR), &cbRet);
    if (!NT_SUCCESS(rcNt))
        s_aBufs[i].NameInfo.NameLength = 0;
    s_aBufs[i].NameInfo.Name[s_aBufs[i].NameInfo.NameLength] = '\0';
    return s_aBufs[i].NameInfo.Name;
}
#endif

/**
 * Registry modifier state.
 */
typedef struct VBPSREGSTATE
{
    /** Where the classes and stuff are to be registered. */
    HKEY hkeyClassesRootDst;
    /** The handle to the CLSID key under hkeyClassesRootDst. */
    HKEY hkeyClsidRootDst;
    /** The handle to the Interface key under hkeyClassesRootDst. */
    HKEY hkeyInterfaceRootDst;

    /** Alternative locations where data needs to be deleted, but never updated.  */
    struct
    {
        /** The classes root key handle. */
        HKEY hkeyClasses;
        /** The classes/CLSID key handle. */
        HKEY hkeyClsid;
        /** The classes/Interface key handle. */
        HKEY hkeyInterface;
    } aAltDeletes[3];
    /** Alternative delete locations. */
    uint32_t cAltDeletes;

    /** The current total result. */
    LSTATUS lrc;

    /** KEY_WOW64_32KEY, KEY_WOW64_64KEY or 0 (for default).  Allows doing all
     * almost the work from one process (at least W7+ due to aliases). */
    DWORD   fSamWow;
    /** Desired key access when only deleting. */
    DWORD   fSamDelete;
    /** Desired key access when only doing updates. */
    DWORD   fSamUpdate;
    /** Desired key access when both deleting and updating. */
    DWORD   fSamBoth;
    /** Whether to delete registrations first. */
    bool    fDelete;
    /** Whether to update registry value and keys. */
    bool    fUpdate;

} VBPSREGSTATE;


/**
 * Initializes a registry modification job state.
 *
 * Always call vbpsRegTerm!
 *
 * @returns Windows error code (ERROR_SUCCESS on success).
 * @param   pState          The state to init.
 * @param   hkeyRoot        The registry root tree constant.
 * @param   pszSubRoot      The path to the where the classes are registered,
 *                          NULL if @a hkeyRoot.
 * @param   fDelete         Whether to delete registrations first.
 * @param   fUpdate         Whether to update registrations.
 * @param   fSamWow         KEY_WOW64_32KEY or 0.
 */
static LSTATUS vbpsRegInit(VBPSREGSTATE *pState, HKEY hkeyRoot, const char *pszSubRoot, bool fDelete, bool fUpdate, DWORD fSamWow)
{
    LSTATUS lrc;
    unsigned i = 0;

    /*
     * Initialize the whole structure first so we can safely call vbpsRegTerm on failure.
     */
    pState->hkeyClassesRootDst          = NULL;
    pState->hkeyClsidRootDst            = NULL;
    pState->hkeyInterfaceRootDst        = NULL;
    for (i = 0; i < RT_ELEMENTS(pState->aAltDeletes); i++)
    {
        pState->aAltDeletes[i].hkeyClasses   = NULL;
        pState->aAltDeletes[i].hkeyClsid     = NULL;
        pState->aAltDeletes[i].hkeyInterface = NULL;
    }
    pState->cAltDeletes                 = 0;
    pState->lrc                         = ERROR_SUCCESS;
    pState->fDelete                     = fDelete;
    pState->fUpdate                     = fUpdate;
    pState->fSamWow                     = fSamWow;
    pState->fSamDelete                  = 0;
    if (fDelete)
        pState->fSamDelete = pState->fSamWow | DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE
                           | STANDARD_RIGHTS_READ | STANDARD_RIGHTS_WRITE;
    pState->fSamUpdate                  = 0;
    if (fUpdate)
        pState->fSamUpdate = pState->fSamWow | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY
                           | STANDARD_RIGHTS_READ | STANDARD_RIGHTS_WRITE;
    pState->fSamBoth                    = pState->fSamDelete | pState->fSamUpdate;

    /*
     * Open the root keys.
     */
    lrc = RegOpenKeyExA(hkeyRoot, pszSubRoot, 0 /*fOptions*/, pState->fSamBoth, &pState->hkeyClassesRootDst);
    if (lrc == ERROR_SUCCESS)
    {
        lrc = RegCreateKeyExW(pState->hkeyClassesRootDst, L"CLSID", 0 /*Reserved*/, NULL /*pszClass*/, 0 /*fOptions*/,
                              pState->fSamBoth, NULL /*pSecAttr*/, &pState->hkeyClsidRootDst, NULL /*pdwDisposition*/);
        if (lrc == ERROR_SUCCESS)
            return ERROR_SUCCESS;

        /* Ignore access denied errors as these may easily happen for
           non-admin users. Just give up when this happens */
        AssertLogRelMsgReturn(lrc == ERROR_ACCESS_DENIED, ("%u\n", lrc), pState->lrc = lrc);
    }
    else
       AssertLogRelMsgReturn(lrc == ERROR_ACCESS_DENIED, ("%u\n", lrc), pState->lrc = lrc);
    return pState->lrc = lrc;
}


/**
 * Terminates the state, closing all open keys.
 *
 * @param   pState          The state to clean up.
 */
static void vbpsRegTerm(VBPSREGSTATE *pState)
{
    LSTATUS lrc;
    if (pState->hkeyClassesRootDst)
    {
        lrc = RegCloseKey(pState->hkeyClassesRootDst);
        Assert(lrc == ERROR_SUCCESS);
        pState->hkeyClassesRootDst = NULL;
    }
    if (pState->hkeyClsidRootDst)
    {
        lrc = RegCloseKey(pState->hkeyClsidRootDst);
        Assert(lrc == ERROR_SUCCESS);
        pState->hkeyClsidRootDst = NULL;
    }
    if (pState->hkeyInterfaceRootDst)
    {
        lrc = RegCloseKey(pState->hkeyInterfaceRootDst);
        Assert(lrc == ERROR_SUCCESS);
        pState->hkeyInterfaceRootDst = NULL;
    }

    while (pState->cAltDeletes > 0 && pState->cAltDeletes <= RT_ELEMENTS(pState->aAltDeletes))
    {
        unsigned i = --pState->cAltDeletes;
        if (pState->aAltDeletes[i].hkeyClasses)
        {
            lrc = RegCloseKey(pState->aAltDeletes[i].hkeyClasses);
            Assert(lrc == ERROR_SUCCESS);
            pState->aAltDeletes[i].hkeyClasses = NULL;
        }
        if (pState->aAltDeletes[i].hkeyClsid)
        {
            lrc = RegCloseKey(pState->aAltDeletes[i].hkeyClsid);
            Assert(lrc == ERROR_SUCCESS);
            pState->aAltDeletes[i].hkeyClsid = NULL;
        }
        if (pState->aAltDeletes[i].hkeyInterface)
        {
            lrc = RegCloseKey(pState->aAltDeletes[i].hkeyInterface);
            Assert(lrc == ERROR_SUCCESS);
            pState->aAltDeletes[i].hkeyInterface = NULL;
        }
    }
}


/**
 * Add an alternative registry classes tree from which to remove keys.
 *
 * @returns ERROR_SUCCESS if we successfully opened the destination root, other
 *          wise windows error code (remebered).
 * @param   pState              The registry modifier state.
 * @param   hkeyAltRoot         The root of the alternate registry classes
 *                              location.
 * @param   pszAltSubRoot       The path to the 'classes' sub-key, or NULL if
 *                              hkeyAltRoot is it.
 */
static LSTATUS vbpsRegAddAltDelete(VBPSREGSTATE *pState, HKEY hkeyAltRoot, const char *pszAltSubRoot)
{
    unsigned i;
    LSTATUS lrc;

    /* Ignore call if not in delete mode. */
    if (!pState->fDelete)
        return ERROR_SUCCESS;

    /* Check that there is space in the state. */
    i = pState->cAltDeletes;
    AssertReturn(i < RT_ELEMENTS(pState->aAltDeletes), pState->lrc = ERROR_TOO_MANY_NAMES);


    /* Open the root. */
    lrc = RegOpenKeyExA(hkeyAltRoot, pszAltSubRoot, 0 /*fOptions*/, pState->fSamDelete,
                       &pState->aAltDeletes[i].hkeyClasses);
    if (lrc == ERROR_SUCCESS)
    {
        /* Try open the CLSID subkey, it's fine if it doesn't exists. */
        lrc = RegOpenKeyExW(pState->aAltDeletes[i].hkeyClasses, L"CLSID", 0 /*fOptions*/, pState->fSamDelete,
                            &pState->aAltDeletes[i].hkeyClsid);
        if (lrc == ERROR_SUCCESS || lrc == ERROR_FILE_NOT_FOUND)
        {
            if (lrc == ERROR_FILE_NOT_FOUND)
                pState->aAltDeletes[i].hkeyClsid = NULL;
            pState->cAltDeletes = i + 1;
            return ERROR_SUCCESS;
        }
        AssertLogRelMsgFailed(("%u\n", lrc));
        RegCloseKey(pState->aAltDeletes[i].hkeyClasses);
    }
    /* No need to add non-existing alternative roots, nothing to delete in the void. */
    else if (lrc == ERROR_FILE_NOT_FOUND)
        lrc = ERROR_SUCCESS;
    else
    {
        AssertLogRelMsgFailed(("%u (%#x %s)\n", lrc));
        pState->lrc = lrc;
    }

    pState->aAltDeletes[i].hkeyClasses = NULL;
    pState->aAltDeletes[i].hkeyClsid   = NULL;
    return lrc;
}


/**
 * Open the 'Interface' keys under the current classes roots.
 *
 * We don't do this during vbpsRegInit as it's only needed for updating.
 *
 * @returns ERROR_SUCCESS if we successfully opened the destination root, other
 *          wise windows error code (remebered).
 * @param   pState              The registry modifier state.
 */
static LSTATUS vbpsRegOpenInterfaceKeys(VBPSREGSTATE *pState)
{
    unsigned i;
    LSTATUS lrc;

    /*
     * Under the root destination.
     */
    if (pState->hkeyInterfaceRootDst == NULL)
    {
        if (pState->fSamUpdate)
            lrc = RegCreateKeyExW(pState->hkeyClassesRootDst, L"Interface", 0 /*Reserved*/, NULL /*pszClass*/, 0 /*fOptions*/,
                                  pState->fSamBoth, NULL /*pSecAttr*/, &pState->hkeyInterfaceRootDst, NULL /*pdwDisposition*/);
        else
            lrc = RegOpenKeyExW(pState->hkeyClassesRootDst, L"Interface", 0 /*fOptions*/, pState->fSamBoth,
                                &pState->hkeyClsidRootDst);
        if (lrc == ERROR_ACCESS_DENIED)
        {
            pState->hkeyInterfaceRootDst = NULL;
            return pState->lrc = lrc;
        }
        AssertLogRelMsgReturnStmt(lrc == ERROR_SUCCESS, ("%u\n", lrc), pState->hkeyInterfaceRootDst = NULL,  pState->lrc = lrc);
    }

    /*
     * Under the alternative delete locations.
     */
    i = pState->cAltDeletes;
    while (i-- > 0)
        if (pState->aAltDeletes[i].hkeyInterface == NULL)
        {
            lrc = RegOpenKeyExW(pState->aAltDeletes[i].hkeyClasses, L"Interface", 0 /*fOptions*/, pState->fSamDelete,
                                &pState->aAltDeletes[i].hkeyInterface);
            if (lrc != ERROR_SUCCESS)
            {
                AssertMsgStmt(lrc == ERROR_FILE_NOT_FOUND || lrc == ERROR_ACCESS_DENIED, ("%u\n", lrc), pState->lrc = lrc);
                pState->aAltDeletes[i].hkeyInterface = NULL;
            }
        }

    return ERROR_SUCCESS;
}


/** The destination buffer size required by vbpsFormatUuidInCurly. */
#define CURLY_UUID_STR_BUF_SIZE     40

/**
 * Formats a UUID to a string, inside curly braces.
 *
 * @returns @a pszString
 * @param   pszString           Output buffer of size CURLY_UUID_STR_BUF_SIZE.
 * @param   pUuidIn             The UUID to format.
 */
static const char *vbpsFormatUuidInCurly(char pszString[CURLY_UUID_STR_BUF_SIZE], const CLSID *pUuidIn)
{
    static const char s_achDigits[17] = "0123456789abcdef";
    PCRTUUID pUuid = (PCRTUUID)pUuidIn;
    uint32_t u32TimeLow;
    unsigned u;

    pszString[ 0] = '{';
    u32TimeLow = RT_H2LE_U32(pUuid->Gen.u32TimeLow);
    pszString[ 1] = s_achDigits[(u32TimeLow >> 28)/*& 0xf*/];
    pszString[ 2] = s_achDigits[(u32TimeLow >> 24) & 0xf];
    pszString[ 3] = s_achDigits[(u32TimeLow >> 20) & 0xf];
    pszString[ 4] = s_achDigits[(u32TimeLow >> 16) & 0xf];
    pszString[ 5] = s_achDigits[(u32TimeLow >> 12) & 0xf];
    pszString[ 6] = s_achDigits[(u32TimeLow >>  8) & 0xf];
    pszString[ 7] = s_achDigits[(u32TimeLow >>  4) & 0xf];
    pszString[ 8] = s_achDigits[(u32TimeLow/*>>0*/)& 0xf];
    pszString[ 9] = '-';
    u = RT_H2LE_U16(pUuid->Gen.u16TimeMid);
    pszString[10] = s_achDigits[(u >> 12)/*& 0xf*/];
    pszString[11] = s_achDigits[(u >>  8) & 0xf];
    pszString[12] = s_achDigits[(u >>  4) & 0xf];
    pszString[13] = s_achDigits[(u/*>>0*/)& 0xf];
    pszString[14] = '-';
    u = RT_H2LE_U16(pUuid->Gen.u16TimeHiAndVersion);
    pszString[15] = s_achDigits[(u >> 12)/*& 0xf*/];
    pszString[16] = s_achDigits[(u >>  8) & 0xf];
    pszString[17] = s_achDigits[(u >>  4) & 0xf];
    pszString[18] = s_achDigits[(u/*>>0*/)& 0xf];
    pszString[19] = '-';
    pszString[20] = s_achDigits[pUuid->Gen.u8ClockSeqHiAndReserved >> 4];
    pszString[21] = s_achDigits[pUuid->Gen.u8ClockSeqHiAndReserved & 0xf];
    pszString[22] = s_achDigits[pUuid->Gen.u8ClockSeqLow >> 4];
    pszString[23] = s_achDigits[pUuid->Gen.u8ClockSeqLow & 0xf];
    pszString[24] = '-';
    pszString[25] = s_achDigits[pUuid->Gen.au8Node[0] >> 4];
    pszString[26] = s_achDigits[pUuid->Gen.au8Node[0] & 0xf];
    pszString[27] = s_achDigits[pUuid->Gen.au8Node[1] >> 4];
    pszString[28] = s_achDigits[pUuid->Gen.au8Node[1] & 0xf];
    pszString[29] = s_achDigits[pUuid->Gen.au8Node[2] >> 4];
    pszString[30] = s_achDigits[pUuid->Gen.au8Node[2] & 0xf];
    pszString[31] = s_achDigits[pUuid->Gen.au8Node[3] >> 4];
    pszString[32] = s_achDigits[pUuid->Gen.au8Node[3] & 0xf];
    pszString[33] = s_achDigits[pUuid->Gen.au8Node[4] >> 4];
    pszString[34] = s_achDigits[pUuid->Gen.au8Node[4] & 0xf];
    pszString[35] = s_achDigits[pUuid->Gen.au8Node[5] >> 4];
    pszString[36] = s_achDigits[pUuid->Gen.au8Node[5] & 0xf];
    pszString[37] = '}';
    pszString[38] = '\0';

    return pszString;

}


/**
 * Sets a registry string value, wide char variant.
 *
 * @returns See RegSetValueExA (errors are remembered in the state).
 * @param   pState              The registry modifier state.
 * @param   hkey                The key to add the value to.
 * @param   pwszValueNm         The value name. NULL for setting the default.
 * @param   pwszValue           The value string.
 * @param   uLine               The line we're called from.
 */
static LSTATUS vbpsSetRegValueWW(VBPSREGSTATE *pState, HKEY hkey, PCRTUTF16 pwszValueNm, PCRTUTF16 pwszValue, unsigned uLine)
{
    DWORD const cbValue = (DWORD)((RTUtf16Len(pwszValue) + 1) * sizeof(RTUTF16));
    LSTATUS lrc;
    Assert(pState->fUpdate);

    /*
     * If we're not deleting the key prior to updating, we're in gentle update
     * mode where we will query if the existing value matches the incoming one.
     */
    if (!pState->fDelete)
    {
        DWORD       cbExistingData   = cbValue + 128;
        PRTUTF16    pwszExistingData = (PRTUTF16)alloca(cbExistingData);
        DWORD       dwExistingType;
        lrc = RegQueryValueExW(hkey, pwszValueNm, 0 /*Reserved*/, &dwExistingType, (BYTE *)pwszExistingData, &cbExistingData);
        if (lrc == ERROR_SUCCESS)
        {
            if (   dwExistingType == REG_SZ
                && cbExistingData == cbValue)
            {
                if (memcmp(pwszValue, pwszExistingData, cbValue) == 0)
                    return ERROR_SUCCESS;
            }
            VBSP_LOG_VALUE_CHANGE(("vbpsSetRegValueWW: Value difference: dwExistingType=%d cbExistingData=%#x cbValue=%#x\n"
                                   " hkey=%#x %ls; value name=%ls\n"
                                   "existing: %.*Rhxs (%.*ls)\n"
                                   "     new: %.*Rhxs (%ls)\n",
                                   dwExistingType, cbExistingData, cbValue,
                                   hkey, vbpsDebugKeyToWSZ(hkey), pwszValueNm ? pwszValueNm : L"(default)",
                                   cbExistingData, pwszExistingData, cbExistingData / sizeof(RTUTF16), pwszExistingData,
                                   cbValue, pwszValue, pwszValue));
        }
        else
            Assert(lrc == ERROR_FILE_NOT_FOUND || lrc == ERROR_MORE_DATA);
    }

    /*
     * Set the value.
     */
    lrc = RegSetValueExW(hkey, pwszValueNm, 0 /*Reserved*/, REG_SZ, (const BYTE *)pwszValue, cbValue);
    if (lrc == ERROR_SUCCESS)
    {
        VBSP_LOG_SET_VALUE(("vbpsSetRegValueWW: %ls/%ls=%ls (at %d)\n",
                            vbpsDebugKeyToWSZ(hkey), pwszValueNm ? pwszValueNm : L"(Default)", pwszValue, uLine));
        return ERROR_SUCCESS;
    }

    AssertLogRelMsg(VBPS_LOGREL_NO_ASSERT(lrc == ERROR_ACCESS_DENIED),
                    ("%d: '%ls'='%ls' -> %u\n", uLine, pwszValueNm, pwszValue, lrc));
    pState->lrc = lrc;
    return lrc;
}


/**
 * Sets a registry string value.
 *
 * @returns See RegSetValueExA (errors are remembered in the state).
 * @param   pState              The registry modifier state.
 * @param   hkey                The key to add the value to.
 * @param   pszValueNm          The value name. NULL for setting the default.
 * @param   pszValue            The value string.
 * @param   uLine               The line we're called from.
 */
static LSTATUS vbpsSetRegValueAA(VBPSREGSTATE *pState, HKEY hkey, const char *pszValueNm, const char *pszValue, unsigned uLine)
{
    DWORD const cbValue = (DWORD)strlen(pszValue) + 1;
    LSTATUS lrc;
    Assert(pState->fUpdate);

    /*
     * If we're not deleting the key prior to updating, we're in gentle update
     * mode where we will query if the existing value matches the incoming one.
     */
    if (!pState->fDelete)
    {
        DWORD cbExistingData = cbValue + 128;
        char *pszExistingData = alloca(cbExistingData);
        DWORD dwExistingType;
        lrc = RegQueryValueExA(hkey, pszValueNm, 0 /*Reserved*/, &dwExistingType, (PBYTE)pszExistingData, &cbExistingData);
        if (lrc == ERROR_SUCCESS)
        {
            if (   dwExistingType == REG_SZ
                && cbExistingData == cbValue)
            {
                if (memcmp(pszValue, pszExistingData, cbValue) == 0)
                    return ERROR_SUCCESS;
                if (memicmp(pszValue, pszExistingData, cbValue) == 0)
                    return ERROR_SUCCESS;
            }
            VBSP_LOG_VALUE_CHANGE(("vbpsSetRegValueAA: Value difference: dwExistingType=%d cbExistingData=%#x cbValue=%#x\n"
                                   " hkey=%#x %ls; value name=%s\n"
                                   "existing: %.*Rhxs (%.*s)\n"
                                   "     new: %.*Rhxs (%s)\n",
                                   dwExistingType, cbExistingData, cbValue,
                                   hkey, vbpsDebugKeyToWSZ(hkey), pszValueNm ? pszValueNm : "(default)",
                                   cbExistingData, pszExistingData, cbExistingData, pszExistingData,
                                   cbValue, pszValue, pszValue));
        }
        else
            Assert(lrc == ERROR_FILE_NOT_FOUND || lrc == ERROR_MORE_DATA);
    }

    /*
     * Set the value.
     */
    lrc = RegSetValueExA(hkey, pszValueNm, 0 /*Reserved*/, REG_SZ, (PBYTE)pszValue, cbValue);
    if (lrc == ERROR_SUCCESS)
    {
        VBSP_LOG_SET_VALUE(("vbpsSetRegValueAA: %ls/%s=%s (at %d)\n",
                            vbpsDebugKeyToWSZ(hkey), pszValueNm ? pszValueNm : "(Default)", pszValue, uLine));
        return ERROR_SUCCESS;
    }

    AssertLogRelMsg(VBPS_LOGREL_NO_ASSERT(lrc == ERROR_ACCESS_DENIED),
                    ("%d: '%s'='%s' -> %u\n", uLine, pszValueNm, pszValue, lrc));
    pState->lrc = lrc;
    return lrc;
}


/**
 * Closes a registry key.
 *
 * @returns See RegCloseKey (errors are remembered in the state).
 * @param   pState              The registry modifier state.
 * @param   hkey                The key to close.
 * @param   uLine               The line we're called from.
 */
static LSTATUS vbpsCloseKey(VBPSREGSTATE *pState, HKEY hkey, unsigned uLine)
{
    LSTATUS lrc = RegCloseKey(hkey);
    if (lrc == ERROR_SUCCESS)
        return ERROR_SUCCESS;

    AssertLogRelMsgFailed(("%d: close key -> %u\n", uLine, lrc));
    pState->lrc = lrc;
    return lrc;
}


/**
 * Creates a registry key.
 *
 * @returns See RegCreateKeyA and RegSetValueExA (errors are remembered in the
 *          state).
 * @param   pState              The registry modifier state.
 * @param   hkeyParent          The parent key.
 * @param   pszKey              The new key under @a hkeyParent.
 * @param   phkey               Where to return the handle to the new key.
 * @param   uLine               The line we're called from.
 */
static LSTATUS vbpsCreateRegKeyA(VBPSREGSTATE *pState, HKEY hkeyParent, const char *pszKey, PHKEY phkey, unsigned uLine)
{
    /*
     * This will open if it exists and create if new, which is exactly what we want.
     */
    HKEY hNewKey;
    DWORD dwDisposition = 0;
    LSTATUS lrc = RegCreateKeyExA(hkeyParent, pszKey, 0 /*Reserved*/, NULL /*pszClass*/, 0 /*fOptions*/,
                                  pState->fSamBoth, NULL /*pSecAttr*/, &hNewKey, &dwDisposition);
    if (lrc == ERROR_SUCCESS)
    {
        *phkey = hNewKey;
        if (dwDisposition == REG_CREATED_NEW_KEY)
            VBSP_LOG_NEW_KEY(("vbpsCreateRegKeyA: %ls/%s (at %d)\n", vbpsDebugKeyToWSZ(hkeyParent), pszKey, uLine));
    }
    else
    {
        AssertLogRelMsg(VBPS_LOGREL_NO_ASSERT(lrc == ERROR_ACCESS_DENIED),
                        ("%d: create key '%s' -> %u\n", uLine, pszKey,  lrc));
        pState->lrc = lrc;
        *phkey = NULL;
    }
    return lrc;
}


/**
 * Creates a registry key with a default string value.
 *
 * @returns See RegCreateKeyA and RegSetValueExA (errors are remembered in the
 *          state).
 * @param   pState              The registry modifier state.
 * @param   hkeyParent          The parent key.
 * @param   pszKey              The new key under @a hkeyParent.
 * @param   pszValue            The value string.
 * @param   uLine               The line we're called from.
 */
static LSTATUS vbpsCreateRegKeyWithDefaultValueAA(VBPSREGSTATE *pState, HKEY hkeyParent, const char *pszKey,
                                                  const char *pszValue, unsigned uLine)
{
    HKEY hNewKey;
    LSTATUS lrc = vbpsCreateRegKeyA(pState, hkeyParent, pszKey, &hNewKey, uLine);
    if (lrc == ERROR_SUCCESS)
    {
        lrc = vbpsSetRegValueAA(pState, hNewKey, NULL /*pszValueNm*/, pszValue, uLine);
        vbpsCloseKey(pState, hNewKey, uLine);
    }
    else
    {
        AssertLogRelMsg(VBPS_LOGREL_NO_ASSERT(lrc == ERROR_ACCESS_DENIED),
                        ("%d: create key '%s'(/Default='%s') -> %u\n", uLine, pszKey, pszValue, lrc));
        pState->lrc = lrc;
    }
    return lrc;
}


/**
 * Creates a registry key with a default wide string value.
 *
 * @returns See RegCreateKeyA and RegSetValueExA (errors are remembered in the
 *          state).
 * @param   pState              The registry modifier state.
 * @param   hkeyParent          The parent key.
 * @param   pszKey              The new key under @a hkeyParent.
 * @param   pwszValue           The value string.
 * @param   uLine               The line we're called from.
 */
static LSTATUS vbpsCreateRegKeyWithDefaultValueAW(VBPSREGSTATE *pState, HKEY hkeyParent, const char *pszKey,
                                                  PCRTUTF16 pwszValue, unsigned uLine)
{
    HKEY hNewKey;
    LSTATUS lrc = vbpsCreateRegKeyA(pState, hkeyParent, pszKey, &hNewKey, uLine);
    if (lrc == ERROR_SUCCESS)
    {
        lrc = vbpsSetRegValueWW(pState, hNewKey, NULL /*pwszValueNm*/, pwszValue, uLine);
        vbpsCloseKey(pState, hNewKey, uLine);
    }
    else
    {
        AssertLogRelMsg(VBPS_LOGREL_NO_ASSERT(lrc == ERROR_ACCESS_DENIED),
                        ("%d: create key '%s'(/Default='%ls') -> %u\n", uLine, pszKey, pwszValue, lrc));
        pState->lrc = lrc;
    }
    return lrc;
}


/**
 * Creates a registry key with a default string value, return the key.
 *
 * @returns See RegCreateKeyA and RegSetValueExA (errors are remembered in the
 *          state).
 * @param   pState              The registry modifier state.
 * @param   hkeyParent          The parent key.
 * @param   pszKey              The new key under @a hkeyParent.
 * @param   pszValue            The value string.
 * @param   phkey               Where to return the handle to the new key.
 * @param   uLine               The line we're called from.
 */
static LSTATUS vbpsCreateRegKeyWithDefaultValueAAEx(VBPSREGSTATE *pState, HKEY hkeyParent, const char *pszKey,
                                                    const char *pszValue, PHKEY phkey, unsigned uLine)
{
    HKEY hNewKey;
    LSTATUS lrc = vbpsCreateRegKeyA(pState, hkeyParent, pszKey, &hNewKey, uLine);
    if (lrc == ERROR_SUCCESS)
    {
        lrc = vbpsSetRegValueAA(pState, hNewKey, NULL /*pszValueNm*/, pszValue, uLine);
        *phkey = hNewKey;
    }
    else
    {
        AssertLogRelMsg(VBPS_LOGREL_NO_ASSERT(lrc == ERROR_ACCESS_DENIED),
                        ("%d: create key '%s'(/Default='%s') -> %u\n", uLine, pszKey, pszValue, lrc));
        pState->lrc = lrc;
        *phkey = NULL;
    }
    return lrc;
}


/**
 * Recursively deletes a registry key.
 *
 * @returns See SHDeleteKeyA (errors are remembered in the state).
 * @param   pState              The registry modifier state.
 * @param   hkeyParent          The parent key.
 * @param   pszKey              The key under @a hkeyParent that should be
 *                              deleted.
 * @param   uLine               The line we're called from.
 */
static LSTATUS vbpsDeleteKeyRecursiveA(VBPSREGSTATE *pState, HKEY hkeyParent, const char *pszKey, unsigned uLine)
{
    LSTATUS lrc;

    Assert(pState->fDelete);
    Assert(pszKey);
    AssertReturn(*pszKey != '\0', pState->lrc = ERROR_INVALID_PARAMETER);

#ifdef VBSP_LOG_ENABLED
    {
        HKEY hkeyLog;
        lrc = RegOpenKeyExA(hkeyParent, pszKey, 0 /*fOptions*/, pState->fSamDelete, &hkeyLog);
        if (lrc != ERROR_FILE_NOT_FOUND)
            VBSP_LOG_DEL_KEY(("vbpsDeleteKeyRecursiveA: %ls/%s (at %d)\n", vbpsDebugKeyToWSZ(hkeyParent), pszKey, uLine));
        if (lrc == ERROR_SUCCESS)
            RegCloseKey(hkeyLog);
    }
#endif

    lrc = SHDeleteKeyA(hkeyParent, pszKey);
    if (lrc == ERROR_SUCCESS || lrc == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;

    AssertLogRelMsg(VBPS_LOGREL_NO_ASSERT(lrc == ERROR_ACCESS_DENIED),
                    ("%d: delete key '%s' -> %u\n", uLine, pszKey, lrc));
    pState->lrc = lrc;
    return lrc;
}


/**
 * Recursively deletes a registry key, wide char version.
 *
 * @returns See SHDeleteKeyW (errors are remembered in the state).
 * @param   pState              The registry modifier state.
 * @param   hkeyParent          The parent key.
 * @param   pwszKey             The key under @a hkeyParent that should be
 *                              deleted.
 * @param   uLine               The line we're called from.
 */
static LSTATUS vbpsDeleteKeyRecursiveW(VBPSREGSTATE *pState, HKEY hkeyParent, PCRTUTF16 pwszKey, unsigned uLine)
{
    LSTATUS lrc;

    Assert(pState->fDelete);
    Assert(pwszKey);
    AssertReturn(*pwszKey != '\0', pState->lrc = ERROR_INVALID_PARAMETER);

#ifdef VBSP_LOG_ENABLED
    {
        HKEY hkeyLog;
        lrc = RegOpenKeyExW(hkeyParent, pwszKey, 0 /*fOptions*/, pState->fSamDelete, &hkeyLog);
        if (lrc != ERROR_FILE_NOT_FOUND)
            VBSP_LOG_DEL_KEY(("vbpsDeleteKeyRecursiveW: %ls/%ls (at %d)\n", vbpsDebugKeyToWSZ(hkeyParent), pwszKey, uLine));
        if (lrc == ERROR_SUCCESS)
            RegCloseKey(hkeyLog);
    }
#endif

    lrc = SHDeleteKeyW(hkeyParent, pwszKey);
    if (lrc == ERROR_SUCCESS || lrc == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;

    AssertLogRelMsg(VBPS_LOGREL_NO_ASSERT(lrc == ERROR_ACCESS_DENIED),
                    ("%d: delete key '%ls' -> %u\n", uLine, pwszKey, lrc));
    pState->lrc = lrc;
    return lrc;
}


/**
 * Register an application id.
 *
 * @returns Windows error code (errors are rememberd in the state).
 * @param   pState              The registry modifier state.
 * @param   pszModuleName       The module name.
 * @param   pszAppId            The application UUID string.
 * @param   pszDescription      The description string.
 * @param   pszServiceName      The window service name if the application is a
 *                              service, otherwise this must be NULL.
 */
LSTATUS VbpsRegisterAppId(VBPSREGSTATE *pState, const char *pszModuleName, const char *pszAppId,
                          const char *pszDescription, const char *pszServiceName)
{
    LSTATUS lrc;
    HKEY hkeyAppIds;
    Assert(*pszAppId == '{');

    /*
     * Delete.
     */
    if (pState->fDelete)
    {
        unsigned i = pState->cAltDeletes;
        while (i-- > 0)
        {
            lrc = RegOpenKeyExW(pState->aAltDeletes[i].hkeyClasses, L"AppID", 0 /*fOptions*/, pState->fSamDelete, &hkeyAppIds);
            AssertLogRelMsgStmt(lrc == ERROR_SUCCESS || lrc == ERROR_FILE_NOT_FOUND, ("%u\n", lrc), pState->lrc = lrc);
            if (lrc == ERROR_SUCCESS)
            {
                vbpsDeleteKeyRecursiveA(pState, hkeyAppIds, pszAppId, __LINE__);
                vbpsCloseKey(pState, hkeyAppIds, __LINE__);
            }
        }
    }

    if (pState->fUpdate)
    {
        lrc = RegCreateKeyExW(pState->hkeyClassesRootDst, L"AppID", 0 /*Reserved*/, NULL /*pszClass*/, 0 /*fOptions*/,
                              pState->fSamBoth, NULL /*pSecAttr*/, &hkeyAppIds, NULL /*pdwDisposition*/);
        if (lrc == ERROR_ACCESS_DENIED)
            return ERROR_SUCCESS;
    }
    else
    {
        lrc = RegOpenKeyExW(pState->hkeyClassesRootDst, L"AppID", 0 /*fOptions*/, pState->fSamBoth, &hkeyAppIds);
        if (lrc == ERROR_FILE_NOT_FOUND || lrc == ERROR_ACCESS_DENIED)
            return ERROR_SUCCESS;
    }
    if (lrc == ERROR_ACCESS_DENIED)
        return pState->lrc = lrc;
    AssertLogRelMsgReturn(lrc == ERROR_SUCCESS, ("%u\n", lrc), pState->lrc = lrc);

    if (pState->fDelete)
    {
        vbpsDeleteKeyRecursiveA(pState, hkeyAppIds, pszAppId, __LINE__);
        vbpsDeleteKeyRecursiveA(pState, hkeyAppIds, pszModuleName, __LINE__);
    }

    /*
     * Register / update.
     */
    if (pState->fUpdate)
    {
        HKEY hkey;
        lrc = vbpsCreateRegKeyA(pState, hkeyAppIds, pszAppId, &hkey, __LINE__);
        if (lrc == ERROR_SUCCESS)
        {
            vbpsSetRegValueAA(pState, hkey, NULL /*pszValueNm*/, pszDescription, __LINE__);
            if (pszServiceName)
                vbpsSetRegValueAA(pState, hkey, "LocalService", pszServiceName, __LINE__);
            vbpsCloseKey(pState, hkey, __LINE__);
        }

        lrc = vbpsCreateRegKeyA(pState, hkeyAppIds, pszModuleName, &hkey, __LINE__);
        if (lrc == ERROR_SUCCESS)
        {
            vbpsSetRegValueAA(pState, hkey, NULL /*pszValueNm*/, "", __LINE__);
            vbpsSetRegValueAA(pState, hkey, "AppID", pszAppId, __LINE__);
            vbpsCloseKey(pState, hkey, __LINE__);
        }
    }

    vbpsCloseKey(pState, hkeyAppIds, __LINE__);

    return pState->lrc;
}


/**
 * Register an class name.
 *
 * @returns Windows error code (errors are rememberd in the state).
 * @param   pState              The registry modifier state.
 * @param   pszClassName        The name of the class.
 * @param   pszDescription      The description string
 * @param   pClsId              The UUID for the class.
 * @param   pszCurVerSuffIfRootName     This is the current version suffix to
 *                                      append to @a pszClassName when
 *                                      registering the version idependent name.
 */
LSTATUS VbpsRegisterClassName(VBPSREGSTATE *pState, const char *pszClassName, const char *pszDescription,
                              const CLSID *pClsId, const char *pszCurVerSuffIfRootName)
{
    LSTATUS lrc;

    /*
     * Delete.
     */
    if (pState->fDelete)
    {
        unsigned i = pState->cAltDeletes;
        while (i-- > 0)
            vbpsDeleteKeyRecursiveA(pState, pState->aAltDeletes[i].hkeyClasses, pszClassName, __LINE__);
        vbpsDeleteKeyRecursiveA(pState, pState->hkeyClassesRootDst, pszClassName, __LINE__);
    }

    /*
     * Update.
     */
    if (pState->fUpdate)
    {
        /* pszClassName/Default = description. */
        HKEY hkeyClass;
        lrc = vbpsCreateRegKeyWithDefaultValueAAEx(pState, pState->hkeyClassesRootDst, pszClassName, pszDescription,
                                                   &hkeyClass, __LINE__);
        if (lrc == ERROR_SUCCESS)
        {
            char szClsId[CURLY_UUID_STR_BUF_SIZE];

            /* CLSID/Default = pClsId. */
            vbpsCreateRegKeyWithDefaultValueAA(pState, hkeyClass, "CLSID", vbpsFormatUuidInCurly(szClsId, pClsId), __LINE__);

            /* CurVer/Default = pszClassName+Suffix. */
            if (pszCurVerSuffIfRootName != NULL)
            {
                char szCurClassNameVer[128];
                lrc = RTStrCopy(szCurClassNameVer, sizeof(szCurClassNameVer), pszClassName);
                if (RT_SUCCESS(lrc))
                    lrc = RTStrCat(szCurClassNameVer, sizeof(szCurClassNameVer), pszCurVerSuffIfRootName);
                AssertStmt(RT_SUCCESS(lrc), pState->lrc = lrc = ERROR_INVALID_DATA);
                if (lrc == ERROR_SUCCESS)
                    vbpsCreateRegKeyWithDefaultValueAA(pState, hkeyClass, "CurVer", szCurClassNameVer, __LINE__);
            }

            vbpsCloseKey(pState, hkeyClass, __LINE__);
        }
    }

    return pState->lrc;
}


/**
 * Registers a class ID.
 *
 * @returns Windows error code (errors are rememberd in the state).
 * @param   pState                      The registry modifier state.
 * @param   pClsId                      The UUID for the class.
 * @param   pszDescription              The description string.
 * @param   pszAppId                    The application ID.
 * @param   pszClassName                The version idependent class name.
 * @param   pszCurClassNameVerSuffix    The suffix to add to @a pszClassName for
 *                                      the current version.
 * @param   pTypeLibId                  The UUID for the typelib this class
 *                                      belongs to.
 * @param   pszServerType               The server type (InprocServer32 or
 *                                      LocalServer32).
 * @param   pwszVBoxDir                 The VirtualBox install directory
 *                                      (unicode), trailing slash.
 * @param   pszServerSubPath            What to append to @a pwszVBoxDir to
 *                                      construct the server module name.
 * @param   pszThreadingModel           The threading model for inproc servers,
 *                                      NULL for local servers.
 */
LSTATUS VbpsRegisterClassId(VBPSREGSTATE *pState, const CLSID *pClsId, const char *pszDescription, const char *pszAppId,
                            const char *pszClassName, const char *pszCurClassNameVerSuffix, const CLSID *pTypeLibId,
                            const char *pszServerType, PCRTUTF16 pwszVBoxDir, const char *pszServerSubPath,
                            const char *pszThreadingModel)
{
    LSTATUS lrc;
    char szClsId[CURLY_UUID_STR_BUF_SIZE];
    RT_NOREF(pszAppId);

    Assert(!pszAppId || *pszAppId == '{');
    Assert((pwszVBoxDir == NULL && !pState->fUpdate) || pwszVBoxDir[RTUtf16Len(pwszVBoxDir) - 1] == '\\');

    /*
     * We need this, whatever we end up having to do.
     */
    vbpsFormatUuidInCurly(szClsId, pClsId);

    /*
     * Delete.
     */
    if (pState->fDelete)
    {
        unsigned i = pState->cAltDeletes;
        while (i-- > 0)
            if (pState->aAltDeletes[i].hkeyClsid != NULL)
                vbpsDeleteKeyRecursiveA(pState, pState->aAltDeletes[i].hkeyClsid, szClsId, __LINE__);
        vbpsDeleteKeyRecursiveA(pState, pState->hkeyClsidRootDst, szClsId, __LINE__);
    }

    /*
     * Update.
     */
    if (pState->fUpdate)
    {
        HKEY hkeyClass;
        lrc = vbpsCreateRegKeyWithDefaultValueAAEx(pState, pState->hkeyClsidRootDst, szClsId, pszDescription,
                                                   &hkeyClass, __LINE__);
        if (lrc == ERROR_SUCCESS)
        {
            bool const fIsLocalServer32 = strcmp(pszServerType, "LocalServer32") == 0;
            HKEY hkeyServerType;
            char szCurClassNameVer[128];

            /* pszServerType/Default = module. */
            lrc = vbpsCreateRegKeyA(pState, hkeyClass, pszServerType, &hkeyServerType, __LINE__);
            if (lrc == ERROR_SUCCESS)
            {
                RTUTF16 wszModule[MAX_PATH * 2];
                PRTUTF16 pwszCur = wszModule;
                if (fIsLocalServer32)
                    *pwszCur++ = '"';

                int vrc = RTUtf16Copy(pwszCur, MAX_PATH, pwszVBoxDir); AssertRC(vrc);
                pwszCur += RTUtf16Len(pwszCur);
                vrc = RTUtf16CopyAscii(pwszCur, MAX_PATH - 3, pszServerSubPath); AssertRC(vrc);
                pwszCur += RTUtf16Len(pwszCur);

                if (fIsLocalServer32)
                    *pwszCur++ = '"';
                *pwszCur++ = '\0';      /* included, so ++. */

                vbpsSetRegValueWW(pState, hkeyServerType, NULL /*pszValueNm*/, wszModule, __LINE__);

                /* pszServerType/ThreadingModel = pszThreading Model. */
                if (pszThreadingModel)
                    vbpsSetRegValueAA(pState, hkeyServerType, "ThreadingModel", pszThreadingModel, __LINE__);

                vbpsCloseKey(pState, hkeyServerType, __LINE__);
            }

            /* ProgId/Default = pszClassName + pszCurClassNameVerSuffix. */
            if (pszClassName)
            {
                int vrc = RTStrCopy(szCurClassNameVer, sizeof(szCurClassNameVer), pszClassName);
                if (RT_SUCCESS(vrc))
                    vrc = RTStrCat(szCurClassNameVer, sizeof(szCurClassNameVer), pszCurClassNameVerSuffix);
                AssertStmt(RT_SUCCESS(vrc), pState->lrc = lrc = ERROR_INVALID_DATA);
                if (lrc == ERROR_SUCCESS)
                    vbpsCreateRegKeyWithDefaultValueAA(pState, hkeyClass, "ProgId", szCurClassNameVer, __LINE__);

                /* VersionIndependentProgID/Default = pszClassName. */
                vbpsCreateRegKeyWithDefaultValueAA(pState, hkeyClass, "VersionIndependentProgID", pszClassName, __LINE__);
            }

            /* TypeLib/Default = pTypeLibId. */
            if (pTypeLibId)
            {
                char szTypeLibId[CURLY_UUID_STR_BUF_SIZE];
                vbpsCreateRegKeyWithDefaultValueAA(pState, hkeyClass, "TypeLib",
                                                   vbpsFormatUuidInCurly(szTypeLibId, pTypeLibId), __LINE__);
            }

            /* AppID = pszAppId */
            if (pszAppId && fIsLocalServer32)
                vbpsSetRegValueAA(pState, hkeyClass, "AppID", pszAppId, __LINE__);

            vbpsCloseKey(pState, hkeyClass, __LINE__);
        }
    }

    return pState->lrc;
}


/**
 * Register modules and classes from the VirtualBox.xidl file.
 *
 * @param   pState
 * @param   pwszVBoxDir         The VirtualBox application directory.
 * @param   fIs32On64           Set if this is the 32-bit on 64-bit component.
 *
 * @todo convert to XSLT.
 */
void RegisterXidlModulesAndClassesGenerated(VBPSREGSTATE *pState, PCRTUTF16 pwszVBoxDir, bool fIs32On64)
{
    const char *pszAppId            = "{819B4D85-9CEE-493C-B6FC-64FFE759B3C9}";
    const char *pszInprocDll        = !fIs32On64 ? "VBoxC.dll" : "x86\\VBoxClient-x86.dll";
    const char *pszLocalServer      = "VBoxSVC.exe";
#ifdef VBOX_WITH_SDS
    const char *pszSdsAppId         = "{EC0E78E8-FA43-43E8-AC0A-02C784C4A4FA}";
    const char *pszSdsExe           = "VBoxSDS.exe";
    const char *pszSdsServiceName   = "VBoxSDS";
#endif

    /* VBoxSVC */
    VbpsRegisterAppId(pState, pszLocalServer, pszAppId, "VirtualBox Application", NULL);
    VbpsRegisterClassName(pState, "VirtualBox.VirtualBox.1", "VirtualBox Class", &CLSID_VirtualBox, NULL);
    VbpsRegisterClassName(pState, "VirtualBox.VirtualBox",   "VirtualBox Class", &CLSID_VirtualBox, ".1");
    VbpsRegisterClassId(pState, &CLSID_VirtualBox, "VirtualBox Class", pszAppId, "VirtualBox.VirtualBox", ".1",
                        &LIBID_VirtualBox, "LocalServer32", pwszVBoxDir, pszLocalServer, NULL /*N/A*/);
    /* VBoxC */
    VbpsRegisterClassName(pState, "VirtualBox.Session.1", "Session Class", &CLSID_Session, NULL);
    VbpsRegisterClassName(pState, "VirtualBox.Session", "Session Class", &CLSID_Session, ".1");
    VbpsRegisterClassId(pState, &CLSID_Session, "Session Class", pszAppId, "VirtualBox.Session", ".1",
                        &LIBID_VirtualBox, "InprocServer32", pwszVBoxDir, pszInprocDll, "Free");

    VbpsRegisterClassName(pState, "VirtualBox.VirtualBoxClient.1", "VirtualBoxClient Class", &CLSID_VirtualBoxClient, NULL);
    VbpsRegisterClassName(pState, "VirtualBox.VirtualBoxClient", "VirtualBoxClient Class", &CLSID_VirtualBoxClient, ".1");
    VbpsRegisterClassId(pState, &CLSID_VirtualBoxClient, "VirtualBoxClient Class", pszAppId,
                        "VirtualBox.VirtualBoxClient", ".1",
                        &LIBID_VirtualBox, "InprocServer32", pwszVBoxDir, pszInprocDll, "Free");

#ifdef VBOX_WITH_SDS
    /* VBoxSDS */
    VbpsRegisterAppId(pState, pszSdsExe, pszSdsAppId, "VirtualBox System Service", pszSdsServiceName);
    VbpsRegisterClassName(pState, "VirtualBox.VirtualBoxSDS.1", "VirtualBoxSDS Class", &CLSID_VirtualBoxSDS, NULL);
    VbpsRegisterClassName(pState, "VirtualBox.VirtualBoxSDS", "VirtualBoxSDS Class", &CLSID_VirtualBoxSDS, ".1");
    VbpsRegisterClassId(pState, &CLSID_VirtualBoxSDS, "VirtualBoxSDS Class", pszSdsAppId, "VirtualBox.VirtualBoxSDS", ".1",
                        &LIBID_VirtualBox, "LocalServer32", pwszVBoxDir, pszSdsExe, NULL /*N/A*/);
#endif
}


/**
 * Updates the VBox type lib registration.
 *
 * This is only used when updating COM registrations during com::Initialize.
 * For normal registration and unregistrations we use the RegisterTypeLib and
 * UnRegisterTypeLib APIs.
 *
 * @param   pState              The registry modifier state.
 * @param   pwszVBoxDir         The VirtualBox install directory (unicode),
 *                              trailing slash.
 * @param   fIs32On64           Set if we're registering the 32-bit proxy stub
 *                              on a 64-bit system.
 */
static void vbpsUpdateTypeLibRegistration(VBPSREGSTATE *pState, PCRTUTF16 pwszVBoxDir, bool fIs32On64)
{
    const char * const pszTypeLibDll  = VBPS_PROXY_STUB_FILE(fIs32On64);
#if ARCH_BITS == 32 && !defined(VBOX_IN_32_ON_64_MAIN_API)
    const char * const pszWinXx       = "win32";
#else
    const char * const pszWinXx       = !fIs32On64 ? "win64" : "win32";
#endif
    const char * const pszDescription = "VirtualBox Type Library";

    char szTypeLibId[CURLY_UUID_STR_BUF_SIZE];
    HKEY hkeyTypeLibs;
    HKEY hkeyTypeLibId;
    LSTATUS lrc;
    RT_NOREF(fIs32On64);

    Assert(pState->fUpdate && !pState->fDelete);

    /*
     * Type library registration (w/o interfaces).
     */

    /* Open Classes/TypeLib/. */
    lrc = vbpsCreateRegKeyA(pState, pState->hkeyClassesRootDst, "TypeLib", &hkeyTypeLibs, __LINE__);
    if (lrc != ERROR_SUCCESS)
        return;

    /* Create TypeLib/{UUID}. */
    lrc = vbpsCreateRegKeyA(pState, hkeyTypeLibs, vbpsFormatUuidInCurly(szTypeLibId, &LIBID_VirtualBox), &hkeyTypeLibId, __LINE__);
    if (lrc == ERROR_SUCCESS)
    {
        /* {UUID}/Major.Minor/Default = pszDescription. */
        HKEY hkeyMajMin;
        char szMajMin[64];
        sprintf(szMajMin, "%u.%u", kTypeLibraryMajorVersion, kTypeLibraryMinorVersion);
        lrc = vbpsCreateRegKeyWithDefaultValueAAEx(pState, hkeyTypeLibId, szMajMin, pszDescription, &hkeyMajMin, __LINE__);
        if (lrc == ERROR_SUCCESS)
        {
            RTUTF16 wszBuf[MAX_PATH * 2];

            /* {UUID}/Major.Minor/0. */
            HKEY hkey0;
            lrc = vbpsCreateRegKeyA(pState, hkeyMajMin, "0", &hkey0, __LINE__);
            if (lrc == ERROR_SUCCESS)
            {
                /* {UUID}/Major.Minor/0/winXX/Default = VBoxProxyStub. */
                int vrc = RTUtf16Copy(wszBuf, MAX_PATH, pwszVBoxDir); AssertRC(vrc);
                vrc = RTUtf16CatAscii(wszBuf, MAX_PATH * 2, pszTypeLibDll); AssertRC(vrc);

                vbpsCreateRegKeyWithDefaultValueAW(pState, hkey0, pszWinXx, wszBuf, __LINE__);
                vbpsCloseKey(pState, hkey0, __LINE__);
            }

            /* {UUID}/Major.Minor/FLAGS */
            vbpsCreateRegKeyWithDefaultValueAA(pState, hkeyMajMin, "FLAGS", "0", __LINE__);

            /* {UUID}/Major.Minor/HELPDIR */
            int vrc = RTUtf16Copy(wszBuf, MAX_PATH, pwszVBoxDir); AssertRC(vrc);
#if 0 /* MSI: trailing slash;  regsvr32/comregister: strip unnecessary trailing slash.  Go with MSI to avoid user issues. */
            {
                size_t off = RTUtf16Len(wszBuf);
                while (off > 2 && wszBuf[off - 2] != ':' && RTPATH_IS_SLASH(wszBuf[off - 1]))
                    off--;
                wszBuf[off] = '\0';
            }
#endif
            vbpsCreateRegKeyWithDefaultValueAW(pState, hkeyMajMin, "HELPDIR", wszBuf, __LINE__);

            vbpsCloseKey(pState, hkeyMajMin, __LINE__);
        }
        vbpsCloseKey(pState, hkeyTypeLibId, __LINE__);
    }
    vbpsCloseKey(pState, hkeyTypeLibs, __LINE__);
}


/**
 * Update the VBox proxy stub registration.
 *
 * This is only used when updating COM registrations during com::Initialize.
 * For normal registration and unregistrations we use the NdrDllRegisterProxy
 * and NdrDllUnregisterProxy.
 *
 * @param   pState              The registry modifier state.
 * @param   pwszVBoxDir         The VirtualBox install directory (unicode),
 *                              trailing slash.
 * @param   fIs32On64           Set if we're registering the 32-bit proxy stub
 *                              on a 64-bit system.
 */
static void vbpsUpdateProxyStubRegistration(VBPSREGSTATE *pState, PCRTUTF16 pwszVBoxDir, bool fIs32On64)
{
    /*
     * Register the proxy stub factory class ID.
     * It's simple compared to the VBox classes, thus all the NULL parameters.
     */
    const char *pszPsDll = VBPS_PROXY_STUB_FILE(fIs32On64);
    RT_NOREF(fIs32On64);
    Assert(pState->fUpdate && !pState->fDelete);
    VbpsRegisterClassId(pState, &g_ProxyClsId, "PSFactoryBuffer", NULL /*pszAppId*/,
                        NULL /*pszClassName*/, NULL /*pszCurClassNameVerSuffix*/, NULL /*pTypeLibId*/,
                        "InprocServer32", pwszVBoxDir, pszPsDll, "Both");
}


/**
 * Updates the VBox interface registrations.
 *
 * This is only used when updating COM registrations during com::Initialize.
 * For normal registration and unregistrations we use the NdrDllRegisterProxy
 * and NdrDllUnregisterProxy.
 *
 * @param   pState              The registry modifier state.
 */
static void vbpsUpdateInterfaceRegistrations(VBPSREGSTATE *pState)
{
    const ProxyFileInfo **ppProxyFile = &g_apProxyFiles[0];
    const ProxyFileInfo  *pProxyFile;
    LSTATUS               lrc;
    char                  szProxyClsId[CURLY_UUID_STR_BUF_SIZE];
    char                  szTypeLibId[CURLY_UUID_STR_BUF_SIZE];
    char                  szTypeLibVersion[64];

    vbpsFormatUuidInCurly(szProxyClsId, &g_ProxyClsId);
    vbpsFormatUuidInCurly(szTypeLibId, &LIBID_VirtualBox);
    sprintf(szTypeLibVersion, "%u.%u", kTypeLibraryMajorVersion, kTypeLibraryMinorVersion);

    Assert(pState->fUpdate && !pState->fDelete);
    lrc = vbpsRegOpenInterfaceKeys(pState);
    if (lrc != ERROR_SUCCESS)
        return;

    /*
     * We walk the proxy file list (even if we only have one).
     */
    while ((pProxyFile = *ppProxyFile++) != NULL)
    {
        const PCInterfaceStubVtblList * const   papStubVtbls  = pProxyFile->pStubVtblList;
        const char * const                     *papszNames    = pProxyFile->pNamesArray;
        unsigned                                iIf           = pProxyFile->TableSize;
        AssertStmt(iIf < 1024, iIf = 0);
        Assert(pProxyFile->TableVersion == 2);

        /*
         * Walk the interfaces in that file, picking data from the various tables.
         */
        while (iIf-- > 0)
        {
            char                szIfId[CURLY_UUID_STR_BUF_SIZE];
            const char * const  pszIfNm  = papszNames[iIf];
            size_t const        cchIfNm  = RT_VALID_PTR(pszIfNm) ? strlen(pszIfNm) : 0;
            char                szMethods[32];
            uint32_t const      cMethods = papStubVtbls[iIf]->header.DispatchTableCount;
            HKEY                hkeyIfId;

            AssertReturnVoidStmt(cchIfNm >= 3 && cchIfNm <= 72, pState->lrc = ERROR_INVALID_DATA);

            AssertReturnVoidStmt(cMethods >= 3 && cMethods < 1024, pState->lrc = ERROR_INVALID_DATA);
            sprintf(szMethods, "%u", cMethods);

            lrc = vbpsCreateRegKeyWithDefaultValueAAEx(pState, pState->hkeyInterfaceRootDst,
                                                       vbpsFormatUuidInCurly(szIfId, papStubVtbls[iIf]->header.piid),
                                                       pszIfNm, &hkeyIfId, __LINE__);
            if (lrc == ERROR_SUCCESS)
            {
                HKEY hkeyTypeLib;
                vbpsCreateRegKeyWithDefaultValueAA(pState, hkeyIfId, "ProxyStubClsid32", szProxyClsId, __LINE__);
                vbpsCreateRegKeyWithDefaultValueAA(pState, hkeyIfId, "NumMethods", szMethods, __LINE__);

                /* The MSI seems to still be putting TypeLib keys here. So, let's do that too. */
                lrc = vbpsCreateRegKeyWithDefaultValueAAEx(pState, hkeyIfId, "TypeLib", szTypeLibId, &hkeyTypeLib, __LINE__);
                if (lrc == ERROR_SUCCESS)
                {
                    vbpsSetRegValueAA(pState, hkeyTypeLib, "Version", szTypeLibVersion, __LINE__);
                    vbpsCloseKey(pState, hkeyTypeLib, __LINE__);
                }

                vbpsCloseKey(pState, hkeyIfId, __LINE__);
            }
        }
    }
}


static bool vbpsIsUpToDate(VBPSREGSTATE *pState)
{
    /** @todo read some registry key and */
    NOREF(pState);
    return false;
}

static bool vbpsMarkUpToDate(VBPSREGSTATE *pState)
{
    /** @todo write the key vbpsIsUpToDate uses, if pState indicates success. */
    NOREF(pState);
    return false;
}



/**
 * Strips the stub dll name and any x86 subdir off the full DLL path to get a
 * path to the VirtualBox application directory.
 *
 * @param   pwszDllPath     The path to strip, returns will end with a slash.
 */
static void vbpsDllPathToVBoxDir(PRTUTF16 pwszDllPath)
{
    RTUTF16 wc;
    size_t off = RTUtf16Len(pwszDllPath);
    while (   off > 0
           && (   (wc = pwszDllPath[off - 1]) >= 127U
               || !RTPATH_IS_SEP((unsigned char)wc)))
        off--;

#ifdef VBOX_IN_32_ON_64_MAIN_API
    /*
     * The -x86 variant is in a x86 subdirectory, drop it.
     */
    while (   off > 0
           && (   (wc = pwszDllPath[off - 1]) < 127U
               && RTPATH_IS_SEP((unsigned char)wc)))
        off--;
    while (   off > 0
           && (   (wc = pwszDllPath[off - 1]) >= 127U
               || !RTPATH_IS_SEP((unsigned char)wc)))
        off--;
#endif
    pwszDllPath[off] = '\0';
}


/**
 * Wrapper around RegisterXidlModulesAndClassesGenerated for the convenience of
 * the standard registration entry points.
 *
 * @returns COM status code.
 * @param   pwszVBoxDir         The VirtualBox install directory (unicode),
 *                              trailing slash.
 * @param   fDelete             Whether to delete registration keys and values.
 * @param   fUpdate             Whether to update registration keys and values.
 */
HRESULT RegisterXidlModulesAndClasses(PRTUTF16 pwszVBoxDir, bool fDelete, bool fUpdate)
{
#ifdef VBOX_IN_32_ON_64_MAIN_API
    bool const      fIs32On64 = true;
#else
    bool const      fIs32On64 = false;
#endif
    VBPSREGSTATE    State;
    LSTATUS         lrc;

    /*
     * Do registration for the current execution mode of the DLL.
     */
    lrc = vbpsRegInit(&State, HKEY_CLASSES_ROOT, NULL /* Alt: HKEY_LOCAL_MACHINE, "Software\\Classes", */, fDelete, fUpdate, 0);
    if (lrc == ERROR_SUCCESS)
    {
        if (!fUpdate)
        {
            /* When only unregistering, really purge everything twice or trice. :-) */
            vbpsRegAddAltDelete(&State, HKEY_LOCAL_MACHINE, "Software\\Classes");
            vbpsRegAddAltDelete(&State, HKEY_CURRENT_USER,  "Software\\Classes");
            vbpsRegAddAltDelete(&State, HKEY_CLASSES_ROOT,  NULL);
        }

        RegisterXidlModulesAndClassesGenerated(&State, pwszVBoxDir, fIs32On64);
        lrc = State.lrc;
    }
    vbpsRegTerm(&State);

    /*
     * Translate error code? Return.
     */
    if (lrc == ERROR_SUCCESS)
        return S_OK;
    return E_FAIL;
}


/**
 * Checks if the string matches any of our type library versions.
 *
 * @returns true on match, false on mismatch.
 * @param   pwszTypeLibVersion      The type library version string.
 */
DECLINLINE(bool) vbpsIsTypeLibVersionToRemove(PCRTUTF16 pwszTypeLibVersion)
{
    AssertCompile(RT_ELEMENTS(g_apwszTypelibVersions) == 2);

    /* ASSUMES: 1.x version strings and that the input buffer is at least 3 wchars long. */
    if (   g_apwszTypelibVersions[0][3] == pwszTypeLibVersion[3]
        && RTUtf16Cmp(g_apwszTypelibVersions[0], pwszTypeLibVersion) == 0)
        return true;
    if (   g_apwszTypelibVersions[1][3] == pwszTypeLibVersion[3]
        && RTUtf16Cmp(g_apwszTypelibVersions[1], pwszTypeLibVersion) == 0)
        return true;

    return false;
}


/**
 * Quick check whether the given string looks like a UUID in braces.
 *
 * This does not check the whole string, just do a quick sweep.
 *
 * @returns true if possible UUID, false if definitely not.
 * @param   pwszUuid            Alleged UUID in braces.
 */
DECLINLINE(bool) vbpsIsUuidInBracesQuickW(PCRTUTF16 pwszUuid)
{
    return pwszUuid[ 0] == '{'
        && pwszUuid[ 9] == '-'
        && pwszUuid[14] == '-'
        && pwszUuid[19] == '-'
        && pwszUuid[24] == '-'
        && pwszUuid[37] == '}'
        && pwszUuid[38] == '\0'
        && RT_C_IS_XDIGIT(pwszUuid[1]);
}


/**
 * Compares two UUIDs (in braces).
 *
 * @returns true on match, false if no match.
 * @param   pwszUuid1       The first UUID.
 * @param   pwszUuid2       The second UUID.
 */
static bool vbpsCompareUuidW(PCRTUTF16 pwszUuid1, PCRTUTF16 pwszUuid2)
{
#define COMPARE_EXACT_RET(a_wch1, a_wch2) \
        if ((a_wch1) == (a_wch2)) { } else return false

#define COMPARE_XDIGITS_RET(a_wch1, a_wch2) \
        if ((a_wch1) == (a_wch2)) { } \
        else if (RT_C_TO_UPPER(a_wch1) != RT_C_TO_UPPER(a_wch2) || (a_wch1) >= 127U || (a_wch2) >= 127U) \
            return false
    COMPARE_EXACT_RET(  pwszUuid1[ 0], pwszUuid2[ 0]);  /* {  */
    COMPARE_XDIGITS_RET(pwszUuid1[ 1], pwszUuid2[ 1]);  /* 5  */
    COMPARE_XDIGITS_RET(pwszUuid1[ 2], pwszUuid2[ 2]);  /* e  */
    COMPARE_XDIGITS_RET(pwszUuid1[ 3], pwszUuid2[ 3]);  /* 5  */
    COMPARE_XDIGITS_RET(pwszUuid1[ 4], pwszUuid2[ 4]);  /* e  */
    COMPARE_XDIGITS_RET(pwszUuid1[ 5], pwszUuid2[ 5]);  /* 3  */
    COMPARE_XDIGITS_RET(pwszUuid1[ 6], pwszUuid2[ 6]);  /* 6  */
    COMPARE_XDIGITS_RET(pwszUuid1[ 7], pwszUuid2[ 7]);  /* 4  */
    COMPARE_XDIGITS_RET(pwszUuid1[ 8], pwszUuid2[ 8]);  /* 0  */
    COMPARE_EXACT_RET(  pwszUuid1[ 9], pwszUuid2[ 9]);  /* -  */
    COMPARE_XDIGITS_RET(pwszUuid1[10], pwszUuid2[10]);  /* 7  */
    COMPARE_XDIGITS_RET(pwszUuid1[11], pwszUuid2[11]);  /* 4  */
    COMPARE_XDIGITS_RET(pwszUuid1[12], pwszUuid2[12]);  /* f  */
    COMPARE_XDIGITS_RET(pwszUuid1[13], pwszUuid2[13]);  /* 3  */
    COMPARE_EXACT_RET(  pwszUuid1[14], pwszUuid2[14]);  /* -  */
    COMPARE_XDIGITS_RET(pwszUuid1[15], pwszUuid2[15]);  /* 4  */
    COMPARE_XDIGITS_RET(pwszUuid1[16], pwszUuid2[16]);  /* 6  */
    COMPARE_XDIGITS_RET(pwszUuid1[17], pwszUuid2[17]);  /* 8  */
    COMPARE_XDIGITS_RET(pwszUuid1[18], pwszUuid2[18]);  /* 9  */
    COMPARE_EXACT_RET(  pwszUuid1[19], pwszUuid2[19]);  /* -  */
    COMPARE_XDIGITS_RET(pwszUuid1[20], pwszUuid2[20]);  /* 9  */
    COMPARE_XDIGITS_RET(pwszUuid1[21], pwszUuid2[21]);  /* 7  */
    COMPARE_XDIGITS_RET(pwszUuid1[22], pwszUuid2[22]);  /* 9  */
    COMPARE_XDIGITS_RET(pwszUuid1[23], pwszUuid2[23]);  /* f  */
    COMPARE_EXACT_RET(  pwszUuid1[24], pwszUuid2[24]);  /* -  */
    COMPARE_XDIGITS_RET(pwszUuid1[25], pwszUuid2[25]);  /* 6  */
    COMPARE_XDIGITS_RET(pwszUuid1[26], pwszUuid2[26]);  /* b  */
    COMPARE_XDIGITS_RET(pwszUuid1[27], pwszUuid2[27]);  /* 1  */
    COMPARE_XDIGITS_RET(pwszUuid1[28], pwszUuid2[28]);  /* b  */
    COMPARE_XDIGITS_RET(pwszUuid1[29], pwszUuid2[29]);  /* 8  */
    COMPARE_XDIGITS_RET(pwszUuid1[30], pwszUuid2[30]);  /* d  */
    COMPARE_XDIGITS_RET(pwszUuid1[31], pwszUuid2[31]);  /* 7  */
    COMPARE_XDIGITS_RET(pwszUuid1[32], pwszUuid2[32]);  /* 6  */
    COMPARE_XDIGITS_RET(pwszUuid1[33], pwszUuid2[33]);  /* 0  */
    COMPARE_XDIGITS_RET(pwszUuid1[34], pwszUuid2[34]);  /* 9  */
    COMPARE_XDIGITS_RET(pwszUuid1[35], pwszUuid2[35]);  /* a  */
    COMPARE_XDIGITS_RET(pwszUuid1[36], pwszUuid2[36]);  /* 5  */
    COMPARE_EXACT_RET(  pwszUuid1[37], pwszUuid2[37]);  /* }  */
    COMPARE_EXACT_RET(  pwszUuid1[38], pwszUuid2[38]);  /* \0 */
#undef COMPARE_EXACT_RET
#undef COMPARE_XDIGITS_RET
    return true;
}


/**
 * Checks if the type library ID is one of the ones we wish to clean up.
 *
 * @returns true if it should be cleaned up, false if not.
 * @param   pwszTypeLibId  The type library ID as a bracketed string.
 */
DECLINLINE(bool) vbpsIsTypeLibIdToRemove(PRTUTF16 pwszTypeLibId)
{
    AssertCompile(RT_ELEMENTS(g_apwszTypeLibIds) == 2);
#ifdef VBOX_STRICT
    static bool s_fDoneStrict = false;
    if (s_fDoneStrict) { }
    else
    {
        Assert(RT_ELEMENTS(g_apwszTypeLibIds) == 2);
        Assert(g_apwszTypeLibIds[0][0] == '{');
        Assert(g_apwszTypeLibIds[1][0] == '{');
        Assert(RT_C_IS_XDIGIT(g_apwszTypeLibIds[0][1]));
        Assert(RT_C_IS_XDIGIT(g_apwszTypeLibIds[1][1]));
        Assert(RT_C_IS_UPPER(g_apwszTypeLibIds[0][1]) || RT_C_IS_DIGIT(g_apwszTypeLibIds[0][1]));
        Assert(RT_C_IS_UPPER(g_apwszTypeLibIds[1][1]) || RT_C_IS_DIGIT(g_apwszTypeLibIds[1][1]));
        s_fDoneStrict = true;
    }
#endif

    /*
     * Rolled out matching with inlined check of the opening braces
     * and first two digits.
     *
     * ASSUMES input buffer is at least 3 wchars big and uppercased UUID in
     * our matching array.
     */
    if (pwszTypeLibId[0] == '{')
    {
        RTUTF16 const wcFirstDigit  = RT_C_TO_UPPER(pwszTypeLibId[1]);
        RTUTF16 const wcSecondDigit = RT_C_TO_UPPER(pwszTypeLibId[2]);
        PCRTUTF16     pwsz2 = g_apwszTypeLibIds[0];
        if (   wcFirstDigit  == pwsz2[1]
            && wcSecondDigit == pwsz2[2]
            && vbpsCompareUuidW(pwszTypeLibId, pwsz2))
            return true;
        pwsz2 = g_apwszTypeLibIds[1];
        if (   wcFirstDigit  == pwsz2[1]
            && wcSecondDigit == pwsz2[2]
            && vbpsCompareUuidW(pwszTypeLibId, pwsz2))
            return true;
    }
    return false;
}


/**
 * Checks if the proxy stub class ID is one of the ones we wish to clean up.
 *
 * @returns true if it should be cleaned up, false if not.
 * @param   pwszProxyStubId     The proxy stub class ID.
 */
DECLINLINE(bool) vbpsIsProxyStubClsIdToRemove(PRTUTF16 pwszProxyStubId)
{
    AssertCompile(RT_ELEMENTS(g_apwszProxyStubClsIds) == 2);
#ifdef VBOX_STRICT
    static bool s_fDoneStrict = false;
    if (s_fDoneStrict) { }
    else
    {
        Assert(RT_ELEMENTS(g_apwszProxyStubClsIds) == 2);
        Assert(g_apwszProxyStubClsIds[0][0] == '{');
        Assert(g_apwszProxyStubClsIds[1][0] == '{');
        Assert(RT_C_IS_XDIGIT(g_apwszProxyStubClsIds[0][1]));
        Assert(RT_C_IS_XDIGIT(g_apwszProxyStubClsIds[1][1]));
        Assert(RT_C_IS_UPPER(g_apwszProxyStubClsIds[0][1]) || RT_C_IS_DIGIT(g_apwszProxyStubClsIds[0][1]));
        Assert(RT_C_IS_UPPER(g_apwszProxyStubClsIds[1][1]) || RT_C_IS_DIGIT(g_apwszProxyStubClsIds[1][1]));
        s_fDoneStrict = true;
    }
#endif

    /*
     * Rolled out matching with inlined check of the opening braces
     * and first two digits.
     *
     * ASSUMES input buffer is at least 3 wchars big and uppercased UUID in
     * our matching array.
     */
    if (pwszProxyStubId[0] == '{')
    {
        RTUTF16 const wcFirstDigit  = RT_C_TO_UPPER(pwszProxyStubId[1]);
        RTUTF16 const wcSecondDigit = RT_C_TO_UPPER(pwszProxyStubId[2]);
        PCRTUTF16     pwsz2 = g_apwszProxyStubClsIds[0];
        if (   wcFirstDigit  == pwsz2[1]
            && wcSecondDigit == pwsz2[2]
            && vbpsCompareUuidW(pwszProxyStubId, pwsz2))
            return true;
        pwsz2 = g_apwszProxyStubClsIds[1];
        if (   wcFirstDigit  == pwsz2[1]
            && wcSecondDigit == pwsz2[2]
            && vbpsCompareUuidW(pwszProxyStubId, pwsz2))
            return true;
    }
    return false;
}


/**
 * Hack to clean out the interfaces belonging to obsolete typelibs on
 * development boxes and such likes.
 */
static void vbpsRemoveOldInterfaces(VBPSREGSTATE *pState)
{
    unsigned iAlt = pState->cAltDeletes;
    while (iAlt-- > 0)
    {
        /*
         * Open the interface root key.   Not using the vbpsRegOpenInterfaceKeys feature
         * here in case it messes things up by keeping the special HKEY_CLASSES_ROOT key
         * open with possibly pending deletes in parent views or other weird stuff.
         */
        HKEY hkeyInterfaces;
        LRESULT lrc = RegOpenKeyExW(pState->aAltDeletes[iAlt].hkeyClasses, L"Interface",
                                    0 /*fOptions*/, pState->fSamDelete, &hkeyInterfaces);
        if (lrc == ERROR_SUCCESS)
        {
            /*
             * This is kind of expensive, but we have to check all registered interfaces.
             * Only use wide APIs to avoid wasting time on string conversion.
             */
            DWORD idxKey;
            for (idxKey = 0;; idxKey++)
            {
                RTUTF16 wszCurNm[128 + 48];
                DWORD   cwcCurNm = 128;
                lrc = RegEnumKeyExW(hkeyInterfaces, idxKey, wszCurNm, &cwcCurNm,
                                    NULL /*pdwReserved*/, NULL /*pwszClass*/, NULL /*pcwcClass*/, NULL /*pLastWriteTime*/);
                if (lrc == ERROR_SUCCESS)
                {
                    /*
                     * We match the interface by type library ID or proxy stub class ID.
                     *
                     * We have to check the proxy ID last, as it is almost always there
                     * and we can safely skip it if there is a mismatching type lib
                     * associated with the interface.
                     */
                    static RTUTF16 const    s_wszTypeLib[] = L"\\TypeLib";
                    bool                    fDeleteMe      = false;
                    HKEY                    hkeySub;
                    RTUTF16                 wszValue[128];
                    DWORD                   cbValue;
                    DWORD                   dwType;

                    /* Skip this entry if it doesn't look like a braced UUID. */
                    wszCurNm[cwcCurNm] = '\0'; /* paranoia */
                    if (vbpsIsUuidInBracesQuickW(wszCurNm)) { }
                    else continue;

                    /* Try the TypeLib sub-key. */
                    memcpy(&wszCurNm[cwcCurNm], s_wszTypeLib, sizeof(s_wszTypeLib));
                    lrc = RegOpenKeyExW(hkeyInterfaces, wszCurNm, 0 /*fOptions*/, KEY_QUERY_VALUE, &hkeySub);
                    if (lrc == ERROR_SUCCESS)
                    {
                        cbValue = sizeof(wszValue) - sizeof(RTUTF16);
                        lrc = RegQueryValueExW(hkeySub, NULL /*pszValueNm*/, NULL /*pdwReserved*/,
                                               &dwType, (PBYTE)&wszValue[0], &cbValue);
                        if (lrc != ERROR_SUCCESS || dwType != REG_SZ)
                            cbValue = 0;
                        wszValue[cbValue / sizeof(RTUTF16)] = '\0';

                        if (   lrc == ERROR_SUCCESS
                            && vbpsIsTypeLibIdToRemove(wszValue))
                        {
                            /* Check the TypeLib/Version value to make sure. */
                            cbValue = sizeof(wszValue) - sizeof(RTUTF16);
                            lrc = RegQueryValueExW(hkeySub, L"Version", 0 /*pdwReserved*/, &dwType, (PBYTE)&wszValue[0], &cbValue);
                            if (lrc != ERROR_SUCCESS)
                                cbValue = 0;
                            wszValue[cbValue] = '\0';

                            if (   lrc == ERROR_SUCCESS
                                && vbpsIsTypeLibVersionToRemove(wszValue))
                                fDeleteMe = true;
                        }
                        vbpsCloseKey(pState, hkeySub, __LINE__);
                    }
                    else if (lrc == ERROR_FILE_NOT_FOUND)
                    {
                        /* No TypeLib, try the ProxyStubClsid32 sub-key next. */
                        static RTUTF16 const    s_wszProxyStubClsid32[] = L"\\ProxyStubClsid32";
                        memcpy(&wszCurNm[cwcCurNm], s_wszProxyStubClsid32, sizeof(s_wszProxyStubClsid32));
                        lrc = RegOpenKeyExW(hkeyInterfaces, wszCurNm, 0 /*fOptions*/, KEY_QUERY_VALUE, &hkeySub);
                        if (lrc == ERROR_SUCCESS)
                        {
                            cbValue = sizeof(wszValue) - sizeof(RTUTF16);
                            lrc = RegQueryValueExW(hkeySub, NULL /*pszValueNm*/, NULL /*pdwReserved*/,
                                                   &dwType, (PBYTE)&wszValue[0], &cbValue);
                            if (lrc != ERROR_SUCCESS || dwType != REG_SZ)
                                cbValue = 0;
                            wszValue[cbValue / sizeof(RTUTF16)] = '\0';

                            if (   lrc == ERROR_SUCCESS
                                && vbpsIsProxyStubClsIdToRemove(wszValue))
                                fDeleteMe = true;

                            vbpsCloseKey(pState, hkeySub, __LINE__);
                        }
                    }

                    if (fDeleteMe)
                    {
                        /*
                         * Ok, it's an orphaned VirtualBox interface. Delete it.
                         */
                        wszCurNm[cwcCurNm] = '\0';
                        vbpsDeleteKeyRecursiveW(pState, hkeyInterfaces, wszCurNm, __LINE__);
                    }
                }
                else
                {
                    Assert(lrc == ERROR_NO_MORE_ITEMS);
                    break;
                }
            }

            vbpsCloseKey(pState, hkeyInterfaces, __LINE__);
        }
    }
}


/**
 * Hack to clean out the class IDs belonging to obsolete typelibs on development
 * boxes and such likes.
 */
static void vbpsRemoveOldClassIDs(VBPSREGSTATE *pState)
{
    unsigned iAlt = pState->cAltDeletes;
    while (iAlt-- > 0)
    {
        /*
         * Open the CLSID key if it exists.
         * We don't use the hKeyClsid member for the same paranoid reasons as
         * already stated in vbpsRemoveOldInterfaces.
         */
        HKEY hkeyClsIds;
        LRESULT lrc;
        lrc = RegOpenKeyExW(pState->aAltDeletes[iAlt].hkeyClasses, L"CLSID", 0 /*fOptions*/, pState->fSamDelete, &hkeyClsIds);
        if (lrc == ERROR_SUCCESS)
        {
            /*
             * This is kind of expensive, but we have to check all registered interfaces.
             * Only use wide APIs to avoid wasting time on string conversion.
             */
            DWORD idxKey;
            for (idxKey = 0;; idxKey++)
            {
                RTUTF16 wszCurNm[128 + 48];
                DWORD   cwcCurNm = 128;
                lrc = RegEnumKeyExW(hkeyClsIds, idxKey, wszCurNm, &cwcCurNm,
                                    NULL /*pdwReserved*/, NULL /*pwszClass*/, NULL /*pcwcClass*/, NULL /*pLastWriteTime*/);
                if (lrc == ERROR_SUCCESS)
                {
                    /*
                     * Match both the type library ID and the program ID.
                     */
                    static RTUTF16 const    s_wszTypeLib[] = L"\\TypeLib";
                    HKEY                    hkeySub;
                    RTUTF16                 wszValue[128];
                    DWORD                   cbValue;
                    DWORD                   dwType;


                    /* Skip this entry if it doesn't look like a braced UUID. (Microsoft
                       has one two malformed ones plus a hack.) */
                    wszCurNm[cwcCurNm] = '\0'; /* paranoia */
                    if (vbpsIsUuidInBracesQuickW(wszCurNm)) { }
                    else continue;

                    /* The TypeLib sub-key. */
                    memcpy(&wszCurNm[cwcCurNm], s_wszTypeLib, sizeof(s_wszTypeLib));
                    lrc = RegOpenKeyExW(hkeyClsIds, wszCurNm, 0 /*fOptions*/, KEY_QUERY_VALUE, &hkeySub);
                    if (lrc == ERROR_SUCCESS)
                    {
                        bool fDeleteMe = false;

                        cbValue = sizeof(wszValue) - sizeof(RTUTF16);
                        lrc = RegQueryValueExW(hkeySub, NULL /*pszValueNm*/, NULL /*pdwReserved*/,
                                               &dwType, (PBYTE)&wszValue[0], &cbValue);
                        if (lrc != ERROR_SUCCESS || dwType != REG_SZ)
                            cbValue = 0;
                        wszValue[cbValue / sizeof(RTUTF16)] = '\0';

                        if (   lrc == ERROR_SUCCESS
                            && vbpsIsTypeLibIdToRemove(wszValue))
                            fDeleteMe = true;

                        vbpsCloseKey(pState, hkeySub, __LINE__);

                        if (fDeleteMe)
                        {
                            /* The ProgId sub-key. */
                            static RTUTF16 const    s_wszProgId[] = L"\\ProgId";
                            memcpy(&wszCurNm[cwcCurNm], s_wszProgId, sizeof(s_wszProgId));
                            lrc = RegOpenKeyExW(hkeyClsIds, wszCurNm, 0 /*fOptions*/, KEY_QUERY_VALUE, &hkeySub);
                            if (lrc == ERROR_SUCCESS)
                            {
                                static RTUTF16 const s_wszProgIdPrefix[] = L"VirtualBox.";

                                cbValue = sizeof(wszValue) - sizeof(RTUTF16);
                                lrc = RegQueryValueExW(hkeySub, NULL /*pszValueNm*/, NULL /*pdwReserved*/,
                                                       &dwType, (PBYTE)&wszValue[0], &cbValue);
                                if (lrc != ERROR_SUCCESS || dwType != REG_SZ)
                                    cbValue = 0;
                                wszValue[cbValue / sizeof(RTUTF16)] = '\0';

                                if (   cbValue < sizeof(s_wszProgIdPrefix)
                                    || memcmp(wszValue, s_wszProgIdPrefix, sizeof(s_wszProgIdPrefix) - sizeof(RTUTF16)) != 0)
                                    fDeleteMe = false;

                                vbpsCloseKey(pState, hkeySub, __LINE__);
                            }
                            else
                                AssertStmt(lrc == ERROR_FILE_NOT_FOUND, fDeleteMe = false);

                            if (fDeleteMe)
                            {
                                /*
                                 * Ok, it's an orphaned VirtualBox interface. Delete it.
                                 */
                                wszCurNm[cwcCurNm] = '\0';
                                vbpsDeleteKeyRecursiveW(pState, hkeyClsIds, wszCurNm, __LINE__);
                            }
                        }
                    }
                    else
                        Assert(lrc == ERROR_FILE_NOT_FOUND);
                }
                else
                {
                    Assert(lrc == ERROR_NO_MORE_ITEMS);
                    break;
                }
            }

            vbpsCloseKey(pState, hkeyClsIds, __LINE__);
        }
        else
            Assert(lrc == ERROR_FILE_NOT_FOUND);
    }
}


/**
 * Hack to clean obsolete typelibs on development boxes and such.
 */
static void vbpsRemoveOldTypeLibs(VBPSREGSTATE *pState)
{
    unsigned iAlt = pState->cAltDeletes;
    while (iAlt-- > 0)
    {
        /*
         * Open the TypeLib key, if it exists.
         */
        HKEY hkeyTypeLibs;
        LSTATUS lrc;
        lrc = RegOpenKeyExW(pState->aAltDeletes[iAlt].hkeyClasses, L"TypeLib", 0 /*fOptions*/, pState->fSamDelete, &hkeyTypeLibs);
        if (lrc == ERROR_SUCCESS)
        {
            /*
             * Look for our type library IDs.
             */
            unsigned iTlb = RT_ELEMENTS(g_apwszTypeLibIds);
            while (iTlb-- > 0)
            {
                HKEY hkeyTypeLibId;
                lrc = RegOpenKeyExW(hkeyTypeLibs, g_apwszTypeLibIds[iTlb], 0 /*fOptions*/, pState->fSamDelete, &hkeyTypeLibId);
                if (lrc == ERROR_SUCCESS)
                {
                    unsigned iVer = RT_ELEMENTS(g_apwszTypelibVersions);
                    while (iVer-- > 0)
                    {
                        HKEY hkeyVer;
                        lrc = RegOpenKeyExW(hkeyTypeLibId, g_apwszTypelibVersions[iVer], 0, KEY_READ, &hkeyVer);
                        if (lrc == ERROR_SUCCESS)
                        {
                            char szValue[128];
                            DWORD cbValue = sizeof(szValue) - 1;
                            lrc = RegQueryValueExA(hkeyVer, NULL, NULL, NULL, (PBYTE)&szValue[0], &cbValue);
                            vbpsCloseKey(pState, hkeyVer, __LINE__);
                            if (lrc == ERROR_SUCCESS)
                            {
                                szValue[cbValue] = '\0';
                                if (   !strcmp(szValue, "VirtualBox Type Library")
                                    || !strcmp(szValue, "InnoTek VirtualBox Machine Type Library")) /* 2005-05-25 build */
                                {
                                    /*
                                     * Delete the type library version.
                                     * We do not delete the whole type library ID, just this version of it.
                                     */
                                    vbpsDeleteKeyRecursiveW(pState, hkeyTypeLibId, g_apwszTypelibVersions[iVer], __LINE__);
                                }
                            }
                        }
                    }
                    vbpsCloseKey(pState, hkeyTypeLibId, __LINE__);

                    /*
                     * The type library ID key should be empty now, so we can try remove it (non-recursively).
                     */
                    lrc = RegDeleteKeyW(hkeyTypeLibs, g_apwszTypeLibIds[iTlb]);
                    Assert(lrc == ERROR_SUCCESS);
                }
            }
        }
        else
            Assert(lrc == ERROR_FILE_NOT_FOUND);
    }
}


/**
 * Hack to clean out obsolete typelibs on development boxes and such.
 */
static void vbpsRemoveOldMessSub(REGSAM fSamWow)
{
    /*
     * Note! The worker procedures does not use the default destination,
     *       because it's much much simpler to enumerate alternative locations.
     */
    VBPSREGSTATE State;
    LRESULT lrc = vbpsRegInit(&State, HKEY_CLASSES_ROOT, NULL, true /*fDelete*/, false /*fUpdate*/, fSamWow);
    if (lrc == ERROR_SUCCESS)
    {
        vbpsRegAddAltDelete(&State, HKEY_CURRENT_USER,  "Software\\Classes");
        vbpsRegAddAltDelete(&State, HKEY_LOCAL_MACHINE, "Software\\Classes");
        vbpsRegAddAltDelete(&State, HKEY_CLASSES_ROOT,  NULL);

        vbpsRemoveOldInterfaces(&State);
        vbpsRemoveOldClassIDs(&State);
        vbpsRemoveOldTypeLibs(&State);
    }
    vbpsRegTerm(&State);
}


/**
 * Hack to clean out obsolete typelibs on development boxes and such.
 */
static void removeOldMess(void)
{
    vbpsRemoveOldMessSub(0 /*fSamWow*/);
#if ARCH_BITS == 64 || defined(VBOX_IN_32_ON_64_MAIN_API)
    vbpsRemoveOldMessSub(KEY_WOW64_32KEY);
#endif
}



/**
 * Register the interfaces proxied by this DLL, and to avoid duplication and
 * minimize work the VBox type library, classes and servers are also registered.
 *
 * This is normally only used by developers via comregister.cmd and the heat.exe
 * tool during MSI creation.  The only situation where users may end up here is
 * if they're playing around or we recommend it as a solution to COM problems.
 * So, no problem if this approach is less gentle, though we leave the cleaning
 * up of orphaned interfaces to DllUnregisterServer.
 *
 * @returns COM status code.
 */
HRESULT STDAPICALLTYPE DllRegisterServer(void)
{
    HRESULT hrc;

    /*
     * Register the type library first.
     */
    ITypeLib *pITypeLib;
    WCHAR wszDllName[MAX_PATH];
    DWORD cwcRet = GetModuleFileNameW(g_hDllSelf, wszDllName, RT_ELEMENTS(wszDllName));
    AssertReturn(cwcRet > 0 && cwcRet < RT_ELEMENTS(wszDllName), CO_E_PATHTOOLONG);

    hrc = LoadTypeLib(wszDllName, &pITypeLib);
    AssertMsgReturn(SUCCEEDED(hrc), ("%Rhrc\n", hrc), hrc);
    hrc = RegisterTypeLib(pITypeLib, wszDllName, NULL /*pszHelpDir*/);
    pITypeLib->lpVtbl->Release(pITypeLib);
    AssertMsgReturn(SUCCEEDED(hrc), ("%Rhrc\n", hrc), hrc);

    /*
     * Register proxy stub.
     */
    hrc = NdrDllRegisterProxy(g_hDllSelf, &g_apProxyFiles[0], &g_ProxyClsId);         /* see DLLREGISTRY_ROUTINES in RpcProxy.h */
    AssertMsgReturn(SUCCEEDED(hrc), ("%Rhrc\n", hrc), hrc);

    /*
     * Register the VBox modules and classes.
     */
    vbpsDllPathToVBoxDir(wszDllName);
    hrc = RegisterXidlModulesAndClasses(wszDllName, true /*fDelete*/, true /*fUpdate*/);
    AssertMsgReturn(SUCCEEDED(hrc), ("%Rhrc\n", hrc), hrc);

    return S_OK;
}


/**
 * Reverse of DllRegisterServer.
 *
 * This is normally only used by developers via comregister.cmd.  Users may be
 * asked to perform it in order to fix some COM issue.  So, it's OK if we spend
 * some extra time and clean up orphaned interfaces, because developer boxes
 * will end up with a bunch of those as interface UUIDs changes.
 *
 * @returns COM status code.
 */
HRESULT STDAPICALLTYPE DllUnregisterServer(void)
{
    HRESULT hrc = S_OK;
    HRESULT hrc2;

    /*
     * Unregister the type library.
     *
     * We ignore TYPE_E_REGISTRYACCESS as that is what is returned if the
     * type lib hasn't been registered (W10).
     */
    hrc2 = UnRegisterTypeLib(&LIBID_VirtualBox, kTypeLibraryMajorVersion, kTypeLibraryMinorVersion,
                             0 /*LCid*/, RT_CONCAT(SYS_WIN, ARCH_BITS));
    AssertMsgStmt(SUCCEEDED(hrc2) || hrc2 == TYPE_E_REGISTRYACCESS, ("%Rhrc\n", hrc2), if (SUCCEEDED(hrc)) hrc = hrc2);

    /*
     * Unregister the proxy stub.
     *
     * We ignore ERROR_FILE_NOT_FOUND as that is returned if not registered (W10).
     */
    hrc2 = NdrDllUnregisterProxy(g_hDllSelf, &g_apProxyFiles[0], &g_ProxyClsId);      /* see DLLREGISTRY_ROUTINES in RpcProxy.h */
    AssertMsgStmt(   SUCCEEDED(hrc2)
                  || hrc2 == MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_FILE_NOT_FOUND)
                  || hrc2 == REGDB_E_INVALIDVALUE,
                  ("%Rhrc\n", hrc2), if (SUCCEEDED(hrc)) hrc = hrc2);

    /*
     * Register the VBox modules and classes.
     */
    hrc2 = RegisterXidlModulesAndClasses(NULL, true /*fDelete*/, false /*fUpdate*/);
    AssertMsgStmt(SUCCEEDED(hrc2), ("%Rhrc\n", hrc2), if (SUCCEEDED(hrc)) hrc = hrc2);

    /*
     * Purge old mess.
     */
    removeOldMess();

    return hrc;
}


#ifdef VBOX_WITH_SDS
/**
 * Update a SCM service.
 *
 * @param   pState              The state.
 * @param   pwszVBoxDir         The VirtualBox install directory (unicode),
 *                              trailing slash.
 * @param   pwszModule          The service module.
 * @param   pwszServiceName     The service name.
 * @param   pwszDisplayName     The service display name.
 * @param   pwszDescription     The service description.
 */
static void vbpsUpdateWindowsService(VBPSREGSTATE *pState, const WCHAR *pwszVBoxDir, const WCHAR *pwszModule,
                                     const WCHAR *pwszServiceName, const WCHAR *pwszDisplayName, const WCHAR *pwszDescription)
{
    SC_HANDLE           hSCM;

    /* Configuration options that are currently standard. */
    uint32_t const      uServiceType         = SERVICE_WIN32_OWN_PROCESS;
    uint32_t const      uStartType           = SERVICE_DEMAND_START;
    uint32_t const      uErrorControl        = SERVICE_ERROR_NORMAL;
    WCHAR const * const pwszServiceStartName = L"LocalSystem";
    static WCHAR const  wszzDependencies[]   = L"RPCSS\0";

    /*
     * Make double quoted executable file path. ASSUMES pwszVBoxDir ends with a slash!
     */
    WCHAR wszFilePath[MAX_PATH + 2];
    int vrc = RTUtf16CopyAscii(wszFilePath, RT_ELEMENTS(wszFilePath), "\"");
    if (RT_SUCCESS(vrc))
        vrc = RTUtf16Cat(wszFilePath, RT_ELEMENTS(wszFilePath), pwszVBoxDir);
    if (RT_SUCCESS(vrc))
        vrc = RTUtf16Cat(wszFilePath, RT_ELEMENTS(wszFilePath), pwszModule);
    if (RT_SUCCESS(vrc))
        vrc = RTUtf16CatAscii(wszFilePath, RT_ELEMENTS(wszFilePath), "\"");
    AssertLogRelRCReturnVoid(vrc);

    /*
     * Open the service manager for the purpose of checking the configuration.
     */
    hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCM != NULL)
    {
        union
        {
            QUERY_SERVICE_CONFIGW   Config;
            SERVICE_STATUS          Status;
            SERVICE_DESCRIPTIONW     Desc;
            uint8_t                 abPadding[sizeof(QUERY_SERVICE_CONFIGW) + 5 * _1K];
        } uBuf;
        SC_HANDLE   hService;
        bool        fCreateIt = pState->fUpdate;
        bool        fDeleteIt = true;

        /*
         * Step #1: Open the service and validate the configuration.
         */
        if (pState->fUpdate)
        {
            hService = OpenServiceW(hSCM, pwszServiceName, SERVICE_QUERY_CONFIG);
            if (hService != NULL)
            {
                DWORD cbNeeded = 0;
                if (QueryServiceConfigW(hService, &uBuf.Config, sizeof(uBuf), &cbNeeded))
                {
                    if (uBuf.Config.dwErrorControl)
                    {
                        uint32_t cErrors = 0;
                        if (uBuf.Config.dwServiceType != uServiceType)
                        {
                            LogRel(("update service '%ls': dwServiceType %u, expected %u\n",
                                    pwszServiceName, uBuf.Config.dwServiceType, uServiceType));
                            cErrors++;
                        }
                        if (uBuf.Config.dwStartType != uStartType)
                        {
                            LogRel(("update service '%ls': dwStartType %u, expected %u\n",
                                    pwszServiceName, uBuf.Config.dwStartType, uStartType));
                            cErrors++;
                        }
                        if (uBuf.Config.dwErrorControl != uErrorControl)
                        {
                            LogRel(("update service '%ls': dwErrorControl %u, expected %u\n",
                                    pwszServiceName, uBuf.Config.dwErrorControl, uErrorControl));
                            cErrors++;
                        }
                        if (RTUtf16ICmp(uBuf.Config.lpBinaryPathName, wszFilePath) != 0)
                        {
                            LogRel(("update service '%ls': lpBinaryPathName '%ls', expected '%ls'\n",
                                    pwszServiceName, uBuf.Config.lpBinaryPathName, wszFilePath));
                            cErrors++;
                        }
                        if (   uBuf.Config.lpServiceStartName != NULL
                            && *uBuf.Config.lpServiceStartName != L'\0'
                            && RTUtf16ICmp(uBuf.Config.lpServiceStartName, pwszServiceStartName) != 0)
                        {
                            LogRel(("update service '%ls': lpServiceStartName '%ls', expected '%ls'\n",
                                    pwszServiceName, uBuf.Config.lpBinaryPathName, pwszServiceStartName));
                            cErrors++;
                        }

                        fDeleteIt = fCreateIt = cErrors > 0;
                    }
                }
                else
                    AssertLogRelMsgFailed(("QueryServiceConfigW returned %u (cbNeeded=%u vs %zu)\n",
                                           GetLastError(), cbNeeded, sizeof(uBuf)));
            }
            else
            {
                DWORD dwErr = GetLastError();
                fDeleteIt = dwErr != ERROR_SERVICE_DOES_NOT_EXIST;
                AssertLogRelMsg(dwErr == ERROR_SERVICE_DOES_NOT_EXIST, ("OpenServiceW('%ls') -> %u\n", pwszServiceName, dwErr));
            }
            CloseServiceHandle(hService);
        }

        /*
         * Step #2: Stop and delete the service if needed.
         *          We can do this without reopening the service manager.
         */
        if (fDeleteIt)
        {
            hService = OpenServiceW(hSCM, pwszServiceName, SERVICE_STOP | DELETE);
            if (hService)
            {
                BOOL            fRet;
                DWORD           dwErr;
                RT_ZERO(uBuf.Status);
                SetLastError(ERROR_SERVICE_NOT_ACTIVE);
                fRet = ControlService(hService, SERVICE_CONTROL_STOP, &uBuf.Status);
                dwErr = GetLastError();
                if (   fRet
                    || dwErr == ERROR_SERVICE_NOT_ACTIVE
                    || (   dwErr == ERROR_SERVICE_CANNOT_ACCEPT_CTRL
                        && uBuf.Status.dwCurrentState == SERVICE_STOP_PENDING) )
                {
                    if (DeleteService(hService))
                        LogRel(("update service '%ls': deleted\n", pwszServiceName));
                    else
                        AssertLogRelMsgFailed(("Failed to not delete service %ls: %u\n", pwszServiceName, GetLastError()));
                }
                else
                    AssertMsg(dwErr == ERROR_ACCESS_DENIED,
                              ("Failed to stop service %ls: %u (state=%u)\n", pwszServiceName, dwErr, uBuf.Status.dwCurrentState));
                CloseServiceHandle(hService);
            }
            else
            {
                pState->lrc = GetLastError();
                LogRel(("Failed to not open service %ls for stop+delete: %u\n", pwszServiceName, pState->lrc));
                hService = OpenServiceW(hSCM, pwszServiceName, SERVICE_CHANGE_CONFIG);
            }
            CloseServiceHandle(hService);
        }

        CloseServiceHandle(hSCM);

        /*
         * Step #3: Create the service (if requested).
         *          Need to have the SC_MANAGER_CREATE_SERVICE access right for this.
         */
        if (fCreateIt)
        {
            Assert(pState->fUpdate);
            hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
            if (hSCM)
            {
                hService = CreateServiceW(hSCM,
                                          pwszServiceName,
                                          pwszDisplayName,
                                          SERVICE_CHANGE_CONFIG  /* dwDesiredAccess */,
                                          uServiceType,
                                          uStartType,
                                          uErrorControl,
                                          wszFilePath,
                                          NULL /* pwszLoadOrderGroup */,
                                          NULL /* pdwTagId */,
                                          wszzDependencies,
                                          pwszServiceStartName /* pwszServiceStartName */,
                                          NULL /* pwszPassword */);
                if (hService != NULL)
                {
                    uBuf.Desc.lpDescription = (WCHAR *)pwszDescription;
                    if (ChangeServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, &uBuf.Desc))
                        LogRel(("update service '%ls': created\n", pwszServiceName));
                    else
                        AssertMsgFailed(("Failed to set service description for %ls: %u\n", pwszServiceName, GetLastError()));
                    CloseServiceHandle(hService);
                }
                else
                {
                    pState->lrc = GetLastError();
                    AssertMsgFailed(("Failed to create service '%ls': %u\n", pwszServiceName, pState->lrc));
                }
                CloseServiceHandle(hSCM);
            }
            else
            {
                pState->lrc = GetLastError();
                LogRel(("Failed to open service manager with create service access: %u\n", pState->lrc));
            }
        }
    }
    else
        AssertLogRelMsgFailed(("OpenSCManagerW failed: %u\n", GetLastError()));
}
#endif /* VBOX_WITH_SDS */



/**
 * Gently update the COM registrations for VirtualBox.
 *
 * API that com::Initialize (VBoxCOM/initterm.cpp) calls the first time COM is
 * initialized in a process.  ASSUMES that the caller has initialized IPRT.
 *
 * @returns Windows error code.
 */
DECLEXPORT(uint32_t) VbpsUpdateRegistrations(void)
{
    LSTATUS         lrc;
    VBPSREGSTATE    State;
#ifdef VBOX_IN_32_ON_64_MAIN_API
    bool const      fIs32On64 = true;
#else
    bool const      fIs32On64 = false;
#endif

    /** @todo Should probably skip this when VBoxSVC is already running...  Use
     *        some mutex or something for checking. */

    /*
     * Find the VirtualBox application directory first.
     */
    WCHAR wszVBoxDir[MAX_PATH];
    DWORD cwcRet = GetModuleFileNameW(g_hDllSelf, wszVBoxDir, RT_ELEMENTS(wszVBoxDir));
    AssertReturn(cwcRet > 0 && cwcRet < RT_ELEMENTS(wszVBoxDir), ERROR_BUFFER_OVERFLOW);
    vbpsDllPathToVBoxDir(wszVBoxDir);

    /*
     * Update registry entries for the current CPU bitness.
     */
    lrc = vbpsRegInit(&State, HKEY_CLASSES_ROOT, NULL, false /*fDelete*/, true /*fUpdate*/, 0);
    if (lrc == ERROR_SUCCESS && !vbpsIsUpToDate(&State))
    {

#ifdef VBOX_WITH_SDS
        vbpsUpdateWindowsService(&State, wszVBoxDir, L"VBoxSDS.exe", L"VBoxSDS",
                                 L"VirtualBox system service", L"Used as a COM server for VirtualBox API.");
#endif
        vbpsUpdateTypeLibRegistration(&State, wszVBoxDir, fIs32On64);
        vbpsUpdateProxyStubRegistration(&State, wszVBoxDir, fIs32On64);
        vbpsUpdateInterfaceRegistrations(&State);
        RegisterXidlModulesAndClassesGenerated(&State, wszVBoxDir, fIs32On64);
        vbpsMarkUpToDate(&State);
        lrc = State.lrc;
    }
    vbpsRegTerm(&State);


#if (ARCH_BITS == 64 && defined(VBOX_WITH_32_ON_64_MAIN_API)) /*|| defined(VBOX_IN_32_ON_64_MAIN_API) ??*/
    /*
     * Update registry entries for the other CPU bitness.
     */
    if (lrc == ERROR_SUCCESS)
    {
        lrc = vbpsRegInit(&State, HKEY_CLASSES_ROOT, NULL, false /*fDelete*/, true /*fUpdate*/,
                          !fIs32On64 ? KEY_WOW64_32KEY : KEY_WOW64_64KEY);
        if (lrc == ERROR_SUCCESS && !vbpsIsUpToDate(&State))
        {
            vbpsUpdateTypeLibRegistration(&State, wszVBoxDir, !fIs32On64);
            vbpsUpdateProxyStubRegistration(&State, wszVBoxDir, !fIs32On64);
            vbpsUpdateInterfaceRegistrations(&State);
            RegisterXidlModulesAndClassesGenerated(&State, wszVBoxDir, !fIs32On64);
            vbpsMarkUpToDate(&State);
            lrc = State.lrc;
        }
        vbpsRegTerm(&State);
    }
#endif

    return VINF_SUCCESS;
}
