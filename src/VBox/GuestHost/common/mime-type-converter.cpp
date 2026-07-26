/** @file
 * Common code for mime-type data conversion.
 *
 * This code supposed to be shared between Shared Clipboard and
 * Drag-And-Drop services. The main purpose is to convert data into and
 * from VirtualBox internal representation and host/guest specific format.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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

#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/mem.h>
#include <iprt/log.h>

#include <VBox/GuestHost/mime-type-converter.h>

/** @todo r=bird: Only used with RTStrNCmp, where it is completely
 *        unnecessary and just a potential bug source.  The reason being that we
 *        we control one of the two strings, which limits the comparison to the
 *        length of it.
 *
 *        However, if you mean to do something like RTStrStartsWith,
 *        then RT_MIN(strlen(), VBOX_WAYLAND_MIME_TYPE_NAME_MAX) would be
 *        better.  It wouldn't be all, that good though. */
#define VBOX_WAYLAND_MIME_TYPE_NAME_MAX     (32)

/* Declaration of mime-type conversion helper function. */
typedef DECLCALLBACKTYPE(int, FNVBFMTCONVERTOR, (void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut));
typedef FNVBFMTCONVERTOR *PFNVBFMTCONVERTOR;

/**
 * A helper function that converts UTF-8 string into UTF-16.
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer which contains UTF-8 data.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain UTF-16 data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertUtf8ToUtf16(void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    int rc;
    size_t cwDst;
    void  *pvDst = NULL;

    rc = RTStrValidateEncodingEx((char *)pvBufIn, cbBufIn, 0);
    if (RT_SUCCESS(rc))
    {
        /** @todo r=bird: Wrong buffer pointer type. Just because you return void
         *        pointers, doesn't mean you should use that internally. */
        rc = ShClHlpConvUtf8LFToUtf16CRLF((const char *)pvBufIn, cbBufIn, (PRTUTF16 *)&pvDst, &cwDst);
        if (RT_SUCCESS(rc))
        {
            *ppvBufOut = pvDst;
            *pcbBufOut = (cwDst + 1) * sizeof(RTUTF16);
        }
        else
            LogRel(("Data Converter: unable to convert input UTF8 string into VBox format, rc=%Rrc\n", rc));
    }
    else
        LogRel(("Data Converter: unable to validate input UTF8 string, rc=%Rrc\n", rc));

    return rc;
}

/**
 * A helper function that converts UTF-16 string into UTF-8.
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer which contains UTF-16 data.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain UTF-8 data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertUtf16ToUtf8(void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    int rc;

    rc = RTUtf16ValidateEncodingEx((PCRTUTF16)pvBufIn, cbBufIn / sizeof(RTUTF16),
                                   RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    if (RT_SUCCESS(rc))
    {
        size_t chDst = 0;
        rc = ShClHlpUtf16LenUtf8((PCRTUTF16)pvBufIn, cbBufIn / sizeof(RTUTF16), &chDst);
        if (RT_SUCCESS(rc))
        {
            /* Add space for '\0'. */
            chDst++;

            char *pszDst = (char *)RTMemAllocZ(chDst);
            if (pszDst)
            {
                size_t cbActual = 0;
                rc = ShClHlpConvUtf16CRLFToUtf8LF((PCRTUTF16)pvBufIn, cbBufIn / sizeof(RTUTF16), pszDst, chDst, &cbActual);
                if (RT_SUCCESS(rc))
                {
                    *pcbBufOut = cbActual;
                    *ppvBufOut = pszDst;
                }
            }
            else
            {
                LogRel(("Data Converter: unable to allocate memory for UTF16 string conversion, rc=%Rrc\n", rc));
                rc = VERR_NO_MEMORY;
            }
        }
    }

    return rc;
}

/**
 * A helper function that converts Latin-1 string into UTF-16.
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer which contains Latin-1 data.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain UTF-16 data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertLatin1ToUtf16(void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    int rc;
    size_t cwDst;
    void *pvDst = NULL;

    /** @todo r=bird: Wrong buffer pointer type. Just because you return void
     *        pointers, doesn't mean you should use that internally. */
    rc = ShClHlpConvLatin1LFToUtf16CRLF((char *)pvBufIn, cbBufIn, (PRTUTF16 *)&pvDst, &cwDst);
    if (RT_SUCCESS(rc))
    {
        *ppvBufOut = pvDst;
        *pcbBufOut = (cwDst + 1) * sizeof(RTUTF16);
    }
    else
        LogRel(("Data Converter: unable to convert input Latin1 string into VBox format, rc=%Rrc\n", rc));

    return rc;
}

