/* $Id: http-curl.cpp 106673 2024-10-24 15:04:21Z brent.paulson@oracle.com $ */
/** @file
 * IPRT - HTTP client API, cURL based.
 *
 * Logging groups:
 *      Log4 - request headers.
 *      Log5 - request body.
 *      Log6 - response headers.
 *      Log7 - response body.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_HTTP
#include <iprt/http.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/base64.h>
#include <iprt/cidr.h>
#include <iprt/crypto/store.h>
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/net.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uni.h>
#include <iprt/uri.h>
#include <iprt/utf16.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/pkix.h>
#include <iprt/crypto/key.h>


#include "internal/magics.h"

#ifdef RT_OS_WINDOWS /* curl.h drags in windows.h which isn't necessarily -Wall clean. */
# include <iprt/win/windows.h>
#endif
#include <curl/curl.h>

#ifdef RT_OS_DARWIN
# include <CoreFoundation/CoreFoundation.h>
# include <SystemConfiguration/SystemConfiguration.h>
# include <CoreServices/CoreServices.h>
#endif
#ifdef RT_OS_WINDOWS
# include <Winhttp.h>
# include "../r3/win/internal-r3-win.h"
#endif

#ifdef RT_OS_LINUX
# define IPRT_USE_LIBPROXY
#endif
#ifdef IPRT_USE_LIBPROXY
# include <stdlib.h> /* free */
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Output collection data. */
typedef struct RTHTTPOUTPUTDATA
{
    /** Pointer to the HTTP client instance structure. */
    struct RTHTTPINTERNAL  *pHttp;
    /** Callback specific data. */
    union
    {
        /** For file destination.  */
        RTFILE          hFile;
        /** For memory destination. */
        struct
        {
            /** The current size (sans terminator char). */
            size_t      cb;
            /** The currently allocated size. */
            size_t      cbAllocated;
            /** Pointer to the buffer. */
            uint8_t    *pb;
        } Mem;
    } uData;
} RTHTTPOUTPUTDATA;

/**
 * HTTP header.
 */
typedef struct RTHTTPHEADER
{
    /** The core list structure. */
    struct curl_slist   Core;
    /** The field name length. */
    uint32_t            cchName;
    /** The value offset. */
    uint32_t            offValue;
    /** The full header field. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RT_GCC_EXTENSION char szData[RT_FLEXIBLE_ARRAY];
} RTHTTPHEADER;
/** Pointer to a HTTP header. */
typedef RTHTTPHEADER *PRTHTTPHEADER;

/**
 * Internal HTTP client instance.
 */
typedef struct RTHTTPINTERNAL
{
    /** Magic value. */
    uint32_t            u32Magic;
    /** cURL handle. */
    CURL               *pCurl;
    /** The last response code. */
    long                lLastResp;
    /** Custom headers (PRTHTTPHEADER).
     * The list head is registered with curl, though we do all the allocating. */
    struct curl_slist  *pHeaders;
    /** Where to append the next header. */
    struct curl_slist **ppHeadersTail;

    /** CA certificate file for HTTPS authentication. */
    char               *pszCaFile;
    /** Whether to delete the CA on destruction. */
    bool                fDeleteCaFile;

    /** Set if we've applied a CURLOTP_USERAGENT already.  */
    bool                fHaveSetUserAgent;
    /** Set if we've got a user agent header, otherwise clear.  */
    bool                fHaveUserAgentHeader;

    /** @name Proxy settings.
     * When fUseSystemProxySettings is set, the other members will be updated each
     * time we're presented with a new URL.  The members reflect the cURL
     * configuration.
     *
     * @{ */
    /** Set if we should use the system proxy settings for a URL.
     * This means reconfiguring cURL for each request.  */
    bool                fUseSystemProxySettings;
    /** Set if we've detected no proxy necessary. */
    bool                fNoProxy;
    /** Set if we've reset proxy info in cURL and need to reapply it.  */
    bool                fReapplyProxyInfo;
    /** Proxy host name (RTStrFree). */
    char               *pszProxyHost;
    /** Proxy port number (UINT32_MAX if not specified). */
    uint32_t            uProxyPort;
    /** The proxy type (CURLPROXY_HTTP, CURLPROXY_SOCKS5, ++). */
    curl_proxytype      enmProxyType;
    /** Proxy username (RTStrFree). */
    char               *pszProxyUsername;
    /** Proxy password (RTStrFree). */
    char               *pszProxyPassword;
    /** @} */

    /** @name Cached settings.
     * @{ */
    /** Maximum number of redirects to follow.
     * Zero if not automatically following (default). */
    uint32_t            cMaxRedirects;
    /** Whether to check if Peer lies about his SSL certificate. */
    bool                fVerifyPeer;
    /** @} */

    /** Abort the current HTTP request if true. */
    bool volatile       fAbort;
    /** Set if someone is preforming an HTTP operation. */
    bool volatile       fBusy;
    /** The location field for 301 responses. */
    char               *pszRedirLocation;

    union
    {
        struct
        {
            /** Pointer to the memory block we're feeding the cURL/server. */
            void const *pvMem;
            /** Size of the memory block. */
            size_t      cbMem;
            /** Current memory block offset. */
            size_t      offMem;
        } Mem;
    }                   ReadData;

    /** Body output callback data. */
    RTHTTPOUTPUTDATA    BodyOutput;
    /** Headers output callback data. */
    RTHTTPOUTPUTDATA    HeadersOutput;
    /** The output status.*/
    int                 rcOutput;

    /** @name Upload callback
     * @{ */
    /** Pointer to the upload callback function, if any. */
    PFNRTHTTPUPLOADCALLBACK         pfnUploadCallback;
    /** The user argument for the upload callback function. */
    void                           *pvUploadCallbackUser;
    /** The expected upload size, UINT64_MAX if not known. */
    uint64_t                        cbUploadContent;
    /** The current upload offset. */
    uint64_t                        offUploadContent;
    /** @} */

    /** @name Download callback.
     * @{ */
    /** Pointer to the download callback function, if any. */
    PFNRTHTTPDOWNLOADCALLBACK       pfnDownloadCallback;
    /** The user argument for the download callback function. */
    void                           *pvDownloadCallbackUser;
    /** The flags for the download callback function. */
    uint32_t                        fDownloadCallback;
    /** HTTP status for passing to the download callback, UINT32_MAX if not known. */
    uint32_t                        uDownloadHttpStatus;
    /** The download content length, or UINT64_MAX. */
    uint64_t                        cbDownloadContent;
    /** The current download offset. */
    uint64_t                        offDownloadContent;
    /** @} */

    /** @name Download progress callback.
     * @{ */
    /** Download size hint set by the progress callback. */
    uint64_t                        cbDownloadHint;
    /** Callback called during download. */
    PFNRTHTTPDOWNLDPROGRCALLBACK    pfnDownloadProgress;
    /** User pointer parameter for pfnDownloadProgress. */
    void                           *pvDownloadProgressUser;
    /** @} */

    /** @name Header callback.
     * @{ */
    /** Pointer to the header callback function, if any. */
    PFNRTHTTPHEADERCALLBACK         pfnHeaderCallback;
    /** User pointer parameter for pfnHeaderCallback. */
    void                           *pvHeaderCallbackUser;
    /** @} */

    /** Buffer for human readable error messages from curl on failures or problems. */
    char szErrorBuffer[CURL_ERROR_SIZE];
} RTHTTPINTERNAL;
/** Pointer to an internal HTTP client instance. */
typedef RTHTTPINTERNAL *PRTHTTPINTERNAL;


#ifdef RT_OS_WINDOWS
/** @name Windows: Types for dynamically resolved APIs
 * @{ */
typedef HINTERNET (WINAPI * PFNWINHTTPOPEN)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef BOOL      (WINAPI * PFNWINHTTPCLOSEHANDLE)(HINTERNET);
typedef BOOL      (WINAPI * PFNWINHTTPGETPROXYFORURL)(HINTERNET, LPCWSTR, WINHTTP_AUTOPROXY_OPTIONS *, WINHTTP_PROXY_INFO *);
typedef BOOL      (WINAPI * PFNWINHTTPGETDEFAULTPROXYCONFIGURATION)(WINHTTP_PROXY_INFO *);
typedef BOOL      (WINAPI * PFNWINHTTPGETIEPROXYCONFIGFORCURRENTUSER)(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG *);
/** @} */
#endif

#ifdef IPRT_USE_LIBPROXY
typedef struct px_proxy_factory *PLIBPROXYFACTORY;
typedef PLIBPROXYFACTORY (* PFNLIBPROXYFACTORYCTOR)(void);
typedef void             (* PFNLIBPROXYFACTORYDTOR)(PLIBPROXYFACTORY);
typedef char          ** (* PFNLIBPROXYFACTORYGETPROXIES)(PLIBPROXYFACTORY, const char *);
typedef void             (* PFNLIBPROXYFACTORYFREEPROXIES)(char **);
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def RTHTTP_MAX_MEM_DOWNLOAD_SIZE
 * The max size we are allowed to download to a memory buffer.
 *
 * @remarks The minus 1 is for the trailing zero terminator we always add.
 */
#if ARCH_BITS == 64
# define RTHTTP_MAX_MEM_DOWNLOAD_SIZE       (UINT32_C(64)*_1M - 1)
#else
# define RTHTTP_MAX_MEM_DOWNLOAD_SIZE       (UINT32_C(32)*_1M - 1)
#endif

/** Checks whether a cURL return code indicates success. */
#define CURL_SUCCESS(rcCurl)    RT_LIKELY(rcCurl == CURLE_OK)
/** Checks whether a cURL return code indicates failure. */
#define CURL_FAILURE(rcCurl)    RT_UNLIKELY(rcCurl != CURLE_OK)

/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTHTTP_VALID_RETURN_RC(hHttp, a_rc) \
    do { \
        AssertPtrReturn((hHttp), (a_rc)); \
        AssertReturn((hHttp)->u32Magic == RTHTTP_MAGIC, (a_rc)); \
    } while (0)

/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTHTTP_VALID_RETURN(hHTTP) RTHTTP_VALID_RETURN_RC((hHttp), VERR_INVALID_HANDLE)

/** Validates a handle and returns (void) if not valid. */
#define RTHTTP_VALID_RETURN_VOID(hHttp) \
    do { \
        AssertPtrReturnVoid(hHttp); \
        AssertReturnVoid((hHttp)->u32Magic == RTHTTP_MAGIC); \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef RT_OS_WINDOWS
/** @name Windows: Dynamically resolved APIs
 * @{ */
static RTONCE                                   g_WinResolveImportsOnce = RTONCE_INITIALIZER;
static PFNWINHTTPOPEN                           g_pfnWinHttpOpen = NULL;
static PFNWINHTTPCLOSEHANDLE                    g_pfnWinHttpCloseHandle = NULL;
static PFNWINHTTPGETPROXYFORURL                 g_pfnWinHttpGetProxyForUrl = NULL;
static PFNWINHTTPGETDEFAULTPROXYCONFIGURATION   g_pfnWinHttpGetDefaultProxyConfiguration = NULL;
static PFNWINHTTPGETIEPROXYCONFIGFORCURRENTUSER g_pfnWinHttpGetIEProxyConfigForCurrentUser = NULL;
/** @} */
#endif

#ifdef IPRT_USE_LIBPROXY
/** @name Dynamaically resolved libproxy APIs.
 * @{ */
static RTONCE                                   g_LibProxyResolveImportsOnce = RTONCE_INITIALIZER;
static RTLDRMOD                                 g_hLdrLibProxy = NIL_RTLDRMOD;
static PFNLIBPROXYFACTORYCTOR                   g_pfnLibProxyFactoryCtor = NULL;
static PFNLIBPROXYFACTORYDTOR                   g_pfnLibProxyFactoryDtor = NULL;
static PFNLIBPROXYFACTORYGETPROXIES             g_pfnLibProxyFactoryGetProxies = NULL;
/** Can be NULL, as it was introduced with libproxy v0.4.16 (2020-12-04). */
static PFNLIBPROXYFACTORYFREEPROXIES            g_pfnLibProxyFactoryFreeProxies = NULL;
/** @} */
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtHttpUnsetCaFile(PRTHTTPINTERNAL pThis);
#ifdef RT_OS_DARWIN
static int rtHttpDarwinTryConfigProxies(PRTHTTPINTERNAL pThis, CFArrayRef hArrayProxies, CFURLRef hUrlTarget, bool fIgnorePacType);
#endif
static void rtHttpFreeHeaders(PRTHTTPINTERNAL pThis);


RTR3DECL(int) RTHttpCreate(PRTHTTP phHttp)
{
    AssertPtrReturn(phHttp, VERR_INVALID_PARAMETER);

    /** @todo r=bird: rainy day: curl_global_init is not thread safe, only a
     *        problem if multiple threads get here at the same time. */
    int rc = VERR_HTTP_INIT_FAILED;
    CURLcode rcCurl = curl_global_init(CURL_GLOBAL_ALL);
    if (CURL_SUCCESS(rcCurl))
    {
        CURL *pCurl = curl_easy_init();
        if (pCurl)
        {
            PRTHTTPINTERNAL pThis = (PRTHTTPINTERNAL)RTMemAllocZ(sizeof(RTHTTPINTERNAL));
            if (pThis)
            {
                pThis->u32Magic                 = RTHTTP_MAGIC;
                pThis->pCurl                    = pCurl;
                pThis->ppHeadersTail            = &pThis->pHeaders;
                pThis->fHaveSetUserAgent        = false;
                pThis->fHaveUserAgentHeader     = false;
                pThis->fUseSystemProxySettings  = true;
                pThis->cMaxRedirects            = 0; /* no automatic redir following */
                pThis->fVerifyPeer              = true;
                pThis->BodyOutput.pHttp         = pThis;
                pThis->HeadersOutput.pHttp      = pThis;
                pThis->uDownloadHttpStatus      = UINT32_MAX;
                pThis->cbDownloadContent        = UINT64_MAX;
                pThis->offDownloadContent       = 0;
                pThis->cbUploadContent          = UINT64_MAX;
                pThis->offUploadContent         = 0;

                /* ask curl to give us back error messages */
                curl_easy_setopt(pThis->pCurl, CURLOPT_ERRORBUFFER, pThis->szErrorBuffer);

                *phHttp = (RTHTTP)pThis;

                return VINF_SUCCESS;
            }
            rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_HTTP_INIT_FAILED;
    }
    curl_global_cleanup();
    return rc;
}


RTR3DECL(int) RTHttpReset(RTHTTP hHttp, uint32_t fFlags)
{
    /* Validate the instance handle, state and flags. */
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(!pThis->fBusy, VERR_WRONG_ORDER);
    AssertReturn(!(fFlags & ~RTHTTP_RESET_F_VALID_MASK), VERR_INVALID_FLAGS);

    /* This resets options, but keeps open connections, cookies, etc. */
    curl_easy_reset(pThis->pCurl);

    /** @todo check if CURLOPT_SSL_VERIFYPEER is affected by curl_easy_reset. */

    if (!(fFlags & RTHTTP_RESET_F_KEEP_HEADERS))
        rtHttpFreeHeaders(pThis);

    pThis->uDownloadHttpStatus      = UINT32_MAX;
    pThis->cbDownloadContent        = UINT64_MAX;
    pThis->offDownloadContent       = 0;
    pThis->cbUploadContent          = UINT64_MAX;
    pThis->offUploadContent         = 0;
    pThis->rcOutput                 = VINF_SUCCESS;

    /* Tell the proxy configuration code to reapply settings even if they
       didn't change as cURL has forgotten them: */
    pThis->fReapplyProxyInfo        = true;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpDestroy(RTHTTP hHttp)
{
    if (hHttp == NIL_RTHTTP)
        return VINF_SUCCESS;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    Assert(!pThis->fBusy);

    pThis->u32Magic = RTHTTP_MAGIC_DEAD;

    curl_easy_cleanup(pThis->pCurl);
    pThis->pCurl = NULL;

    rtHttpFreeHeaders(pThis);

    rtHttpUnsetCaFile(pThis);
    Assert(!pThis->pszCaFile);

    if (pThis->pszRedirLocation)
    {
        RTStrFree(pThis->pszRedirLocation);
        pThis->pszRedirLocation = NULL;
    }

    RTStrFree(pThis->pszProxyHost);
    pThis->pszProxyHost = NULL;
    RTStrFree(pThis->pszProxyUsername);
    pThis->pszProxyUsername = NULL;
    if (pThis->pszProxyPassword)
    {
        RTMemWipeThoroughly(pThis->pszProxyPassword, strlen(pThis->pszProxyPassword), 2);
        RTStrFree(pThis->pszProxyPassword);
        pThis->pszProxyPassword = NULL;
    }

    RTMemFree(pThis);

    curl_global_cleanup();

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpAbort(RTHTTP hHttp)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    pThis->fAbort = true;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpGetRedirLocation(RTHTTP hHttp, char **ppszRedirLocation)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    Assert(!pThis->fBusy);

    if (!pThis->pszRedirLocation)
        return VERR_HTTP_NOT_FOUND;

    return RTStrDupEx(ppszRedirLocation, pThis->pszRedirLocation);
}


RTR3DECL(int) RTHttpSetFollowRedirects(RTHTTP hHttp, uint32_t cMaxRedirects)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(!pThis->fBusy, VERR_WRONG_ORDER);

    /*
     * Update the redirection settings.
     */
    if (pThis->cMaxRedirects != cMaxRedirects)
    {
        CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_MAXREDIRS, (long)cMaxRedirects);
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("CURLOPT_MAXREDIRS=%u: %d (%#x)\n", cMaxRedirects, rcCurl, rcCurl),
                        VERR_HTTP_CURL_ERROR);

        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_FOLLOWLOCATION, (long)(cMaxRedirects > 0));
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("CURLOPT_FOLLOWLOCATION=%d: %d (%#x)\n", cMaxRedirects > 0, rcCurl, rcCurl),
                        VERR_HTTP_CURL_ERROR);

        pThis->cMaxRedirects = cMaxRedirects;
    }
    return VINF_SUCCESS;
}


RTR3DECL(uint32_t) RTHttpGetFollowRedirects(RTHTTP hHttp)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN_RC(pThis, 0);
    return pThis->cMaxRedirects;
}


/*********************************************************************************************************************************
*   Proxy handling.                                                                                                              *
*********************************************************************************************************************************/

RTR3DECL(int) RTHttpUseSystemProxySettings(RTHTTP hHttp)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(!pThis->fBusy, VERR_WRONG_ORDER);

    /*
     * Change the settings.
     */
    pThis->fUseSystemProxySettings = true;
    return VINF_SUCCESS;
}


/**
 * rtHttpConfigureProxyForUrl: Update cURL proxy settings as needed.
 *
 * @returns IPRT status code.
 * @param   pThis           The HTTP client instance.
 * @param   enmProxyType    The proxy type.
 * @param   pszHost         The proxy host name.
 * @param   uPort           The proxy port number.
 * @param   pszUsername     The proxy username, or NULL if none.
 * @param   pszPassword     The proxy password, or NULL if none.
 */
