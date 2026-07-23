/* $Id: mime-type-converter.cpp 114760 2026-07-23 13:37:13Z knut.osmundsen@oracle.com $ */
/** @file
 * Common code for mime-type data conversion.
 *
 * This code supposed to be shared between Shared Clipboard and Drag-And-Drop
 * services. The main purpose is to convert data into and from VirtualBox
 * internal clipboard representation and host/guest specific format.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
#include <VBox/GuestHost/mime-type-converter.h>
#include <VBox/GuestHost/clipboard-helper.h>

#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/utf16.h>
#include <iprt/mem.h>
#include <iprt/log.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @todo r=bird: Only used with RTStrNCmp, where it is completely
 *        unnecessary and just a potential bug source.  The reason being that we
 *        we control one of the two strings, which limits the comparison to the
 *        length of it.
 *
 *        However, if you mean to do something like RTStrStartsWith,
 *        then RT_MIN(strlen(), VBOX_WAYLAND_MIME_TYPE_NAME_MAX) would be
 *        better.  It wouldn't be all, that good though. */
#define VBOX_WAYLAND_MIME_TYPE_NAME_MAX     (32)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Generic data converter function.
 *
 * @returns IPRT status code.
 * @param   pvBufIn     Input data buffer.
 * @param   cbBufIn     The size of input data.
 * @param   ppvBufOut   Where to return pointer to the converted data on
 *                      success.  This must be freed by the caller.
 * @param   pcbBufOut   Where to return the size of the converted data on success.
 */
typedef DECLCALLBACKTYPE(int, FNVBFMTCONVERTOR, (void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut));
/** Pointer to a generic data converter function. */
typedef FNVBFMTCONVERTOR *PFNVBFMTCONVERTOR;



/**
 * @callback_method_impl{FNVBFMTCONVERTOR,
 *  A helper function that converts an UTF-8 (LF) string into VBox UTF-16 (CRLF).}
 *
 * @note    Blindly ASSUMES input is using LF as EOL, not CRLF (like the output).
 */
static DECLCALLBACK(int) vbConvertUtf8ToVBox(void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    int rc = RTStrValidateEncodingEx((char const *)pvBufIn, cbBufIn, 0);
    if (RT_SUCCESS(rc))
    {
        size_t   cwcDst  = 0;
        PRTUTF16 pwszDst = NULL;
        rc = ShClHlpConvUtf8LFToUtf16CRLF((const char *)pvBufIn, cbBufIn, &pwszDst, &cwcDst);
        if (RT_SUCCESS(rc))
        {
            *ppvBufOut = pwszDst;
            *pcbBufOut = (cwcDst + 1) * sizeof(RTUTF16); /* (The terminator is included for VBox string data.) */
        }
        else
            LogRel(("Data Converter: unable to convert input UTF8 string into VBox format, rc=%Rrc\n", rc));
    }
    else
        LogRel(("Data Converter: unable to validate input UTF8 string, rc=%Rrc\n", rc));

    return rc;
}

/**
 * @callback_method_impl{FNVBFMTCONVERTOR,
 *  A helper function that converts VBox UTF-16 (CRLF) string into UTF-8 (LF)}
 *
 * @note The returned data size excludes the string terminator.
 */