/**
 * A helper function that converts UTF-16 string into Latin-1.
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer which contains UTF-16 data.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain Latin-1 data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertUtf16ToLatin1(void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    int rc;

    /* Make RTUtf16ToLatin1ExTag() to allocate output buffer. */
    *ppvBufOut = NULL;

    rc = RTUtf16ValidateEncodingEx((PCRTUTF16)pvBufIn, cbBufIn / sizeof(RTUTF16),
                                   RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    if (RT_SUCCESS(rc))
        /** @todo r=bird: Inconsistent handing indent (the one above is preferred). */
        rc = RTUtf16ToLatin1ExTag(
            (PCRTUTF16)pvBufIn, cbBufIn / sizeof(RTUTF16), (char **)ppvBufOut, cbBufIn, pcbBufOut, RTSTR_TAG);

    return rc;
}

/**
 * A helper function that converts HTML data into internal VBox representation (UTF-8).
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer which contains HTML data.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain UTF-8 data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertHtmlToVBox(void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    /** @todo r=bird: What's with the C coding? This is C++, so declare variables
     *        where they are used.  Unnecessary initialization of 'rc' is just
     *        confusing (value is never used). */
    int rc = VERR_PARSE_ERROR;
    void *pvDst = NULL;
    size_t cbDst = 0;

    /* From X11 counterpart: */

    /*
     * The common VBox HTML encoding will be - UTF-8
     * because it more general for HTML formats then UTF-16
     * X11 clipboard returns UTF-16, so before sending it we should
     * convert it to UTF-8.
     *
     * Some applications sends data in UTF-16, some in UTF-8,
     * without indication it in MIME.
     *
     * In case of UTF-16, at least [Open|Libre] Office adds an byte order mark (0xfeff)
     * at the start of the clipboard data.
     */

    if (   cbBufIn >= (int)sizeof(RTUTF16)
        && *(PRTUTF16)pvBufIn == VBOX_SHCL_UTF16LEMARKER)
    {
        /* Input buffer is expected to be UTF-16 encoded. */
        LogRel(("Data Converter: unable to convert UTF-16 encoded HTML data into VBox format\n"));

        rc = RTUtf16ValidateEncodingEx((PCRTUTF16)pvBufIn, cbBufIn / sizeof(RTUTF16),
                                       RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        if (RT_SUCCESS(rc))
        {
            rc = ShClHlpConvUtf16ToUtf8HTML((PRTUTF16)pvBufIn, cbBufIn / sizeof(RTUTF16), (char**)&pvDst, &cbDst);
            if (RT_SUCCESS(rc))
            {
                *ppvBufOut = pvDst;
                *pcbBufOut = cbDst;
            }
            else
                LogRel(("Data Converter: unable to convert input UTF16 string into VBox format, rc=%Rrc\n", rc));
        }
        else
            LogRel(("Data Converter: unable to validate input UTF8 string, rc=%Rrc\n", rc));
    }
    else
    {
        LogRel(("Data Converter: converting UTF-8 encoded HTML data into VBox format\n"));

        /* Input buffer is expected to be UTF-8 encoded. */
        rc = RTStrValidateEncodingEx((char *)pvBufIn, cbBufIn, 0);
        if (RT_SUCCESS(rc))
        {
            pvDst = RTMemAllocZ(cbBufIn + 1 /* '\0' */);
            if (pvDst)
            {
                memcpy(pvDst, pvBufIn, cbBufIn);
                *ppvBufOut = pvDst;
                *pcbBufOut = cbBufIn + 1 /* '\0' */;
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }

    return rc;
}

/**
 * A helper function that validates HTML data in UTF-8 format and passes out a duplicated buffer.
 *
 * This function supposed to convert HTML data from internal VBox representation (UTF-8)
 * into what will be accepted by X11/Wayland clients. However, since we paste HTML content
 * in UTF-8 representation, there is nothing to do with conversion. We only validate buffer here.
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer which contains HTML data.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain UTF-8 data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertVBoxToHtml(void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    int rc;

    rc = RTStrValidateEncodingEx((char *)pvBufIn, cbBufIn, 0);
    if (RT_SUCCESS(rc))
    {
        void *pvBuf = RTMemAllocZ(cbBufIn);
        if (pvBuf)
        {
            memcpy(pvBuf, pvBufIn, cbBufIn);
            *ppvBufOut = pvBuf;
            *pcbBufOut = cbBufIn;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}

/**
 * A helper function that converts BMP image data into internal VBox representation.
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer which contains BMP image data.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain image data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertBmpToVBox(void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    int rc;
    const void *pvBufOutTmp = NULL;
    size_t cbBufOutTmp = 0;

    rc = ShClHlpBmpGetDib(pvBufIn, cbBufIn, &pvBufOutTmp, &cbBufOutTmp);
    if (RT_SUCCESS(rc))
    {
        void *pvBuf = RTMemAllocZ(cbBufOutTmp);
        if (pvBuf)
        {
            memcpy(pvBuf, pvBufOutTmp, cbBufOutTmp);
            *ppvBufOut = pvBuf;
            *pcbBufOut = cbBufOutTmp;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        LogRel(("Data Converter: unable to convert image data (%u bytes) into BMP format, rc=%Rrc\n", cbBufIn, rc));

    return rc;
}

/**
 * A helper function that converts image data from internal VBox representation into BMP.
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer which contains image data in VBox format.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain BMP image data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertVBoxToBmp(void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    return ShClHlpDibToBmp(pvBufIn, cbBufIn, ppvBufOut, pcbBufOut);
}

/**
 * This table represents MIME types cache and contains its
 * content converted into VirtualBox internal representation.
 */
static struct VBCONVERTERFMTTABLE
{
    /** Content mime-type as reported by X11/Wayland. */
    const char             *pcszMimeType;
    /** VBox content type representation. */
    SHCLFORMAT              uFmtVBox;
    /** The priority of MIME types mapping to the same SHCLFORMAT and flags.
     * Higher value means higher priority. Range is range 0 thru 15.
     * @note This assumes that we can use one common priority for all
     *       clipboard/toolkit implementations. Should we end up with different
     *       preferences, we'd have to partition it.
     * @todo We would also use part of this for flags... */
    uint32_t                fFlagsAndPriority;
    /** Function converting from X11/Wayland to VirtualBox clipboard data format. */
    PFNVBFMTCONVERTOR       pfnConvertToVBox;
    /** Function converting from VirtualBox to X11/Wayland clipboard data format. */
    PFNVBFMTCONVERTOR       pfnConvertFromVBox;
} const g_aConverterFormats[] =
{
    { "INVALID",                      VBOX_SHCL_FMT_NONE,                               0, NULL,                   NULL                   },

    { "UTF8_STRING",                  VBOX_SHCL_FMT_UNICODETEXT,                       14, vbConvertUtf8ToUtf16,   vbConvertUtf16ToUtf8   },
    { "text/plain;charset=utf-8",     VBOX_SHCL_FMT_UNICODETEXT,                       12, vbConvertUtf8ToUtf16,   vbConvertUtf16ToUtf8   },
    { "text/plain;charset=UTF-8",     VBOX_SHCL_FMT_UNICODETEXT, VBGH_MIME_CONV_F_RO | 11, vbConvertUtf8ToUtf16,   vbConvertUtf16ToUtf8   }, /** @todo who puts this on the clipboard? */
    { "STRING",                       VBOX_SHCL_FMT_UNICODETEXT,                        3, vbConvertLatin1ToUtf16, vbConvertUtf16ToLatin1 },
    { "TEXT",                         VBOX_SHCL_FMT_UNICODETEXT,                        2, vbConvertLatin1ToUtf16, vbConvertUtf16ToLatin1 },
    { "text/plain",                   VBOX_SHCL_FMT_UNICODETEXT,                        1, vbConvertLatin1ToUtf16, vbConvertUtf16ToLatin1 },

    { "text/html;charset=utf-8",      VBOX_SHCL_FMT_HTML,                              14, vbConvertHtmlToVBox,    vbConvertVBoxToHtml    },
    { "text/html",                    VBOX_SHCL_FMT_HTML,                              12, vbConvertHtmlToVBox,    vbConvertVBoxToHtml    },
    /** @todo r=bird: application/x-moz-nativehtml (kNativeHTMLMime) is Windows
     * CF_HTML, see ShClWinConvertCFHTMLToMIME() and ShClWinConvertMIMEToCFHTML for
     * how to properly convert it. For reference:
     * https://github.com/mozilla-firefox/firefox/blob/2dad02d1765ec525589c574612ecad90a714a5bb/editor/libeditor/HTMLEditorDataTransfer.cpp#L2175
     */
    { "application/x-moz-nativehtml", VBOX_SHCL_FMT_HTML,         VBGH_MIME_CONV_F_RO | 4, vbConvertHtmlToVBox,    vbConvertVBoxToHtml    }, /** @todo what's the format here actually? */

    { "image/bmp",                    VBOX_SHCL_FMT_BITMAP,                             1, vbConvertBmpToVBox,     vbConvertVBoxToBmp     },
    { "image/x-bmp",                  VBOX_SHCL_FMT_BITMAP,                             1, vbConvertBmpToVBox,     vbConvertVBoxToBmp     },
    { "image/x-MS-bmp",               VBOX_SHCL_FMT_BITMAP,                             1, vbConvertBmpToVBox,     vbConvertVBoxToBmp     },
};

/**
 * Enumerate list of MIME types by ID mask.
 *
 * This function goes through the list of supported MIME types and
 * triggers given callback function for each of them.
 *
 * @param   fVBoxFmts       One or more VBOX_SHCL_FMT_XXX values ORed together.
 * @param   pfnCallback     Callback function.
 * @param   pvUser          User data.
 */
VBGH_DECL(void) VbghMimeConvEnumerateByVBoxFormats(SHCLFORMATS fVBoxFmts, PFNVBGHMIMECONVENUM pfnCallback, void *pvUser)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aConverterFormats); i++)
        if (g_aConverterFormats[i].uFmtVBox & fVBoxFmts)
            pfnCallback(g_aConverterFormats[i].pcszMimeType, g_aConverterFormats[i].fFlagsAndPriority, pvUser);
}