static int rtHttpUpdateProxyConfig(PRTHTTPINTERNAL pThis, curl_proxytype enmProxyType, const char *pszHost,
                                   uint32_t uPort, const char *pszUsername, const char *pszPassword)
{
    CURLcode rcCurl;
    AssertReturn(pszHost, VERR_INVALID_PARAMETER);
    Log(("rtHttpUpdateProxyConfig: pThis=%p type=%d host='%s' port=%u user='%s'%s\n",
         pThis, enmProxyType, pszHost, uPort, pszUsername, pszPassword ? " with password" : " without password"));

    if (pThis->fNoProxy)
    {
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_NOPROXY, (const char *)NULL);
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("CURLOPT_NOPROXY=NULL: %d (%#x)\n", rcCurl, rcCurl),
                        VERR_HTTP_CURL_PROXY_CONFIG);
        pThis->fNoProxy = false;
    }

    if (   pThis->fReapplyProxyInfo
        || enmProxyType != pThis->enmProxyType)
    {
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_PROXYTYPE, (long)enmProxyType);
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("CURLOPT_PROXYTYPE=%d: %d (%#x)\n", enmProxyType, rcCurl, rcCurl),
                        VERR_HTTP_CURL_PROXY_CONFIG);
        pThis->enmProxyType = enmProxyType;
    }

    if (   pThis->fReapplyProxyInfo
        || uPort != pThis->uProxyPort)
    {
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_PROXYPORT, (long)uPort);
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("CURLOPT_PROXYPORT=%d: %d (%#x)\n", uPort, rcCurl, rcCurl),
                        VERR_HTTP_CURL_PROXY_CONFIG);
        pThis->uProxyPort = uPort;
    }

    if (   pThis->fReapplyProxyInfo
        || pszUsername != pThis->pszProxyUsername
        || RTStrCmp(pszUsername, pThis->pszProxyUsername))
    {
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_PROXYUSERNAME, pszUsername);
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("CURLOPT_PROXYUSERNAME=%s: %d (%#x)\n", pszUsername, rcCurl, rcCurl),
                        VERR_HTTP_CURL_PROXY_CONFIG);
        if (pThis->pszProxyUsername)
        {
            RTStrFree(pThis->pszProxyUsername);
            pThis->pszProxyUsername = NULL;
        }
        if (pszUsername)
        {
            pThis->pszProxyUsername = RTStrDup(pszUsername);
            AssertReturn(pThis->pszProxyUsername, VERR_NO_STR_MEMORY);
        }
    }

    if (   pThis->fReapplyProxyInfo
        || pszPassword != pThis->pszProxyPassword
        || RTStrCmp(pszPassword, pThis->pszProxyPassword))
    {
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_PROXYPASSWORD, pszPassword);
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("CURLOPT_PROXYPASSWORD=%s: %d (%#x)\n", pszPassword ? "xxx" : NULL, rcCurl, rcCurl),
                        VERR_HTTP_CURL_PROXY_CONFIG);
        if (pThis->pszProxyPassword)
        {
            RTMemWipeThoroughly(pThis->pszProxyPassword, strlen(pThis->pszProxyPassword), 2);
            RTStrFree(pThis->pszProxyPassword);
            pThis->pszProxyPassword = NULL;
        }
        if (pszPassword)
        {
            pThis->pszProxyPassword = RTStrDup(pszPassword);
            AssertReturn(pThis->pszProxyPassword, VERR_NO_STR_MEMORY);
        }
    }

    if (   pThis->fReapplyProxyInfo
        || pszHost != pThis->pszProxyHost
        || RTStrCmp(pszHost, pThis->pszProxyHost))
    {
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_PROXY, pszHost);
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("CURLOPT_PROXY=%s: %d (%#x)\n", pszHost, rcCurl, rcCurl),
                        VERR_HTTP_CURL_PROXY_CONFIG);
        if (pThis->pszProxyHost)
        {
            RTStrFree(pThis->pszProxyHost);
            pThis->pszProxyHost = NULL;
        }
        if (pszHost)
        {
            pThis->pszProxyHost = RTStrDup(pszHost);
            AssertReturn(pThis->pszProxyHost, VERR_NO_STR_MEMORY);
        }
    }

    pThis->fReapplyProxyInfo = false;
    return VINF_SUCCESS;
}


/**
 * rtHttpConfigureProxyForUrl: Disables proxying.
 *
 * @returns IPRT status code.
 * @param   pThis               The HTTP client instance.
 */
static int rtHttpUpdateAutomaticProxyDisable(PRTHTTPINTERNAL pThis)
{
    Log(("rtHttpUpdateAutomaticProxyDisable: pThis=%p\n", pThis));

    AssertReturn(curl_easy_setopt(pThis->pCurl, CURLOPT_PROXYTYPE,   (long)CURLPROXY_HTTP) == CURLE_OK, VERR_INTERNAL_ERROR_2);
    pThis->enmProxyType = CURLPROXY_HTTP;

    AssertReturn(curl_easy_setopt(pThis->pCurl, CURLOPT_PROXYPORT,             (long)1080) == CURLE_OK, VERR_INTERNAL_ERROR_2);
    pThis->uProxyPort = 1080;

    AssertReturn(curl_easy_setopt(pThis->pCurl, CURLOPT_PROXYUSERNAME, (const char *)NULL) == CURLE_OK, VERR_INTERNAL_ERROR_2);
    if (pThis->pszProxyUsername)
    {
        RTStrFree(pThis->pszProxyUsername);
        pThis->pszProxyUsername = NULL;
    }

    AssertReturn(curl_easy_setopt(pThis->pCurl, CURLOPT_PROXYPASSWORD, (const char *)NULL) == CURLE_OK, VERR_INTERNAL_ERROR_2);
    if (pThis->pszProxyPassword)
    {
        RTStrFree(pThis->pszProxyPassword);
        pThis->pszProxyPassword = NULL;
    }

    AssertReturn(curl_easy_setopt(pThis->pCurl, CURLOPT_PROXY, "") == CURLE_OK, VERR_INTERNAL_ERROR_2);
    if (pThis->pszProxyHost)
    {
        RTStrFree(pThis->pszProxyHost);
        pThis->pszProxyHost = NULL;
    }

    /* No proxy for everything! */
    AssertReturn(curl_easy_setopt(pThis->pCurl, CURLOPT_NOPROXY, "*") == CURLE_OK, CURLOPT_PROXY);
    pThis->fNoProxy = true;

    return VINF_SUCCESS;
}


/**
 * See if the host name of the URL is included in the stripped no_proxy list.
 *
 * The no_proxy list is a colon or space separated list of domain names for
 * which there should be no proxying.  Given "no_proxy=oracle.com" neither the
 * URL "http://www.oracle.com" nor "http://oracle.com" will not be proxied, but
 * "http://notoracle.com" will be.
 *
 * @returns true if the URL is in the no_proxy list, otherwise false.
 * @param   pszUrl          The URL.
 * @param   pszNoProxyList  The stripped no_proxy list.
 */
static bool rtHttpUrlInNoProxyList(const char *pszUrl, const char *pszNoProxyList)
{
    /*
     * Check for just '*', diabling proxying for everything.
     * (Caller stripped pszNoProxyList.)
     */
    if (*pszNoProxyList == '*' && pszNoProxyList[1] == '\0')
        return true;

    /*
     * Empty list? (Caller stripped it, remember).
     */
    if (!*pszNoProxyList)
        return false;

    /*
     * We now need to parse the URL and extract the host name.
     */
    RTURIPARSED Parsed;
    int rc = RTUriParse(pszUrl, &Parsed);
    AssertRCReturn(rc, false);
    char *pszHost = RTUriParsedAuthorityHost(pszUrl, &Parsed);
    if (!pszHost) /* Don't assert, in case of file:///xxx or similar blunder. */
        return false;

    bool fRet = false;
    size_t const cchHost = strlen(pszHost);
    if (cchHost)
    {
        /*
         * The list is comma or space separated, walk it and match host names.
         */
        while (*pszNoProxyList != '\0')
        {
            /* Strip leading slashes, commas and dots. */
            char ch;
            while (   (ch = *pszNoProxyList) == ','
                   || ch == '.'
                   || RT_C_IS_SPACE(ch))
                pszNoProxyList++;

            /* Find the end. */
            size_t cch     = RTStrOffCharOrTerm(pszNoProxyList, ',');
            size_t offNext = RTStrOffCharOrTerm(pszNoProxyList, ' ');
            cch = RT_MIN(cch, offNext);
            offNext = cch;

            /* Trip trailing spaces, well tabs and stuff. */
            while (cch > 0 && RT_C_IS_SPACE(pszNoProxyList[cch - 1]))
                cch--;

            /* Do the matching, if we have anything to work with. */
            if (cch > 0)
            {
                if (   (   cch == cchHost
                        && RTStrNICmp(pszNoProxyList, pszHost, cch) == 0)
                    || (   cch <  cchHost
                        && pszHost[cchHost - cch - 1] == '.'
                        && RTStrNICmp(pszNoProxyList, &pszHost[cchHost - cch], cch) == 0) )
                {
                    fRet = true;
                    break;
                }
            }

            /* Next. */
            pszNoProxyList += offNext;
        }
    }

    RTStrFree(pszHost);
    return fRet;
}


/**
 * Configures a proxy given a "URL" like specification.
 *
 * The format is:
 * @verbatim
 *      [<scheme>"://"][<userid>[@<password>]:]<server>[":"<port>]
 * @endverbatim
 *
 * Where the scheme gives the type of proxy server we're dealing with rather
 * than the protocol of the external server we wish to talk to.
 *
 * @returns IPRT status code.
 * @param   pThis               The HTTP client instance.
 * @param   pszProxyUrl         The proxy server "URL".
 */
static int rtHttpConfigureProxyFromUrl(PRTHTTPINTERNAL pThis, const char *pszProxyUrl)
{
    /*
     * Make sure it can be parsed as an URL.
     */
    char *pszFreeMe = NULL;
    if (!strstr(pszProxyUrl, "://"))
    {
        static const char s_szPrefix[] = "http://";
        size_t cchProxyUrl = strlen(pszProxyUrl);
        pszFreeMe = (char *)RTMemTmpAlloc(sizeof(s_szPrefix) + cchProxyUrl);
        if (pszFreeMe)
        {
            memcpy(pszFreeMe, s_szPrefix, sizeof(s_szPrefix) - 1);
            memcpy(&pszFreeMe[sizeof(s_szPrefix) - 1], pszProxyUrl, cchProxyUrl);
            pszFreeMe[sizeof(s_szPrefix) - 1 + cchProxyUrl] = '\0';
            pszProxyUrl = pszFreeMe;
        }
        else
            return VERR_NO_TMP_MEMORY;
    }

    RTURIPARSED Parsed;
    int rc = RTUriParse(pszProxyUrl, &Parsed);
    if (RT_SUCCESS(rc))
    {
        char *pszHost = RTUriParsedAuthorityHost(pszProxyUrl, &Parsed);
        if (pszHost)
        {
            /*
             * We've got a host name, try get the rest.
             */
            char    *pszUsername = RTUriParsedAuthorityUsername(pszProxyUrl, &Parsed);
            char    *pszPassword = RTUriParsedAuthorityPassword(pszProxyUrl, &Parsed);
            uint32_t uProxyPort  = RTUriParsedAuthorityPort(pszProxyUrl, &Parsed);
            bool     fUnknownProxyType = false;
            curl_proxytype enmProxyType;
            if (RTUriIsSchemeMatch(pszProxyUrl, "http"))
            {
                enmProxyType  = CURLPROXY_HTTP;
                if (uProxyPort == UINT32_MAX)
                    uProxyPort = 80;
            }
#ifdef CURL_AT_LEAST_VERSION
# if CURL_AT_LEAST_VERSION(7,52,0)
            else if (RTUriIsSchemeMatch(pszProxyUrl, "https"))
            {
                enmProxyType  = CURLPROXY_HTTPS;
                if (uProxyPort == UINT32_MAX)
                    uProxyPort = 443;
            }
# endif
#endif
            else if (   RTUriIsSchemeMatch(pszProxyUrl, "socks4")
                     || RTUriIsSchemeMatch(pszProxyUrl, "socks"))
                enmProxyType = CURLPROXY_SOCKS4;
            else if (RTUriIsSchemeMatch(pszProxyUrl, "socks4a"))
                enmProxyType = CURLPROXY_SOCKS4A;
            else if (RTUriIsSchemeMatch(pszProxyUrl, "socks5"))
                enmProxyType = CURLPROXY_SOCKS5;
            else if (RTUriIsSchemeMatch(pszProxyUrl, "socks5h"))
                enmProxyType = CURLPROXY_SOCKS5_HOSTNAME;
            else
            {
                fUnknownProxyType = true;
                enmProxyType = CURLPROXY_HTTP;
                if (uProxyPort == UINT32_MAX)
                    uProxyPort = 8080;
            }

            /* Guess the port from the proxy type if not given. */
            if (uProxyPort == UINT32_MAX)
                uProxyPort = 1080; /* CURL_DEFAULT_PROXY_PORT */

            rc = rtHttpUpdateProxyConfig(pThis, enmProxyType, pszHost, uProxyPort, pszUsername, pszPassword);
            if (RT_SUCCESS(rc) && fUnknownProxyType)
                rc = VWRN_WRONG_TYPE;

            RTStrFree(pszUsername);
            RTStrFree(pszPassword);
            RTStrFree(pszHost);
        }
        else
            AssertMsgFailed(("RTUriParsedAuthorityHost('%s',) -> NULL\n", pszProxyUrl));
    }
    else
        AssertMsgFailed(("RTUriParse('%s',) -> %Rrc\n", pszProxyUrl, rc));

    if (pszFreeMe)
        RTMemTmpFree(pszFreeMe);
    return rc;
}


RTR3DECL(int) RTHttpSetProxyByUrl(RTHTTP hHttp, const char *pszUrl)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertPtrNullReturn(pszUrl, VERR_INVALID_PARAMETER);
    AssertReturn(!pThis->fBusy, VERR_WRONG_ORDER);

    if (!pszUrl || !*pszUrl)
        return RTHttpUseSystemProxySettings(pThis);

    pThis->fUseSystemProxySettings = false;

    if (RTStrNICmpAscii(pszUrl, RT_STR_TUPLE("direct://")) == 0)
        return rtHttpUpdateAutomaticProxyDisable(pThis);
    return rtHttpConfigureProxyFromUrl(pThis, pszUrl);
}


/**
 * Consults enviornment variables that cURL/lynx/wget/lynx uses for figuring out
 * the proxy config.
 *
 * @returns IPRT status code.
 * @param   pThis               The HTTP client instance.
 * @param   pszUrl              The URL to configure a proxy for.
 */
static int rtHttpConfigureProxyForUrlFromEnv(PRTHTTPINTERNAL pThis, const char *pszUrl)
{
    char szTmp[_1K];

    /*
     * First we consult the "no_proxy" / "NO_PROXY" environment variable.
     */
    const char *pszNoProxyVar;
    size_t cchActual;
    char  *pszNoProxyFree = NULL;
    char  *pszNoProxy = szTmp;
    int rc = RTEnvGetEx(RTENV_DEFAULT, pszNoProxyVar = "no_proxy", szTmp, sizeof(szTmp), &cchActual);
    if (rc == VERR_ENV_VAR_NOT_FOUND)
        rc = RTEnvGetEx(RTENV_DEFAULT, pszNoProxyVar = "NO_PROXY", szTmp, sizeof(szTmp), &cchActual);
    if (rc == VERR_BUFFER_OVERFLOW)
    {
        pszNoProxyFree = pszNoProxy = (char *)RTMemTmpAlloc(cchActual + _1K);
        AssertReturn(pszNoProxy, VERR_NO_TMP_MEMORY);
        rc = RTEnvGetEx(RTENV_DEFAULT, pszNoProxyVar, pszNoProxy, cchActual + _1K, NULL);
    }
    AssertMsg(rc == VINF_SUCCESS || rc == VERR_ENV_VAR_NOT_FOUND, ("rc=%Rrc\n", rc));
    bool fNoProxy = false;
    if (RT_SUCCESS(rc))
        fNoProxy = rtHttpUrlInNoProxyList(pszUrl, RTStrStrip(pszNoProxy));
    RTMemTmpFree(pszNoProxyFree);
    if (!fNoProxy)
    {
        /*
         * Get the schema specific specific env var, falling back on the
         * generic all_proxy if not found.
         */
        const char *apszEnvVars[4];
        unsigned    cEnvVars = 0;
        if (!RTStrNICmp(pszUrl, RT_STR_TUPLE("http:")))
            apszEnvVars[cEnvVars++] = "http_proxy"; /* Skip HTTP_PROXY because of cgi paranoia */
        else if (!RTStrNICmp(pszUrl, RT_STR_TUPLE("https:")))
        {
            apszEnvVars[cEnvVars++] = "https_proxy";
            apszEnvVars[cEnvVars++] = "HTTPS_PROXY";
        }
        else if (!RTStrNICmp(pszUrl, RT_STR_TUPLE("ftp:")))
        {
            apszEnvVars[cEnvVars++] = "ftp_proxy";
            apszEnvVars[cEnvVars++] = "FTP_PROXY";
        }
        else
            AssertMsgFailedReturn(("Unknown/unsupported schema in URL: '%s'\n", pszUrl), VERR_NOT_SUPPORTED);
        apszEnvVars[cEnvVars++] = "all_proxy";
        apszEnvVars[cEnvVars++] = "ALL_PROXY";

        /*
         * We try the env vars out and goes with the first one we can make sense out of.
         * If we cannot make sense of any, we return the first unexpected rc we got.
         */
        rc = VINF_SUCCESS;
        for (uint32_t i = 0; i < cEnvVars; i++)
        {
            size_t cchValue;
            int rc2 = RTEnvGetEx(RTENV_DEFAULT, apszEnvVars[i], szTmp, sizeof(szTmp) - sizeof("http://"), &cchValue);
            if (RT_SUCCESS(rc2))
            {
                if (cchValue != 0)
                {
                    /* Add a http:// prefix so RTUriParse groks it (cheaper to do it here). */
                    if (!strstr(szTmp, "://"))
                    {
                        memmove(&szTmp[sizeof("http://") - 1], szTmp, cchValue + 1);
                        memcpy(szTmp, RT_STR_TUPLE("http://"));
                    }

                    rc2 = rtHttpConfigureProxyFromUrl(pThis, szTmp);
                    if (RT_SUCCESS(rc2))
                        rc = rc2;
                }
                /*
                 * The variable is empty.  Guess that means no proxying wanted.
                 */
                else
                {
                    rc = rtHttpUpdateAutomaticProxyDisable(pThis);
                    break;
                }
            }
            else
                AssertMsgStmt(rc2 == VERR_ENV_VAR_NOT_FOUND, ("%Rrc\n", rc2), if (RT_SUCCESS(rc)) rc = rc2);
        }
    }
    /*
     * The host is the no-proxy list, it seems.
     */
    else
        rc = rtHttpUpdateAutomaticProxyDisable(pThis);

    return rc;
}

#ifdef IPRT_USE_LIBPROXY

/**
 * @callback_method_impl{FNRTONCE,
 *      Attempts to load libproxy.so.1 and resolves APIs}
 */
