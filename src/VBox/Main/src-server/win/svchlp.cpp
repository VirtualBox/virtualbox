/* $Id: svchlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Definition of SVC Helper Process control routines.
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

#define LOG_GROUP LOG_GROUP_MAIN
#include "svchlp.h"

//#include "HostImpl.h"
#include "LoggingNew.h"

#include <iprt/errcore.h>

int netIfNetworkInterfaceHelperServer(SVCHlpClient *aClient, SVCHlpMsg::Code aMsgCode);

using namespace com;

enum { PipeBufSize = 1024 };

////////////////////////////////////////////////////////////////////////////////

/**
 *  GetLastError() is known to return NO_ERROR even after the Win32 API
 *  function (i.e. Write() to a non-connected server end of a pipe) returns
 *  FALSE... This method ensures that at least VERR_GENERAL_FAILURE is returned
 *  in cases like that. Intended to be called immediately after a failed API
 *  call.
 */
static inline int rtErrConvertFromWin32OnFailure()
{
    DWORD err = GetLastError();
    return err == NO_ERROR ? VERR_GENERAL_FAILURE
                           : RTErrConvertFromWin32 (err);
}

////////////////////////////////////////////////////////////////////////////////

SVCHlpClient::SVCHlpClient()
    : mIsOpen (false), mIsServer (false)
    , mReadEnd (NULL), mWriteEnd (NULL)
{
}

SVCHlpClient::~SVCHlpClient()
{
    close();
}

int SVCHlpClient::create(const char *aName)
{
    AssertReturn(aName, VERR_INVALID_PARAMETER);

    if (mIsOpen)
        return VERR_WRONG_ORDER;

    Bstr pipeName = Utf8StrFmt("\\\\.\\pipe\\%s", aName);

    HANDLE pipe = CreateNamedPipe(pipeName.raw(),
                                  PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                  1, // PIPE_UNLIMITED_INSTANCES,
                                  PipeBufSize, PipeBufSize,
                                  NMPWAIT_USE_DEFAULT_WAIT,
                                  NULL);

    if (pipe == INVALID_HANDLE_VALUE)
        rtErrConvertFromWin32OnFailure();

    mIsOpen = true;
    mIsServer = true;
    mReadEnd = pipe;
    mWriteEnd = pipe;
    mName = aName;

    return VINF_SUCCESS;
}

int SVCHlpClient::open(const char *aName)
{
    AssertReturn(aName, VERR_INVALID_PARAMETER);

    if (mIsOpen)
        return VERR_WRONG_ORDER;

    Bstr pipeName = Utf8StrFmt("\\\\.\\pipe\\%s", aName);

    HANDLE pipe = CreateFile(pipeName.raw(),
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             0,
                             NULL);

    if (pipe == INVALID_HANDLE_VALUE)
        rtErrConvertFromWin32OnFailure();

    mIsOpen = true;
    mIsServer = false;
    mReadEnd = pipe;
    mWriteEnd = pipe;
    mName = aName;

    return VINF_SUCCESS;
}

int SVCHlpClient::connect()
{
    if (!mIsOpen || !mIsServer)
        return VERR_WRONG_ORDER;

    BOOL ok = ConnectNamedPipe (mReadEnd, NULL);
    if (!ok && GetLastError() != ERROR_PIPE_CONNECTED)
        rtErrConvertFromWin32OnFailure();

    return VINF_SUCCESS;
}

int SVCHlpClient::close()
{
    if (!mIsOpen)
        return VERR_WRONG_ORDER;

    if (mWriteEnd != NULL && mWriteEnd != mReadEnd)
    {
        if (!CloseHandle (mWriteEnd))
            rtErrConvertFromWin32OnFailure();
        mWriteEnd = NULL;
    }

    if (mReadEnd != NULL)
    {
        if (!CloseHandle (mReadEnd))
            rtErrConvertFromWin32OnFailure();
        mReadEnd = NULL;
    }

    mIsOpen = false;
    mIsServer = false;
    mName.setNull();

    return VINF_SUCCESS;
}

