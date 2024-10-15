/* $Id: VirtualBoxErrorInfoImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBoxErrorInfo COM class implementation
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
#include "VirtualBoxErrorInfoImpl.h"

#include <VBox/com/ErrorInfo.h>

// public initializer/uninitializer for internal purposes only
////////////////////////////////////////////////////////////////////////////////

HRESULT VirtualBoxErrorInfo::init(HRESULT aResultCode,
                                  const GUID &aIID,
                                  const char *pcszComponent,
                                  const Utf8Str &strText,
                                  IVirtualBoxErrorInfo *aNext)
{
    m_resultCode = aResultCode;
    m_resultDetail = 0; /* Not being used. */
    m_IID = aIID;
    m_strComponent = pcszComponent;
    m_strText = strText;
    mNext = aNext;

    return S_OK;
}

HRESULT VirtualBoxErrorInfo::initEx(HRESULT aResultCode,
                                    LONG aResultDetail,
                                    const GUID &aIID,
                                    const char *pcszComponent,
                                    const Utf8Str &strText,
                                    IVirtualBoxErrorInfo *aNext)
{
    HRESULT hrc = init(aResultCode, aIID, pcszComponent, strText, aNext);
    m_resultDetail = aResultDetail;

    return hrc;
}

HRESULT VirtualBoxErrorInfo::init(const com::ErrorInfo &info,
                                  IVirtualBoxErrorInfo *aNext)
{
    m_resultCode = info.getResultCode();
    m_resultDetail = info.getResultDetail();
    m_IID = info.getInterfaceID();
    m_strComponent = info.getComponent();
    m_strText = info.getText();

    /* Recursively create VirtualBoxErrorInfo instances for the next objects. */
    const com::ErrorInfo *pInfo = info.getNext();
    if (pInfo)
    {
        ComObjPtr<VirtualBoxErrorInfo> nextEI;
        HRESULT hrc = nextEI.createObject();
        if (FAILED(hrc)) return hrc;
        hrc = nextEI->init(*pInfo, aNext);
        if (FAILED(hrc)) return hrc;
        mNext = nextEI;
    }
    else
        mNext = aNext;

    return S_OK;
}

// IVirtualBoxErrorInfo properties
////////////////////////////////////////////////////////////////////////////////

STDMETHODIMP VirtualBoxErrorInfo::COMGETTER(ResultCode)(LONG *aResultCode)
{
    CheckComArgOutPointerValid(aResultCode);

    *aResultCode = (LONG)m_resultCode;
    return S_OK;
}

STDMETHODIMP VirtualBoxErrorInfo::COMGETTER(ResultDetail)(LONG *aResultDetail)
{
    CheckComArgOutPointerValid(aResultDetail);

    *aResultDetail = m_resultDetail;
    return S_OK;
}

STDMETHODIMP VirtualBoxErrorInfo::COMGETTER(InterfaceID)(BSTR *aIID)
{
    CheckComArgOutPointerValid(aIID);

    m_IID.toUtf16().cloneTo(aIID);
    return S_OK;
}

STDMETHODIMP VirtualBoxErrorInfo::COMGETTER(Component)(BSTR *aComponent)
{
    CheckComArgOutPointerValid(aComponent);

    m_strComponent.cloneTo(aComponent);
    return S_OK;
}

STDMETHODIMP VirtualBoxErrorInfo::COMGETTER(Text)(BSTR *aText)
{
    CheckComArgOutPointerValid(aText);

    m_strText.cloneTo(aText);
    return S_OK;
}

STDMETHODIMP VirtualBoxErrorInfo::COMGETTER(Next)(IVirtualBoxErrorInfo **aNext)
{
    CheckComArgOutPointerValid(aNext);

    /* this will set aNext to NULL if mNext is null */
    return mNext.queryInterfaceTo(aNext);
}

#if !defined(VBOX_WITH_XPCOM)

/**
 *  Initializes itself by fetching error information from the given error info
 *  object.
 */
HRESULT VirtualBoxErrorInfo::init(IErrorInfo *aInfo)
{
    AssertReturn(aInfo, E_FAIL);

    /* We don't return a failure if talking to IErrorInfo fails below to
     * protect ourselves from bad IErrorInfo implementations (the
     * corresponding fields will simply remain null in this case). */

    m_resultCode = S_OK;
    m_resultDetail = 0;
    HRESULT hrc = aInfo->GetGUID(m_IID.asOutParam());
    AssertComRC(hrc);
    Bstr bstrComponent;
    hrc = aInfo->GetSource(bstrComponent.asOutParam());
    AssertComRC(hrc);
    m_strComponent = bstrComponent;
    Bstr bstrText;
    hrc = aInfo->GetDescription(bstrText.asOutParam());
    AssertComRC(hrc);
    m_strText = bstrText;

    return S_OK;
}

