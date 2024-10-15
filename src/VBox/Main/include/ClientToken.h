/* $Id: ClientToken.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VirtualBox API client session token abstraction
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

#ifndef MAIN_INCLUDED_ClientToken_h
#define MAIN_INCLUDED_ClientToken_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/ptr.h>
#include <VBox/com/AutoLock.h>

#include "MachineImpl.h"
#ifdef VBOX_WITH_GENERIC_SESSION_WATCHER
# include "TokenImpl.h"
#endif /* VBOX_WITH_GENERIC_SESSION_WATCHER */

#if defined(RT_OS_WINDOWS)
# define CTTOKENARG NULL
# define CTTOKENTYPE HANDLE
#elif defined(RT_OS_OS2)
# define CTTOKENARG NULLHANDLE
# define CTTOKENTYPE HMTX
#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)
# define CTTOKENARG -1
# define CTTOKENTYPE int
#elif defined(VBOX_WITH_GENERIC_SESSION_WATCHER)
# define CTTOKENARG NULL
# define CTTOKENTYPE MachineToken *
#else
# error "Port me!"
#endif

/**
 * Class which represents a token which can be used to check for client
 * crashes and similar purposes.
 */
class Machine::ClientToken
{
public:
    /**
     * Constructor which creates a usable instance
     *
     * @param pMachine          Reference to Machine object
     * @param pSessionMachine   Reference to corresponding SessionMachine object
     */
    ClientToken(const ComObjPtr<Machine> &pMachine, SessionMachine *pSessionMachine);

    /**
     * Default destructor. Cleans everything up.
     */
    ~ClientToken();

    /**
     * Check if object contains a usable token.
     */
    bool isReady();

    /**
     * Query token ID, which is a unique string value for this token. Do not
     * assume any specific content/format, it is opaque information.
     */
    void getId(Utf8Str &strId);

    /**
     * Query token, which is platform dependent.
     */
    CTTOKENTYPE getToken();

#ifndef VBOX_WITH_GENERIC_SESSION_WATCHER
    /**
     * Release token now. Returns information if the client has terminated.
     */
    bool release();
#endif /* !VBOX_WITH_GENERIC_SESSION_WATCHER */

private:
    /**
     * Default constructor. Don't use, will not create a sensible instance.
     */
    ClientToken();

    Machine *mMachine;
    CTTOKENTYPE mClientToken;
    Utf8Str mClientTokenId;
#ifdef VBOX_WITH_GENERIC_SESSION_WATCHER
    bool mClientTokenPassed;
#endif
};

#endif /* !MAIN_INCLUDED_ClientToken_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