static DECLCALLBACK(int) rtHttpLibProxyResolveImports(void *pvUser)
{
    RTLDRMOD hMod;
    int rc = RTLdrLoadSystem("libproxy.so.1", false /*fNoUnload*/,  &hMod);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(hMod, "px_proxy_factory_new", (void **)&g_pfnLibProxyFactoryCtor);
        if (RT_SUCCESS(rc))
            rc = RTLdrGetSymbol(hMod, "px_proxy_factory_free", (void **)&g_pfnLibProxyFactoryDtor);
        if (RT_SUCCESS(rc))
            rc = RTLdrGetSymbol(hMod, "px_proxy_factory_get_proxies", (void **)&g_pfnLibProxyFactoryGetProxies);
        if (RT_SUCCESS(rc))
            /* libproxy < 0.4.16 does not have this function, so ignore the return code. */
            RTLdrGetSymbol(hMod, "px_proxy_factory_free_proxies", (void **)&g_pfnLibProxyFactoryFreeProxies);
        if (RT_SUCCESS(rc))
        {
            RTMEM_WILL_LEAK(hMod);
            g_hLdrLibProxy = hMod;
        }
        else
            RTLdrClose(hMod);
        AssertRC(rc);
    }

    NOREF(pvUser);
    return rc;
}

/**
 * Reconfigures the cURL proxy settings for the given URL, libproxy style.
 *
 * @returns IPRT status code. VINF_NOT_SUPPORTED if we should try fallback.
 * @param   pThis       The HTTP client instance.
 * @param   pszUrl      The URL.
 */
static int rtHttpLibProxyConfigureProxyForUrl(PRTHTTPINTERNAL pThis, const char *pszUrl)
{
    int rcRet = VINF_NOT_SUPPORTED;

    int rc = RTOnce(&g_LibProxyResolveImportsOnce, rtHttpLibProxyResolveImports, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Instanciate the factory and ask for a list of proxies.
         */
        PLIBPROXYFACTORY pFactory = g_pfnLibProxyFactoryCtor();
        if (pFactory)
        {
            char **papszProxies = g_pfnLibProxyFactoryGetProxies(pFactory, pszUrl);
            if (papszProxies)
            {
                /*
                 * Look for something we can use.
                 */
                for (unsigned i = 0; papszProxies[i]; i++)
                {
                    if (strncmp(papszProxies[i], RT_STR_TUPLE("direct://")) == 0)
                        rcRet = rtHttpUpdateAutomaticProxyDisable(pThis);
                    else if (   strncmp(papszProxies[i], RT_STR_TUPLE("http://")) == 0
                             || strncmp(papszProxies[i], RT_STR_TUPLE("socks5://")) == 0
                             || strncmp(papszProxies[i], RT_STR_TUPLE("socks4://")) == 0
                             || strncmp(papszProxies[i], RT_STR_TUPLE("socks://")) == 0 /** @todo same problem as on OS X. */
                            )
                        rcRet = rtHttpConfigureProxyFromUrl(pThis, papszProxies[i]);
                    else
                        continue;
                    if (rcRet != VINF_NOT_SUPPORTED)
                        break;
                }

                /* Free the result. */
                if (g_pfnLibProxyFactoryFreeProxies) /* libproxy >= 0.4.16. */
                    g_pfnLibProxyFactoryFreeProxies(papszProxies);
                else
                {
                    for (unsigned i = 0; papszProxies[i]; i++)
                        free(papszProxies[i]);
                    free(papszProxies);
                }
                papszProxies = NULL;
            }
            g_pfnLibProxyFactoryDtor(pFactory);
        }
    }

    return rcRet;
}

#endif /* IPRT_USE_LIBPROXY */

#ifdef RT_OS_DARWIN

/**
 * Get a boolean like integer value from a dictionary.
 *
 * @returns true / false.
 * @param   hDict       The dictionary.
 * @param   pvKey       The dictionary value key.
 */
static bool rtHttpDarwinGetBooleanFromDict(CFDictionaryRef hDict, void const *pvKey, bool fDefault)
{
    CFNumberRef hNum = (CFNumberRef)CFDictionaryGetValue(hDict, pvKey);
    if (hNum)
    {
        int fEnabled;
        if (!CFNumberGetValue(hNum, kCFNumberIntType, &fEnabled))
            return fDefault;
        return fEnabled != 0;
    }
    return fDefault;
}


/**
 * Creates a CFURL object for an URL.
 *
 * @returns CFURL object reference.
 * @param   pszUrl              The URL.
 */
static CFURLRef rtHttpDarwinUrlToCFURL(const char *pszUrl)
{
    /* CFURLCreateStringByAddingPercentEscapes is deprecated, so try use CFURLCreateWithBytes
       as it doesn't validate as much as as CFUrlCreateWithString does. */
#if 0
    CFURLRef    hUrl = NULL;
    CFStringRef hStrUrl = CFStringCreateWithCString(kCFAllocatorDefault, pszUrl, kCFStringEncodingUTF8);
    if (hStrUrl)
    {
        CFStringRef hStrUrlEscaped = CFURLCreateStringByAddingPercentEscapes(kCFAllocatorDefault, hStrUrl,
                                                                             NULL /*charactersToLeaveUnescaped*/,
                                                                             NULL /*legalURLCharactersToBeEscaped*/,
                                                                             kCFStringEncodingUTF8);
        if (hStrUrlEscaped)
        {
            hUrl = CFURLCreateWithString(kCFAllocatorDefault, hStrUrlEscaped, NULL /*baseURL*/);
            Assert(hUrl);
            CFRelease(hStrUrlEscaped);
        }
        else
            AssertFailed();
        CFRelease(hStrUrl);
    }
    else
        AssertFailed();
#else
    CFURLRef hUrl = CFURLCreateWithBytes(kCFAllocatorDefault, (const uint8_t *)pszUrl, strlen(pszUrl),
                                         kCFStringEncodingUTF8, NULL /*baseURL*/);
    Assert(hUrl);
#endif
    return hUrl;
}


/**
 * For passing results from rtHttpDarwinPacCallback to
 * rtHttpDarwinExecuteProxyAutoConfigurationUrl.
 */
typedef struct RTHTTPDARWINPACRESULT
{
    CFArrayRef  hArrayProxies;
    CFErrorRef  hError;
} RTHTTPDARWINPACRESULT;
typedef RTHTTPDARWINPACRESULT *PRTHTTPDARWINPACRESULT;

/**
 * Stupid callback for getting the result from
 * CFNetworkExecuteProxyAutoConfigurationURL.
 *
 * @param   pvUser          Pointer to a RTHTTPDARWINPACRESULT on the stack of
 *                          rtHttpDarwinExecuteProxyAutoConfigurationUrl.
 * @param   hArrayProxies   The result array.
 * @param   hError          Errors, if any.
 */
static void rtHttpDarwinPacCallback(void *pvUser, CFArrayRef hArrayProxies, CFErrorRef hError)
{
    PRTHTTPDARWINPACRESULT pResult = (PRTHTTPDARWINPACRESULT)pvUser;

    Assert(pResult->hArrayProxies == NULL);
    if (hArrayProxies)
        pResult->hArrayProxies  = (CFArrayRef)CFRetain(hArrayProxies);

    Assert(pResult->hError == NULL);
    if (hError)
        pResult->hError         = (CFErrorRef)CFRetain(hError);

    CFRunLoopStop(CFRunLoopGetCurrent());
}


/**
 * Executes a PAC script and returning the proxies it suggests.
 *
 * @returns Array of proxy configs (CFProxySupport.h style).
 * @param   hUrlTarget      The URL we're about to use.
 * @param   hUrlScript      The PAC script URL.
 */
static CFArrayRef rtHttpDarwinExecuteProxyAutoConfigurationUrl(CFURLRef hUrlTarget, CFURLRef hUrlScript)
{
    char szTmp[256];
    if (LogIsFlowEnabled())
    {
        szTmp[0] = '\0';
        CFStringGetCString(CFURLGetString(hUrlScript), szTmp, sizeof(szTmp), kCFStringEncodingUTF8);
        LogFlow(("rtHttpDarwinExecuteProxyAutoConfigurationUrl: hUrlScript=%p:%s\n", hUrlScript, szTmp));
    }

    /*
     * Use CFNetworkExecuteProxyAutoConfigurationURL here so we don't have to
     * download the script ourselves and mess around with too many CF APIs.
     */
    CFRunLoopRef hRunLoop = CFRunLoopGetCurrent();
    AssertReturn(hRunLoop, NULL);

    RTHTTPDARWINPACRESULT Result = { NULL, NULL };
    CFStreamClientContext Ctx    = { 0, &Result, NULL, NULL, NULL };
    CFRunLoopSourceRef hRunLoopSrc = CFNetworkExecuteProxyAutoConfigurationURL(hUrlScript, hUrlTarget,
                                                                               rtHttpDarwinPacCallback, &Ctx);
    AssertReturn(hRunLoopSrc, NULL);

    CFStringRef kMode = CFSTR("com.apple.dts.CFProxySupportTool");
    CFRunLoopAddSource(hRunLoop, hRunLoopSrc, kMode);
    CFRunLoopRunInMode(kMode, 1.0e10, false); /* callback will force a return. */
    CFRunLoopRemoveSource(hRunLoop, hRunLoopSrc, kMode);

    /** @todo convert errors, maybe even fail. */

    /*
     * Autoconfig (or missing wpad server) typically results in:
     *      domain:kCFErrorDomainCFNetwork; code=kCFHostErrorUnknown (2).
     *
     * In the autoconfig case, it looks like we're getting two entries, first
     * one that's http://wpad/wpad.dat and a noproxy entry.  So, no reason to
     * be very upset if this fails, just continue trying alternatives.
     */
    if (Result.hError)
    {
        if (LogIsEnabled())
        {
            szTmp[0] = '\0';
            CFStringGetCString(CFErrorCopyDescription(Result.hError), szTmp, sizeof(szTmp), kCFStringEncodingUTF8);
            Log(("rtHttpDarwinExecuteProxyAutoConfigurationUrl: error! code=%ld desc='%s'\n", (long)CFErrorGetCode(Result.hError), szTmp));
        }
        CFRelease(Result.hError);
    }
    return Result.hArrayProxies;
}


/**
 * Attempt to configure the proxy according to @a hDictProxy.
 *
 * @returns IPRT status code. VINF_NOT_SUPPORTED if not able to configure it and
 *          the caller should try out alternative proxy configs and fallbacks.
 * @param   pThis           The HTTP client instance.
 * @param   hDictProxy      The proxy configuration (see CFProxySupport.h).
 * @param   hUrlTarget      The URL we're about to use.
 * @param   fIgnorePacType  Whether to ignore PAC type proxy entries (i.e.
 *                          javascript URL).  This is set when we're processing
 *                          the output from a PAC script.
 */
static int rtHttpDarwinTryConfigProxy(PRTHTTPINTERNAL pThis, CFDictionaryRef hDictProxy, CFURLRef hUrlTarget, bool fIgnorePacType)
{
    CFStringRef hStrProxyType = (CFStringRef)CFDictionaryGetValue(hDictProxy, kCFProxyTypeKey);
    AssertReturn(hStrProxyType, VINF_NOT_SUPPORTED);

    /*
     * No proxy is fairly simple and common.
     */
    if (CFEqual(hStrProxyType, kCFProxyTypeNone))
        return rtHttpUpdateAutomaticProxyDisable(pThis);

    /*
     * PAC URL means recursion, however we only do one level.
     */
    if (CFEqual(hStrProxyType, kCFProxyTypeAutoConfigurationURL))
    {
        AssertReturn(!fIgnorePacType, VINF_NOT_SUPPORTED);

        CFURLRef hUrlScript = (CFURLRef)CFDictionaryGetValue(hDictProxy, kCFProxyAutoConfigurationURLKey);
        AssertReturn(hUrlScript, VINF_NOT_SUPPORTED);

        int rcRet = VINF_NOT_SUPPORTED;
        CFArrayRef hArray = rtHttpDarwinExecuteProxyAutoConfigurationUrl(hUrlTarget, hUrlScript);
        if (hArray)
        {
            rcRet = rtHttpDarwinTryConfigProxies(pThis, hArray, hUrlTarget, true /*fIgnorePacType*/);
            CFRelease(hArray);
        }
        return rcRet;
    }

    /*
     * Determine the proxy type (not entirely sure about type == proxy type and
     * not scheme/protocol)...
     */
    curl_proxytype  enmProxyType      = CURLPROXY_HTTP;
    uint32_t        uDefaultProxyPort = 8080;
    if (   CFEqual(hStrProxyType, kCFProxyTypeHTTP)
        || CFEqual(hStrProxyType, kCFProxyTypeHTTPS))
    {  /* defaults */ }
    else if (CFEqual(hStrProxyType, kCFProxyTypeSOCKS))
    {
        /** @todo All we get from darwin is 'SOCKS', no idea whether it's SOCK4 or
         *        SOCK5 on the other side... Selecting SOCKS5 for now. */
        enmProxyType = CURLPROXY_SOCKS5;
        uDefaultProxyPort = 1080;
    }
    /* Unknown proxy type. */
    else
        return VINF_NOT_SUPPORTED;

    /*
     * Extract the proxy configuration.
     */
    /* The proxy host name. */
    char szHostname[_1K];
    CFStringRef hStr = (CFStringRef)CFDictionaryGetValue(hDictProxy, kCFProxyHostNameKey);
    AssertReturn(hStr, VINF_NOT_SUPPORTED);
    AssertReturn(CFStringGetCString(hStr, szHostname, sizeof(szHostname), kCFStringEncodingUTF8), VINF_NOT_SUPPORTED);

    /* Get the port number (optional). */
    SInt32      iProxyPort;
    CFNumberRef hNum = (CFNumberRef)CFDictionaryGetValue(hDictProxy, kCFProxyPortNumberKey);
    if (hNum && CFNumberGetValue(hNum, kCFNumberSInt32Type, &iProxyPort))
        AssertMsgStmt(iProxyPort > 0 && iProxyPort < _64K, ("%d\n", iProxyPort), iProxyPort = uDefaultProxyPort);
    else
        iProxyPort = uDefaultProxyPort;

    /* The proxy username. */
    char szUsername[256];
    hStr = (CFStringRef)CFDictionaryGetValue(hDictProxy, kCFProxyUsernameKey);
    if (hStr)
        AssertReturn(CFStringGetCString(hStr, szUsername, sizeof(szUsername), kCFStringEncodingUTF8), VINF_NOT_SUPPORTED);
    else
        szUsername[0] = '\0';

    /* The proxy password. */
    char szPassword[384];
    hStr = (CFStringRef)CFDictionaryGetValue(hDictProxy, kCFProxyPasswordKey);
    if (hStr)
        AssertReturn(CFStringGetCString(hStr, szPassword, sizeof(szPassword), kCFStringEncodingUTF8), VINF_NOT_SUPPORTED);
    else
        szPassword[0] = '\0';

    /*
     * Apply the proxy config.
     */
    return rtHttpUpdateProxyConfig(pThis, enmProxyType, szHostname, iProxyPort,
                                   szUsername[0] ? szUsername : NULL, szPassword[0] ? szPassword : NULL);
}


/**
 * Try do proxy config for our HTTP client instance given an array of proxies.
 *
 * This is used with the output from a CFProxySupport.h API.
 *
 * @returns IPRT status code. VINF_NOT_SUPPORTED if not able to configure it and
 *          we might want to try out fallbacks.
 * @param   pThis           The HTTP client instance.
 * @param   hArrayProxies   The proxies CFPRoxySupport have given us.
 * @param   hUrlTarget      The URL we're about to use.
 * @param   fIgnorePacType  Whether to ignore PAC type proxy entries (i.e.
 *                          javascript URL).  This is set when we're processing
 *                          the output from a PAC script.
 */
static int rtHttpDarwinTryConfigProxies(PRTHTTPINTERNAL pThis, CFArrayRef hArrayProxies, CFURLRef hUrlTarget, bool fIgnorePacType)
{
    int rcRet = VINF_NOT_SUPPORTED;
    CFIndex const cEntries = CFArrayGetCount(hArrayProxies);
    LogFlow(("rtHttpDarwinTryConfigProxies: cEntries=%d\n", cEntries));
    for (CFIndex i = 0; i < cEntries; i++)
    {
        CFDictionaryRef hDictProxy = (CFDictionaryRef)CFArrayGetValueAtIndex(hArrayProxies, i);
        AssertContinue(hDictProxy);

        rcRet = rtHttpDarwinTryConfigProxy(pThis, hDictProxy, hUrlTarget, fIgnorePacType);
        if (rcRet != VINF_NOT_SUPPORTED)
            break;
    }
    return rcRet;
}


/**
 * Inner worker for rtHttpWinConfigureProxyForUrl.
 *
 * @returns IPRT status code. VINF_NOT_SUPPORTED if we should try fallback.
 * @param   pThis       The HTTP client instance.
 * @param   pszUrl      The URL.
 */