static DECLCALLBACK(int) vbConvertUtf8FromVBox(void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    PCRTUTF16 const pwszBufIn = (PCRTUTF16)pvBufIn;
    size_t const    cwcBufIn  = cbBufIn / sizeof(RTUTF16);
    int rc = RTUtf16ValidateEncodingEx(pwszBufIn, cwcBufIn,
                                       RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    if (RT_SUCCESS(rc))
    {
        size_t cbDst = 0;
        rc = ShClHlpUtf16LenUtf8(pwszBufIn, cwcBufIn, &cbDst);
        if (RT_SUCCESS(rc))
        {
            cbDst++; /* Add space for '\0'. */

            char *pszDst = (char *)RTMemAllocZ(cbDst);
            if (pszDst)
            {
                size_t cbActualSansTerm = 0;
                rc = ShClHlpConvUtf16CRLFToUtf8LF(pwszBufIn, cwcBufIn, pszDst, cbDst, &cbActualSansTerm);
                if (RT_SUCCESS(rc))
                {
                    *pcbBufOut = cbActualSansTerm;
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
 * @callback_method_impl{FNVBFMTCONVERTOR,
 *  A helper function that converts an Latin-1 (LF) string into VBox UTF-16 (CRLF).}
 *
 * @note    Blindly ASSUMES input is using LF as EOL, not CRLF (like the output).
 */
static DECLCALLBACK(int) vbConvertLatin1ToVBox(void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    size_t   cwcDst  = 0;
    PRTUTF16 pswzDst = NULL;
    int rc = ShClHlpConvLatin1LFToUtf16CRLF((const char *)pvBufIn, cbBufIn, &pswzDst, &cwcDst);
    if (RT_SUCCESS(rc))
    {
        *ppvBufOut = pswzDst;
        *pcbBufOut = (cwcDst + 1) * sizeof(RTUTF16);  /* (The terminator is included for VBox string data.) */
    }
    else
        LogRel(("vbConvertLatin1ToVBox: ShClHlpConvLatin1LFToUtf16CRLF failed with rc=%Rrc\n", rc));

    return rc;
}

/**
 * @callback_method_impl{FNVBFMTCONVERTOR,
 *  A helper function that converts VBox UTF-16 (CRLF) string into Latin-1 (LF)}
 *
 * @note The returned data size excludes the string terminator.
 */
static DECLCALLBACK(int) vbConvertLatin1FromVBox(void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    PCRTUTF16 const pwszBufIn = (PCRTUTF16)pvBufIn;
    size_t const    cwcBufIn  = cbBufIn / sizeof(RTUTF16);
    int rc = RTUtf16ValidateEncodingEx(pwszBufIn, cwcBufIn,
                                       RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    if (RT_SUCCESS(rc))
    {
        /*
         * Manual conversion to latin-1, with stuff that cannot be encoded escaped '\uxxxxx' style.
         */
        /* 1. calculate the output size. */
        size_t cchLatin1 = 0;
        RTUNICP   uc;
        PCRTUTF16 pwsz = pwszBufIn;
        while ((uc = RTUtf16GetCp(pwsz)) != 0)
            if (uc < 0x100)
            {
                cchLatin1++;
                pwsz++;
                if (uc == '\r' && *pwsz == '\n')
                    pwsz++;
            }
            else
            {
                AssertReturn(uc != RTUNICP_INVALID, VERR_INVALID_UTF16_ENCODING);
                cchLatin1 += sizeof("\\uxxxx") - 1;
                if (uc > 0xffff)
                {
                    cchLatin1 += 1;
                    if (uc > 0xfffff)
                    {
                        cchLatin1 += 1;
                        Assert(uc <= 0x10ffff);
                    }
                }
                pwsz = RTUtf16NextCp(pwsz);
            }

        /* 2. allocate the output buffer. */
        char * const pszDst = (char *)RTMemAllocZ(cchLatin1 + 1);
        AssertReturn(pszDst, VERR_NO_MEMORY);

        /* 3. do the actual conversion. */
        rc = VINF_SUCCESS;
        pwsz = pwszBufIn;
        size_t offDst = 0;
        while ((uc = RTUtf16GetCp(pwsz)) != 0)
            if (uc < 0x100)
            {
                AssertBreakStmt(offDst < cchLatin1, rc = VINF_BUFFER_OVERFLOW);
                if (uc != '\r' || pwsz[1] != '\n')
                {
                    pszDst[offDst++] = (char)uc;
                    pwsz++;
                }
                else
                {
                    pszDst[offDst++] = '\n';
                    pwsz += 2;
                }
            }
            else
            {
                AssertReturnStmt(uc != RTUNICP_INVALID, RTMemFree(pszDst), VERR_INVALID_UTF16_ENCODING);
                Assert(uc <= 0x10ffff);
                AssertBreakStmt(offDst + (uc <= 0xffff ? 6 : uc <= 0xfffff ? 6+1 : 6+2) <= cchLatin1, VERR_BUFFER_OVERFLOW);

                pszDst[offDst++] = '\\';
                pszDst[offDst++] = 'u';
                static char const s_szDigits[] = "0123456789abcdef";
                if ((uc >> 20) & 0xf)
                    pszDst[offDst++] = s_szDigits[(uc >> 20) & 0xf];
                if ((uc >> 16) & 0xff)
                    pszDst[offDst++] = s_szDigits[(uc >> 16) & 0xf];
                pszDst[offDst++] = s_szDigits[(uc >> 12) & 0xf];
                pszDst[offDst++] = s_szDigits[(uc >>  8) & 0xf];
                pszDst[offDst++] = s_szDigits[(uc >>  4) & 0xf];
                pszDst[offDst++] = s_szDigits[ uc        & 0xf];

                pwsz = RTUtf16NextCp(pwsz);
                rc = VWRN_NO_TRANSLATION;
            }
        pszDst[offDst] = '\0';

        /* 4. set the return values. */
        *ppvBufOut = pszDst;
        *pcbBufOut = offDst;
    }

    return rc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * A helper function that validates and copies a UTF-8 URI list unchanged.
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer containing URI-list data.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain URI-list data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertUriListCopy(void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    AssertPtrReturn(ppvBufOut, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbBufOut, VERR_INVALID_POINTER);
    AssertReturn(cbBufIn >= 0, VERR_INVALID_PARAMETER);
    AssertReturn(pvBufIn || cbBufIn == 0, VERR_INVALID_POINTER);

    *ppvBufOut = NULL;
    *pcbBufOut = 0;

    /** @todo r=bird: The issue of whether the zero terminator is part of
     * cbBufIn/pcbBufOut or not is not mentioned anywhere... Iff
     * VBOX_SHCL_FMT_URI_LIST follows the same rules as VBOX_SHCL_FMT_UNICODETEXT,
     * then we can assume that VBox will include it.  The native clipboard
     * text/uri-list data, though, should probably not include it.
     *
     * The code in ShClTransferRootsSetFromStringListEx indicates that VBox expects
     * the zero terminator to be included in the output length.
     *
     * The GNOME file manager uses CRLF in it's text/uri-list, so we probably don't
     * need to pay too much attention CRLF vs LF here. No zero terminator, though.
     * Same for KDE in kubuntu 26.04.
     */
    int rc = cbBufIn ? RTStrValidateEncodingEx((char *)pvBufIn, cbBufIn, 0) : VINF_SUCCESS;
    if (RT_SUCCESS(rc))
    {
        char *pszDst = (char *)RTMemAllocZ((size_t)cbBufIn + 1);
        if (pszDst)
        {
            if (cbBufIn)
                memcpy(pszDst, pvBufIn, cbBufIn);
            *ppvBufOut = pszDst;
            *pcbBufOut = (size_t)cbBufIn;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        LogRel(("Data Converter: unable to validate URI-list data, rc=%Rrc\n", rc));

    return rc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/**
 * A helper function that converts HTML data into internal VBox representation (UTF-8).
 *
 * @returns IPRT status code.
 * @param   pvBufIn         Input buffer which contains HTML data.
 * @param   cbBufIn         Size of input buffer in bytes.
 * @param   ppvBufOut       Newly allocated output buffer which will contain UTF-8 data (must be freed by caller).
 * @param   pcbBufOut       Size of output buffer.
 */
static DECLCALLBACK(int) vbConvertHtmlToVBox(void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
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
     * because it more general for HTML formats than UTF-16
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
            /** @todo r=bird: This adds a terminating zero bytes even if one is already
             *        there... */
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
static DECLCALLBACK(int) vbConvertHtmlFromVBox(void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    int rc = RTStrValidateEncodingEx((char *)pvBufIn, cbBufIn, 0);
    if (RT_SUCCESS(rc))
    {
        void *pvBuf = RTMemAllocZ(cbBufIn);
        if (pvBuf)
        {
            /** @todo r=bird: cbBufIn typically includes the teminating string zero,
             *        whereas wayland clipboard protocols does not (normally) include it in
             *        the transfers... So, exclude it from *pcbBufOut? */
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
static DECLCALLBACK(int) vbConvertBmpToVBox(void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
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
static DECLCALLBACK(int) vbConvertBmpFromVBox(void const *pvBufIn, int cbBufIn, void **ppvBufOut, size_t *pcbBufOut)
{
    return ShClHlpDibToBmp(pvBufIn, cbBufIn, ppvBufOut, pcbBufOut);
}

/**
 * This table represents MIME types cache and contains its
 * content converted into VirtualBox internal clipboard representation.
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
    { "INVALID",                      VBOX_SHCL_FMT_NONE,                               0, NULL,                    NULL                    },

    { "UTF8_STRING",                  VBOX_SHCL_FMT_UNICODETEXT,                       14, vbConvertUtf8ToVBox,     vbConvertUtf8FromVBox   },
    { "text/plain;charset=utf-8",     VBOX_SHCL_FMT_UNICODETEXT,                       12, vbConvertUtf8ToVBox,     vbConvertUtf8FromVBox   },
    { "text/plain;charset=UTF-8",     VBOX_SHCL_FMT_UNICODETEXT, VBGH_MIME_CONV_F_RO | 11, vbConvertUtf8ToVBox,     vbConvertUtf8FromVBox   },
    { "STRING",                       VBOX_SHCL_FMT_UNICODETEXT,                        3, vbConvertLatin1ToVBox,   vbConvertLatin1FromVBox },
    { "TEXT",                         VBOX_SHCL_FMT_UNICODETEXT,                        2, vbConvertLatin1ToVBox,   vbConvertLatin1FromVBox },
    { "text/plain",                   VBOX_SHCL_FMT_UNICODETEXT,                        1, vbConvertLatin1ToVBox,   vbConvertLatin1FromVBox },

    { "text/html;charset=utf-8",      VBOX_SHCL_FMT_HTML,                              14, vbConvertHtmlToVBox,     vbConvertHtmlFromVBox   },
    { "text/html",                    VBOX_SHCL_FMT_HTML,                              12, vbConvertHtmlToVBox,     vbConvertHtmlFromVBox   },
    /** @todo r=bird: application/x-moz-nativehtml (kNativeHTMLMime) is Windows
     * CF_HTML, see ShClWinConvertCFHTMLToMIME() and ShClWinConvertMIMEToCFHTML for
     * how to properly convert it. For reference:
     * https://github.com/mozilla-firefox/firefox/blob/2dad02d1765ec525589c574612ecad90a714a5bb/editor/libeditor/HTMLEditorDataTransfer.cpp#L2175
     */
    { "application/x-moz-nativehtml", VBOX_SHCL_FMT_HTML,         VBGH_MIME_CONV_F_RO | 4, vbConvertHtmlToVBox,     vbConvertHtmlFromVBox   }, /** @todo what's the format here actually? */
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    { "text/uri-list",                VBOX_SHCL_FMT_URI_LIST,                          10, vbConvertUriListCopy,    vbConvertUriListCopy    },
#endif

    { "image/bmp",                    VBOX_SHCL_FMT_BITMAP,                             1, vbConvertBmpToVBox,      vbConvertBmpFromVBox    },
    { "image/x-bmp",                  VBOX_SHCL_FMT_BITMAP,                             1, vbConvertBmpToVBox,      vbConvertBmpFromVBox    },
    { "image/x-MS-bmp",               VBOX_SHCL_FMT_BITMAP,                             1, vbConvertBmpToVBox,      vbConvertBmpFromVBox    },
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
VBGH_DECL(int) VbghMimeConvFromVBox(const char *pcszMimeType, void const *pvBufIn, int cbBufIn,
                                    void **ppvBufOut, size_t *pcbBufOut)
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
VBGH_DECL(int) VbghMimeConvToVBox(const char *pcszMimeType, void const *pvBufIn, int cbBufIn,
                                  void **ppvBufOut, size_t *pcbBufOut)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aConverterFormats); i++)
        if (RTStrNCmp(g_aConverterFormats[i].pcszMimeType, pcszMimeType, VBOX_WAYLAND_MIME_TYPE_NAME_MAX) == 0)
            return g_aConverterFormats[i].pfnConvertToVBox(pvBufIn, cbBufIn, ppvBufOut, pcbBufOut);

    return VERR_NOT_FOUND;
}


/**
 * Frees a buffer returned by VbghMimeConvFromVBox or VbghMimeConvToVBox.
 *
 * @param   pvBuf           Buffer to free. Optional.
 * @param   cbBuf           Size of the buffer in bytes.
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
