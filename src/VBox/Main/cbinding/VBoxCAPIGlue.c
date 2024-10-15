/* $Id: VBoxCAPIGlue.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Glue code for dynamically linking to VBoxCAPI.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
/* NOTE: do NOT use any include files here which are only available in the
 * VirtualBox tree, e.g. iprt. They are not available in the SDK, which is
 * where this file will provided as source code and has to be compilable. */
#include "VBoxCAPIGlue.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#ifdef WIN32
# define _INTPTR 2 /* on Windows stdint.h compares this in #if, causing warnings if not defined */
#endif /* WIN32 */
#include <stdint.h>
#ifndef WIN32
# include <dlfcn.h>
# include <pthread.h>
#else  /* WIN32 */
# include <Windows.h>
#endif /* WIN32 */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(__i386__) || defined(_M_IX86) || defined(__X86__)
/** This is a 32-bit process running against a 64-bit VBox installation.
 * Since there are only 64-bit VBox now, we don't need any runtime checks.  */
# define IS_32_ON_64
#endif

#if defined(__linux__) || defined(__linux_gnu__) || defined(__sun__) || defined(__FreeBSD__)
# ifndef IS_32_ON_64
#  define DYNLIB_NAME           "VBoxXPCOMC.so"
# else
#  define DYNLIB_NAME           "VBoxCAPI-x86.so"
# endif
#elif defined(__APPLE__)
# ifndef IS_32_ON_64
#  define DYNLIB_NAME           "VBoxXPCOMC.dylib"
# else
#  define DYNLIB_NAME           "VBoxCAPI-x86.dylib"
# endif
#elif defined(WIN32)
# ifndef IS_32_ON_64
#  define DYNLIB_NAME           "VBoxCAPI.dll"
# else
#  define DYNLIB_NAME           "VBoxCAPI-x86.dll"
#  define DYNLIB_SUBDIR         "x86"
#  define SLASH_DYNLIB_SUBDIR   "\\" DYNLIB_SUBDIR
# endif
#else
# error "Port me"
#endif
#ifndef DYNLIB_SUBDIR
# define DYNLIB_SUBDIR          ""
# define SLASH_DYNLIB_SUBDIR    ""
#endif

#ifdef WIN32
# define DIR_SLASH_CH           '\\'
#else
# define DIR_SLASH_CH           '/'
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The so/dynsym/dll handle for VBoxCAPI. */
#ifndef WIN32
void *g_hVBoxCAPI = NULL;
#else /* WIN32 */
HMODULE g_hVBoxCAPI = NULL;
#endif /* WIN32 */
/** The last load error. */
char g_szVBoxErrMsg[256] = "";
/** Pointer to the VBOXCAPI function table. */
PCVBOXCAPI g_pVBoxFuncs = NULL;
/** Pointer to VBoxGetCAPIFunctions for the loaded VBoxCAPI so/dylib/dll. */
PFNVBOXGETCAPIFUNCTIONS g_pfnGetFunctions = NULL;

typedef void FNDUMMY(void);
typedef FNDUMMY *PFNDUMMY;
/** Just a dummy global structure containing a bunch of
 * function pointers to code which is wanted in the link. */
PFNDUMMY g_apfnVBoxCAPIGlue[] =
{
#ifndef WIN32
    /* The following link dependency is for helping gdb as it gets hideously
     * confused if the application doesn't drag in pthreads, but uses it. */
    (PFNDUMMY)pthread_create,
#endif /* !WIN32 */
    NULL
};


/**
 * Wrapper for setting g_szVBoxErrMsg. Can be an empty stub.
 *
 * @param   fAlways         When 0 the g_szVBoxErrMsg is only set if empty.
 * @param   pszFormat       The format string.
 * @param   ...             The arguments.
 */
static void setErrMsg(int fAlways, const char *pszFormat, ...)
{
    if (    fAlways
        ||  !g_szVBoxErrMsg[0])
    {
        va_list va;
        va_start(va, pszFormat);
        vsnprintf(g_szVBoxErrMsg, sizeof(g_szVBoxErrMsg), pszFormat, va);
        va_end(va);
    }
}


/**
 * Try load C API .so/dylib/dll from the specified location and resolve all
 * the symbols we need. Tries both the new style and legacy name.
 *
 * @returns 0 on success, -1 on failure.
 * @param   pszHome         The directory where to try load VBoxCAPI/VBoxXPCOMC
 *                          from. Can be NULL.
 * @param   fSetAppHome     Whether to set the VBOX_APP_HOME env.var. or not
 *                          (boolean).
 */