static int rtHttpDarwinConfigureProxyForUrlWorker(PRTHTTPINTERNAL pThis, CFDictionaryRef hDictProxies,
                                                  const char *pszUrl, const char *pszHost)
{
    CFArrayRef  hArray;

    /*
     * From what I can tell, the CFNetworkCopyProxiesForURL API doesn't apply
     * proxy exclusion rules (tested on 10.9).  So, do that manually.
     */
    RTNETADDRU  HostAddr;
    int         fIsHostIpv4Address = -1;
    char        szTmp[_4K];

    /* If we've got a simple hostname, something containing no dots, we must check
       whether such simple hostnames are excluded from proxying by default or not. */
    if (strchr(pszHost, '.') == NULL)
    {
        if (rtHttpDarwinGetBooleanFromDict(hDictProxies, kSCPropNetProxiesExcludeSimpleHostnames, false))
            return rtHttpUpdateAutomaticProxyDisable(pThis);
        fIsHostIpv4Address = false;
    }

    /* Consult the exclusion list.  This is an array of strings.
       This is very similar to what we do on windows. */
    hArray = (CFArrayRef)CFDictionaryGetValue(hDictProxies, kSCPropNetProxiesExceptionsList);
    if (hArray)
    {
        CFIndex const cEntries = CFArrayGetCount(hArray);
        for (CFIndex i = 0; i < cEntries; i++)
        {
            CFStringRef hStr = (CFStringRef)CFArrayGetValueAtIndex(hArray, i);
            AssertContinue(hStr);
            AssertContinue(CFStringGetCString(hStr, szTmp, sizeof(szTmp), kCFStringEncodingUTF8));
            RTStrToLower(szTmp);

            bool fRet;
            if (   strchr(szTmp, '*')
                || strchr(szTmp, '?'))
                fRet = RTStrSimplePatternMatch(szTmp, pszHost);
            else
            {
                if (fIsHostIpv4Address == -1)
                    fIsHostIpv4Address = RT_SUCCESS(RTNetStrToIPv4Addr(pszHost, &HostAddr.IPv4));
                RTNETADDRIPV4 Network, Netmask;
                if (   fIsHostIpv4Address
                    && RT_SUCCESS(RTCidrStrToIPv4(szTmp, &Network, &Netmask)) )
                    fRet = (HostAddr.IPv4.u & Netmask.u) == Network.u;
                else
                    fRet = strcmp(szTmp, pszHost) == 0;
            }
            if (fRet)
                return rtHttpUpdateAutomaticProxyDisable(pThis);
        }
    }

#if 0 /* The start of a manual alternative to CFNetworkCopyProxiesForURL below, hopefully we won't need this. */
    /*
     * Is proxy auto config (PAC) enabled?  If so, we must consult it first.
     */
    if (rtHttpDarwinGetBooleanFromDict(hDictProxies, kSCPropNetProxiesProxyAutoConfigEnable, false))
    {
        /* Convert the auto config url string to a CFURL object. */
        CFStringRef hStrAutoConfigUrl = (CFStringRef)CFDictionaryGetValue(hDictProxies, kSCPropNetProxiesProxyAutoConfigURLString);
        if (hStrAutoConfigUrl)
        {
            if (CFStringGetCString(hStrAutoConfigUrl, szTmp, sizeof(szTmp), kCFStringEncodingUTF8))
            {
                CFURLRef hUrlScript = rtHttpDarwinUrlToCFURL(szTmp);
                if (hUrlScript)
                {
                    int      rcRet      = VINF_NOT_SUPPORTED;
                    CFURLRef hUrlTarget = rtHttpDarwinUrlToCFURL(pszUrl);
                    if (hUrlTarget)
                    {
                        /* Work around for <rdar://problem/5530166>, whatever that is.  Initializes
                           some internal CFNetwork state, they say.  See CFPRoxySupportTool example. */
                        hArray = CFNetworkCopyProxiesForURL(hUrlTarget, NULL);
                        if (hArray)
                            CFRelease(hArray);

                        hArray = rtHttpDarwinExecuteProxyAutoConfigurationUrl(hUrlTarget, hUrlScript);
                        if (hArray)
                        {
                            rcRet = rtHttpDarwinTryConfigProxies(pThis, hArray, hUrlTarget, true /*fIgnorePacType*/);
                            CFRelease(hArray);
                        }
                    }
                    CFRelease(hUrlScript);
                    if (rcRet != VINF_NOT_SUPPORTED)
                        return rcRet;
                }
            }
        }
    }

    /*
     * Try static proxy configs.
     */
    /** @todo later if needed. */
    return VERR_NOT_SUPPORTED;

#else
    /*
     * Simple solution - "just" use CFNetworkCopyProxiesForURL.
     */
    CFURLRef hUrlTarget = rtHttpDarwinUrlToCFURL(pszUrl);
    AssertReturn(hUrlTarget, VERR_INTERNAL_ERROR);
    int rcRet = VINF_NOT_SUPPORTED;

    /* Work around for <rdar://problem/5530166>, whatever that is.  Initializes
       some internal CFNetwork state, they say.  See CFPRoxySupportTool example. */
    CFDictionaryRef hDictNull = (CFDictionaryRef)(42-42); /*workaround for -Wnonnull warning in Clang 11. */
    hArray = CFNetworkCopyProxiesForURL(hUrlTarget, hDictNull);
    if (hArray)
        CFRelease(hArray);

    /* The actual run. */
    hArray = CFNetworkCopyProxiesForURL(hUrlTarget, hDictProxies);
    if (hArray)
    {
        rcRet = rtHttpDarwinTryConfigProxies(pThis, hArray, hUrlTarget, false /*fIgnorePacType*/);
        CFRelease(hArray);
    }
    CFRelease(hUrlTarget);

    return rcRet;
#endif
}

/**
 * Reconfigures the cURL proxy settings for the given URL, OS X style.
 *
 * @returns IPRT status code. VINF_NOT_SUPPORTED if we should try fallback.
 * @param   pThis       The HTTP client instance.
 * @param   pszUrl      The URL.
 */
static int rtHttpDarwinConfigureProxyForUrl(PRTHTTPINTERNAL pThis, const char *pszUrl)
{
    /*
     * Parse the URL, if there isn't any host name (like for file:///xxx.txt)
     * we don't need to run thru proxy settings to know what to do.
     */
    RTURIPARSED Parsed;
    int rc = RTUriParse(pszUrl, &Parsed);
    AssertRCReturn(rc, false);
    if (Parsed.cchAuthorityHost == 0)
        return rtHttpUpdateAutomaticProxyDisable(pThis);
    char *pszHost = RTUriParsedAuthorityHost(pszUrl, &Parsed);
    AssertReturn(pszHost, VERR_NO_STR_MEMORY);
    RTStrToLower(pszHost);

    /*
     * Get a copy of the proxy settings (10.6 API).
     */
    CFDictionaryRef hDictProxies = CFNetworkCopySystemProxySettings(); /* Alt for 10.5: SCDynamicStoreCopyProxies(NULL); */
    if (hDictProxies)
        rc = rtHttpDarwinConfigureProxyForUrlWorker(pThis, hDictProxies, pszUrl, pszHost);
    else
        rc = VINF_NOT_SUPPORTED;
    CFRelease(hDictProxies);

    RTStrFree(pszHost);
    return rc;
}

#endif /* RT_OS_DARWIN */

#ifdef RT_OS_WINDOWS

/**
 * @callback_method_impl{FNRTONCE, Loads WinHttp.dll and resolves APIs}
 */
static DECLCALLBACK(int) rtHttpWinResolveImports(void *pvUser)
{
    /*
     * winhttp.dll is not present on NT4 and probably was first introduced with XP.
     */
    RTLDRMOD hMod;
    int rc = RTLdrLoadSystem("winhttp.dll", true /*fNoUnload*/, &hMod);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(hMod, "WinHttpOpen", (void **)&g_pfnWinHttpOpen);
        if (RT_SUCCESS(rc))
            rc = RTLdrGetSymbol(hMod, "WinHttpCloseHandle", (void **)&g_pfnWinHttpCloseHandle);
        if (RT_SUCCESS(rc))
            rc = RTLdrGetSymbol(hMod, "WinHttpGetProxyForUrl", (void **)&g_pfnWinHttpGetProxyForUrl);
        if (RT_SUCCESS(rc))
            rc = RTLdrGetSymbol(hMod, "WinHttpGetDefaultProxyConfiguration", (void **)&g_pfnWinHttpGetDefaultProxyConfiguration);
        if (RT_SUCCESS(rc))
            rc = RTLdrGetSymbol(hMod, "WinHttpGetIEProxyConfigForCurrentUser", (void **)&g_pfnWinHttpGetIEProxyConfigForCurrentUser);
        RTLdrClose(hMod);
        AssertRC(rc);
    }
    else
        AssertMsg(g_enmWinVer < kRTWinOSType_XP, ("%Rrc\n", rc));

    NOREF(pvUser);
    return rc;
}


/**
 * Matches the URL against the given Windows by-pass list.
 *
 * @returns true if we should by-pass the proxy for this URL, false if not.
 * @param   pszUrl              The URL.
 * @param   pwszBypass          The Windows by-pass list.
 */
static bool rtHttpWinIsUrlInBypassList(const char *pszUrl, PCRTUTF16 pwszBypass)
{
    /*
     * Don't bother parsing the URL if we've actually got nothing to work with
     * in the by-pass list.
     */
    if (!pwszBypass)
        return false;

    RTUTF16 wc;
    while (   (wc = *pwszBypass) != '\0'
           && (   RTUniCpIsSpace(wc)
               || wc == ';') )
        pwszBypass++;
    if (wc == '\0')
        return false;

    /*
     * We now need to parse the URL and extract the host name.
     */
    RTURIPARSED Parsed;
    int rc = RTUriParse(pszUrl, &Parsed);
    AssertRCReturn(rc, false);
    char *pszHost = RTUriParsedAuthorityHost(pszUrl, &Parsed);
    if (!pszHost) /* Don't assert, in case of file:///xxx or similar blunder. */
        return false;
    RTStrToLower(pszHost);

    bool  fRet = false;
    char *pszBypassFree;
    rc = RTUtf16ToUtf8(pwszBypass, &pszBypassFree);
    if (RT_SUCCESS(rc))
    {
        /*
         * Walk the by-pass list.
         *
         * According to https://msdn.microsoft.com/en-us/library/aa384098(v=vs.85).aspx
         * a by-pass list is semicolon delimited list.  The entries are either host
         * names or IP addresses, and may use wildcard ('*', '?', I guess).  There
         * special "<local>" entry matches anything without a dot.
         */
        RTNETADDRU  HostAddr = { 0, 0 };
        int         fIsHostIpv4Address = -1;
        char *pszEntry = pszBypassFree;
        while (*pszEntry != '\0')
        {
            /*
             * Find end of entry.
             */
            char   ch;
            size_t cchEntry = 1;
            while (   (ch = pszEntry[cchEntry]) != '\0'
                   && ch != ';'
                   && !RT_C_IS_SPACE(ch))
                cchEntry++;

            char chSaved = pszEntry[cchEntry];
            pszEntry[cchEntry] = '\0';
            RTStrToLower(pszEntry);

            if (   cchEntry == sizeof("<local>")  - 1
                && memcmp(pszEntry, RT_STR_TUPLE("<local>")) == 0)
                fRet = strchr(pszHost, '.') == NULL;
            else if (   memchr(pszEntry, '*', cchEntry) != NULL
                     || memchr(pszEntry, '?', cchEntry) != NULL)
                fRet = RTStrSimplePatternMatch(pszEntry, pszHost);
            else
            {
                if (fIsHostIpv4Address == -1)
                    fIsHostIpv4Address = RT_SUCCESS(RTNetStrToIPv4Addr(pszHost, &HostAddr.IPv4));
                RTNETADDRIPV4 Network, Netmask;
                if (   fIsHostIpv4Address
                    && RT_SUCCESS(RTCidrStrToIPv4(pszEntry, &Network, &Netmask)) )
                    fRet = (HostAddr.IPv4.u & Netmask.u) == Network.u;
                else
                    fRet = strcmp(pszEntry, pszHost) == 0;
            }

            pszEntry[cchEntry] = chSaved;
            if (fRet)
                break;

            /*
             * Next entry.
             */
            pszEntry += cchEntry;
            while (   (ch = *pszEntry) != '\0'
                   && (   ch == ';'
                       || RT_C_IS_SPACE(ch)) )
                pszEntry++;
        }

        RTStrFree(pszBypassFree);
    }

    RTStrFree(pszHost);
    return false;
}


/**
 * Searches a Windows proxy server list for the best fitting proxy to use, then
 * reconfigures the HTTP client instance to use it.
 *
 * @returns IPRT status code, VINF_NOT_SUPPORTED if we need to consult fallback.
 * @param   pThis               The HTTP client instance.
 * @param   pszUrl              The URL needing proxying.
 * @param   pwszProxies         The list of proxy servers to choose from.
 */
static int rtHttpWinSelectProxyFromList(PRTHTTPINTERNAL pThis, const char *pszUrl, PCRTUTF16 pwszProxies)
{
    /*
     * Fend off empty strings (very unlikely, but just in case).
     */
    if (!pwszProxies)
        return VINF_NOT_SUPPORTED;

    RTUTF16 wc;
    while (   (wc = *pwszProxies) != '\0'
           && (   RTUniCpIsSpace(wc)
               || wc == ';') )
        pwszProxies++;
    if (wc == '\0')
        return VINF_NOT_SUPPORTED;

    /*
     * We now need to parse the URL and extract the scheme.
     */
    RTURIPARSED Parsed;
    int rc = RTUriParse(pszUrl, &Parsed);
    AssertRCReturn(rc, false);
    char *pszUrlScheme = RTUriParsedScheme(pszUrl, &Parsed);
    AssertReturn(pszUrlScheme, VERR_NO_STR_MEMORY);
    size_t const cchUrlScheme = strlen(pszUrlScheme);

    int rcRet = VINF_NOT_SUPPORTED;
    char *pszProxiesFree;
    rc = RTUtf16ToUtf8(pwszProxies, &pszProxiesFree);
    if (RT_SUCCESS(rc))
    {
        /*
         * Walk the server list.
         *
         * According to https://msdn.microsoft.com/en-us/library/aa383912(v=vs.85).aspx
         * this is also a semicolon delimited list.  The entries are on the form:
         *      [<scheme>=][<scheme>"://"]<server>[":"<port>]
         */
        bool        fBestEntryHasSameScheme = false;
        const char *pszBestEntry = NULL;
        char       *pszEntry = pszProxiesFree;
        while (*pszEntry != '\0')
        {
            /*
             * Find end of entry.  We include spaces here in addition to ';'.
             */
            char   ch;
            size_t cchEntry = 1;
            while (   (ch = pszEntry[cchEntry]) != '\0'
                   && ch != ';'
                   && !RT_C_IS_SPACE(ch))
                cchEntry++;

            char const chSaved = pszEntry[cchEntry];
            pszEntry[cchEntry] = '\0';

            /* Parse the entry. */
            const char *pszEndOfScheme = strstr(pszEntry, "://");
            const char *pszEqual       = (const char *)memchr(pszEntry, '=',
                                                              pszEndOfScheme ? pszEndOfScheme - pszEntry : cchEntry);
            if (pszEqual)
            {
                if (   (uintptr_t)(pszEqual - pszEntry) == cchUrlScheme
                    && RTStrNICmp(pszEntry, pszUrlScheme, cchUrlScheme) == 0)
                {
                    pszBestEntry = pszEqual + 1;
                    break;
                }
            }
            else
            {
                bool fSchemeMatch = pszEndOfScheme
                                 && (uintptr_t)(pszEndOfScheme - pszEntry) == cchUrlScheme
                                 && RTStrNICmp(pszEntry, pszUrlScheme, cchUrlScheme) == 0;
                if (   !pszBestEntry
                    || (   !fBestEntryHasSameScheme
                        && fSchemeMatch) )
                {
                    pszBestEntry = pszEntry;
                    fBestEntryHasSameScheme = fSchemeMatch;
                }
            }

            /*
             * Next entry.
             */
            if (!chSaved)
                break;
            pszEntry += cchEntry + 1;
            while (   (ch = *pszEntry) != '\0'
                   && (   ch == ';'
                       || RT_C_IS_SPACE(ch)) )
                pszEntry++;
        }

        /*
         * If we found something, try use it.
         */
        if (pszBestEntry)
            rcRet = rtHttpConfigureProxyFromUrl(pThis, pszBestEntry);

        RTStrFree(pszProxiesFree);
    }

    RTStrFree(pszUrlScheme);
    return rc;
}


/**
 * Reconfigures the cURL proxy settings for the given URL, Windows style.
 *
 * @returns IPRT status code. VINF_NOT_SUPPORTED if we should try fallback.
 * @param   pThis       The HTTP client instance.
 * @param   pszUrl      The URL.
 */
static int rtHttpWinConfigureProxyForUrl(PRTHTTPINTERNAL pThis, const char *pszUrl)
{
    int rcRet = VINF_NOT_SUPPORTED;

    int rc = RTOnce(&g_WinResolveImportsOnce, rtHttpWinResolveImports, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Try get some proxy info for the URL.  We first try getting the IE
         * config and seeing if we can use WinHttpGetIEProxyConfigForCurrentUser
         * in some way, if we can we prepare ProxyOptions with a non-zero dwFlags.
         */
        WINHTTP_PROXY_INFO          ProxyInfo;
        WINHTTP_AUTOPROXY_OPTIONS   AutoProxyOptions;
        RT_ZERO(AutoProxyOptions);
        RT_ZERO(ProxyInfo);

        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG IeProxyConfig;
        if (g_pfnWinHttpGetIEProxyConfigForCurrentUser(&IeProxyConfig))
        {
            AutoProxyOptions.fAutoLogonIfChallenged = FALSE;
            AutoProxyOptions.lpszAutoConfigUrl      = IeProxyConfig.lpszAutoConfigUrl;
            if (IeProxyConfig.fAutoDetect)
            {
                AutoProxyOptions.dwFlags            = WINHTTP_AUTOPROXY_AUTO_DETECT | WINHTTP_AUTOPROXY_RUN_INPROCESS;
                AutoProxyOptions.dwAutoDetectFlags  = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
            }
            else if (AutoProxyOptions.lpszAutoConfigUrl)
                AutoProxyOptions.dwFlags            = WINHTTP_AUTOPROXY_CONFIG_URL;
            else if (ProxyInfo.lpszProxy)
                ProxyInfo.dwAccessType              = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            ProxyInfo.lpszProxy       = IeProxyConfig.lpszProxy;
            ProxyInfo.lpszProxyBypass = IeProxyConfig.lpszProxyBypass;
        }
        else
        {
            AssertMsgFailed(("WinHttpGetIEProxyConfigForCurrentUser -> %u\n", GetLastError()));
            if (!g_pfnWinHttpGetDefaultProxyConfiguration(&ProxyInfo))
            {
                AssertMsgFailed(("WinHttpGetDefaultProxyConfiguration -> %u\n", GetLastError()));
                RT_ZERO(ProxyInfo);
            }
        }

        /*
         * Should we try WinHttGetProxyForUrl?
         */
        if (AutoProxyOptions.dwFlags != 0)
        {
            HINTERNET hSession = g_pfnWinHttpOpen(NULL /*pwszUserAgent*/, WINHTTP_ACCESS_TYPE_NO_PROXY,
                                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 /*dwFlags*/ );
            if (hSession != NULL)
            {
                PRTUTF16 pwszUrl;
                rc = RTStrToUtf16(pszUrl, &pwszUrl);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Try autodetect first, then fall back on the config URL if there is one.
                     *
                     * Also, we first try without auto authentication, then with.  This will according
                     * to http://msdn.microsoft.com/en-us/library/aa383153%28v=VS.85%29.aspx help with
                     * caching the result when it's processed out-of-process (seems default here on W10).
                     */
                    WINHTTP_PROXY_INFO TmpProxyInfo;
                    BOOL fRc = g_pfnWinHttpGetProxyForUrl(hSession, pwszUrl, &AutoProxyOptions, &TmpProxyInfo);
                    if (   !fRc
                        && GetLastError() == ERROR_WINHTTP_LOGIN_FAILURE)
                    {
                        AutoProxyOptions.fAutoLogonIfChallenged = TRUE;
                        fRc = g_pfnWinHttpGetProxyForUrl(hSession, pwszUrl, &AutoProxyOptions, &TmpProxyInfo);
                    }

                    if (   !fRc
                        && AutoProxyOptions.dwFlags != WINHTTP_AUTOPROXY_CONFIG_URL
                        && AutoProxyOptions.lpszAutoConfigUrl)
                    {
                        AutoProxyOptions.fAutoLogonIfChallenged = FALSE;
                        AutoProxyOptions.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
                        AutoProxyOptions.dwAutoDetectFlags = 0;
                        fRc = g_pfnWinHttpGetProxyForUrl(hSession, pwszUrl, &AutoProxyOptions, &TmpProxyInfo);
                        if (   !fRc
                            && GetLastError() == ERROR_WINHTTP_LOGIN_FAILURE)
                        {
                            AutoProxyOptions.fAutoLogonIfChallenged = TRUE;
                            fRc = g_pfnWinHttpGetProxyForUrl(hSession, pwszUrl, &AutoProxyOptions, &TmpProxyInfo);
                        }
                    }

                    if (fRc)
                    {
                        if (ProxyInfo.lpszProxy)
                            GlobalFree(ProxyInfo.lpszProxy);
                        if (ProxyInfo.lpszProxyBypass)
                            GlobalFree(ProxyInfo.lpszProxyBypass);
                        ProxyInfo = TmpProxyInfo;
                    }
                    /*
                     * If the autodetection failed, assume no proxy.
                     */
                    else
                    {
                        DWORD dwErr = GetLastError();
                        if (   dwErr == ERROR_WINHTTP_AUTODETECTION_FAILED
                            || dwErr == ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT
                            || (   dwErr == ERROR_WINHTTP_UNRECOGNIZED_SCHEME
                                && (   RTStrNICmp(pszUrl, RT_STR_TUPLE("https://")) == 0
                                    || RTStrNICmp(pszUrl, RT_STR_TUPLE("http://")) == 0) ) )
                            rcRet = rtHttpUpdateAutomaticProxyDisable(pThis);
                        else
                            AssertMsgFailed(("g_pfnWinHttpGetProxyForUrl(%s) -> %u; lpszAutoConfigUrl=%sx\n",
                                             pszUrl, dwErr, AutoProxyOptions.lpszAutoConfigUrl));
                    }
                    RTUtf16Free(pwszUrl);
                }
                else
                {
                    AssertMsgFailed(("RTStrToUtf16(%s,) -> %Rrc\n", pszUrl, rc));
                    rcRet = rc;
                }
                 g_pfnWinHttpCloseHandle(hSession);
            }
            else
                AssertMsgFailed(("g_pfnWinHttpOpen -> %u\n", GetLastError()));
        }

        /*
         * Try use the proxy info we've found.
         */
        switch (ProxyInfo.dwAccessType)
        {
            case WINHTTP_ACCESS_TYPE_NO_PROXY:
                rcRet = rtHttpUpdateAutomaticProxyDisable(pThis);
                break;

            case WINHTTP_ACCESS_TYPE_NAMED_PROXY:
                if (!rtHttpWinIsUrlInBypassList(pszUrl, ProxyInfo.lpszProxyBypass))
                    rcRet = rtHttpWinSelectProxyFromList(pThis, pszUrl, ProxyInfo.lpszProxy);
                else
                    rcRet = rtHttpUpdateAutomaticProxyDisable(pThis);
                break;

            case 0:
                break;

            default:
                AssertMsgFailed(("%#x\n", ProxyInfo.dwAccessType));
        }

        /*
         * Cleanup.
         */
        if (ProxyInfo.lpszProxy)
            GlobalFree(ProxyInfo.lpszProxy);
        if (ProxyInfo.lpszProxyBypass)
            GlobalFree(ProxyInfo.lpszProxyBypass);
        if (AutoProxyOptions.lpszAutoConfigUrl)
            GlobalFree((PRTUTF16)AutoProxyOptions.lpszAutoConfigUrl);
    }

    return rcRet;
}

