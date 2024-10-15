/* $Id: svchlp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Declaration of SVC Helper Process control routines.
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

#ifndef MAIN_INCLUDED_SRC_src_server_win_svchlp_h
#define MAIN_INCLUDED_SRC_src_server_win_svchlp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBox/com/string.h"
#include "VBox/com/guid.h"

#include <VBox/err.h>

#include <iprt/win/windows.h>

struct SVCHlpMsg
{
    enum Code
    {
        Null = 0, /* no parameters */
        OK, /* no parameters */
        Error, /* Utf8Str string (may be null but must present) */

        CreateHostOnlyNetworkInterface = 100, /* see usage in code */
        CreateHostOnlyNetworkInterface_OK, /* see usage in code */
        RemoveHostOnlyNetworkInterface, /* see usage in code */
        EnableDynamicIpConfig, /* see usage in code */
        EnableStaticIpConfig, /* see usage in code */
        EnableStaticIpConfigV6, /* see usage in code */
        DhcpRediscover, /* see usage in code */
    };
};

class SVCHlpClient
{
public:

    SVCHlpClient();
    virtual ~SVCHlpClient();

    int create (const char *aName);
    int connect();
    int open (const char *aName);
    int close();

    bool isOpen() const { return mIsOpen; }
    bool isServer() const { return mIsServer; }
    const com::Utf8Str &name() const { return mName; }

    int write (const void *aVal, size_t aLen);
    template <typename Scalar>
    int write (Scalar aVal) { return write (&aVal, sizeof (aVal)); }
    int write (const com::Utf8Str &aVal);
    int write (const com::Guid &aGuid);

    int read (void *aVal, size_t aLen);
    template <typename Scalar>
    int read (Scalar &aVal) { return read (&aVal, sizeof (aVal)); }
    int read (com::Utf8Str &aVal);
    int read (com::Guid &aGuid);

private:

    bool mIsOpen : 1;
    bool mIsServer : 1;

    HANDLE mReadEnd;
    HANDLE mWriteEnd;
    com::Utf8Str mName;
};

class SVCHlpServer : public SVCHlpClient
{
public:

    SVCHlpServer();

    int run();
};

#endif /* !MAIN_INCLUDED_SRC_src_server_win_svchlp_h */