static int tryLoadLibrary(const char *pszHome, const char *pszSub, int fSetAppHome)
{
    size_t const cchHome = pszHome ? strlen(pszHome) : 0;
    size_t const cchSub  = pszSub  ? strlen(pszSub)  : 0;
    size_t       cbBufNeeded;
    size_t       offName;
    char         szName[4096];

    /*
     * Construct the full name.
     */
    cbBufNeeded = cchHome + !!cchHome + cchSub + !!cchSub + sizeof(DYNLIB_NAME);
    if (cbBufNeeded > sizeof(szName))
    {
        setErrMsg(1, "path buffer too small: %u bytes needed",
                  (unsigned)cbBufNeeded);
        return -1;
    }
    offName = 0;
    if (cchHome)
    {
        memcpy(&szName[offName], pszHome, cchHome);
        offName += cchHome;
        szName[offName++] = DIR_SLASH_CH;
    }
    if (cchSub)
    {
        memcpy(&szName[offName], pszSub, cchSub);
        offName += cchSub;
        szName[offName++] = DIR_SLASH_CH;
    }
    memcpy(&szName[offName], DYNLIB_NAME, sizeof(DYNLIB_NAME));

    /*
     * Try load it by that name, setting the VBOX_APP_HOME first (for now).
     * Then resolve and call the function table getter.
     */
    if (fSetAppHome)
    {
#ifndef WIN32
        if (pszHome)
            setenv("VBOX_APP_HOME", pszHome, 1 /* always override */);
        else
            unsetenv("VBOX_APP_HOME");
#endif /* !WIN32 */
    }

#ifndef WIN32
    g_hVBoxCAPI = dlopen(szName, RTLD_NOW | RTLD_LOCAL);
#else /* WIN32 */
    g_hVBoxCAPI = LoadLibraryExA(szName, NULL /* hFile */,
                                 cchHome ? LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 : 0 /* dwFlags */);
    if (!g_hVBoxCAPI && GetLastError() == ERROR_INVALID_PARAMETER)
        g_hVBoxCAPI = LoadLibraryExA(szName, NULL /* hFile */, 0 /* dwFlags */);
#endif /* WIN32 */
    if (g_hVBoxCAPI)
    {
        PFNVBOXGETCAPIFUNCTIONS pfnGetFunctions;
#ifndef WIN32
        pfnGetFunctions = (PFNVBOXGETCAPIFUNCTIONS)(uintptr_t)
            dlsym(g_hVBoxCAPI, VBOX_GET_CAPI_FUNCTIONS_SYMBOL_NAME);
# ifdef VBOX_GET_XPCOM_FUNCTIONS_SYMBOL_NAME
        if (!pfnGetFunctions)
            pfnGetFunctions = (PFNVBOXGETCAPIFUNCTIONS)(uintptr_t)
                dlsym(g_hVBoxCAPI, VBOX_GET_XPCOM_FUNCTIONS_SYMBOL_NAME);
# endif /* VBOX_GET_XPCOM_FUNCTIONS_SYMBOL_NAME */
#else /* WIN32 */
        pfnGetFunctions = (PFNVBOXGETCAPIFUNCTIONS)
            GetProcAddress(g_hVBoxCAPI, VBOX_GET_CAPI_FUNCTIONS_SYMBOL_NAME);
#endif /* WIN32 */
        if (pfnGetFunctions)
        {
            g_pVBoxFuncs = pfnGetFunctions(VBOX_CAPI_VERSION);
            if (g_pVBoxFuncs)
            {
                if (      VBOX_CAPI_MAJOR(g_pVBoxFuncs->uVersion)
                       == VBOX_CAPI_MAJOR(VBOX_CAPI_VERSION)
                    &&    VBOX_CAPI_MINOR(g_pVBoxFuncs->uVersion)
                       >= VBOX_CAPI_MINOR(VBOX_CAPI_VERSION))
                {
                    g_pfnGetFunctions = pfnGetFunctions;
                    return 0;
                }
                setErrMsg(1, "%.80s: pfnGetFunctions(%#x) returned incompatible version %#x",
                          szName, VBOX_CAPI_VERSION, g_pVBoxFuncs->uVersion);
                g_pVBoxFuncs = NULL;
            }
            else
            {
                /* bail out */
                setErrMsg(1, "%.80s: pfnGetFunctions(%#x) failed",
                          szName, VBOX_CAPI_VERSION);
            }
        }
        else
        {
#ifndef WIN32
            setErrMsg(1, "dlsym(%.80s/%.32s): %.128s",
                      szName, VBOX_GET_CAPI_FUNCTIONS_SYMBOL_NAME, dlerror());
#else /* WIN32 */
            setErrMsg(1, "GetProcAddress(%.80s/%.32s): %d",
                      szName, VBOX_GET_CAPI_FUNCTIONS_SYMBOL_NAME, GetLastError());
#endif /* WIN32 */
        }

#ifndef WIN32
        dlclose(g_hVBoxCAPI);
#else /* WIN32 */
        FreeLibrary(g_hVBoxCAPI);
#endif /* WIN32 */
        g_hVBoxCAPI = NULL;
    }
    else
    {
#ifndef WIN32
        setErrMsg(0, "dlopen(%.80s): %.160s", szName, dlerror());
#else /* WIN32 */
        setErrMsg(0, "LoadLibraryEx(%.80s): %d", szName, GetLastError());
#endif /* WIN32 */
    }

    return -1;
}


