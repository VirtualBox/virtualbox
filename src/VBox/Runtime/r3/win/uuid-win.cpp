/* $Id: uuid-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - UUID, Windows implementation.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_UUID
#include <iprt/win/windows.h>

#include <iprt/uuid.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/errcore.h>


RTDECL(int)  RTUuidClear(PRTUUID pUuid)
{
    /* check params */
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);

    return RTErrConvertFromWin32(UuidCreateNil((UUID *)pUuid));
}


RTDECL(bool)  RTUuidIsNull(PCRTUUID pUuid)
{
    /* check params */
    AssertPtrReturn(pUuid, true);

    RPC_STATUS status;
    return !!UuidIsNil((UUID *)pUuid, &status);
}


RTDECL(int)  RTUuidCompare(PCRTUUID pUuid1, PCRTUUID pUuid2)
{
    /*
     * Special cases.
     */
    if (pUuid1 == pUuid2)
        return 0;
    if (!pUuid1)
        return RTUuidIsNull(pUuid2) ? 0 : -1;
    if (!pUuid2)
        return RTUuidIsNull(pUuid1) ? 0 : 1;
    AssertPtrReturn(pUuid1, -1);
    AssertPtrReturn(pUuid2, 1);

    /*
     * Hand the rest to the Windows API.
     */
    RPC_STATUS status;
    return UuidCompare((UUID *)pUuid1, (UUID *)pUuid2, &status);
}


RTDECL(int)  RTUuidCompareStr(PCRTUUID pUuid1, const char *pszString2)
{
    /* check params */
    AssertPtrReturn(pUuid1, -1);
    AssertPtrReturn(pszString2, 1);

    /*
     * Try convert the string to a UUID and then compare the two.
     */
    RTUUID Uuid2;
    int rc = RTUuidFromStr(&Uuid2, pszString2);
    AssertRCReturn(rc, 1);

    return RTUuidCompare(pUuid1, &Uuid2);
}


RTDECL(int)  RTUuidCompare2Strs(const char *pszString1, const char *pszString2)
{
    RTUUID Uuid1;
    RTUUID Uuid2;
    int rc;

    /* check params */
    AssertPtrReturn(pszString1, -1);
    AssertPtrReturn(pszString2, 1);

    /*
     * Try convert the strings to UUIDs and then compare them.
     */
    rc = RTUuidFromStr(&Uuid1, pszString1);
    AssertRCReturn(rc, -1);

    rc = RTUuidFromStr(&Uuid2, pszString2);
    AssertRCReturn(rc, 1);

    return RTUuidCompare(&Uuid1, &Uuid2);
}


RTDECL(int)  RTUuidToStr(PCRTUUID pUuid, char *pszString, size_t cchString)
{
    /* check params */
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);
    AssertPtrReturn(pszString, VERR_INVALID_POINTER);
    AssertReturn(cchString >= RTUUID_STR_LENGTH, VERR_INVALID_PARAMETER);

    /*
     * Try convert it.
     *
     * The API allocates a new string buffer for us, so we can do our own
     * buffer overflow handling.
     */
    RPC_STATUS Status;
    unsigned char *pszTmpStr = NULL;
#ifdef RPC_UNICODE_SUPPORTED
    /* always use ASCII version! */
    Status = UuidToStringA((UUID *)pUuid, &pszTmpStr);
#else
    Status = UuidToString((UUID *)pUuid, &pszTmpStr);
#endif
    if (Status != RPC_S_OK)
        return RTErrConvertFromWin32(Status);

    /* copy it. */
    int rc = VINF_SUCCESS;
    size_t cchTmpStr = strlen((char *)pszTmpStr);
    if (cchTmpStr < cchString)
        memcpy(pszString, pszTmpStr, cchTmpStr + 1);
    else
    {
        AssertFailed();
        rc = ERROR_BUFFER_OVERFLOW;
    }

    /* free buffer */
#ifdef RPC_UNICODE_SUPPORTED
    /* always use ASCII version! */
    RpcStringFreeA(&pszTmpStr);
#else
    RpcStringFree(&pszTmpStr);
#endif

    /* all done */
    return rc;
}


RTDECL(int)  RTUuidFromStr(PRTUUID pUuid, const char *pszString)
{
    /* check params */
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);
    AssertPtrReturn(pszString, VERR_INVALID_POINTER);

    RPC_STATUS rc;
#ifdef RPC_UNICODE_SUPPORTED
    /* always use ASCII version! */
    rc = UuidFromStringA((unsigned char *)pszString, (UUID *)pUuid);
#else
    rc = UuidFromString((unsigned char *)pszString, (UUID *)pUuid);
#endif

    return RTErrConvertFromWin32(rc);
}