/**
 * Find VBox format for the given MIME type.
 *
 * @returns VBox format. VBOX_SHCL_FMT_NONE if no translation found.
 * @param   pcszMimeType            MIME type to convert.
 * @param   pfFlagsAndPriority      The priority and flags (VBGH_MIME_CONV_F_XXX).
 *                                  Optional.
 * @param   ppszPersistentMimeType  Where to return a persisten, readonly, MIME
 *                                  type string upon a successful mapping.
 *                                  Optional.
 */
VBGH_DECL(SHCLFORMAT) VbghMimeConvGetVBoxFormatByMime(const char *pcszMimeType, uint32_t *pfFlagsAndPriority,
                                                      const char **ppszPersistentMimeType)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aConverterFormats); i++)
        if (RTStrNCmp(g_aConverterFormats[i].pcszMimeType, pcszMimeType, VBOX_WAYLAND_MIME_TYPE_NAME_MAX) == 0)
        {
            if (pfFlagsAndPriority)
                *pfFlagsAndPriority = g_aConverterFormats[i].fFlagsAndPriority;
            if (ppszPersistentMimeType)
                *ppszPersistentMimeType = g_aConverterFormats[i].pcszMimeType; /* Kind of ASSUMES exact match! */
            return g_aConverterFormats[i].uFmtVBox;
        }

    if (pfFlagsAndPriority)
        *pfFlagsAndPriority = 0;
    if (ppszPersistentMimeType)
        *ppszPersistentMimeType = NULL;
    return VBOX_SHCL_FMT_NONE;
}

