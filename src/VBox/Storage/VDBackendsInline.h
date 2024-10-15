/* $Id: VDBackendsInline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VD - backends inline helpers.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/

#ifndef VBOX_INCLUDED_SRC_Storage_VDBackendsInline_h
#define VBOX_INCLUDED_SRC_Storage_VDBackendsInline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN

/**
 * Stub for VDIMAGEBACKEND::pfnGetComment when the backend doesn't suport comments.
 *
 * @returns VBox status code.
 * @param a_StubName        The name of the stub.
 */
#define VD_BACKEND_CALLBACK_GET_COMMENT_DEF_NOT_SUPPORTED(a_StubName) \
    static DECLCALLBACK(int) a_StubName(void *pBackendData, char *pszComment, size_t cbComment) \
    { \
        RT_NOREF2(pszComment, cbComment); \
        LogFlowFunc(("pBackendData=%#p pszComment=%#p cbComment=%zu\n", pBackendData, pszComment, cbComment)); \
        AssertPtrReturn(pBackendData, VERR_VD_NOT_OPENED); \
        LogFlowFunc(("returns %Rrc comment='%s'\n", VERR_NOT_SUPPORTED, pszComment)); \
        return VERR_NOT_SUPPORTED; \
    } \
    typedef int ignore_semicolon


/**
 * Stub for VDIMAGEBACKEND::pfnSetComment when the backend doesn't suport comments.
 *
 * @returns VBox status code.
 * @param a_StubName        The name of the stub.
 * @param a_ImageStateType  Type of the backend image state.
 */
#define VD_BACKEND_CALLBACK_SET_COMMENT_DEF_NOT_SUPPORTED(a_StubName, a_ImageStateType) \
    static DECLCALLBACK(int) a_StubName(void *pBackendData, const char *pszComment) \
    { \
        RT_NOREF1(pszComment); \
        LogFlowFunc(("pBackendData=%#p pszComment=\"%s\"\n", pBackendData, pszComment)); \
        a_ImageStateType pThis = (a_ImageStateType)pBackendData; \
        AssertPtrReturn(pThis, VERR_VD_NOT_OPENED); \
        int rc = VERR_NOT_SUPPORTED; \
        if (pThis->uOpenFlags & VD_OPEN_FLAGS_READONLY) \
            rc = VERR_VD_IMAGE_READ_ONLY; \
        LogFlowFunc(("returns %Rrc\n", rc)); \
        return rc; \
    } \
    typedef int ignore_semicolon


/**
 * Stub for VDIMAGEBACKEND::pfnGetUuid when the backend doesn't suport UUIDs.
 *
 * @returns VBox status code.
 * @param a_StubName        The name of the stub.
 */
#define VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(a_StubName) \
    static DECLCALLBACK(int) a_StubName(void *pBackendData, PRTUUID pUuid) \
    { \
        RT_NOREF1(pUuid); \
        LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid)); \
        AssertPtrReturn(pBackendData, VERR_VD_NOT_OPENED); \
        LogFlowFunc(("returns %Rrc (%RTuuid)\n", VERR_NOT_SUPPORTED, pUuid)); \
        return VERR_NOT_SUPPORTED; \
    } \
    typedef int ignore_semicolon


/**
 * Stub for VDIMAGEBACKEND::pfnSetComment when the backend doesn't suport UUIDs.
 *
 * @returns VBox status code.
 * @param a_StubName        The name of the stub.
 * @param a_ImageStateType  Type of the backend image state.
 */
#define VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(a_StubName, a_ImageStateType) \
    static DECLCALLBACK(int) a_StubName(void *pBackendData, PCRTUUID pUuid) \
    { \
        RT_NOREF1(pUuid); \
        LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid)); \
        a_ImageStateType pThis = (a_ImageStateType)pBackendData; \
        AssertPtrReturn(pThis, VERR_VD_NOT_OPENED); \
        int rc = VERR_NOT_SUPPORTED; \
        if (pThis->uOpenFlags & VD_OPEN_FLAGS_READONLY) \
            rc = VERR_VD_IMAGE_READ_ONLY; \
        LogFlowFunc(("returns %Rrc\n", rc)); \
        return rc; \
    } \
    typedef int ignore_semicolon

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Storage_VDBackendsInline_h */