#endif /* RT_OS_WINDOWS */


static int rtHttpConfigureProxyForUrl(PRTHTTPINTERNAL pThis, const char *pszUrl)
{
    if (pThis->fUseSystemProxySettings)
    {
#ifdef IPRT_USE_LIBPROXY
        int rc = rtHttpLibProxyConfigureProxyForUrl(pThis, pszUrl);
        if (rc == VINF_SUCCESS || RT_FAILURE(rc))
            return rc;
        Assert(rc == VINF_NOT_SUPPORTED);
#endif
#ifdef RT_OS_DARWIN
        int rc = rtHttpDarwinConfigureProxyForUrl(pThis, pszUrl);
        if (rc == VINF_SUCCESS || RT_FAILURE(rc))
            return rc;
        Assert(rc == VINF_NOT_SUPPORTED);
#endif
#ifdef RT_OS_WINDOWS
        int rc = rtHttpWinConfigureProxyForUrl(pThis, pszUrl);
        if (rc == VINF_SUCCESS || RT_FAILURE(rc))
            return rc;
        Assert(rc == VINF_NOT_SUPPORTED);
#endif
/** @todo system specific class here, fall back on env vars if necessary. */
        return rtHttpConfigureProxyForUrlFromEnv(pThis, pszUrl);
    }

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpSetProxy(RTHTTP hHttp, const char *pcszProxy, uint32_t uPort,
                             const char *pcszProxyUser, const char *pcszProxyPwd)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(!pThis->fBusy, VERR_WRONG_ORDER);

    /*
     * Update the settings.
     *
     * Currently, we don't make alot of effort parsing or checking the input, we
     * leave that to cURL.  (A bit afraid of breaking user settings.)
     */
    pThis->fUseSystemProxySettings = false;
    if (!pcszProxy)
        return rtHttpUpdateAutomaticProxyDisable(pThis);

    return rtHttpUpdateProxyConfig(pThis, CURLPROXY_HTTP, pcszProxy, uPort ? uPort : 1080, pcszProxyUser, pcszProxyPwd);
}



/*********************************************************************************************************************************
*   HTTP Headers                                                                                                                 *
*********************************************************************************************************************************/

/**
 * Helper for RTHttpSetHeaders and RTHttpAddRawHeader that unsets the user agent
 * if it is now in one of the headers.
 */
static int rtHttpUpdateUserAgentHeader(PRTHTTPINTERNAL pThis, PRTHTTPHEADER pNewHdr)
{
    static const char s_szUserAgent[] = "User-Agent";
    if (   pNewHdr->cchName == sizeof(s_szUserAgent) - 1
        && RTStrNICmpAscii(pNewHdr->szData, RT_STR_TUPLE(s_szUserAgent)) == 0)
    {
        pThis->fHaveUserAgentHeader = true;
        if (pThis->fHaveSetUserAgent)
        {
            CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_USERAGENT, (char *)NULL);
            Assert(CURL_SUCCESS(rcCurl)); NOREF(rcCurl);
            pThis->fHaveSetUserAgent = false;
        }
    }
    return VINF_SUCCESS;
}


/**
 * Free the headers associated with the instance (w/o telling cURL about it).
 *
 * @param   pThis       The HTTP client instance.
 */
static void rtHttpFreeHeaders(PRTHTTPINTERNAL pThis)
{
    struct curl_slist *pHead = pThis->pHeaders;
    pThis->pHeaders = NULL;
    pThis->ppHeadersTail = &pThis->pHeaders;
    pThis->fHaveUserAgentHeader = false;

    while (pHead)
    {
        struct curl_slist *pFree = pHead;
        pHead = pHead->next;
        ASMCompilerBarrier(); /* paranoia */

        pFree->next = NULL;
        pFree->data = NULL;
        RTMemFree(pFree);
    }
}


/**
 * Worker for RTHttpSetHeaders and RTHttpAddHeader.
 *
 * @returns IPRT status code.
 * @param   pThis       The HTTP client instance.
 * @param   pchName     The field name.  Does not need to be terminated.
 * @param   cchName     The field name length.
 * @param   pchValue    The field value.  Does not need to be terminated.
 * @param   cchValue    The field value length.
 * @param   fFlags      RTHTTPADDHDR_F_XXX.
 */
static int rtHttpAddHeaderWorker(PRTHTTPINTERNAL pThis, const char *pchName, size_t cchName,
                                 const char *pchValue, size_t cchValue, uint32_t fFlags)
{
    /*
     * Create the list entry.
     */
    size_t        cbData = cchName + 2 + cchValue + 1;
    PRTHTTPHEADER pHdr   = (PRTHTTPHEADER)RTMemAlloc(RT_UOFFSETOF_DYN(RTHTTPHEADER, szData[cbData]));
    if (pHdr)
    {
        pHdr->Core.next = NULL;
        pHdr->Core.data = pHdr->szData;
        pHdr->cchName   = (uint32_t)cchName;
        pHdr->offValue  = (uint32_t)(cchName + 2);
        char *psz = pHdr->szData;
        memcpy(psz, pchName, cchName);
        psz += cchName;
        *psz++ = ':';
        *psz++ = ' ';
        memcpy(psz, pchValue, cchValue);
        psz[cchValue] = '\0';

        /*
         * Appending to an existing list requires no cURL interaction.
         */
        AssertCompile(RTHTTPADDHDR_F_FRONT != 0);
        if (   !(fFlags & RTHTTPADDHDR_F_FRONT)
            && pThis->pHeaders != NULL)
        {
            *pThis->ppHeadersTail = &pHdr->Core;
            pThis->ppHeadersTail  = &pHdr->Core.next;
            return rtHttpUpdateUserAgentHeader(pThis, pHdr);
        }

        /*
         * When prepending or adding the first header we need to inform cURL
         * about the new list head.
         */
        pHdr->Core.next = pThis->pHeaders;
        if (!pThis->pHeaders)
            pThis->ppHeadersTail = &pHdr->Core.next;
        pThis->pHeaders = &pHdr->Core;

        CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HTTPHEADER, pThis->pHeaders);
        if (CURL_SUCCESS(rcCurl))
            return rtHttpUpdateUserAgentHeader(pThis, pHdr);
        return VERR_HTTP_CURL_ERROR;
    }
    return VERR_NO_MEMORY;
}


RTR3DECL(int) RTHttpSetHeaders(RTHTTP hHttp, size_t cHeaders, const char * const *papszHeaders)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    /*
     * Drop old headers and reset state.
     */
    if (pThis->pHeaders)
    {
        rtHttpFreeHeaders(pThis);
        curl_easy_setopt(pThis->pCurl, CURLOPT_HTTPHEADER, (struct curl_slist *)NULL);
    }
    pThis->ppHeadersTail = &pThis->pHeaders;
    pThis->fHaveUserAgentHeader = false;

    /*
     * We're done if no headers specified.
     */
    if (!cHeaders)
        return VINF_SUCCESS;

    /*
     * Add the headers, one by one.
     */
    int rc = VINF_SUCCESS;
    for (size_t i = 0; i < cHeaders; i++)
    {
        const char *pszHeader = papszHeaders[i];
        size_t      cchHeader = strlen(pszHeader);
        size_t      cchName   = (const char *)memchr(pszHeader, ':', cchHeader) - pszHeader;
        AssertBreakStmt(cchName < cchHeader, rc = VERR_INVALID_PARAMETER);
        size_t      offValue  = RT_C_IS_BLANK(pszHeader[cchName + 1]) ? cchName + 2 : cchName + 1;
        rc = rtHttpAddHeaderWorker(pThis, pszHeader, cchName, &pszHeader[offValue], cchHeader - offValue, RTHTTPADDHDR_F_BACK);
        AssertRCBreak(rc);
    }
    if (RT_SUCCESS(rc))
        return rc;
    rtHttpFreeHeaders(pThis);
    curl_easy_setopt(pThis->pCurl, CURLOPT_HTTPHEADER, (struct curl_slist *)NULL);
    return rc;
}


#if 0 /** @todo reimplement RTHttpAddRawHeader if ever actually needed. */
RTR3DECL(int) RTHttpAddRawHeader(RTHTTP hHttp, const char *pszHeader, uint32_t fFlags)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(!(fFlags & ~RTHTTPADDHDR_F_BACK), VERR_INVALID_FLAGS);
/** @todo implement RTHTTPADDHDR_F_FRONT */

    /*
     * Append it to the header list, checking for User-Agent and such.
     */
    struct curl_slist *pHeaders = pThis->pHeaders;
    struct curl_slist *pNewHeaders = curl_slist_append(pHeaders, pszHeader);
    if (pNewHeaders)
        pHeaders = pNewHeaders;
    else
        return VERR_NO_MEMORY;

    if (strncmp(pszHeader, RT_STR_TUPLE("User-Agent:")) == 0)
        pThis->fHaveUserAgentHeader = true;

    /*
     * If this is the first header, we need to tell curl.
     */
    if (!pThis->pHeaders)
    {
        CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HTTPHEADER, pHeaders);
        if (CURL_FAILURE(rcCurl))
        {
            curl_slist_free_all(pHeaders);
            return VERR_INVALID_PARAMETER;
        }
        pThis->pHeaders = pHeaders;
    }
    else
        Assert(pThis->pHeaders == pHeaders);

    rtHttpUpdateUserAgentHeader(pThis);

    return VINF_SUCCESS;
}
#endif


RTR3DECL(int) RTHttpAddHeader(RTHTTP hHttp, const char *pszField, const char *pszValue, size_t cchValue, uint32_t fFlags)
{
    /*
     * Validate input and calc string lengths.
     */
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(!(fFlags & ~RTHTTPADDHDR_F_BACK), VERR_INVALID_FLAGS);
    AssertPtr(pszField);
    size_t const cchField = strlen(pszField);
    AssertReturn(cchField > 0, VERR_INVALID_PARAMETER);
    AssertReturn(pszField[cchField - 1] != ':', VERR_INVALID_PARAMETER);
    AssertReturn(!RT_C_IS_SPACE(pszField[cchField - 1]), VERR_INVALID_PARAMETER);
#ifdef RT_STRICT
    for (size_t i = 0; i < cchField; i++)
    {
        char const ch = pszField[i];
        Assert(RT_C_IS_PRINT(ch) && ch != ':');
    }
#endif

    AssertPtr(pszValue);
    if (cchValue == RTSTR_MAX)
        cchValue = strlen(pszValue);

    /*
     * Just pass it along to the worker.
     */
    return rtHttpAddHeaderWorker(pThis, pszField, cchField, pszValue, cchValue, fFlags);
}


RTR3DECL(const char *) RTHttpGetHeader(RTHTTP hHttp, const char *pszField, size_t cchField)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN_RC(pThis, NULL);

    PRTHTTPHEADER pCur = (PRTHTTPHEADER)pThis->pHeaders;
    if (pCur)
    {
        if (cchField == RTSTR_MAX)
            cchField = strlen(pszField);
        do
        {
            if (   pCur->cchName == cchField
                && RTStrNICmpAscii(pCur->szData, pszField, cchField) == 0)
                return &pCur->szData[pCur->offValue];

            /* next field. */
            pCur = (PRTHTTPHEADER)pCur->Core.next;
        } while (pCur);
    }
    return NULL;
}


RTR3DECL(size_t) RTHttpGetHeaderCount(RTHTTP hHttp)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN_RC(pThis, 0);

    /* Note! Only for test cases and debugging, so we don't care about performance. */
    size_t cHeaders = 0;
    for (PRTHTTPHEADER pCur = (PRTHTTPHEADER)pThis->pHeaders; pCur != NULL; pCur = (PRTHTTPHEADER)pCur->Core.next)
        cHeaders++;
    return cHeaders;
}


RTR3DECL(const char *) RTHttpGetByOrdinal(RTHTTP hHttp, size_t iOrdinal)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN_RC(pThis, NULL);

    /* Note! Only for test cases and debugging, so we don't care about performance. */
    for (PRTHTTPHEADER pCur = (PRTHTTPHEADER)pThis->pHeaders; pCur != NULL; pCur = (PRTHTTPHEADER)pCur->Core.next)
    {
        if (iOrdinal == 0)
            return pCur->szData;
        iOrdinal--;
    }

    return NULL;
}



RTR3DECL(int) RTHttpSignHeaders(RTHTTP hHttp, RTHTTPMETHOD enmMethod, const char *pszUrl,
                                RTCRKEY hKey, const char *pszKeyId, uint32_t fFlags)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(enmMethod > RTHTTPMETHOD_INVALID && enmMethod < RTHTTPMETHOD_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszUrl, VERR_INVALID_POINTER);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);
    AssertPtrReturn(pszKeyId, VERR_INVALID_POINTER);

    /*
     * Do a little bit of preprocessing while we can easily return without
     * needing clean anything up..
     */
    RTURIPARSED ParsedUrl;
    int rc = RTUriParse(pszUrl, &ParsedUrl);
    AssertRCReturn(rc, rc);
    const char * const pszPath = pszUrl + ParsedUrl.offPath;

    const char *pszMethodSp = NULL;
    switch (enmMethod)
    {
        case RTHTTPMETHOD_GET:      pszMethodSp = "get "; break;
        case RTHTTPMETHOD_PUT:      pszMethodSp = "put "; break;
        case RTHTTPMETHOD_POST:     pszMethodSp = "post "; break;
        case RTHTTPMETHOD_PATCH:    pszMethodSp = "patch "; break;
        case RTHTTPMETHOD_DELETE:   pszMethodSp = "delete "; break;
        case RTHTTPMETHOD_HEAD:     pszMethodSp = "head "; break;
        case RTHTTPMETHOD_OPTIONS:  pszMethodSp = "options "; break;
        case RTHTTPMETHOD_TRACE:    pszMethodSp = "trace "; break;
#ifdef IPRT_HTTP_WITH_WEBDAV
        case RTHTTPMETHOD_PROPFIND: pszMethodSp = "propfind "; break;
#endif
        /* no default! */
        case RTHTTPMETHOD_INVALID:
        case RTHTTPMETHOD_END:
        case RTHTTPMETHOD_32BIT_HACK:
            break;
    }
    AssertReturn(pszMethodSp, VERR_INTERNAL_ERROR_4);

    /*
     * We work the authorization header entry directly here to avoid extra copying and stuff.
     */

    /* Estimate required string length first. */
    static const char s_szSuffixFmt[]    = "Authorization: Signature version=\"1\",keyId=\"%s\",algorithm=\"rsa-sha256\",headers=\"";
    static const char s_szInfix[]        = "\",signature=\"";
    static const char s_szPostfix[]      = "\"";
    static const char s_szRequestField[] = "(request-target)";
    size_t const      cchKeyId           = strlen(pszKeyId);
    size_t const      cbSigRaw           = (RTCrKeyGetBitCount(hKey) + 7) / 8;
    size_t const      cbSigRawAligned    = RT_ALIGN_Z(cbSigRaw, 8);
    size_t const      cchSigStr          = RTBase64EncodedLengthEx(cbSigRaw, RTBASE64_FLAGS_NO_LINE_BREAKS);
    size_t cbEstimated = sizeof(s_szSuffixFmt) + sizeof(s_szInfix) + sizeof(s_szPostfix)
                       + cchKeyId + sizeof(s_szRequestField) + cchSigStr;
    for (PRTHTTPHEADER pCur = (PRTHTTPHEADER)pThis->pHeaders; pCur; pCur = (PRTHTTPHEADER)pCur->Core.next)
        cbEstimated += pCur->cchName + 1;
    cbEstimated += 32; /* safetype fudge */
    /* Lazy bird: Put the raw signature at the end. */
    cbEstimated = RT_ALIGN_Z(cbEstimated, 8) + cbSigRawAligned;

    /* Allocate and initialize header entry. */
    PRTHTTPHEADER const pHdr = (PRTHTTPHEADER)RTMemAllocZ(cbEstimated);
    AssertPtrReturn(pHdr, VERR_NO_MEMORY);
    uint8_t * const pbSigRaw = (uint8_t *)pHdr + cbEstimated - cbSigRawAligned;

    pHdr->cchName   = sizeof("Authorization") - 1;
    pHdr->offValue  = sizeof("Authorization") + 1;
    pHdr->Core.next = NULL;
    pHdr->Core.data = pHdr->szData;
    char  *pszLeft  = pHdr->szData;
    size_t cbLeft   = cbEstimated - RT_UOFFSETOF(RTHTTPHEADER, szData) - cbSigRawAligned;

    size_t cch = RTStrPrintf(pszLeft, cbLeft, s_szSuffixFmt, pszKeyId);
    cbLeft -= cch;
    pszLeft += cch;

    /*
     * Instantiate the digest.
     */
    RTCRDIGEST hDigest = NIL_RTCRDIGEST;
    rc = RTCrDigestCreateByType(&hDigest, RTDIGESTTYPE_SHA256);
    if (RT_SUCCESS(rc))
    {
        /*
         * Add the request-target pseudo header first.
         */
        Assert(cbLeft > sizeof(s_szRequestField));
        memcpy(pszLeft, RT_STR_TUPLE(s_szRequestField));
        pszLeft += sizeof(s_szRequestField) - 1;

        rc = RTCrDigestUpdate(hDigest, RT_STR_TUPLE(s_szRequestField));
        if (RT_SUCCESS(rc))
            rc = RTCrDigestUpdate(hDigest, RT_STR_TUPLE(": "));
        if (RT_SUCCESS(rc))
            rc = RTCrDigestUpdate(hDigest, pszMethodSp, strlen(pszMethodSp));
        if (RT_SUCCESS(rc))
            rc = RTCrDigestUpdate(hDigest, pszPath, strlen(pszPath));

        /*
         * Add the header fields.
         */
        for (PRTHTTPHEADER pCur = (PRTHTTPHEADER)pThis->pHeaders; pCur && RT_SUCCESS(rc); pCur = (PRTHTTPHEADER)pCur->Core.next)
        {
            AssertBreakStmt(cbLeft > pCur->cchName, rc = VERR_INTERNAL_ERROR_3);
            *pszLeft++ = ' ';
            cbLeft--;
            memcpy(pszLeft, pCur->szData, pCur->cchName);
            pszLeft[pCur->cchName] = '\0';
            RTStrToLower(pszLeft);

            rc = RTCrDigestUpdate(hDigest, RT_STR_TUPLE("\n"));
            AssertRCBreak(rc);
            rc = RTCrDigestUpdate(hDigest, pszLeft, pCur->cchName);
            AssertRCBreak(rc);
            rc = RTCrDigestUpdate(hDigest, RT_STR_TUPLE(": "));
            AssertRCBreak(rc);
            const char *pszValue = &pCur->szData[pCur->offValue];
            rc = RTCrDigestUpdate(hDigest, pszValue, strlen(pszValue));
            AssertRCBreak(rc);

            pszLeft += pCur->cchName;
            cbLeft  -= pCur->cchName;
        }
        if (RT_SUCCESS(rc))
            AssertStmt(cbLeft > sizeof(s_szInfix) + cchSigStr + sizeof(s_szPostfix), rc = VERR_INTERNAL_ERROR_3);
        if (RT_SUCCESS(rc))
        {
            /* Complete the header field part. */
            memcpy(pszLeft, RT_STR_TUPLE(s_szInfix));
            pszLeft += sizeof(s_szInfix) - 1;
            cbLeft  -= sizeof(s_szInfix) - 1;

            /*
             * Sign the digest.
             */
            RTCRPKIXSIGNATURE hSigner;
            rc = RTCrPkixSignatureCreateByObjIdString(&hSigner, RTCR_PKCS1_SHA256_WITH_RSA_OID, hKey, NULL, true /*fSigning*/);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                size_t cbActual = cbSigRawAligned;
                rc = RTCrPkixSignatureSign(hSigner, hDigest, pbSigRaw, &cbActual);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    Assert(cbActual == cbSigRaw);
                    RTCrPkixSignatureRelease(hSigner);
                    hSigner = NIL_RTCRPKIXSIGNATURE;
                    RTCrDigestRelease(hDigest);
                    hDigest = NIL_RTCRDIGEST;

                    /*
                     * Convert the signature to Base64 and append it to the string.
                     */
                    size_t cchActual;
                    rc = RTBase64EncodeEx(pbSigRaw, cbActual, RTBASE64_FLAGS_NO_LINE_BREAKS, pszLeft, cbLeft, &cchActual);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        Assert(cchActual == cchSigStr);
                        pszLeft += cchActual;
                        cbLeft  -= cchActual;

                        /*
                         * Append the postfix and add the header to the front of the list.
                         */
                        AssertStmt(cbLeft >= sizeof(s_szPostfix), rc = VERR_INTERNAL_ERROR_3);
                        if (RT_SUCCESS(rc))
                        {
                            memcpy(pszLeft, s_szPostfix, sizeof(s_szPostfix));

                            pHdr->Core.next = pThis->pHeaders;
                            if (!pThis->pHeaders)
                                pThis->ppHeadersTail  = &pHdr->Core.next;
                            pThis->pHeaders = &pHdr->Core;

                            CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HTTPHEADER, pThis->pHeaders);
                            if (CURL_SUCCESS(rcCurl))
                                return VINF_SUCCESS;
                            rc = VERR_HTTP_CURL_ERROR;
                        }
                    }
                }
                RTCrPkixSignatureRelease(hSigner);
            }
        }
        RTCrDigestRelease(hDigest);
    }
    RTMemFree(pHdr);
    return rc;
}