/**
 * Converts from VirtualBox to X11/Wayland clipboard data format.
 *
 * @returns IPRT status code.
 * @param   pcszMimeType    Target MIME type.
 * @param   pvBufIn         Input buffer which contains data in VBox format.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain data
 *                          in specified MIME type format (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
VBGH_DECL(int) VbghMimeConvFromVBox(const char *pcszMimeType, void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aConverterFormats); i++)
        if (RTStrNCmp(g_aConverterFormats[i].pcszMimeType, pcszMimeType, VBOX_WAYLAND_MIME_TYPE_NAME_MAX) == 0)
            return g_aConverterFormats[i].pfnConvertFromVBox(pvBufIn, cbBufIn, ppvBufOut, pcbBufOut);

    return VERR_NOT_FOUND;
}

/**
 * Converts data from native format into VBox internal representation.
 *
 * @returns IPRT status code.
 * @param   pcszMimeType    Source MIME type.
 * @param   pvBufIn         Input buffer which contains data in specified MIME type format.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain image data
 *                          in VBox internal representation format (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
VBGH_DECL(int) VbghMimeConvToVBox(const char *pcszMimeType, void *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aConverterFormats); i++)
        if (RTStrNCmp(g_aConverterFormats[i].pcszMimeType, pcszMimeType, VBOX_WAYLAND_MIME_TYPE_NAME_MAX) == 0)
            return g_aConverterFormats[i].pfnConvertToVBox(pvBufIn, cbBufIn, ppvBufOut, pcbBufOut);

    return VERR_NOT_FOUND;
}


/**
 * Frees a buffer returned by VbghMimeConvFromVBox or VbghMimeConvToVBox.
 */
