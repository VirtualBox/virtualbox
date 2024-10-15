/* $Id: ClientTokenHolder.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VirtualBox API client session token holder (in the client process)
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_ClientTokenHolder_h
#define MAIN_INCLUDED_ClientTokenHolder_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "SessionImpl.h"

#if defined(RT_OS_WINDOWS)
# define CTHSEMARG NULL
# define CTHSEMTYPE HANDLE
/* this second semaphore is only used on Windows */
# define CTHTHREADSEMARG NULL
# define CTHTHREADSEMTYPE HANDLE
#elif defined(RT_OS_OS2)
# define CTHSEMARG NIL_RTSEMEVENT
# define CTHSEMTYPE RTSEMEVENT
#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)
# define CTHSEMARG -1
# define CTHSEMTYPE int
#elif defined(VBOX_WITH_GENERIC_SESSION_WATCHER)
/* the token object based implementation needs no semaphores */
#else
# error "Port me!"
#endif


/**
 * Class which holds a client token.
 */
class Session::ClientTokenHolder
{
public:
#ifndef VBOX_WITH_GENERIC_SESSION_WATCHER
    /**
     * Constructor which creates a usable instance
     *
     * @param strTokenId    String with identifier of the token
     */
    ClientTokenHolder(const Utf8Str &strTokenId);
#else /* VBOX_WITH_GENERIC_SESSION_WATCHER */
    /**
     * Constructor which creates a usable instance
     *
     * @param aToken        Reference to token object
     */
    ClientTokenHolder(IToken *aToken);
#endif /* VBOX_WITH_GENERIC_SESSION_WATCHER */

    /**
     * Default destructor. Cleans everything up.
     */
    ~ClientTokenHolder();

    /**
     * Check if object contains a usable token.
     */
    bool isReady();

private:
    /**
     * Default constructor. Don't use, will not create a sensible instance.
     */
    ClientTokenHolder();

#ifndef VBOX_WITH_GENERIC_SESSION_WATCHER
    Utf8Str mClientTokenId;
#else /* VBOX_WITH_GENERIC_SESSION_WATCHER */
    ComPtr<IToken> mToken;
#endif /* VBOX_WITH_GENERIC_SESSION_WATCHER */
#ifdef CTHSEMTYPE
    CTHSEMTYPE mSem;
#endif
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    RTTHREAD mThread;
#endif
#ifdef RT_OS_WINDOWS
    CTHTHREADSEMTYPE mThreadSem;
#endif
};

#endif /* !MAIN_INCLUDED_ClientTokenHolder_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
