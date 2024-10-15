/* $Id: MultiResult.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MS COM / XPCOM Abstraction Layer - MultiResult class declarations.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_com_MultiResult_h
#define VBOX_INCLUDED_com_MultiResult_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBox/com/defs.h"
#include "VBox/com/string.h"

#include <stdarg.h>

/** @defgroup grp_com_mr    MultiResult Classes
 * @ingroup grp_com
 * @{
 */

namespace com
{

/**
 * "First worst" result type.
 *
 * Variables of this class are used instead of HRESULT variables when it is
 * desirable to memorize the "first worst" result code instead of the last
 * assigned one. In other words, an assignment operation to a variable of this
 * class will succeed only if the result code to assign has worse severity. The
 * following table demonstrate this (the first column lists the previous result
 * code stored in the variable, the first row lists the new result code being
 * assigned, 'A' means the assignment will take place, '> S_OK' means a warning
 * result code):
 *
 * {{{
 *             FAILED    > S_OK    S_OK
 * FAILED        -         -         -
 * > S_OK        A         -         -
 * S_OK          A         A         -
 *
 * }}}
 *
 * In practice, you will need to use a FWResult variable when you call some COM
 * method B after another COM method A fails and want to return the result code
 * of A even if B also fails, but want to return the failed result code of B if
 * A issues a warning or succeeds.
 */
class FWResult
{

public:

    /**
     * Constructs a new variable. Note that by default this constructor sets the
     * result code to E_FAIL to make sure a failure is returned to the caller if
     * the variable is never assigned another value (which is considered as the
     * improper use of this class).
     */
    FWResult (HRESULT aRC = E_FAIL) : mRC (aRC) {}

    FWResult &operator= (HRESULT aRC)
    {
        if ((FAILED (aRC) && !FAILED (mRC)) ||
            (mRC == S_OK && aRC != S_OK))
            mRC = aRC;

        return *this;
    }

    operator HRESULT() const { return mRC; }

    HRESULT *operator&() { return &mRC; }

private:

    HRESULT mRC;
};

/**
 * The MultiResult class is a com::FWResult enhancement that also acts as a
 * switch to turn on multi-error mode for VirtualBoxBase::setError() and
 * VirtualBoxBase::setWarning() calls.
 *
 * When an instance of this class is created, multi-error mode is turned on
 * for the current thread and the turn-on counter is increased by one. In
 * multi-error mode, a call to setError() or setWarning() does not
 * overwrite the current error or warning info object possibly set on the
 * current thread by other method calls, but instead it stores this old
 * object in the IVirtualBoxErrorInfo::next attribute of the new error
 * object being set.
 *
 * This way, error/warning objects are stacked together and form a chain of
 * errors where the most recent error is the first one retrieved by the
 * calling party, the preceding error is what the
 * IVirtualBoxErrorInfo::next attribute of the first error points to, and so
 * on, up to the first error or warning occurred which is the last in the
 * chain. See IVirtualBoxErrorInfo documentation for more info.
 *
 * When the instance of the MultiResult class goes out of scope and gets
 * destroyed, it automatically decreases the turn-on counter by one. If
 * the counter drops to zero, multi-error mode for the current thread is
 * turned off and the thread switches back to single-error mode where every
 * next error or warning object overwrites the previous one.
 *
 * Note that the caller of a COM method uses a non-S_OK result code to
 * decide if the method has returned an error (negative codes) or a warning
 * (positive non-zero codes) and will query extended error info only in
 * these two cases. However, since multi-error mode implies that the method
 * doesn't return control return to the caller immediately after the first
 * error or warning but continues its execution, the functionality provided
 * by the base com::FWResult class becomes very useful because it allows to
 * preserve the error or the warning result code even if it is later assigned
 * a S_OK value multiple times. See com::FWResult for details.
 *
 * Here is the typical usage pattern:
 * @code
    HRESULT Bar::method()
    {
        // assume multi-errors are turned off here...

        if (something)
        {
            // Turn on multi-error mode and make sure severity is preserved
            MultiResult rc = foo->method1();

            // return on fatal error, but continue on warning or on success
            CheckComRCReturnRC (rc);

            rc = foo->method2();
            // no matter what result, stack it and continue

            // ...

            // return the last worst result code (it will be preserved even if
            // foo->method2() returns S_OK.
            return rc;
        }

        // multi-errors are turned off here again...

        return S_OK;
    }
 * @endcode
 *
 * @note This class is intended to be instantiated on the stack, therefore
 *       You cannot create them using new(). Although it is possible to copy
 *       instances of MultiResult or return them by value, please never do
 *       that as it is breaks the class semantics (and will assert);
 */
class MultiResult : public FWResult
{
public:

    /**
     * @copydoc FWResult::FWResult()
     */
    MultiResult(HRESULT aRC = E_FAIL) : FWResult (aRC) { incCounter(); }

    MultiResult(const MultiResult &aThat) : FWResult (aThat)
    {
        /* We need this copy constructor only for GCC that wants to have
         * it in case of expressions like |MultiResult rc = E_FAIL;|. But
         * we assert since the optimizer should actually avoid the
         * temporary and call the other constructor directly instead. */
        AssertFailed();
    }

    ~MultiResult() { decCounter(); }

    MultiResult &operator= (HRESULT aRC)
    {
        FWResult::operator= (aRC);
        return *this;
    }

    MultiResult &operator= (const MultiResult & /* aThat */)
    {
        /* We need this copy constructor only for GCC that wants to have
         * it in case of expressions like |MultiResult rc = E_FAIL;|. But
         * we assert since the optimizer should actually avoid the
         * temporary and call the other constructor directly instead. */
        AssertFailed();
        return *this;
    }

    /**
     * Returns true if multi-mode is enabled for the current thread (i.e. at
     * least one MultiResult instance exists on the stack somewhere).
     * @return
     */
    static bool isMultiEnabled();

private:

    DECLARE_CLS_NEW_DELETE_NOOP(MultiResult);

    static void incCounter();
    static void decCounter();

    static RTTLS sCounter;

    friend class MultiResultRef;
};

/**
 * The MultiResultRef class is equivalent to MultiResult except that it takes
 * a reference to the existing HRESULT variable instead of maintaining its own
 * one.
 */
class MultiResultRef
{
public:

    MultiResultRef (HRESULT &aRC) : mRC (aRC) { MultiResult::incCounter(); }

    ~MultiResultRef() { MultiResult::decCounter(); }

    MultiResultRef &operator= (HRESULT aRC)
    {
        /* Copied from FWResult */
        if ((FAILED (aRC) && !FAILED (mRC)) ||
            (mRC == S_OK && aRC != S_OK))
            mRC = aRC;

        return *this;
    }

    operator HRESULT() const { return mRC; }

    HRESULT *operator&() { return &mRC; }

private:

    DECLARE_CLS_NEW_DELETE_NOOP(MultiResultRef);

    HRESULT &mRC;
};


} /* namespace com */

/** @} */

#endif /* !VBOX_INCLUDED_com_MultiResult_h */