VBGH_DECL(void) VbghMimeConvFreeBuf(void *pvBuf, size_t cbBuf)
{
    ShClHlpFreeBuf(pvBuf, cbBuf);
}


/*********************************************************************************************************************************
*   Cached Conversion                                                                                                            *
*********************************************************************************************************************************/
#if 0 /* unused */

/** Mime-type cache instance. */
typedef struct VBGHMIMECONVCACHEINT
{
    /** Cache lock (serves as magic). */
    RTCRITSECT  CritSect;

    /** Cache entries running parallel to g_aConverterFormats. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    struct
    {
        /** A buffer which contains mime-type data cache in VirtualBox
         *  internal representation. */
        void   *pvBuf;
        /** Size of cached data. */
        size_t  cbBuf;
    } aEntries[RT_ELEMENTS(g_aConverterFormats)];
} VBGHMIMECONVCACHEINT;

VBGH_DECL(int) VbghMimeConvCacheCreate(PVBGHMIMECONVCACHE phCache)
{
    AssertPtrReturn(phCache, VERR_INVALID_PARAMETER);

    int rc;
    VBGHMIMECONVCACHEINT *pThis = (VBGHMIMECONVCACHEINT *)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
        rc = RTCritSectInit(&pThis->CritSect);
    else
        rc = VERR_NO_MEMORY;
    *phCache = RT_SUCCESS(rc) ? pThis : NIL_VBGHMIMECONVCACHE;
    return rc;
}

/** Helper for validating a cache handle.   */
#define VBGH_MIME_CONV_CACHE_VALIDATE_RETURN(a_hCache) do { \
        AssertPtrReturn(a_hCache, VERR_INVALID_HANDLE); \
        AssertReturn(RTCritSectIsInitialized(&(a_hCache)->CritSect), VERR_INVALID_HANDLE); \
    } while (0)

/**
 * Helper for clearing the cache.
 */
static void vbghMimeConvCacheClearLocked(VBGHMIMECONVCACHEINT *pThis)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aConverterFormats); i++)
    {
        pThis->aEntries[i].pvBuf = NULL;
        pThis->aEntries[i].cbBuf = 0;
    }
}

VBGH_DECL(int) VbghMimeConvCacheDestroy(VBGHMIMECONVCACHE hCache)
{
    if (hCache == NIL_VBGHMIMECONVCACHE)
        return VINF_SUCCESS;

    VBGHMIMECONVCACHEINT * const pThis = hCache;
    VBGH_MIME_CONV_CACHE_VALIDATE_RETURN(pThis);

    int rc = RTCritSectDelete(&pThis->CritSect);
    AssertRCReturn(rc, rc);
    vbghMimeConvCacheClearLocked(pThis);
    RTMemFree(pThis);

    return VINF_SUCCESS;
}

VBGH_DECL(int) VbghMimeConvCacheClear(VBGHMIMECONVCACHE hCache)
{
    VBGHMIMECONVCACHEINT * const pThis = hCache;
    VBGH_MIME_CONV_CACHE_VALIDATE_RETURN(pThis);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        vbghMimeConvCacheClearLocked(pThis);
        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;
}

/**
 * Helper for looking up the index of a MIME type.
 */
static unsigned vbghMimeConvLookupType(const char *pcszMimeType)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aConverterFormats); i++)
        if (RTStrNCmp(g_aConverterFormats[i].pcszMimeType, pcszMimeType, VBOX_WAYLAND_MIME_TYPE_NAME_MAX) == 0)
            return i;
    return ~0U;
}

