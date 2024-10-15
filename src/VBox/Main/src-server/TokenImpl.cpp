/* $Id: TokenImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Token COM class implementation - MachineToken and MediumLockToken
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

#define LOG_GROUP LOG_GROUP_MAIN_TOKEN
#include "TokenImpl.h"
#include "MachineImpl.h"
#include "MediumImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(MachineToken)

HRESULT MachineToken::FinalConstruct()
{
    return BaseFinalConstruct();
}

void MachineToken::FinalRelease()
{
    uninit(false);

    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the token object.
 *
 * @param pSessionMachine   Pointer to a SessionMachine object.
 */
HRESULT MachineToken::init(const ComObjPtr<SessionMachine> &pSessionMachine)
{
    LogFlowThisFunc(("pSessionMachine=%p\n", &pSessionMachine));

    ComAssertRet(!pSessionMachine.isNull(), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m.pSessionMachine = pSessionMachine;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Uninitializes the instance and sets the ready flag to FALSE.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void MachineToken::uninit(bool fAbandon)
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    /* Destroy the SessionMachine object, check is paranoia */
    if (!m.pSessionMachine.isNull())
    {
        m.pSessionMachine->uninit(fAbandon ? SessionMachine::Uninit::Normal : SessionMachine::Uninit::Abnormal);
        m.pSessionMachine.setNull();
    }
}

// IToken methods
/////////////////////////////////////////////////////////////////////////////

HRESULT MachineToken::abandon(AutoCaller &aAutoCaller)
{
    /* have to release the AutoCaller before calling uninit(), self-deadlock */
    aAutoCaller.release();

    /* uninit does everything we need */
    uninit(true);
    return S_OK;
}

HRESULT MachineToken::dummy()
{
    /* Remember, the wrapper contains the AutoCaller, which means that after
     * uninit() this code won't be reached any more. */

    /* this is a NOOP, no need to lock */

    return S_OK;
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(MediumLockToken)

HRESULT MediumLockToken::FinalConstruct()
{
    return BaseFinalConstruct();
}

void MediumLockToken::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the token object.
 *
 * @param pMedium   Pointer to a Medium object.
 * @param fWrite    True if this is a write lock, false otherwise.
 */
HRESULT MediumLockToken::init(const ComObjPtr<Medium> &pMedium, bool fWrite)
{
    LogFlowThisFunc(("pMedium=%p\n", &pMedium));

    ComAssertRet(!pMedium.isNull(), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m.pMedium = pMedium;
    m.fWrite = fWrite;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Uninitializes the instance and sets the ready flag to FALSE.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void MediumLockToken::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    /* Release the appropriate lock, check is paranoia */
    if (!m.pMedium.isNull())
    {
        if (m.fWrite)
        {
            HRESULT hrc = m.pMedium->i_unlockWrite(NULL);
            AssertComRC(hrc);
        }
        else
        {
            HRESULT hrc = m.pMedium->i_unlockRead(NULL);
            AssertComRC(hrc);
        }
        m.pMedium.setNull();
    }
}

// IToken methods
/////////////////////////////////////////////////////////////////////////////

HRESULT MediumLockToken::abandon(AutoCaller &aAutoCaller)
{
    /* have to release the AutoCaller before calling uninit(), self-deadlock */
    aAutoCaller.release();

    /* uninit does everything we need */
    uninit();
    return S_OK;
}

HRESULT MediumLockToken::dummy()
{
    /* Remember, the wrapper contains the AutoCaller, which means that after
     * uninit() this code won't be reached any more. */

    /* this is a NOOP, no need to lock */

    return S_OK;
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