int SVCHlpClient::write (const void *aVal, size_t aLen)
{
    AssertReturn(aVal != NULL, VERR_INVALID_PARAMETER);
    AssertReturn(aLen != 0, VERR_INVALID_PARAMETER);

    if (!mIsOpen)
        return VERR_WRONG_ORDER;

    DWORD written = 0;
    BOOL ok = WriteFile (mWriteEnd, aVal, (ULONG)aLen, &written, NULL);
    AssertReturn(!ok || written == aLen, VERR_GENERAL_FAILURE);
    return ok ? VINF_SUCCESS : rtErrConvertFromWin32OnFailure();
}

int SVCHlpClient::write (const Utf8Str &aVal)
{
    if (!mIsOpen)
        return VERR_WRONG_ORDER;

    /* write -1 for NULL strings */
    if (aVal.isEmpty())
        return write ((size_t) ~0);

    size_t len = aVal.length();

    /* write string length */
    int vrc = write (len);
    if (RT_SUCCESS(vrc))
    {
        /* write string data */
        vrc = write (aVal.c_str(), len);
    }

    return vrc;
}

int SVCHlpClient::write (const Guid &aGuid)
{
    Utf8Str guidStr = aGuid.toString();
    return write (guidStr);
}

int SVCHlpClient::read (void *aVal, size_t aLen)
{
    AssertReturn(aVal != NULL, VERR_INVALID_PARAMETER);
    AssertReturn(aLen != 0, VERR_INVALID_PARAMETER);

    if (!mIsOpen)
        return VERR_WRONG_ORDER;

    DWORD read = 0;
    BOOL ok = ReadFile (mReadEnd, aVal, (ULONG)aLen, &read, NULL);
    AssertReturn(!ok || read == aLen, VERR_GENERAL_FAILURE);
    return ok ? VINF_SUCCESS : rtErrConvertFromWin32OnFailure();
}

int SVCHlpClient::read (Utf8Str &aVal)
{
    if (!mIsOpen)
        return VERR_WRONG_ORDER;

    size_t len = 0;

    /* read string length */
    int vrc = read (len);
    if (RT_FAILURE(vrc))
        return vrc;

    /* length -1 means a NULL string */
    if (len == (size_t) ~0)
    {
        aVal.setNull();
        return VINF_SUCCESS;
    }

    aVal.reserve(len + 1);
    aVal.mutableRaw()[len] = 0;

    /* read string data */
    vrc = read (aVal.mutableRaw(), len);

    return vrc;
}

int SVCHlpClient::read (Guid &aGuid)
{
    Utf8Str guidStr;
    int vrc = read (guidStr);
    if (RT_SUCCESS(vrc))
        aGuid = Guid (guidStr.c_str());
    return vrc;
}

////////////////////////////////////////////////////////////////////////////////

SVCHlpServer::SVCHlpServer ()
{
}

int SVCHlpServer::run()
{
    int vrc = VINF_SUCCESS;
    SVCHlpMsg::Code msgCode = SVCHlpMsg::Null;

    do
    {
        vrc = read (msgCode);
        if (RT_FAILURE(vrc))
            return vrc;

        /* terminate request received */
        if (msgCode == SVCHlpMsg::Null)
            return VINF_SUCCESS;

        switch (msgCode)
        {
            case SVCHlpMsg::CreateHostOnlyNetworkInterface:
            case SVCHlpMsg::RemoveHostOnlyNetworkInterface:
            case SVCHlpMsg::EnableDynamicIpConfig:
            case SVCHlpMsg::EnableStaticIpConfig:
            case SVCHlpMsg::EnableStaticIpConfigV6:
            case SVCHlpMsg::DhcpRediscover:
            {
#ifdef VBOX_WITH_NETFLT
                vrc = netIfNetworkInterfaceHelperServer(this, msgCode);
#endif
                break;
            }
            default:
                AssertMsgFailedReturn(("Invalid message code %d (%08lX)\n", msgCode, msgCode),
                                      VERR_GENERAL_FAILURE);
        }

        if (RT_FAILURE(vrc))
            return vrc;
    }
    while (1);

    /* we never get here */
    AssertFailed();
    return VERR_GENERAL_FAILURE;
}