/*********************************************************************************************************************************
*   HTTPS and root certficates                                                                                                   *
*********************************************************************************************************************************/

/**
 * Set the CA file to NULL, deleting any temporary file if necessary.
 *
 * @param   pThis           The HTTP/HTTPS client instance.
 */
static void rtHttpUnsetCaFile(PRTHTTPINTERNAL pThis)
{
    if (pThis->pszCaFile)
    {
        if (pThis->fDeleteCaFile)
        {
            int rc2 = RTFileDelete(pThis->pszCaFile); RT_NOREF_PV(rc2);
            AssertMsg(RT_SUCCESS(rc2) || !RTFileExists(pThis->pszCaFile), ("rc=%Rrc '%s'\n", rc2, pThis->pszCaFile));
        }
        RTStrFree(pThis->pszCaFile);
        pThis->pszCaFile = NULL;
    }
}


RTR3DECL(int) RTHttpSetCAFile(RTHTTP hHttp, const char *pszCaFile)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rtHttpUnsetCaFile(pThis);

    pThis->fDeleteCaFile = false;
    if (pszCaFile)
        return RTStrDupEx(&pThis->pszCaFile, pszCaFile);
    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpUseTemporaryCaFile(RTHTTP hHttp, PRTERRINFO pErrInfo)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    /*
     * Create a temporary file.
     */
    int rc = VERR_NO_STR_MEMORY;
    char *pszCaFile = RTStrAlloc(RTPATH_MAX);
    if (pszCaFile)
    {
        RTFILE hFile;
        rc = RTFileOpenTemp(&hFile, pszCaFile, RTPATH_MAX,
                            RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE | (0600 << RTFILE_O_CREATE_MODE_SHIFT));
        if (RT_SUCCESS(rc))
        {
            /*
             * Gather certificates into a temporary store and export them to the temporary file.
             */
            RTCRSTORE hStore;
            rc = RTCrStoreCreateInMem(&hStore, 256);
            if (RT_SUCCESS(rc))
            {
                rc = RTHttpGatherCaCertsInStore(hStore, 0 /*fFlags*/, pErrInfo);
                if (RT_SUCCESS(rc))
                    /** @todo Consider adding an API for exporting to a RTFILE... */
                    rc = RTCrStoreCertExportAsPem(hStore, 0 /*fFlags*/, pszCaFile);
                RTCrStoreRelease(hStore);
            }
            RTFileClose(hFile);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Set the CA file for the instance.
                 */
                rtHttpUnsetCaFile(pThis);

                pThis->fDeleteCaFile = true;
                pThis->pszCaFile = pszCaFile;
                return VINF_SUCCESS;
            }

            int rc2 = RTFileDelete(pszCaFile);
            AssertRC(rc2);
        }
        else
            RTErrInfoAddF(pErrInfo, rc, "Error creating temorary file: %Rrc", rc);

        RTStrFree(pszCaFile);
    }
    return rc;
}


RTR3DECL(int) RTHttpGatherCaCertsInStore(RTCRSTORE hStore, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    uint32_t const cBefore = RTCrStoreCertCount(hStore);
    AssertReturn(cBefore != UINT32_MAX, VERR_INVALID_HANDLE);
    RT_NOREF_PV(fFlags);

    /*
     * Add the user store, quitely ignoring any errors.
     */
    RTCRSTORE hSrcStore;
    int rcUser = RTCrStoreCreateSnapshotById(&hSrcStore, RTCRSTOREID_USER_TRUSTED_CAS_AND_CERTIFICATES, pErrInfo);
    if (RT_SUCCESS(rcUser))
    {
        rcUser = RTCrStoreCertAddFromStore(hStore, RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR,
                                           hSrcStore);
        RTCrStoreRelease(hSrcStore);
    }

    /*
     * Ditto for the system store.
     */
    int rcSystem = RTCrStoreCreateSnapshotById(&hSrcStore, RTCRSTOREID_SYSTEM_TRUSTED_CAS_AND_CERTIFICATES, pErrInfo);
    if (RT_SUCCESS(rcSystem))
    {
        rcSystem = RTCrStoreCertAddFromStore(hStore, RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR,
                                             hSrcStore);
        RTCrStoreRelease(hSrcStore);
    }

    /*
     * If the number of certificates increased, we consider it a success.
     */
    if (RTCrStoreCertCount(hStore) > cBefore)
    {
        if (RT_FAILURE(rcSystem))
            return -rcSystem;
        if (RT_FAILURE(rcUser))
            return -rcUser;
        return rcSystem != VINF_SUCCESS ? rcSystem : rcUser;
    }

    if (RT_FAILURE(rcSystem))
        return rcSystem;
    if (RT_FAILURE(rcUser))
        return rcUser;
    return VERR_NOT_FOUND;
}


RTR3DECL(int) RTHttpGatherCaCertsInFile(const char *pszCaFile, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    RTCRSTORE hStore;
    int rc = RTCrStoreCreateInMem(&hStore, 256);
    if (RT_SUCCESS(rc))
    {
        rc = RTHttpGatherCaCertsInStore(hStore, fFlags, pErrInfo);
        if (RT_SUCCESS(rc))
            rc = RTCrStoreCertExportAsPem(hStore, 0 /*fFlags*/, pszCaFile);
        RTCrStoreRelease(hStore);
    }
    return rc;
}


RTR3DECL(bool) RTHttpGetVerifyPeer(RTHTTP hHttp)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN_RC(pThis, false);
    return pThis->fVerifyPeer;
}


RTR3DECL(int) RTHttpSetVerifyPeer(RTHTTP hHttp, bool fVerify)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(!pThis->fBusy, VERR_WRONG_ORDER);

    if (pThis->fVerifyPeer != fVerify)
    {
        CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_SSL_VERIFYPEER, (long)fVerify);
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("CURLOPT_SSL_VERIFYPEER=%RTbool: %d (%#x)\n", fVerify, rcCurl, rcCurl),
                        VERR_HTTP_CURL_ERROR);
        pThis->fVerifyPeer = fVerify;
    }

    return VINF_SUCCESS;
}



/*********************************************************************************************************************************
*   .......
*********************************************************************************************************************************/


/**
 * Figures out the IPRT status code for a GET.
 *
 * @returns IPRT status code.
 * @param   pThis           The HTTP/HTTPS client instance.
 * @param   rcCurl          What curl returned.
 * @param   puHttpStatus    Where to optionally return the HTTP status.  If specified,
 *                          the HTTP statuses are not translated to IPRT status codes.
 */
static int rtHttpGetCalcStatus(PRTHTTPINTERNAL pThis, CURLcode rcCurl, uint32_t *puHttpStatus)
{
    int rc = VERR_HTTP_CURL_ERROR;

    if (pThis->pszRedirLocation)
    {
        RTStrFree(pThis->pszRedirLocation);
        pThis->pszRedirLocation = NULL;
    }
    if (CURL_SUCCESS(rcCurl))
    {
        curl_easy_getinfo(pThis->pCurl, CURLINFO_RESPONSE_CODE, &pThis->lLastResp);
        if (puHttpStatus)
        {
            *puHttpStatus = pThis->lLastResp;
            rc = VINF_SUCCESS;
        }

        switch (pThis->lLastResp)
        {
            case 200:
                /* OK, request was fulfilled */
            case 204:
                /* empty response */
                rc = VINF_SUCCESS;
                break;
            case 301: /* Moved permantently. */
            case 302: /* Found / Moved temporarily. */
            case 303: /* See Other. */
            case 307: /* Temporary redirect. */
            case 308: /* Permanent redirect. */
            {
                const char *pszRedirect = NULL;
                curl_easy_getinfo(pThis->pCurl, CURLINFO_REDIRECT_URL, &pszRedirect);
                size_t cb = pszRedirect ? strlen(pszRedirect) : 0;
                if (cb > 0 && cb < 2048)
                    pThis->pszRedirLocation = RTStrDup(pszRedirect);
                if (!puHttpStatus)
                    rc = VERR_HTTP_REDIRECTED;
                break;
            }
            case 400:
                /* bad request */
                if (!puHttpStatus)
                    rc = VERR_HTTP_BAD_REQUEST;
                break;
            case 403:
                /* forbidden, authorization will not help */
                if (!puHttpStatus)
                    rc = VERR_HTTP_ACCESS_DENIED;
                break;
            case 404:
                /* URL not found */
                if (!puHttpStatus)
                    rc = VERR_HTTP_NOT_FOUND;
                break;
            case 501:
                /* Requested function not supported */
                if (!puHttpStatus)
                    rc = VERR_HTTP_NOT_SUPPORTED;
                break;
        }

        if (pThis->pszRedirLocation)
            Log(("rtHttpGetCalcStatus: rc=%Rrc lastResp=%lu redir='%s'\n", rc, pThis->lLastResp, pThis->pszRedirLocation));
        else
            Log(("rtHttpGetCalcStatus: rc=%Rrc lastResp=%lu\n", rc, pThis->lLastResp));
    }
    else
    {
        switch (rcCurl)
        {
            case CURLE_URL_MALFORMAT:
            case CURLE_COULDNT_RESOLVE_HOST:
                rc = VERR_HTTP_HOST_NOT_FOUND;
                break;
            case CURLE_COULDNT_CONNECT:
                rc = VERR_HTTP_COULDNT_CONNECT;
                break;
            case CURLE_SSL_CONNECT_ERROR:
                rc = VERR_HTTP_SSL_CONNECT_ERROR;
                break;
            case CURLE_SSL_CACERT:
                /* The peer certificate cannot be authenticated with the CA certificates
                 * set by RTHttpSetCAFile(). We need other or additional CA certificates. */
                rc = VERR_HTTP_CACERT_CANNOT_AUTHENTICATE;
                break;
            case CURLE_SSL_CACERT_BADFILE:
                /* CAcert file (see RTHttpSetCAFile()) has wrong format */
                rc = VERR_HTTP_CACERT_WRONG_FORMAT;
                break;
            case CURLE_ABORTED_BY_CALLBACK:
                /* forcefully aborted */
                rc = VERR_HTTP_ABORTED;
                break;
            case CURLE_COULDNT_RESOLVE_PROXY:
                rc = VERR_HTTP_PROXY_NOT_FOUND;
                break;
            case CURLE_WRITE_ERROR:
                rc = RT_FAILURE_NP(pThis->rcOutput) ? pThis->rcOutput : VERR_WRITE_ERROR;
                break;
            //case CURLE_READ_ERROR

            default:
                break;
        }
        Log(("%s: %Rrc: %u = %s%s%.*s\n",
             __FUNCTION__,
             rc, rcCurl, curl_easy_strerror((CURLcode)rcCurl),
             pThis->szErrorBuffer[0] != '\0' ? ": " : "",
             (int) sizeof(pThis->szErrorBuffer),
             pThis->szErrorBuffer[0] != '\0' ? pThis->szErrorBuffer : ""));
    }

    return rc;
}


/**
 * cURL callback for reporting progress, we use it for checking for abort.
 */
static int rtHttpProgress(void *pData, double rdTotalDownload, double rdDownloaded, double rdTotalUpload, double rdUploaded) RT_NOTHROW_DEF
{
    PRTHTTPINTERNAL pThis = (PRTHTTPINTERNAL)pData;
    AssertReturn(pThis->u32Magic == RTHTTP_MAGIC, 1);
    RT_NOREF_PV(rdTotalUpload);
    RT_NOREF_PV(rdUploaded);

    pThis->cbDownloadHint = (uint64_t)rdTotalDownload;

    if (pThis->pfnDownloadProgress)
        pThis->pfnDownloadProgress(pThis, pThis->pvDownloadProgressUser, (uint64_t)rdTotalDownload, (uint64_t)rdDownloaded);

    return pThis->fAbort ? 1 : 0;
}


/**
 * Whether we're likely to need SSL to handle the give URL.
 *
 * @returns true if we need, false if we probably don't.
 * @param   pszUrl              The URL.
 */
static bool rtHttpNeedSsl(const char *pszUrl)
{
    return RTStrNICmp(pszUrl, RT_STR_TUPLE("https:")) == 0;
}


/**
 * Applies recoded settings to the cURL instance before doing work.
 *
 * @returns IPRT status code.
 * @param   pThis           The HTTP/HTTPS client instance.
 * @param   pszUrl          The URL.
 */
static int rtHttpApplySettings(PRTHTTPINTERNAL pThis, const char *pszUrl)
{
    /*
     * The URL.
     */
    CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_URL, pszUrl);
    if (CURL_FAILURE(rcCurl))
        return VERR_INVALID_PARAMETER;

    /*
     * Proxy config.
     */
    int rc = rtHttpConfigureProxyForUrl(pThis, pszUrl);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Setup SSL.  Can be a bit of work.
     */
    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1);
    if (CURL_FAILURE(rcCurl))
        return VERR_INVALID_PARAMETER;

    const char *pszCaFile = pThis->pszCaFile;
    if (   !pszCaFile
        && rtHttpNeedSsl(pszUrl))
    {
        rc = RTHttpUseTemporaryCaFile(pThis, NULL);
        if (RT_SUCCESS(rc))
            pszCaFile = pThis->pszCaFile;
        else
            return rc; /* Non-portable alternative: pszCaFile = "/etc/ssl/certs/ca-certificates.crt"; */
    }
    if (pszCaFile)
    {
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_CAINFO, pszCaFile);
        if (CURL_FAILURE(rcCurl))
            return VERR_HTTP_CURL_ERROR;
    }

    /*
     * Progress/abort.
     */
    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_PROGRESSFUNCTION, &rtHttpProgress);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;
    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_PROGRESSDATA, (void *)pThis);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;
    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_NOPROGRESS, (long)0);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    /*
     * Set default user agent string if necessary.  Some websites take offence
     * if we don't set it.
     */
    if (   !pThis->fHaveSetUserAgent
        && !pThis->fHaveUserAgentHeader)
    {
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_USERAGENT, "Mozilla/5.0 (AgnosticOS; Blend) IPRT/64.42");
        if (CURL_FAILURE(rcCurl))
            return VERR_HTTP_CURL_ERROR;
        pThis->fHaveSetUserAgent = true;
    }

    /*
     * Use GET by default.
     */
    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_NOBODY, 0L);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;
    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HEADER, 0L);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


/**
 * Resets state.
 *
 * @param   pThis        HTTP client instance.
 */
static void rtHttpResetState(PRTHTTPINTERNAL pThis)
{
    pThis->fAbort                   = false;
    pThis->rcOutput                 = VINF_SUCCESS;
    pThis->uDownloadHttpStatus      = UINT32_MAX;
    pThis->cbDownloadContent        = UINT64_MAX;
    pThis->offDownloadContent       = 0;
    pThis->offUploadContent         = 0;
    pThis->rcOutput                 = VINF_SUCCESS;
    pThis->cbDownloadHint           = 0;
    Assert(pThis->BodyOutput.pHttp == pThis);
    Assert(pThis->HeadersOutput.pHttp == pThis);
}


