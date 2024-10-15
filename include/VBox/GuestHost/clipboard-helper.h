/* $Id: clipboard-helper.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard - Some helper function for converting between the various EOLs.
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

#ifndef VBOX_INCLUDED_GuestHost_clipboard_helper_h
#define VBOX_INCLUDED_GuestHost_clipboard_helper_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/string.h>

#include <VBox/GuestHost/SharedClipboard.h>

/** Guest property which is set by GUI in order to notify guest about VM window focus change.  */
#define VBOX_GUI_FOCUS_CHANGE_GUEST_PROP_NAME   "/VirtualBox/GuestAdd/GuiOnFocus"

/** Constants needed for string conversions done by the Linux/Mac clipboard code. */
enum
{
    /** In Linux, lines end with a linefeed character. */
    VBOX_SHCL_LINEFEED = 0xa,
    /** In Windows, lines end with a carriage return and a linefeed character. */
    VBOX_SHCL_CARRIAGERETURN = 0xd,
    /** Little endian "real" UTF-16 strings start with this marker. */
    VBOX_SHCL_UTF16LEMARKER = 0xfeff,
    /** Big endian "real" UTF-16 strings start with this marker. */
    VBOX_SHCL_UTF16BEMARKER = 0xfffe
};

/**
 * Returns calculated length (in UTF-8 characters) of normalized UTF-16 string with CRLF EOL.
 *
 * This function should be used in order to calculate number of UTF-8 characters
 * in normalized UTF-16. Normalized string is expected to have LF characters replaced
 * with CRLF sequences.
 *
 * @returns VBox status code.
 * @param   pcwszSrc            UTF-16 string to return size for.
 * @param   cwcSrc              Length of the string in RTUTF16 units.
 * @param   pchLen              Where to return the length (in UTF-8 characters).
 *                              Does not include terminator.
 */
int ShClUtf16CalcNormalizedEolToCRLFLength(PCRTUTF16 pcwszSrc, size_t cwcSrc, size_t *pchLen);

/**
 * Returns the length (in UTF-8 characters) of an UTF-16 string with CRLF EOL.
 *
 * @returns VBox status code.
 * @param   pcwszSrc            UTF-16 string to return size for.
 * @param   cwcSrc              Length of the source string in RTUTF16 units.
 * @param   pchLen              Where to return the length (in UTF-8 characters).
 *                              Does not include terminator.
 */
int ShClUtf16CRLFLenUtf8(PCRTUTF16 pcwszSrc, size_t cwcSrc, size_t *pchLen);

/**
 * Returns the length (in characters) of an UTF-16 string, including terminator.
 *
 * @returns VBox status code.
 * @param  pcwszSrc             UTF-16 string to return size for.
 * @param  cwcSrc               Length of the source string in RTUTF16 units.
 * @param  pchLen               Where to return the length (in UTF-8 characters).
 *                              Does not include terminator.
 */
int ShClUtf16LenUtf8(PCRTUTF16 pcwszSrc, size_t cwcSrc, size_t *pchLen);

/**
 * Converts an UTF-16 string with LF EOL to an UTF-16 string with CRLF EOL.
 *
 * @returns VBox status code.
 * @param   pcwszSrc            UTF-16 string to convert.
 * @param   cwcSrc              Size of the string int RTUTF16 units.
 * @param   pwszDst             Buffer to store the converted string to.
 * @param   cwcDst              The size of \a pwszDst in RTUTF16 units.
 */
int ShClConvUtf16LFToCRLF(PCRTUTF16 pcwszSrc, size_t cwcSrc, PRTUTF16 pwszDst, size_t cwcDst);

/**
 * Converts an UTF-16 string with LF EOL to an UTF-16 string with CRLF EOL.
 *
 * Convenience function which returns the allocated + converted string on success.
 *
 * @returns VBox status code.
 * @param   pcwszSrc            UTF-16 string to convert.
 * @param   cwcSrc              Size of the string int RTUTF16 units.
 * @param   ppwszDst            Where to return the allocated converted string. Must be free'd by the caller.
 * @param   pcwDst              Where to return the size of the converted string in RTUTF16 units.
 *                              Does not include the terminator.
 */
int ShClConvUtf16LFToCRLFA(PCRTUTF16 pcwszSrc, size_t cwcSrc, PRTUTF16 *ppwszDst, size_t *pcwDst);

/**
 * Converts an UTF-16 string with CRLF EOL to an UTF-16 string with LF EOL.
 *
 * @returns VBox status code.
 * @param   pcwszSrc            UTF-16 string to convert.
 * @param   cwcSrc              Size of the string in RTUTF16 units.
 * @param   pwszDst             Where to store the converted string to.
 * @param   cwcDst              The size of \a pwszDst in RTUTF16 units.
 */
int ShClConvUtf16CRLFToLF(PCRTUTF16 pcwszSrc, size_t cwcSrc, PRTUTF16 pwszDst, size_t cwcDst);

