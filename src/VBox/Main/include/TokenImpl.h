/* $Id: TokenImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Token COM class implementations - MachineToken and MediumLockToken
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

#ifndef MAIN_INCLUDED_TokenImpl_h
#define MAIN_INCLUDED_TokenImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "TokenWrap.h"
#include "MachineImpl.h"


/**
 * The MachineToken class automates cleanup of a SessionMachine object.
 */
class ATL_NO_VTABLE MachineToken :
    public TokenWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(MachineToken)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(const ComObjPtr<SessionMachine> &pSessionMachine);
    void uninit(bool fAbandon);

private:

    // wrapped IToken methods
    HRESULT abandon(AutoCaller &aAutoCaller);
    HRESULT dummy();

    // data
    struct Data
    {
        Data()
        {
        }

        ComObjPtr<SessionMachine> pSessionMachine;
    };

    Data m;
};


class Medium;

/**
 * The MediumLockToken class automates cleanup of a Medium lock.
 */
class ATL_NO_VTABLE MediumLockToken :
    public TokenWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(MediumLockToken)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(const ComObjPtr<Medium> &pMedium, bool fWrite);
    void uninit();

private:

    // wrapped IToken methods
    HRESULT abandon(AutoCaller &aAutoCaller);
    HRESULT dummy();

    // data
    struct Data
    {
        Data() :
            fWrite(false)
        {
        }

        ComObjPtr<Medium> pMedium;
        bool fWrite;
    };

    Data m;
};


#endif /* !MAIN_INCLUDED_TokenImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