/**
 * Tries to determin uDownloadHttpStatus and cbDownloadContent.
 *
 * @param   pThis        HTTP client instance.
 */
static void rtHttpGetDownloadStatusAndLength(PRTHTTPINTERNAL pThis)
{
    long lHttpStatus = 0;
    curl_easy_getinfo(pThis->pCurl, CURLINFO_RESPONSE_CODE, &lHttpStatus);
    pThis->uDownloadHttpStatus = (uint32_t)lHttpStatus;

#ifdef CURLINFO_CONTENT_LENGTH_DOWNLOAD_T
    curl_off_t cbContent = -1;
    curl_easy_getinfo(pThis->pCurl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cbContent);
    if (cbContent >= 0)
        pThis->cbDownloadContent = (uint64_t)cbContent;
#else
    double rdContent = -1.0;
    curl_easy_getinfo(pThis->pCurl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &rdContent);
    if (rdContent >= 0.0)
        pThis->cbDownloadContent = (uint64_t)rdContent;
#endif
}


/**
 * Worker for rtHttpWriteHeaderData and rtHttpWriteBodyData.
 */
static size_t rtHttpWriteDataToMemOutput(PRTHTTPINTERNAL pThis, RTHTTPOUTPUTDATA *pOutput, char const *pchBuf, size_t cbToAppend)
{
    /*
     * Do max size and overflow checks.
     */
    size_t const cbCurSize  = pOutput->uData.Mem.cb;
    size_t const cbNewSize  = cbCurSize + cbToAppend;
    if (   cbToAppend < RTHTTP_MAX_MEM_DOWNLOAD_SIZE
        && cbNewSize  < RTHTTP_MAX_MEM_DOWNLOAD_SIZE)
    {
        if (cbNewSize + 1 <= pOutput->uData.Mem.cbAllocated)
        {
            memcpy(&pOutput->uData.Mem.pb[cbCurSize], pchBuf, cbToAppend);
            pOutput->uData.Mem.cb = cbNewSize;
            pOutput->uData.Mem.pb[cbNewSize] = '\0';
            return cbToAppend;
        }

        /*
         * We need to reallocate the output buffer.
         */
        /** @todo this could do with a better strategy wrt growth. */
        size_t cbAlloc = RT_ALIGN_Z(cbNewSize + 1, 64);
        if (   cbAlloc <= pThis->cbDownloadHint
            && pThis->cbDownloadHint < RTHTTP_MAX_MEM_DOWNLOAD_SIZE
            && pOutput == &pThis->BodyOutput)
            cbAlloc = RT_ALIGN_Z(pThis->cbDownloadHint + 1, 64);

        uint8_t *pbNew = (uint8_t *)RTMemRealloc(pOutput->uData.Mem.pb, cbAlloc);
        if (pbNew)
        {
            memcpy(&pbNew[cbCurSize], pchBuf, cbToAppend);
            pbNew[cbNewSize] = '\0';

            pOutput->uData.Mem.cbAllocated = cbAlloc;
            pOutput->uData.Mem.pb = pbNew;
            pOutput->uData.Mem.cb = cbNewSize;
            return cbToAppend;
        }

        pThis->rcOutput = VERR_NO_MEMORY;
    }
    else
        pThis->rcOutput = VERR_TOO_MUCH_DATA;

    /*
     * Failure - abort.
     */
    RTMemFree(pOutput->uData.Mem.pb);
    pOutput->uData.Mem.pb = NULL;
    pOutput->uData.Mem.cb = RTHTTP_MAX_MEM_DOWNLOAD_SIZE;
    pThis->fAbort   = true;
    return 0;
}


/**
 * cURL callback for writing body data.
 */
static size_t rtHttpWriteBodyData(char *pchBuf, size_t cbUnit, size_t cUnits, void *pvUser) RT_NOTHROW_DEF
{
    PRTHTTPINTERNAL   pThis      = (PRTHTTPINTERNAL)pvUser;
    size_t const      cbToAppend = cbUnit * cUnits;

    /*
     * Check if this belongs to the body download callback.
     */
    if (pThis->pfnDownloadCallback)
    {
        if (pThis->offDownloadContent == 0)
            rtHttpGetDownloadStatusAndLength(pThis);

        if (   (pThis->fDownloadCallback & RTHTTPDOWNLOAD_F_ONLY_STATUS_MASK) == RTHTTPDOWNLOAD_F_ANY_STATUS
            || (pThis->fDownloadCallback & RTHTTPDOWNLOAD_F_ONLY_STATUS_MASK) == pThis->uDownloadHttpStatus)
        {
            int rc = pThis->pfnDownloadCallback(pThis, pchBuf, cbToAppend, pThis->uDownloadHttpStatus, pThis->offDownloadContent,
                                                pThis->cbDownloadContent, pThis->pvDownloadCallbackUser);
            if (RT_SUCCESS(rc))
            {
                pThis->offDownloadContent += cbToAppend;
                return cbToAppend;
            }
            if (RT_SUCCESS(pThis->rcOutput))
                pThis->rcOutput = rc;
            pThis->fAbort = true;
            return 0;
        }
    }

    /*
     * Otherwise, copy to memory output buffer.
     */
    return rtHttpWriteDataToMemOutput(pThis, &pThis->BodyOutput, pchBuf, cbToAppend);
}


/**
 * cURL callback for writing header data.
 */
static size_t rtHttpWriteHeaderData(char *pchBuf, size_t cbUnit, size_t cUnits, void *pvUser) RT_NOTHROW_DEF
{
    PRTHTTPINTERNAL   pThis      = (PRTHTTPINTERNAL)pvUser;
    size_t const      cbToAppend = cbUnit * cUnits;

    /*
     * Work the header callback, if one.
     * ASSUMES cURL is giving us one header at a time.
     */
    if (pThis->pfnHeaderCallback)
    {
        /*
         * Find the end of the field name first.
         */
        uint32_t    uMatchWord;
        size_t      cchField;
        const char *pchField = pchBuf;
        size_t      cchValue;
        const char *pchValue = (const char *)memchr(pchBuf, ':', cbToAppend);
        if (pchValue)
        {
            cchField = pchValue - pchField;
            if (RT_LIKELY(cchField >= 3))
                uMatchWord = RTHTTP_MAKE_HDR_MATCH_WORD(cchField, RT_C_TO_LOWER(pchBuf[0]),
                                                        RT_C_TO_LOWER(pchBuf[1]), RT_C_TO_LOWER(pchBuf[2]));
            else
                uMatchWord = RTHTTP_MAKE_HDR_MATCH_WORD(cchField,
                                                        cchField >= 1 ? RT_C_TO_LOWER(pchBuf[0]) : 0,
                                                        cchField >= 2 ? RT_C_TO_LOWER(pchBuf[1]) : 0,
                                                        0);
            pchValue++;
            cchValue = cbToAppend - cchField - 1;
        }
        /* Since cURL gives us the "HTTP/{version} {code} {status}" line too,
           we slap a fictitious field name ':http-status-line' in front of it. */
        else if (cbToAppend > 5 && pchBuf[0] == 'H' && pchBuf[1] == 'T' && pchBuf[2] == 'T' && pchBuf[3] == 'P' && pchBuf[4] == '/')
        {
            pchField   = ":http-status-line";
            cchField   = 17;
            uMatchWord = RTHTTP_MAKE_HDR_MATCH_WORD(17, ':', 'h', 't');
            pchValue   = pchBuf;
            cchValue   = cbToAppend;
        }
        /* cURL also gives us the empty line before the body, so we slap another
           fictitious field name ':end-of-headers' in front of it as well. */
        else if (cbToAppend == 2 && pchBuf[0] == '\r' && pchBuf[1] == '\n')
        {
            pchField   = ":end-of-headers";
            cchField   = 15;
            uMatchWord = RTHTTP_MAKE_HDR_MATCH_WORD(15, ':', 'e', 'n');
            pchValue   = pchBuf;
            cchValue   = cbToAppend;
        }
        else
            AssertMsgFailedReturn(("pchBuf=%.*s\n", cbToAppend, pchBuf), cbToAppend);

        /*
         * Determin the field value, stripping one leading blank and all
         * trailing spaces.
         */
        if (cchValue > 0 && RT_C_IS_BLANK(*pchValue))
            pchValue++, cchValue--;
        while (cchValue > 0 && RT_C_IS_SPACE(pchValue[cchValue - 1]))
            cchValue--;

        /*
         * Pass it to the callback.
         */
        Log6(("rtHttpWriteHeaderData: %.*s: %.*s\n", cchField, pchBuf, cchValue, pchValue));
        int rc = pThis->pfnHeaderCallback(pThis, uMatchWord, pchBuf, cchField,
                                          pchValue, cchValue, pThis->pvHeaderCallbackUser);
        if (RT_SUCCESS(rc))
            return cbToAppend;

        /* Abort on error. */
        if (RT_SUCCESS(pThis->rcOutput))
            pThis->rcOutput = rc;
        pThis->fAbort = true;
        return 0;
    }

    return rtHttpWriteDataToMemOutput(pThis, &pThis->HeadersOutput, pchBuf, cbToAppend);
}


/**
 * cURL callback for working the upload callback.
 */
static size_t rtHttpWriteDataToDownloadCallback(char *pchBuf, size_t cbUnit, size_t cUnits, void *pvUser) RT_NOTHROW_DEF
{
    PRTHTTPINTERNAL   pThis = (PRTHTTPINTERNAL)pvUser;
    size_t const      cbBuf = cbUnit * cUnits;

    /* Get download info the first time we're called. */
    if (pThis->offDownloadContent == 0)
        rtHttpGetDownloadStatusAndLength(pThis);

    /* Call the callback if the HTTP status code matches, otherwise let it go to /dev/null. */
    if (   (pThis->fDownloadCallback & RTHTTPDOWNLOAD_F_ONLY_STATUS_MASK) == RTHTTPDOWNLOAD_F_ANY_STATUS
        || (pThis->fDownloadCallback & RTHTTPDOWNLOAD_F_ONLY_STATUS_MASK) == pThis->uDownloadHttpStatus)
    {
        int rc = pThis->pfnDownloadCallback(pThis, pchBuf, cbBuf, pThis->uDownloadHttpStatus, pThis->offDownloadContent,
                                            pThis->cbDownloadContent, pThis->pvDownloadCallbackUser);
        if (RT_SUCCESS(rc))
        {   /* likely */ }
        else
        {
            if (RT_SUCCESS(pThis->rcOutput))
                pThis->rcOutput = rc;
            pThis->fAbort = true;
            return 0;
        }
    }
    pThis->offDownloadContent += cbBuf;
    return cbBuf;
}


/**
 * Callback feeding cURL data from RTHTTPINTERNAL::ReadData::Mem.
 */
static size_t rtHttpReadData(void *pvDst, size_t cbUnit, size_t cUnits, void *pvUser) RT_NOTHROW_DEF
{
    PRTHTTPINTERNAL pThis = (PRTHTTPINTERNAL)pvUser;
    size_t const cbReq    = cbUnit * cUnits;
    size_t const offMem   = pThis->ReadData.Mem.offMem;
    size_t cbToCopy = pThis->ReadData.Mem.cbMem - offMem;
    if (cbToCopy > cbReq)
        cbToCopy = cbReq;
    memcpy(pvDst, (uint8_t const *)pThis->ReadData.Mem.pvMem + offMem, cbToCopy);
    pThis->ReadData.Mem.offMem = offMem + cbToCopy;
    return cbToCopy;
}


/**
 * Callback feeding cURL data via the user upload callback.
 */
static size_t rtHttpReadDataFromUploadCallback(void *pvDst, size_t cbUnit, size_t cUnits, void *pvUser) RT_NOTHROW_DEF
{
    PRTHTTPINTERNAL pThis = (PRTHTTPINTERNAL)pvUser;
    size_t const cbReq    = cbUnit * cUnits;

    size_t cbActual = 0;
    int rc = pThis->pfnUploadCallback(pThis, pvDst, cbReq, pThis->offUploadContent, &cbActual, pThis->pvUploadCallbackUser);
    if (RT_SUCCESS(rc))
    {
        pThis->offUploadContent += cbActual;
        return cbActual;
    }

    if (RT_SUCCESS(pThis->rcOutput))
        pThis->rcOutput = rc;
    pThis->fAbort = true;
    return CURL_READFUNC_ABORT;
}


/**
 * Helper for installing a (body) write callback function.
 *
 * @returns cURL status code.
 * @param   pThis               The HTTP client instance.
 * @param   pfnWrite            The callback.
 * @param   pvUser              The callback user argument.
 */
static CURLcode rtHttpSetWriteCallback(PRTHTTPINTERNAL pThis, PFNRTHTTPWRITECALLBACKRAW pfnWrite, void *pvUser)
{
    CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_WRITEFUNCTION, pfnWrite);
    if (CURL_SUCCESS(rcCurl))
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_WRITEDATA, pvUser);
    return rcCurl;
}


/**
 * Helper for installing a header write callback function.
 *
 * @returns cURL status code.
 * @param   pThis               The HTTP client instance.
 * @param   pfnWrite            The callback.
 * @param   pvUser              The callback user argument.
 */
static CURLcode rtHttpSetHeaderCallback(PRTHTTPINTERNAL pThis, PFNRTHTTPWRITECALLBACKRAW pfnWrite, void *pvUser)
{
    CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HEADERFUNCTION, pfnWrite);
    if (CURL_SUCCESS(rcCurl))
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HEADERDATA, pvUser);
    return rcCurl;
}


/**
 * Helper for installing a (body) read callback function.
 *
 * @returns cURL status code.
 * @param   pThis               The HTTP client instance.
 * @param   pfnRead             The callback.
 * @param   pvUser              The callback user argument.
 */
static CURLcode rtHttpSetReadCallback(PRTHTTPINTERNAL pThis, PFNRTHTTPREADCALLBACKRAW pfnRead, void *pvUser)
{
    CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_READFUNCTION, pfnRead);
    if (CURL_SUCCESS(rcCurl))
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_READDATA, pvUser);
    return rcCurl;
}


/**
 * Internal worker that performs a HTTP GET.
 *
 * @returns IPRT status code.
 * @param   hHttp               The HTTP/HTTPS client instance.
 * @param   pszUrl              The URL.
 * @param   fNoBody             Set to suppress the body.
 * @param   ppvResponse         Where to return the pointer to the allocated
 *                              response data (RTMemFree).  There will always be
 *                              an zero terminator char after the response, that
 *                              is not part of the size returned via @a pcb.
 * @param   pcb                 The size of the response data.
 *
 * @remarks We ASSUME the API user doesn't do concurrent GETs in different
 *          threads, because that will probably blow up!
 */
static int rtHttpGetToMem(RTHTTP hHttp, const char *pszUrl, bool fNoBody, uint8_t **ppvResponse, size_t *pcb)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    /*
     * Reset the return values in case of more "GUI programming" on the client
     * side (i.e. a programming style not bothering checking return codes).
     */
    *ppvResponse = NULL;
    *pcb         = 0;

    /*
     * Set the busy flag (paranoia).
     */
    bool fBusy = ASMAtomicXchgBool(&pThis->fBusy, true);
    AssertReturn(!fBusy, VERR_WRONG_ORDER);

    /*
     * Reset the state and apply settings.
     */
    rtHttpResetState(pThis);
    int rc = rtHttpApplySettings(hHttp, pszUrl);
    if (RT_SUCCESS(rc))
    {
        RT_ZERO(pThis->BodyOutput.uData.Mem);
        CURLcode rcCurl = rtHttpSetWriteCallback(pThis, &rtHttpWriteBodyData, pThis);
        if (fNoBody)
        {
            if (CURL_SUCCESS(rcCurl))
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_NOBODY, 1L);
            if (CURL_SUCCESS(rcCurl))
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HEADER, 1L);
        }
        if (CURL_SUCCESS(rcCurl))
        {
            /*
             * Perform the HTTP operation.
             */
            rcCurl = curl_easy_perform(pThis->pCurl);
            rc = rtHttpGetCalcStatus(pThis, rcCurl, NULL);
            if (RT_SUCCESS(rc))
                rc = pThis->rcOutput;
            if (RT_SUCCESS(rc))
            {
                *ppvResponse = pThis->BodyOutput.uData.Mem.pb;
                *pcb         = pThis->BodyOutput.uData.Mem.cb;
                Log(("rtHttpGetToMem: %zx bytes (allocated %zx)\n",
                     pThis->BodyOutput.uData.Mem.cb, pThis->BodyOutput.uData.Mem.cbAllocated));
            }
            else if (pThis->BodyOutput.uData.Mem.pb)
                RTMemFree(pThis->BodyOutput.uData.Mem.pb);
            RT_ZERO(pThis->BodyOutput.uData.Mem);
        }
        else
            rc = VERR_HTTP_CURL_ERROR;
    }

    ASMAtomicWriteBool(&pThis->fBusy, false);
    return rc;
}


RTR3DECL(int) RTHttpGetText(RTHTTP hHttp, const char *pszUrl, char **ppszNotUtf8)
{
    Log(("RTHttpGetText: hHttp=%p pszUrl=%s\n", hHttp, pszUrl));
    uint8_t *pv;
    size_t   cb;
    int rc = rtHttpGetToMem(hHttp, pszUrl, false /*fNoBody*/, &pv, &cb);
    if (RT_SUCCESS(rc))
    {
        if (pv) /* paranoia */
            *ppszNotUtf8 = (char *)pv;
        else
            *ppszNotUtf8 = (char *)RTMemDup("", 1);
    }
    else
        *ppszNotUtf8 = NULL;
    return rc;
}


RTR3DECL(int) RTHttpGetHeaderText(RTHTTP hHttp, const char *pszUrl, char **ppszNotUtf8)
{
    Log(("RTHttpGetText: hHttp=%p pszUrl=%s\n", hHttp, pszUrl));
    uint8_t *pv;
    size_t   cb;
    int rc = rtHttpGetToMem(hHttp, pszUrl, true /*fNoBody*/, &pv, &cb);
    if (RT_SUCCESS(rc))
    {
        if (pv) /* paranoia */
            *ppszNotUtf8 = (char *)pv;
        else
            *ppszNotUtf8 = (char *)RTMemDup("", 1);
    }
    else
        *ppszNotUtf8 = NULL;
    return rc;

}


RTR3DECL(void) RTHttpFreeResponseText(char *pszNotUtf8)
{
    RTMemFree(pszNotUtf8);
}


RTR3DECL(int) RTHttpGetBinary(RTHTTP hHttp, const char *pszUrl, void **ppvResponse, size_t *pcb)
{
    Log(("RTHttpGetBinary: hHttp=%p pszUrl=%s\n", hHttp, pszUrl));
    return rtHttpGetToMem(hHttp, pszUrl, false /*fNoBody*/, (uint8_t **)ppvResponse, pcb);
}


RTR3DECL(int) RTHttpGetHeaderBinary(RTHTTP hHttp, const char *pszUrl, void **ppvResponse, size_t *pcb)
{
    Log(("RTHttpGetBinary: hHttp=%p pszUrl=%s\n", hHttp, pszUrl));
    return rtHttpGetToMem(hHttp, pszUrl, true /*fNoBody*/, (uint8_t **)ppvResponse, pcb);
}


RTR3DECL(void) RTHttpFreeResponse(void *pvResponse)
{
    RTMemFree(pvResponse);
}


/**
 * cURL callback for writing data to a file.
 */