/**
 * Converts an UTF-16 string with CRLF EOL to UTF-8 LF.
 *
 * @returns VBox status code. Will return VERR_NO_DATA if no data was converted.
 * @param  pcwszSrc             UTF-16 string to convert.
 * @param  cbSrc                Length of @a pwszSrc (in bytes).
 * @param  pszBuf               Where to write the converted string.
 * @param  cbBuf                The size of the buffer pointed to by @a pszBuf.
 * @param  pcbLen               Where to store the size (in bytes) of the converted string.
 *                              Does not include terminator.
 */
int ShClConvUtf16CRLFToUtf8LF(PCRTUTF16 pcwszSrc, size_t cbSrc, char *pszBuf, size_t cbBuf, size_t *pcbLen);

/**
* Converts an HTML string from UTF-16 into UTF-8.
*
* @returns VBox status code.
* @param  pcwszSrc              UTF-16 string to convert.
* @param  cwcSrc                Length (in RTUTF16 units) of the source text.
* @param  ppszDst               Where to store the converted result on success.
* @param  pcbDst                Where to store the number of bytes written.
*/
int ShClConvUtf16ToUtf8HTML(PCRTUTF16 pcwszSrc, size_t cwcSrc, char **ppszDst, size_t *pcbDst);

/**
 * Converts an UTF-8 string with LF EOL into UTF-16 CRLF.
 *
 * @returns VBox status code.
 * @param  pcszSrc              UTF-8 string to convert.
 * @param  cbSrc                Size of UTF-8 string to convert (in bytes), not counting the terminating zero.
 * @param  ppwszDst             Where to return the allocated buffer on success.
 * @param  pcwDst               Where to return the size (in RTUTF16 units) of the allocated buffer on success.
 *                              Does not include terminator.
 */
int ShClConvUtf8LFToUtf16CRLF(const char *pcszSrc, size_t cbSrc, PRTUTF16 *ppwszDst, size_t *pcwDst);

/**
 * Converts a Latin-1 string with LF EOL into UTF-16 CRLF.
 *
 * @returns VBox status code.
 * @param  pcszSrc              UTF-8 string to convert.
 * @param  cbSrc                Size of string (in bytes), not counting the terminating zero.
 * @param  ppwszDst             Where to return the allocated buffer on success.
 * @param  pcwDst               Where to return the size (in RTUTF16 units) of the allocated buffer on success.
 *                              Does not include terminator.
 */
int ShClConvLatin1LFToUtf16CRLF(const char *pcszSrc, size_t cbSrc, PRTUTF16 *ppwszDst, size_t *pcwDst);

/**
 * Convert CF_DIB data to full BMP data by prepending the BM header.
 * Allocates with RTMemAlloc.
 *
 * @returns VBox status code.
 * @param   pvSrc         DIB data to convert
 * @param   cbSrc         Size of the DIB data to convert in bytes
 * @param   ppvDst        Where to store the pointer to the buffer for the
 *                        destination data
 * @param   pcbDst        Pointer to the size of the buffer for the destination
 *                        data in bytes.
 */
int ShClDibToBmp(const void *pvSrc, size_t cbSrc, void **ppvDst, size_t *pcbDst);

/**
 * Get the address and size of CF_DIB data in a full BMP data in the input buffer.
 * Does not do any allocation.
 *
 * @returns VBox status code.
 * @param   pvSrc         BMP data to convert
 * @param   cbSrc         Size of the BMP data to convert in bytes
 * @param   ppvDst        Where to store the pointer to the destination data
 * @param   pcbDst        Pointer to the size of the destination data in bytes
 */
int ShClBmpGetDib(const void *pvSrc, size_t cbSrc, const void **ppvDst, size_t *pcbDst);

#ifdef LOG_ENABLED
/**
 * Dumps HTML data to the debug log.
 *
 * @returns VBox status code.
 * @param   pszSrc              HTML data to dump.
 * @param   cbSrc               Size (in bytes) of HTML data to dump.
 */
int ShClDbgDumpHtml(const char *pszSrc, size_t cbSrc);

/**
 * Dumps data using a specified clipboard format.
 *
 * @param   pv                  Pointer to data to dump.
 * @param   cb                  Size (in bytes) of data to dump.
 * @param   u32Format           Clipboard format to use for dumping.
 */
void ShClDbgDumpData(const void *pv, size_t cb, SHCLFORMAT u32Format);
#endif /* LOG_ENABLED */

/**
 * Translates a Shared Clipboard host function number to a string.
 *
 * @returns Function ID string name.
 * @param   uFn                 The function to translate.
 */
const char *ShClHostFunctionToStr(uint32_t uFn);

/**
 * Translates a Shared Clipboard host message enum to a string.
 *
 * @returns Message ID string name.
 * @param   uMsg                The message to translate.
 */
const char *ShClHostMsgToStr(uint32_t uMsg);

/**
 * Translates a Shared Clipboard guest message enum to a string.
 *
 * @returns Message ID string name.
 * @param   uMsg                The message to translate.
 */
const char *ShClGuestMsgToStr(uint32_t uMsg);

char *ShClFormatsToStrA(SHCLFORMATS fFormats);

#endif /* !VBOX_INCLUDED_GuestHost_clipboard_helper_h */

