/* $Id: ErrorInfo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * ErrorInfo class definition
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

#if defined(VBOX_WITH_XPCOM)
# include <nsIServiceManager.h>
# include <nsIExceptionService.h>
# include <nsCOMPtr.h>
#endif

#include "VBox/com/VirtualBox.h"
#include "VBox/com/ErrorInfo.h"
#include "VBox/com/assert.h"
#include "VBox/com/com.h"
#include "VBox/com/MultiResult.h"

#include <iprt/stream.h>
#include <iprt/string.h>

#include <iprt/errcore.h>

namespace com
{

////////////////////////////////////////////////////////////////////////////////
//
// ErrorInfo class
//
////////////////////////////////////////////////////////////////////////////////

HRESULT ErrorInfo::getVirtualBoxErrorInfo(ComPtr<IVirtualBoxErrorInfo> &pVirtualBoxErrorInfo)
{
    HRESULT hrc = S_OK;
    if (mErrorInfo)
        hrc = mErrorInfo.queryInterfaceTo(pVirtualBoxErrorInfo.asOutParam());
    else
        pVirtualBoxErrorInfo.setNull();
    return hrc;
}

void ErrorInfo::copyFrom(const ErrorInfo &x)
{
    mIsBasicAvailable = x.mIsBasicAvailable;
    mIsFullAvailable = x.mIsFullAvailable;

    mResultCode = x.mResultCode;
    mResultDetail = x.mResultDetail;
    mInterfaceID = x.mInterfaceID;
    mComponent = x.mComponent;
    mText = x.mText;

    if (x.m_pNext != NULL)
        m_pNext = new ErrorInfo(*x.m_pNext);
    else
        m_pNext = NULL;

    mInterfaceName = x.mInterfaceName;
    mCalleeIID = x.mCalleeIID;
    mCalleeName = x.mCalleeName;

    mErrorInfo = x.mErrorInfo;
}

void ErrorInfo::cleanup()
{
    mIsBasicAvailable = false;
    mIsFullAvailable = false;

    if (m_pNext)
    {
        delete m_pNext;
        m_pNext = NULL;
    }

    mResultCode = S_OK;
    mResultDetail = 0;
    mInterfaceID.clear();
    mComponent.setNull();
    mText.setNull();
    mInterfaceName.setNull();
    mCalleeIID.clear();
    mCalleeName.setNull();
    mErrorInfo.setNull();
}

void ErrorInfo::init(bool aKeepObj /* = false */)
{
    HRESULT hrc = E_FAIL;

#if !defined(VBOX_WITH_XPCOM)

    ComPtr<IErrorInfo> err;
    hrc = ::GetErrorInfo(0, err.asOutParam());
    if (hrc == S_OK && err)
    {
        if (aKeepObj)
            mErrorInfo = err;

        ComPtr<IVirtualBoxErrorInfo> info;
        hrc = err.queryInterfaceTo(info.asOutParam());
        if (SUCCEEDED(hrc) && info)
            init(info);

        if (!mIsFullAvailable)
        {
            bool gotSomething = false;

            hrc = err->GetGUID(mInterfaceID.asOutParam());
            gotSomething |= SUCCEEDED(hrc);
            if (SUCCEEDED(hrc))
                GetInterfaceNameByIID(mInterfaceID.ref(), mInterfaceName.asOutParam());

            hrc = err->GetSource(mComponent.asOutParam());
            gotSomething |= SUCCEEDED(hrc);

            hrc = err->GetDescription(mText.asOutParam());
            gotSomething |= SUCCEEDED(hrc);

            if (gotSomething)
                mIsBasicAvailable = true;

            AssertMsg(gotSomething, ("Nothing to fetch!\n"));
        }
    }

#else // defined(VBOX_WITH_XPCOM)

    nsCOMPtr<nsIExceptionService> es;
    es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &hrc);
    if (NS_SUCCEEDED(hrc))
    {
        nsCOMPtr<nsIExceptionManager> em;
        hrc = es->GetCurrentExceptionManager(getter_AddRefs(em));
        if (NS_SUCCEEDED(hrc))
        {
            ComPtr<nsIException> ex;
            hrc = em->GetCurrentException(ex.asOutParam());
            if (NS_SUCCEEDED(hrc) && ex)
            {
                if (aKeepObj)
                    mErrorInfo = ex;

                ComPtr<IVirtualBoxErrorInfo> info;
                hrc = ex.queryInterfaceTo(info.asOutParam());
                if (NS_SUCCEEDED(hrc) && info)
                    init(info);

                if (!mIsFullAvailable)
                {
                    bool gotSomething = false;

                    hrc = ex->GetResult(&mResultCode);
                    gotSomething |= NS_SUCCEEDED(hrc);

                    char *pszMsg;
                    hrc = ex->GetMessage(&pszMsg);
                    gotSomething |= NS_SUCCEEDED(hrc);
                    if (NS_SUCCEEDED(hrc))
                    {
                        mText = Bstr(pszMsg);
                        nsMemory::Free(pszMsg);
                    }

                    if (gotSomething)
                        mIsBasicAvailable = true;

                    AssertMsg(gotSomething, ("Nothing to fetch!\n"));
                }

                // set the exception to NULL (to emulate Win32 behavior)
                em->SetCurrentException(NULL);

                hrc = NS_OK;
            }
        }
    }
    /* Ignore failure when called after nsComponentManagerImpl::Shutdown(). */
    else if (hrc == NS_ERROR_UNEXPECTED)
        hrc = NS_OK;

    AssertComRC(hrc);