// IErrorInfo methods
////////////////////////////////////////////////////////////////////////////////

STDMETHODIMP VirtualBoxErrorInfo::GetDescription(BSTR *description)
{
    return COMGETTER(Text)(description);
}

STDMETHODIMP VirtualBoxErrorInfo::GetGUID(GUID *guid)
{
    Bstr iid;
    HRESULT hrc = COMGETTER(InterfaceID)(iid.asOutParam());
    if (SUCCEEDED(hrc))
        *guid = Guid(iid).ref();
    return hrc;
}

STDMETHODIMP VirtualBoxErrorInfo::GetHelpContext(DWORD *pdwHelpContext)
{
    RT_NOREF(pdwHelpContext);
    return E_NOTIMPL;
}

STDMETHODIMP VirtualBoxErrorInfo::GetHelpFile(BSTR *pBstrHelpFile)
{
    RT_NOREF(pBstrHelpFile);
    return E_NOTIMPL;
}

STDMETHODIMP VirtualBoxErrorInfo::GetSource(BSTR *pBstrSource)
{
    return COMGETTER(Component)(pBstrSource);
}

#else // defined(VBOX_WITH_XPCOM)

/**
 *  Initializes itself by fetching error information from the given error info
 *  object.
 */
HRESULT VirtualBoxErrorInfo::init(nsIException *aInfo)
{
    AssertReturn(aInfo, E_FAIL);

    /* We don't return a failure if talking to nsIException fails below to
     * protect ourselves from bad nsIException implementations (the
     * corresponding fields will simply remain null in this case). */

    HRESULT hrc = aInfo->GetResult(&m_resultCode);
    AssertComRC(hrc);
    m_resultDetail = 0; /* Not being used. */

    char *pszMsg;             /* No Utf8Str.asOutParam, different allocator! */
    hrc = aInfo->GetMessage(&pszMsg);
    AssertComRC(hrc);
    if (NS_SUCCEEDED(hrc))
    {
        m_strText = pszMsg;
        nsMemory::Free(pszMsg);
    }
    else
        m_strText.setNull();

    return S_OK;
}

// nsIException methods
////////////////////////////////////////////////////////////////////////////////

/* readonly attribute string message; */
NS_IMETHODIMP VirtualBoxErrorInfo::GetMessage(char **aMessage)
{
    CheckComArgOutPointerValid(aMessage);

    m_strText.cloneTo(aMessage);
    return S_OK;
}

/* readonly attribute nsresult result; */
NS_IMETHODIMP VirtualBoxErrorInfo::GetResult(nsresult *aResult)
{
    AssertReturn(aResult, NS_ERROR_INVALID_POINTER);

    PRInt32 lrc;
    nsresult hrc = COMGETTER(ResultCode)(&lrc);
    if (SUCCEEDED(hrc))
      *aResult = (nsresult)lrc;
    return hrc;
}

/* readonly attribute string name; */
NS_IMETHODIMP VirtualBoxErrorInfo::GetName(char ** /* aName */)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* readonly attribute string filename; */
NS_IMETHODIMP VirtualBoxErrorInfo::GetFilename(char ** /* aFilename */)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* readonly attribute PRUint32 lineNumber; */
NS_IMETHODIMP VirtualBoxErrorInfo::GetLineNumber(PRUint32 * /* aLineNumber */)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* readonly attribute PRUint32 columnNumber; */
NS_IMETHODIMP VirtualBoxErrorInfo::GetColumnNumber(PRUint32 * /*aColumnNumber */)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* readonly attribute nsIStackFrame location; */
NS_IMETHODIMP VirtualBoxErrorInfo::GetLocation(nsIStackFrame ** /* aLocation */)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* readonly attribute nsIException inner; */
NS_IMETHODIMP VirtualBoxErrorInfo::GetInner(nsIException **aInner)
{
    ComPtr<IVirtualBoxErrorInfo> info;
    nsresult rv = COMGETTER(Next)(info.asOutParam());
    if (FAILED(rv)) return rv;
    return info.queryInterfaceTo(aInner);
}

/* readonly attribute nsISupports data; */
NS_IMETHODIMP VirtualBoxErrorInfo::GetData(nsISupports ** /* aData */)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* string toString(); */
NS_IMETHODIMP VirtualBoxErrorInfo::ToString(char ** /* retval */)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMPL_THREADSAFE_ISUPPORTS2(VirtualBoxErrorInfo,
                              nsIException, IVirtualBoxErrorInfo)

#endif // defined(VBOX_WITH_XPCOM)
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