static size_t rtHttpWriteDataToFile(char *pchBuf, size_t cbUnit, size_t cUnits, void *pvUser) RT_NOTHROW_DEF
{
    RTHTTPOUTPUTDATA *pOutput   = (RTHTTPOUTPUTDATA *)pvUser;
    PRTHTTPINTERNAL   pThis     = pOutput->pHttp;

    size_t            cbWritten = 0;
    int rc = RTFileWrite(pOutput->uData.hFile, pchBuf, cbUnit * cUnits, &cbWritten);
    if (RT_SUCCESS(rc))
        return cbWritten;

    Log(("rtHttpWriteDataToFile: rc=%Rrc cbUnit=%zd cUnits=%zu\n", rc, cbUnit, cUnits));
    pThis->rcOutput = rc;
    return 0;
}


RTR3DECL(int) RTHttpGetFile(RTHTTP hHttp, const char *pszUrl, const char *pszDstFile)
{
    Log(("RTHttpGetBinary: hHttp=%p pszUrl=%s pszDstFile=%s\n", hHttp, pszUrl, pszDstFile));
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    /*
     * Set the busy flag (paranoia).
     */
    bool fBusy = ASMAtomicXchgBool(&pThis->fBusy, true);
    AssertReturn(!fBusy, VERR_WRONG_ORDER);

    /*
     * Reset the state and apply settings.
     */
    rtHttpResetState(pThis);
    int rc = rtHttpApplySettings(hHttp, pszUrl);
    if (RT_SUCCESS(rc))
    {
        pThis->BodyOutput.uData.hFile = NIL_RTFILE;
        CURLcode rcCurl = rtHttpSetWriteCallback(pThis, &rtHttpWriteDataToFile, (void *)&pThis->BodyOutput);
        if (CURL_SUCCESS(rcCurl))
        {
            /*
             * Open the output file.
             */
            rc = RTFileOpen(&pThis->BodyOutput.uData.hFile, pszDstFile,
                            RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_READWRITE);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Perform the HTTP operation.
                 */
                rcCurl = curl_easy_perform(pThis->pCurl);
                rc = rtHttpGetCalcStatus(pThis, rcCurl, NULL);
                if (RT_SUCCESS(rc))
                     rc = pThis->rcOutput;

                int rc2 = RTFileClose(pThis->BodyOutput.uData.hFile);
                if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                    rc = rc2;
            }
            pThis->BodyOutput.uData.hFile = NIL_RTFILE;
        }
        else
            rc = VERR_HTTP_CURL_ERROR;
    }

    ASMAtomicWriteBool(&pThis->fBusy, false);
    return rc;
}


RTR3DECL(int) RTHttpQueryProxyInfoForUrl(RTHTTP hHttp, const char *pszUrl, PRTHTTPPROXYINFO pProxy)
{
    /*
     * Validate input and clear output.
     */
    Log(("RTHttpQueryProxyInfoForUrl: hHttp=%p pszUrl=%s pProxy=%s\n", hHttp, pszUrl, pProxy));
    RT_ZERO(*pProxy);
    pProxy->uProxyPort = UINT32_MAX;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    /*
     * Set up the proxy for the URL.
     */
    rtHttpResetState(pThis);
    /** @todo this does a bit too much (we don't need to set up SSL for instance). */
    int rc = rtHttpApplySettings(pThis, pszUrl);
    if (RT_SUCCESS(rc))
    {
        /*
         * Copy out the result.
         */
        if (pThis->fNoProxy)
            pProxy->enmProxyType = RTHTTPPROXYTYPE_NOPROXY;
        else
        {
            switch (pThis->enmProxyType)
            {
                case CURLPROXY_HTTP:
#ifdef CURL_AT_LEAST_VERSION
# if CURL_AT_LEAST_VERSION(7,19,4)
                case CURLPROXY_HTTP_1_0:
# endif
#endif
                    pProxy->enmProxyType = RTHTTPPROXYTYPE_HTTP;
                    break;
#ifdef CURL_AT_LEAST_VERSION
# if CURL_AT_LEAST_VERSION(7,52,0)
                case CURLPROXY_HTTPS:
                    pProxy->enmProxyType = RTHTTPPROXYTYPE_HTTPS;
                    break;
# endif
#endif
                case CURLPROXY_SOCKS4:
                case CURLPROXY_SOCKS4A:
                    pProxy->enmProxyType = RTHTTPPROXYTYPE_SOCKS4;
                    break;
                case CURLPROXY_SOCKS5:
                case CURLPROXY_SOCKS5_HOSTNAME:
                    pProxy->enmProxyType = RTHTTPPROXYTYPE_SOCKS5;
                    break;
                default:
                    AssertFailed();
                    pProxy->enmProxyType = RTHTTPPROXYTYPE_UNKNOWN;
                    break;
            }
            pProxy->uProxyPort = pThis->uProxyPort;
            if (pThis->pszProxyHost != NULL)
            {
                rc = RTStrDupEx(&pProxy->pszProxyHost, pThis->pszProxyHost);
                if (pThis->pszProxyUsername && RT_SUCCESS(rc))
                    rc = RTStrDupEx(&pProxy->pszProxyUsername, pThis->pszProxyUsername);
                if (pThis->pszProxyPassword && RT_SUCCESS(rc))
                    rc = RTStrDupEx(&pProxy->pszProxyPassword, pThis->pszProxyPassword);
                if (RT_FAILURE(rc))
                    RTHttpFreeProxyInfo(pProxy);
            }
            else
            {
                AssertFailed();
                rc = VERR_INTERNAL_ERROR;
            }
        }
    }
    return rc;
}


RTR3DECL(int) RTHttpFreeProxyInfo(PRTHTTPPROXYINFO pProxy)
{
    if (pProxy)
    {
        RTStrFree(pProxy->pszProxyHost);
        RTStrFree(pProxy->pszProxyUsername);
        RTStrFree(pProxy->pszProxyPassword);
        pProxy->pszProxyHost     = NULL;
        pProxy->pszProxyUsername = NULL;
        pProxy->pszProxyPassword = NULL;
        pProxy->enmProxyType     = RTHTTPPROXYTYPE_INVALID;
        pProxy->uProxyPort       = UINT32_MAX;
    }
    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpPerform(RTHTTP hHttp, const char *pszUrl, RTHTTPMETHOD enmMethod, void const *pvReqBody, size_t cbReqBody,
                            uint32_t *puHttpStatus, void **ppvHeaders, size_t *pcbHeaders, void **ppvBody, size_t *pcbBody)
{
    /*
     * Set safe return values and validate input.
     */
    Log(("RTHttpPerform: hHttp=%p pszUrl=%s enmMethod=%d pvReqBody=%p cbReqBody=%zu puHttpStatus=%p ppvHeaders=%p ppvBody=%p\n",
         hHttp, pszUrl, enmMethod, pvReqBody, cbReqBody, puHttpStatus, ppvHeaders, ppvBody));

    if (ppvHeaders)
        *ppvHeaders = NULL;
    if (pcbHeaders)
        *pcbHeaders = 0;
    if (ppvBody)
        *ppvBody = NULL;
    if (pcbBody)
        *pcbBody = 0;
    if (puHttpStatus)
        *puHttpStatus = UINT32_MAX;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(enmMethod > RTHTTPMETHOD_INVALID && enmMethod < RTHTTPMETHOD_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszUrl, VERR_INVALID_POINTER);

#ifdef LOG_ENABLED
    if (LogIs4Enabled() && pThis->pHeaders)
    {
        Log4(("RTHttpPerform: headers:\n"));
        for (struct curl_slist const *pCur = pThis->pHeaders; pCur; pCur = pCur->next)
            Log4(("%s\n", pCur->data));
    }
    if (pvReqBody && cbReqBody)
        Log5(("RTHttpPerform: request body:\n%.*Rhxd\n", cbReqBody, pvReqBody));
#endif

    /*
     * Set the busy flag (paranoia).
     */
    bool fBusy = ASMAtomicXchgBool(&pThis->fBusy, true);
    AssertReturn(!fBusy, VERR_WRONG_ORDER);

    /*
     * Reset the state and apply settings.
     */
    rtHttpResetState(pThis);
    int rc = rtHttpApplySettings(hHttp, pszUrl);
    if (RT_SUCCESS(rc))
    {
        /* Set the HTTP method. */
        CURLcode rcCurl = CURLE_BAD_FUNCTION_ARGUMENT;
        switch (enmMethod)
        {
            case RTHTTPMETHOD_GET:
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HTTPGET, 1L);
                break;
            case RTHTTPMETHOD_PUT:
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_PUT, 1L);
                break;
            case RTHTTPMETHOD_POST:
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_POST, 1L);
                break;
            case RTHTTPMETHOD_PATCH:
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_CUSTOMREQUEST, "PATCH");
                break;
            case RTHTTPMETHOD_DELETE:
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_CUSTOMREQUEST, "DELETE");
                break;
            case RTHTTPMETHOD_HEAD:
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HTTPGET, 1L);
                if (CURL_SUCCESS(rcCurl))
                    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_NOBODY, 1L);
                break;
            case RTHTTPMETHOD_OPTIONS:
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
                break;
            case RTHTTPMETHOD_TRACE:
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_CUSTOMREQUEST, "TRACE");
                break;
#ifdef IPRT_HTTP_WITH_WEBDAV
            case RTHTTPMETHOD_PROPFIND:
                RT_FALL_THROUGH();
#endif
            case RTHTTPMETHOD_END:
            case RTHTTPMETHOD_INVALID:
            case RTHTTPMETHOD_32BIT_HACK:
                AssertFailed();
        }

        /* Request body.  POST requests should always have a body. */
        if (   pvReqBody
            && CURL_SUCCESS(rcCurl)
            && (   cbReqBody > 0
                || enmMethod == RTHTTPMETHOD_POST) )
        {
            if (enmMethod == RTHTTPMETHOD_POST)
            {
                /** @todo ??? */
                rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_POSTFIELDSIZE, cbReqBody);
                if (CURL_SUCCESS(rcCurl))
                    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_POSTFIELDS, pvReqBody);
            }
            else
            {
                pThis->ReadData.Mem.pvMem  = pvReqBody;
                pThis->ReadData.Mem.cbMem  = cbReqBody;
                pThis->ReadData.Mem.offMem = 0;
                rcCurl = rtHttpSetReadCallback(pThis, rtHttpReadData, pThis);
                /* curl will use chunked transfer is it doesn't know the body size */
                if (enmMethod == RTHTTPMETHOD_PUT && CURL_SUCCESS(rcCurl))
                    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_INFILESIZE_LARGE, cbReqBody);
            }
        }
        else if (pThis->pfnUploadCallback && CURL_SUCCESS(rcCurl))
            rcCurl = rtHttpSetReadCallback(pThis, rtHttpReadDataFromUploadCallback, pThis);

        /* Headers. */
        if (CURL_SUCCESS(rcCurl))
        {
            RT_ZERO(pThis->HeadersOutput.uData.Mem);
            rcCurl = rtHttpSetHeaderCallback(pThis, rtHttpWriteHeaderData, pThis);
        }

        /* Body */
        if (ppvBody && CURL_SUCCESS(rcCurl))
        {
            RT_ZERO(pThis->BodyOutput.uData.Mem);
            rcCurl = rtHttpSetWriteCallback(pThis, rtHttpWriteBodyData, pThis);
        }
        else if (pThis->pfnDownloadCallback && CURL_SUCCESS(rcCurl))
            rcCurl = rtHttpSetWriteCallback(pThis, rtHttpWriteDataToDownloadCallback, pThis);

        if (CURL_SUCCESS(rcCurl))
        {
            /*
             * Perform the HTTP operation.
             */
            rcCurl = curl_easy_perform(pThis->pCurl);
            rc = rtHttpGetCalcStatus(pThis, rcCurl, puHttpStatus);
            if (RT_SUCCESS(rc))
                rc = pThis->rcOutput;
            if (RT_SUCCESS(rc))
            {
                if (ppvHeaders)
                {
                    Log(("RTHttpPerform: headers: %zx bytes (allocated %zx)\n",
                         pThis->HeadersOutput.uData.Mem.cb, pThis->HeadersOutput.uData.Mem.cbAllocated));
                    Log6(("RTHttpPerform: headers blob:\n%.*Rhxd\n", pThis->HeadersOutput.uData.Mem.cb, pThis->HeadersOutput.uData.Mem.pb));
                    *ppvHeaders = pThis->HeadersOutput.uData.Mem.pb;
                    *pcbHeaders = pThis->HeadersOutput.uData.Mem.cb;
                    pThis->HeadersOutput.uData.Mem.pb = NULL;
                }
                if (ppvBody)
                {
                    Log(("RTHttpPerform: body: %zx bytes (allocated %zx)\n",
                         pThis->BodyOutput.uData.Mem.cb, pThis->BodyOutput.uData.Mem.cbAllocated));
                    Log7(("RTHttpPerform: body blob:\n%.*Rhxd\n", pThis->BodyOutput.uData.Mem.cb, pThis->BodyOutput.uData.Mem.pb));
                    *ppvBody = pThis->BodyOutput.uData.Mem.pb;
                    *pcbBody = pThis->BodyOutput.uData.Mem.cb;
                    pThis->BodyOutput.uData.Mem.pb = NULL;
                }
            }
        }
        else
            rc = VERR_HTTP_CURL_ERROR;

        /* Ensure we've freed all unused output and dropped references to input memory.*/
        if (pThis->HeadersOutput.uData.Mem.pb)
            RTMemFree(pThis->HeadersOutput.uData.Mem.pb);
        if (pThis->BodyOutput.uData.Mem.pb)
            RTMemFree(pThis->BodyOutput.uData.Mem.pb);
        RT_ZERO(pThis->HeadersOutput.uData.Mem);
        RT_ZERO(pThis->BodyOutput.uData.Mem);
        RT_ZERO(pThis->ReadData);
    }

    ASMAtomicWriteBool(&pThis->fBusy, false);
    return rc;
}


/*********************************************************************************************************************************
*   Callback APIs.                                                                                                               *
*********************************************************************************************************************************/

RTR3DECL(int) RTHttpSetUploadCallback(RTHTTP hHttp, uint64_t cbContent, PFNRTHTTPUPLOADCALLBACK pfnCallback, void *pvUser)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    pThis->pfnUploadCallback        = pfnCallback;
    pThis->pvUploadCallbackUser     = pvUser;
    pThis->cbUploadContent          = cbContent;
    pThis->offUploadContent         = 0;

    if (cbContent != UINT64_MAX)
    {
        AssertCompile(sizeof(curl_off_t) == sizeof(uint64_t));
        CURLcode rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_INFILESIZE_LARGE, cbContent);
        AssertMsgReturn(CURL_SUCCESS(rcCurl), ("%d (%#x)\n", rcCurl, rcCurl), VERR_HTTP_CURL_ERROR);
    }
    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpSetDownloadCallback(RTHTTP hHttp, uint32_t fFlags, PFNRTHTTPDOWNLOADCALLBACK pfnCallback, void *pvUser)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertReturn(!pfnCallback || (fFlags & RTHTTPDOWNLOAD_F_ONLY_STATUS_MASK) != 0, VERR_INVALID_FLAGS);

    pThis->pfnDownloadCallback      = pfnCallback;
    pThis->pvDownloadCallbackUser   = pvUser;
    pThis->fDownloadCallback        = fFlags;
    pThis->uDownloadHttpStatus      = UINT32_MAX;
    pThis->cbDownloadContent        = UINT64_MAX;
    pThis->offDownloadContent       = 0;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpSetDownloadProgressCallback(RTHTTP hHttp, PFNRTHTTPDOWNLDPROGRCALLBACK pfnCallback, void *pvUser)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    pThis->pfnDownloadProgress    = pfnCallback;
    pThis->pvDownloadProgressUser = pvUser;
    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpSetHeaderCallback(RTHTTP hHttp, PFNRTHTTPHEADERCALLBACK pfnCallback, void *pvUser)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    pThis->pfnHeaderCallback    = pfnCallback;
    pThis->pvHeaderCallbackUser = pvUser;
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Temporary raw cURL stuff.  Will be gone before 6.0 is out!                                                                   *
*********************************************************************************************************************************/

RTR3DECL(int) RTHttpRawSetUrl(RTHTTP hHttp, const char *pszUrl)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    int rc = rtHttpConfigureProxyForUrl(pThis, pszUrl);
    if (RT_FAILURE(rc))
        return rc;

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_URL, pszUrl);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetGet(RTHTTP hHttp)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HTTPGET, 1L);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetHead(RTHTTP hHttp)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HTTPGET, 1L);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_NOBODY, 1L);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetPost(RTHTTP hHttp)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_POST, 1L);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetPut(RTHTTP hHttp)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_PUT, 1L);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetDelete(RTHTTP hHttp)
{
    /* curl doesn't provide an option for this */
    return RTHttpRawSetCustomRequest(hHttp, "DELETE");
}


RTR3DECL(int) RTHttpRawSetCustomRequest(RTHTTP hHttp, const char *pszVerb)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_CUSTOMREQUEST, pszVerb);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetPostFields(RTHTTP hHttp, const void *pv, size_t cb)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_POSTFIELDSIZE, cb);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_POSTFIELDS, pv);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}

RTR3DECL(int) RTHttpRawSetInfileSize(RTHTTP hHttp, RTFOFF cb)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_INFILESIZE_LARGE, cb);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetVerbose(RTHTTP hHttp, bool fValue)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_VERBOSE, fValue ? 1L : 0L);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetTimeout(RTHTTP hHttp, long sec)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_TIMEOUT, sec);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawPerform(RTHTTP hHttp)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    /*
     * XXX: Do this here for now as a stop-gap measure as
     * RTHttpReset() resets this (and proxy settings).
     */
    if (pThis->pszCaFile)
    {
        rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_CAINFO, pThis->pszCaFile);
        if (CURL_FAILURE(rcCurl))
            return VERR_HTTP_CURL_ERROR;
    }

    rcCurl = curl_easy_perform(pThis->pCurl);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawGetResponseCode(RTHTTP hHttp, long *plCode)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);
    AssertPtrReturn(plCode, VERR_INVALID_PARAMETER);

    rcCurl = curl_easy_getinfo(pThis->pCurl, CURLINFO_RESPONSE_CODE, plCode);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetReadCallback(RTHTTP hHttp, PFNRTHTTPREADCALLBACKRAW pfnRead, void *pvUser)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_READFUNCTION, pfnRead);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_READDATA, pvUser);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetWriteCallback(RTHTTP hHttp, PFNRTHTTPWRITECALLBACKRAW pfnWrite, void *pvUser)
{
    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    CURLcode rcCurl = rtHttpSetWriteCallback(pThis, pfnWrite, pvUser);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTHttpRawSetWriteHeaderCallback(RTHTTP hHttp, PFNRTHTTPWRITECALLBACKRAW pfnWrite, void *pvUser)
{
    CURLcode rcCurl;

    PRTHTTPINTERNAL pThis = hHttp;
    RTHTTP_VALID_RETURN(pThis);

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HEADERFUNCTION, pfnWrite);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    rcCurl = curl_easy_setopt(pThis->pCurl, CURLOPT_HEADERDATA, pvUser);
    if (CURL_FAILURE(rcCurl))
        return VERR_HTTP_CURL_ERROR;

    return VINF_SUCCESS;
}