#endif // defined(VBOX_WITH_XPCOM)
}

void ErrorInfo::init(IUnknown *aI,
                     const GUID &aIID,
                     bool aKeepObj /* = false */)
{
    AssertReturnVoid(aI);

#if !defined(VBOX_WITH_XPCOM)

    ComPtr<IUnknown> iface = aI;
    ComPtr<ISupportErrorInfo> serr;
    HRESULT hrc = iface.queryInterfaceTo(serr.asOutParam());
    if (SUCCEEDED(hrc))
    {
        hrc = serr->InterfaceSupportsErrorInfo(aIID);
        if (SUCCEEDED(hrc))
            init(aKeepObj);
    }

#else

    init(aKeepObj);

#endif

    if (mIsBasicAvailable)
    {
        mCalleeIID = aIID;
        GetInterfaceNameByIID(aIID, mCalleeName.asOutParam());
    }
}

void ErrorInfo::init(IVirtualBoxErrorInfo *info)
{
    AssertReturnVoid(info);

    HRESULT hrc = E_FAIL;
    bool gotSomething = false;
    bool gotAll = true;
    LONG lrc, lrd;

    hrc = info->COMGETTER(ResultCode)(&lrc); mResultCode = lrc;
    gotSomething |= SUCCEEDED(hrc);
    gotAll &= SUCCEEDED(hrc);

    hrc = info->COMGETTER(ResultDetail)(&lrd); mResultDetail = lrd;
    gotSomething |= SUCCEEDED(hrc);
    gotAll &= SUCCEEDED(hrc);

    Bstr iid;
    hrc = info->COMGETTER(InterfaceID)(iid.asOutParam());
    gotSomething |= SUCCEEDED(hrc);
    gotAll &= SUCCEEDED(hrc);
    if (SUCCEEDED(hrc))
    {
        mInterfaceID = iid;
        GetInterfaceNameByIID(mInterfaceID.ref(), mInterfaceName.asOutParam());
    }

    hrc = info->COMGETTER(Component)(mComponent.asOutParam());
    gotSomething |= SUCCEEDED(hrc);
    gotAll &= SUCCEEDED(hrc);

    hrc = info->COMGETTER(Text)(mText.asOutParam());
    gotSomething |= SUCCEEDED(hrc);
    gotAll &= SUCCEEDED(hrc);

    m_pNext = NULL;

    ComPtr<IVirtualBoxErrorInfo> next;
    hrc = info->COMGETTER(Next)(next.asOutParam());
    if (SUCCEEDED(hrc) && !next.isNull())
    {
        m_pNext = new ErrorInfo(next);
        Assert(m_pNext != NULL);
        if (!m_pNext)
            hrc = E_OUTOFMEMORY;
    }

    gotSomething |= SUCCEEDED(hrc);
    gotAll &= SUCCEEDED(hrc);

    mIsBasicAvailable = gotSomething;
    mIsFullAvailable = gotAll;

    mErrorInfo = info;

    AssertMsg(gotSomething, ("Nothing to fetch!\n"));
}

////////////////////////////////////////////////////////////////////////////////
//
// ProgressErrorInfo class
//
////////////////////////////////////////////////////////////////////////////////

ProgressErrorInfo::ProgressErrorInfo(IProgress *progress) :
    ErrorInfo(false /* aDummy */)
{
    Assert(progress);
    if (!progress)
        return;

    ComPtr<IVirtualBoxErrorInfo> info;
    HRESULT hrc = progress->COMGETTER(ErrorInfo)(info.asOutParam());
    if (SUCCEEDED(hrc) && info)
        init(info);
}

////////////////////////////////////////////////////////////////////////////////
//
// ErrorInfoKeeper class
//
////////////////////////////////////////////////////////////////////////////////

HRESULT ErrorInfoKeeper::restore()
{
    if (mForgot)
        return S_OK;

    HRESULT hrc = S_OK;

#if !defined(VBOX_WITH_XPCOM)

    ComPtr<IErrorInfo> err;
    if (!mErrorInfo.isNull())
    {
        hrc = mErrorInfo.queryInterfaceTo(err.asOutParam());
        AssertComRC(hrc);
    }
    hrc = ::SetErrorInfo(0, err);

#else // defined(VBOX_WITH_XPCOM)

    nsCOMPtr <nsIExceptionService> es;
    es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &hrc);
    if (NS_SUCCEEDED(hrc))
    {
        nsCOMPtr <nsIExceptionManager> em;
        hrc = es->GetCurrentExceptionManager(getter_AddRefs(em));
        if (NS_SUCCEEDED(hrc))
        {
            ComPtr<nsIException> ex;
            if (!mErrorInfo.isNull())
            {
                hrc = mErrorInfo.queryInterfaceTo(ex.asOutParam());
                AssertComRC(hrc);
            }
            hrc = em->SetCurrentException(ex);
        }
    }

#endif // defined(VBOX_WITH_XPCOM)

    if (SUCCEEDED(hrc))
    {
        mErrorInfo.setNull();
        mForgot = true;
    }

    return hrc;
}

} /* namespace com */

