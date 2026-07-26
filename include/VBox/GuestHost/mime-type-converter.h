/* $Id: mime-type-converter.h 114779 2026-07-26 01:05:00Z knut.osmundsen@oracle.com $ */
/** @file
 * MIME type converter for Shared Clipboard and Drag-and-Drop code.
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

#ifndef VBOX_INCLUDED_GuestHost_mime_type_converter_h
#define VBOX_INCLUDED_GuestHost_mime_type_converter_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/GuestHost/clipboard-helper.h>


/** @name VBGH_MIME_CONV_F_XXX - MIME type priority and flags.
 * @{ */
/** Priority mask.
 * The priority of MIME types mapping to the same SHCLFORMAT and flags.
 * Higher value means higher priority. Range is range 0 thru 15.
 *
 * @note This assumes that we can use one common priority for all
 *       clipboard/toolkit implementations. Should we end up with different
 *       preferences, we'd have to partition it.
 */
#define VBGH_MIME_CONV_F_PRIORITY_MASK      UINT32_C(0x0000000f)
/** Readonly type, skip this when exporting. */
#define VBGH_MIME_CONV_F_RO                 UINT32_C(0x00001000)
/** An old X11 type string that doesn't follow RTC-2045 section 5.1. */
#define VBGH_MIME_CONV_F_X11                UINT32_C(0x00002000)
/** @} */

/**
 * MIME type enumeration callback function for use with
 * VbghMimeConvEnumerateByVBoxFormat().
 *
 * This is called for each MIME type matching the VBox format passed to
 * VbghMimeConvEnumerateByVBoxFormat().
 *
 * @param   pcszMimeType        String representation of a MIME type.
 * @param   fFlagsAndPriority   To be exported.
 * @param   pvUser              User data.
 */
typedef DECLCALLBACKTYPE(void, FNVBGHMIMECONVENUM, (const char *pcszMimeType, uint32_t fFlagsAndPriority, void *pvUser));
/** Pointer to a FNVBGHMIMECONVENUM. */
typedef FNVBGHMIMECONVENUM *PFNVBGHMIMECONVENUM;

VBGH_DECL(void) VbghMimeConvEnumerateByVBoxFormats(SHCLFORMATS fVBoxFmts, PFNVBGHMIMECONVENUM pfnCallback, void *pvUser);
VBGH_DECL(SHCLFORMAT) VbghMimeConvGetVBoxFormatByMime(const char *pcszMimeType, uint32_t *pfFlagsAndPriority,
                                                      char const **ppszPersistentMimeType);
VBGH_DECL(int)  VbghMimeConvFromVBox(const char *pcszMimeType, void const *pvBufIn, size_t cbBufIn,
                                     void **ppvBufOut, size_t *pcbBufOut);
VBGH_DECL(int)  VbghMimeConvToVBox(const char *pcszMimeType, void const *pvBufIn, size_t cbBufIn,
                                   void **ppvBufOut, size_t *pcbBufOut);
VBGH_DECL(void) VbghMimeConvFreeBuf(void *pvBuf, size_t cbBuf);


#if 0 /* unused */
/** MIME type cache handle.   */
typedef struct VBGHMIMECONVCACHEINT *VBGHMIMECONVCACHE;
/** Pointe rto a MIME type cache handle. */
typedef VBGHMIMECONVCACHE *PVBGHMIMECONVCACHE;
/** NIL MIME type cache handle. */
#define NIL_VBGHMIMECONVCACHE   ((VBGHMIMECONVCACHE)NULL)

/**
 * Creates a mapping table cache.
 *
 * Must be called before any other VBoxMimeConvXXXCacheYYY call.
 *
 * @returns IPRT status code.
 * @param   phCache         Where to return the cache handle on success.
 */
VBGH_DECL(int) VbghMimeConvCacheCreate(PVBGHMIMECONVCACHE phCache);

/**
 * Destroys mapping a table cache.
 *
 * @returns IPRT status code.
 * @param   hCache          Cache handle.
 */
VBGH_DECL(int) VbghMimeConvCacheDestroy(VBGHMIMECONVCACHE hCache);

/**
 * Clears a mapping table cache.
 *
 * @returns IPRT status code.
 * @param   hCache          Cache handle. NIL is ignored.
 */
VBGH_DECL(int) VbghMimeConvCacheClear(VBGHMIMECONVCACHE hCache);

/**
 * Adds data into cache using MIME type as a key.
 *
 * @returns IPRT status.
 * @retval  VERR_NOT_FOUND if given MIME type is not known.
 * @param   hCache          Cache handle.
 * @param   pcszMimeType    MIME type in string representation.
 * @param   pvBuf           Input buffer which contains data in specified MIME type format.
 * @param   cbBuf           Size of input buffer in bytes.
 */
VBGH_DECL(int) VbghMimeConvCacheSetByMime(VBGHMIMECONVCACHE hCache, const char *pcszMimeType, void *pvBuf, int cbBuf);

/**
 * Extracts data from cache using MIME type as a key.
 *
 * @returns IPRT status.
 * @retval  VERR_NOT_FOUND if given MIME type is not known or has no data
 *          associated with it in the cache.
 * @param   hCache          Cache handle.
 * @param   pcszMimeType    MIME type in string representation.
 * @param   ppvBufOut       Data which corresponds to given MIME type.
 * @param   pcbBufOut       Size of output buffer.
 */
VBGH_DECL(int) VbghMimeConvCacheGetByMime(VBGHMIMECONVCACHE hCache, const char *pcszMimeType, void **ppvBufOut, size_t *pcbBufOut);

/**
 * Extracts data from cache using format ID as a key.
 *
 * @returns IPRT status.
 * @retval  VERR_NOT_FOUND if given VBox format is not known or has no data
 *          associated with it in the cache.
 * @param   hCache          Cache handle.
 * @param   uFmtVBox        Format ID in VBox representation.
 * @param   ppvBufOut       Data which corresponds to given MIME type.
 * @param   pcbBufOut       Size of output buffer.
 */
VBGH_DECL(int) VbghMimeConvCacheGetByVBoxFormat(VBGHMIMECONVCACHE hCache, const SHCLFORMAT uFmtVBox,
                                                void **ppvBufOut, size_t *pcbBufOut);
#endif

#endif /* !VBOX_INCLUDED_GuestHost_mime_type_converter_h */