VBGH_DECL(int) VbghMimeConvCacheSetByMime(VBGHMIMECONVCACHE hCache, const char *pcszMimeType, void *pvBuf, int cbBuf)
{
    /*
     * Validate input.
     */
    VBGHMIMECONVCACHEINT * const pThis = hCache;
    VBGH_MIME_CONV_CACHE_VALIDATE_RETURN(pThis);
    AssertPtr(pcszMimeType);
    AssertPtrReturn(pvBuf, VERR_INVALID_PARAMETER);
    AssertReturn(cbBuf > 0, VERR_INVALID_PARAMETER);

    /*
     * Lookup, lock and set.
     */
    int rc;
    unsigned const idx = vbghMimeConvLookupType(pcszMimeType);
    if (idx < RT_ELEMENTS(g_aConverterFormats))
    {
        rc = RTCritSectEnter(&pThis->CritSect);
        if (RT_SUCCESS(rc))
        {
            pThis->aEntries[idx].pvBuf = pvBuf;
            pThis->aEntries[idx].cbBuf = cbBuf;
            RTCritSectLeave(&pThis->CritSect);
            rc = VINF_SUCCESS; /* paranoia */
        }
    }
    else
        rc = VERR_NOT_FOUND;
    return rc;
}

VBGH_DECL(int) VbghMimeConvCacheGetByMime(VBGHMIMECONVCACHE hCache, const char *pcszMimeType, void **ppvBufOut, size_t *pcbBufOut)
{
    /*
     * Validate input.
     */
    VBGHMIMECONVCACHEINT * const pThis = hCache;
    VBGH_MIME_CONV_CACHE_VALIDATE_RETURN(pThis);
    AssertPtr(pcszMimeType);
    AssertPtr(ppvBufOut);
    AssertPtr(pcbBufOut);
    *ppvBufOut = NULL;
    *pcbBufOut = 0;

    /*
     * Lookup, lock and set.
     */
    int rc;
    unsigned const idx = vbghMimeConvLookupType(pcszMimeType);
    if (idx < RT_ELEMENTS(g_aConverterFormats))
    {
        rc = RTCritSectEnter(&pThis->CritSect);
        if (RT_SUCCESS(rc))
        {
            if (   pThis->aEntries[idx].pvBuf != NULL
                && pThis->aEntries[idx].cbBuf > 0)
            {
                *ppvBufOut = pThis->aEntries[idx].pvBuf;
                *pcbBufOut = pThis->aEntries[idx].cbBuf;
                rc = VINF_SUCCESS; /* paranoia */
            }
            else
                rc = VERR_NOT_FOUND;
            RTCritSectLeave(&pThis->CritSect);
        }
    }
    else
        rc = VERR_NOT_FOUND;
    return rc;
}

VBGH_DECL(int) VbghMimeConvCacheGetByVBoxFormat(VBGHMIMECONVCACHE hCache, const SHCLFORMAT uFmtVBox,
                                                void **ppvBufOut, size_t *pcbBufOut)
{
    /*
     * Validate input.
     */
    VBGHMIMECONVCACHEINT * const pThis = hCache;
    VBGH_MIME_CONV_CACHE_VALIDATE_RETURN(pThis);
    AssertPtr(ppvBufOut);
    AssertPtr(pcbBufOut);
    *ppvBufOut = NULL;
    *pcbBufOut = 0;

    /*
     * Lock the cache and pick the highest priority MIME type with data in cache.
     */
    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        uint32_t uBestMatch = 0;
        rc = VERR_NOT_FOUND;

        for (unsigned idx = 0; idx < RT_ELEMENTS(g_aConverterFormats); idx++)
            if (g_aConverterFormats[idx].uFmtVBox == uFmtVBox)
                if ((g_aConverterFormats[idx].fFlagsAndPriority & VBGH_MIME_CONV_F_PRIORITY_MASK) > uBestMatch)
                    if (   pThis->aEntries[idx].pvBuf != NULL
                        && pThis->aEntries[idx].cbBuf > 0)
                    {
                        *ppvBufOut = pThis->aEntries[idx].pvBuf;
                        *pcbBufOut = pThis->aEntries[idx].cbBuf;
                        rc = VINF_SUCCESS;
                    }

        RTCritSectLeave(&pThis->CritSect);
    }

    return rc;
}

#endif /* unused */