/**
 * Tries to locate and load VBoxCAPI.so/dylib/dll, resolving all the related
 * function pointers.
 *
 * @returns 0 on success, -1 on failure.
 *
 * @remark  This should be considered moved into a separate glue library since
 *          its its going to be pretty much the same for any user of VBoxCAPI
 *          and it will just cause trouble to have duplicate versions of this
 *          source code all around the place.
 */
int VBoxCGlueInit(void)
{
    const char *pszHome;

    memset(g_szVBoxErrMsg, 0, sizeof(g_szVBoxErrMsg));

    /*
     * If the user specifies the location, try only that.
     */
    pszHome = getenv("VBOX_APP_HOME");
    if (pszHome)
        return tryLoadLibrary(pszHome, DYNLIB_SUBDIR, 0);

    /*
     * Try the known standard locations.
     */
#if defined(__gnu__linux__) || defined(__linux__)
    if (tryLoadLibrary("/opt/VirtualBox", DYNLIB_SUBDIR, 1) == 0)
        return 0;
    if (tryLoadLibrary("/usr/lib/virtualbox", DYNLIB_SUBDIR, 1) == 0)
        return 0;
#elif defined(__sun__)
    if (tryLoadLibrary("/opt/VirtualBox/amd64", NULL, 1) == 0)
        return 0;
    if (tryLoadLibrary("/opt/VirtualBox/i386", NULL, 1) == 0)
        return 0;
#elif defined(__APPLE__)
    if (tryLoadLibrary("/Applications/VirtualBox.app/Contents/MacOS", DYNLIB_SUBDIR, 1) == 0)
        return 0;
#elif defined(__FreeBSD__)
    if (tryLoadLibrary("/usr/local/lib/virtualbox", DYNLIB_SUBDIR, 1) == 0)
        return 0;
#elif defined(WIN32)
# ifdef IS_32_ON_64
    pszHome = getenv("ProgramW6432");
    if (pszHome && tryLoadLibrary(pszHome, "\\Oracle\\VirtualBox" SLASH_DYNLIB_SUBDIR, 1) == 0)
        return 0;
# endif
    pszHome = getenv("ProgramFiles");
    if (pszHome && tryLoadLibrary(pszHome, "\\Oracle\\VirtualBox" SLASH_DYNLIB_SUBDIR, 1) == 0)
        return 0;
    if (tryLoadLibrary("C:\\Program Files\\Oracle\\VirtualBox", DYNLIB_SUBDIR, 1) == 0)
        return 0;
#else
# error "port me"
#endif

    /*
     * Finally try the dynamic linker search path.
     */
    if (tryLoadLibrary(NULL, NULL, 1) == 0)
        return 0;

    /* No luck, return failure. */
    return -1;
}


/**
 * Terminate the C glue library.
 */
void VBoxCGlueTerm(void)
{
    if (g_hVBoxCAPI)
    {
#if 0 /* VBoxRT.so doesn't like being reloaded. See @bugref{3725}. */
#ifndef WIN32
        dlclose(g_hVBoxCAPI);
#else
        FreeLibrary(g_hVBoxCAPI);
#endif
#endif
        g_hVBoxCAPI = NULL;
    }
    g_pVBoxFuncs = NULL;
    g_pfnGetFunctions = NULL;
    memset(g_szVBoxErrMsg, 0, sizeof(g_szVBoxErrMsg));
}

