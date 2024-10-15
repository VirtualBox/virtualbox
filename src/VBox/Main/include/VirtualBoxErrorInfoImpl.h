/* $Id: VirtualBoxErrorInfoImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBoxErrorInfo COM class definition.
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

#ifndef MAIN_INCLUDED_VirtualBoxErrorInfoImpl_h
#define MAIN_INCLUDED_VirtualBoxErrorInfoImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VirtualBoxBase.h"

using namespace com;

class ATL_NO_VTABLE VirtualBoxErrorInfo
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , VBOX_SCRIPTABLE_IMPL(IVirtualBoxErrorInfo)
#ifndef VBOX_WITH_XPCOM /* IErrorInfo doesn't inherit from IDispatch, ugly 3am hack: */
    , public IDispatch
#endif
{
public:

    DECLARE_NOT_AGGREGATABLE(VirtualBoxErrorInfo)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(VirtualBoxErrorInfo)
        COM_INTERFACE_ENTRY(IErrorInfo)
        COM_INTERFACE_ENTRY(IVirtualBoxErrorInfo)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_pUnkMarshaler)
    END_COM_MAP()

    DECLARE_TRANSLATE_METHODS(VirtualBoxErrorInfo)

    HRESULT FinalConstruct()
    {
#ifndef VBOX_WITH_XPCOM
        return CoCreateFreeThreadedMarshaler((IUnknown *)(void *)this, &m_pUnkMarshaler);
#else
        return S_OK;
#endif
    }

    void FinalRelease()
    {
#ifndef VBOX_WITH_XPCOM
        if (m_pUnkMarshaler)
        {
            m_pUnkMarshaler->Release();
            m_pUnkMarshaler = NULL;
        }
#endif
    }

#ifndef VBOX_WITH_XPCOM

    HRESULT init(IErrorInfo *aInfo);

    STDMETHOD(GetGUID)(GUID *guid);
    STDMETHOD(GetSource)(BSTR *pBstrSource);
    STDMETHOD(GetDescription)(BSTR *description);
    STDMETHOD(GetHelpFile)(BSTR *pBstrHelpFile);
    STDMETHOD(GetHelpContext)(DWORD *pdwHelpContext);

    // IDispatch forwarding - 3am hack.
    typedef IDispatchImpl<IVirtualBoxErrorInfo, &IID_IVirtualBoxErrorInfo, &LIBID_VirtualBox, kTypeLibraryMajorVersion, kTypeLibraryMinorVersion> idi;

    STDMETHOD(GetTypeInfoCount)(UINT *pcInfo)
    {
        return idi::GetTypeInfoCount(pcInfo);
    }

    STDMETHOD(GetTypeInfo)(UINT iInfo, LCID Lcid, ITypeInfo **ppTypeInfo)
    {
        return idi::GetTypeInfo(iInfo, Lcid, ppTypeInfo);
    }

    STDMETHOD(GetIDsOfNames)(REFIID rIID, LPOLESTR *papwszNames, UINT cNames, LCID Lcid, DISPID *paDispIDs)
    {
        return idi::GetIDsOfNames(rIID, papwszNames, cNames, Lcid, paDispIDs);
    }

    STDMETHOD(Invoke)(DISPID idDispMember, REFIID rIID, LCID Lcid, WORD fw, DISPPARAMS *pDispParams,
                      VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *piErrArg)
    {
        return idi::Invoke(idDispMember, rIID, Lcid, fw, pDispParams, pVarResult, pExcepInfo, piErrArg);
    }

#else // defined(VBOX_WITH_XPCOM)

    HRESULT init(nsIException *aInfo);

    NS_DECL_NSIEXCEPTION

#endif

    VirtualBoxErrorInfo()
        : m_resultCode(S_OK),
          m_resultDetail(0)
    {}
    virtual ~VirtualBoxErrorInfo() {}

    // public initializer/uninitializer for internal purposes only
    HRESULT init(HRESULT aResultCode,
                 const GUID &aIID,
                 const char *pcszComponent,
                 const Utf8Str &strText,
                 IVirtualBoxErrorInfo *aNext = NULL);

    HRESULT initEx(HRESULT aResultCode,
                   LONG aResultDetail,
                   const GUID &aIID,
                   const char *pcszComponent,
                   const Utf8Str &strText,
                   IVirtualBoxErrorInfo *aNext = NULL);

    HRESULT init(const com::ErrorInfo &ei,
                 IVirtualBoxErrorInfo *aNext = NULL);

    // IVirtualBoxErrorInfo properties
    STDMETHOD(COMGETTER(ResultCode))(LONG *aResultCode) RT_OVERRIDE;
    STDMETHOD(COMGETTER(ResultDetail))(LONG *aResultDetail) RT_OVERRIDE;
    STDMETHOD(COMGETTER(InterfaceID))(BSTR *aIID) RT_OVERRIDE;
    STDMETHOD(COMGETTER(Component))(BSTR *aComponent) RT_OVERRIDE;
    STDMETHOD(COMGETTER(Text))(BSTR *aText) RT_OVERRIDE;
    STDMETHOD(COMGETTER(Next))(IVirtualBoxErrorInfo **aNext) RT_OVERRIDE;

    const char* getComponentName() const { return "VirtualBoxErrorInfo"; }

private:
    static HRESULT setError(HRESULT rc,
                            const char * /* a */,
                            const char * /* b */,
                            void *       /* c */) { return rc; }

    HRESULT m_resultCode;
    LONG    m_resultDetail;
    Utf8Str m_strText;
    Guid    m_IID;
    Utf8Str m_strComponent;
    ComPtr<IVirtualBoxErrorInfo> mNext;

#ifndef VBOX_WITH_XPCOM
    IUnknown *m_pUnkMarshaler;
#endif
};

#endif /* !MAIN_INCLUDED_VirtualBoxErrorInfoImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
