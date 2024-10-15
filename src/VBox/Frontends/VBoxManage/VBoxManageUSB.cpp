/* $Id: VBoxManageUSB.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - VirtualBox's command-line interface.
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

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>

#include "VBoxManage.h"

#include <iprt/asm.h>

using namespace com;

DECLARE_TRANSLATION_CONTEXT(Usb);

/**
 * Quick IUSBDevice implementation for detaching / attaching
 * devices to the USB Controller.
 */
class MyUSBDevice : public IUSBDevice
{
public:
    // public initializer/uninitializer for internal purposes only
    MyUSBDevice(uint16_t a_u16VendorId, uint16_t a_u16ProductId, uint16_t a_bcdRevision, uint64_t a_u64SerialHash, const char *a_pszComment)
        :  m_usVendorId(a_u16VendorId), m_usProductId(a_u16ProductId),
           m_bcdRevision(a_bcdRevision), m_u64SerialHash(a_u64SerialHash),
           m_bstrComment(a_pszComment),
           m_cRefs(0)
    {
    }
    virtual ~MyUSBDevice() {}

    STDMETHOD_(ULONG, AddRef)(void)
    {
        return ASMAtomicIncU32(&m_cRefs);
    }
    STDMETHOD_(ULONG, Release)(void)
    {
        ULONG cRefs = ASMAtomicDecU32(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }
    STDMETHOD(QueryInterface)(const IID &iid, void **ppvObject)
    {
        Guid guid(iid);
        if (guid == Guid(COM_IIDOF(IUnknown)))
            *ppvObject = (IUnknown *)this;
#ifdef RT_OS_WINDOWS
        else if (guid == Guid(COM_IIDOF(IDispatch)))
            *ppvObject = (IDispatch *)this;
#endif
        else if (guid == Guid(COM_IIDOF(IUSBDevice)))
            *ppvObject = (IUSBDevice *)this;
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    STDMETHOD(COMGETTER(Id))(OUT_GUID a_pId)                    { NOREF(a_pId); return E_NOTIMPL; }
    STDMETHOD(COMGETTER(VendorId))(USHORT *a_pusVendorId)       { *a_pusVendorId    = m_usVendorId;     return S_OK; }
    STDMETHOD(COMGETTER(ProductId))(USHORT *a_pusProductId)     { *a_pusProductId   = m_usProductId;    return S_OK; }
    STDMETHOD(COMGETTER(Revision))(USHORT *a_pusRevision)       { *a_pusRevision    = m_bcdRevision;    return S_OK; }
    STDMETHOD(COMGETTER(SerialHash))(ULONG64 *a_pullSerialHash) { *a_pullSerialHash = m_u64SerialHash;  return S_OK; }
    STDMETHOD(COMGETTER(Manufacturer))(BSTR *a_pManufacturer)   { NOREF(a_pManufacturer);   return E_NOTIMPL; }
    STDMETHOD(COMGETTER(Product))(BSTR *a_pProduct)             { NOREF(a_pProduct);        return E_NOTIMPL; }
    STDMETHOD(COMGETTER(SerialNumber))(BSTR *a_pSerialNumber)   { NOREF(a_pSerialNumber);   return E_NOTIMPL; }
    STDMETHOD(COMGETTER(Address))(BSTR *a_pAddress)             { NOREF(a_pAddress);        return E_NOTIMPL; }

private:
    /** The vendor id of this USB device. */
    USHORT m_usVendorId;
    /** The product id of this USB device. */
    USHORT m_usProductId;
    /** The product revision number of this USB device.
     * (high byte = integer; low byte = decimal) */
    USHORT m_bcdRevision;
    /** The USB serial hash of the device. */
    uint64_t m_u64SerialHash;
    /** The user comment string. */
    Bstr     m_bstrComment;
    /** Reference counter. */
    uint32_t volatile m_cRefs;
};


// types
///////////////////////////////////////////////////////////////////////////////

template <typename T>
class Nullable
{
public:

    Nullable() : mIsNull(true) {}
    Nullable(const T &aValue, bool aIsNull = false)
        : mIsNull(aIsNull), mValue(aValue) {}

    bool isNull() const { return mIsNull; };
    void setNull(bool aIsNull = true) { mIsNull = aIsNull; }

    operator const T&() const { return mValue; }

    Nullable &operator= (const T &aValue)
    {
        mValue = aValue;
        mIsNull = false;
        return *this;
    }

private:

    bool mIsNull;
    T mValue;
};

/** helper structure to encapsulate USB filter manipulation commands */
struct USBFilterCmd
{
    struct USBFilter
    {
        USBFilter()
            : mAction(USBDeviceFilterAction_Null)
            {}

        Bstr mName;
        Nullable <bool> mActive;
        Bstr mVendorId;
        Bstr mProductId;
        Bstr mRevision;
        Bstr mManufacturer;
        Bstr mProduct;
        Bstr mPort;
        Bstr mRemote;
        Bstr mSerialNumber;
        Nullable <ULONG> mMaskedInterfaces;
        USBDeviceFilterAction_T mAction;
    };

    enum Action { Invalid, Add, Modify, Remove };

    USBFilterCmd() : mAction(Invalid), mIndex(0), mGlobal(false) {}

    Action mAction;
    uint32_t mIndex;
    /** flag whether the command target is a global filter */
    bool mGlobal;
    /** machine this command is targeted at (null for global filters) */
    ComPtr<IMachine> mMachine;
    USBFilter mFilter;
};

RTEXITCODE handleUSBFilter(HandlerArg *a)
{
    HRESULT hrc = S_OK;
    USBFilterCmd cmd;

    /* which command? */
    cmd.mAction = USBFilterCmd::Invalid;
    if (!strcmp(a->argv[0], "add"))
    {
        cmd.mAction = USBFilterCmd::Add;
        setCurrentSubcommand(HELP_SCOPE_USBFILTER_ADD);
    }
    else if (!strcmp(a->argv[0], "modify"))
    {
        cmd.mAction = USBFilterCmd::Modify;
        setCurrentSubcommand(HELP_SCOPE_USBFILTER_MODIFY);
    }
    else if (!strcmp(a->argv[0], "remove"))
    {
        cmd.mAction = USBFilterCmd::Remove;
        setCurrentSubcommand(HELP_SCOPE_USBFILTER_REMOVE);
    }

    if (cmd.mAction == USBFilterCmd::Invalid)
        return errorUnknownSubcommand(a->argv[0]);

    /* which index? */
    if (VINF_SUCCESS != RTStrToUInt32Full(a->argv[1], 10, &cmd.mIndex))
        return errorSyntax(Usb::tr("Invalid index '%s'"), a->argv[1]);

    if (cmd.mAction == USBFilterCmd::Add || cmd.mAction == USBFilterCmd::Modify)
    {
        // set Active to true by default
        // (assuming that the user sets up all necessary attributes
        // at once and wants the filter to be active immediately)
        if (cmd.mAction == USBFilterCmd::Add)
            cmd.mFilter.mActive = true;

        RTGETOPTSTATE               GetState;
        RTGETOPTUNION               ValueUnion;
        static const RTGETOPTDEF    s_aOptions[] =
        {
            { "--target",               't',    RTGETOPT_REQ_STRING },
            { "--name",                 'n',    RTGETOPT_REQ_STRING },
            { "--active",               'a',    RTGETOPT_REQ_STRING },
            { "--vendorid",             'v',    RTGETOPT_REQ_STRING },
            { "--productid",            'p',    RTGETOPT_REQ_STRING },
            { "--revision",             'r',    RTGETOPT_REQ_STRING },
            { "--manufacturer",         'm',    RTGETOPT_REQ_STRING },
            { "--product",              'P',    RTGETOPT_REQ_STRING },
            { "--serialnumber",         's',    RTGETOPT_REQ_STRING },
            { "--port",                 'o',    RTGETOPT_REQ_STRING },
            { "--remote",               'R',    RTGETOPT_REQ_STRING },
            { "--maskedinterfaces",     'M',    RTGETOPT_REQ_UINT32 },
            { "--action",               'A',    RTGETOPT_REQ_STRING }
        };

        int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, 0 /*fFlags*/);
        AssertRCReturn(vrc, RTEXITCODE_FAILURE);

        while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
        {
            switch (vrc)
            {
                case 't':   // --target
                    if (!strcmp(ValueUnion.psz, "global"))
                        cmd.mGlobal = true;
                    else
                        CHECK_ERROR_RET(a->virtualBox, FindMachine(Bstr(ValueUnion.psz).raw(),
                                                                   cmd.mMachine.asOutParam()), RTEXITCODE_FAILURE);
                    break;
                case 'n':   // --name
                    cmd.mFilter.mName = ValueUnion.psz;
                    break;
                case 'a':   // --active
                    if (!strcmp(ValueUnion.psz, "yes"))
                        cmd.mFilter.mActive = true;
                    else if (!strcmp(ValueUnion.psz, "no"))
                        cmd.mFilter.mActive = false;
                   else
                        return errorArgument(Usb::tr("Invalid --active argument '%s'"), ValueUnion.psz);
                    break;
                case 'v':   // --vendorid
                    cmd.mFilter.mVendorId = ValueUnion.psz;
                    break;
                case 'p':   // --productid
                    cmd.mFilter.mProductId = ValueUnion.psz;
                    break;
                case 'r':   // --revision
                    cmd.mFilter.mRevision = ValueUnion.psz;
                    break;
                case 'm':   // --manufacturer
                    cmd.mFilter.mManufacturer = ValueUnion.psz;
                    break;
                case 'P':   // --product
                    cmd.mFilter.mProduct = ValueUnion.psz;
                    break;
                case 's':   // --serialnumber
                    cmd.mFilter.mSerialNumber = ValueUnion.psz;
                    break;
                case 'o':   // --port
                    cmd.mFilter.mPort = ValueUnion.psz;
                    break;
                case 'R':   // --remote
                    cmd.mFilter.mRemote = ValueUnion.psz;
                    break;
                case 'M':   // --maskedinterfaces
                    cmd.mFilter.mMaskedInterfaces = ValueUnion.u32;
                    break;
                case 'A':   // --action
                    if (!strcmp(ValueUnion.psz, "ignore"))
                        cmd.mFilter.mAction = USBDeviceFilterAction_Ignore;
                    else if (!strcmp(ValueUnion.psz, "hold"))
                        cmd.mFilter.mAction = USBDeviceFilterAction_Hold;
                    else
                        return errorArgument(Usb::tr("Invalid USB filter action '%s'"), ValueUnion.psz);
                    break;
                default:
                    return errorGetOpt(vrc, &ValueUnion);
            }
        }

        // mandatory/forbidden options
        if (!cmd.mGlobal && !cmd.mMachine)
            return errorSyntax(Usb::tr("Missing required option: --target"));

        if (cmd.mAction == USBFilterCmd::Add)
        {
            if (cmd.mFilter.mName.isEmpty())
                return errorSyntax(Usb::tr("Missing required option: --name"));

            if (cmd.mGlobal && cmd.mFilter.mAction == USBDeviceFilterAction_Null)
                return errorSyntax(Usb::tr("Missing required option: --action"));

            if (cmd.mGlobal && !cmd.mFilter.mRemote.isEmpty())
                return errorSyntax(Usb::tr("Option --remote applies to VM filters only (--target=<uuid|vmname>)"));
        }
    }
    else if (cmd.mAction == USBFilterCmd::Remove)
    {
        RTGETOPTSTATE               GetState;
        RTGETOPTUNION               ValueUnion;
        static const RTGETOPTDEF    s_aOptions[] =
        {
                { "--target",    't',    RTGETOPT_REQ_STRING }
        };
        int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, 0 /*fFlags*/);
        AssertRCReturn(vrc, RTEXITCODE_FAILURE);

        while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
        {
            switch (vrc)
            {
                case 't':   // --target
                    if (!strcmp(ValueUnion.psz, "global"))
                        cmd.mGlobal = true;
                    else
                        CHECK_ERROR_RET(a->virtualBox, FindMachine(Bstr(ValueUnion.psz).raw(),
                                                                   cmd.mMachine.asOutParam()), RTEXITCODE_FAILURE);
                    break;
                default:
                    return errorGetOpt(vrc, &ValueUnion);
            }
        }
        // mandatory options
        if (!cmd.mGlobal && !cmd.mMachine)
            return errorSyntax(Usb::tr("Missing required option: --target"));
    }

    USBFilterCmd::USBFilter &f = cmd.mFilter;

    ComPtr<IHost> host;
    ComPtr<IUSBDeviceFilters> flts;
    if (cmd.mGlobal)
        CHECK_ERROR_RET(a->virtualBox, COMGETTER(Host)(host.asOutParam()), RTEXITCODE_FAILURE);
    else
    {
        /* open a session for the VM */
        CHECK_ERROR_RET(cmd.mMachine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);
        /* get the mutable session machine */
        a->session->COMGETTER(Machine)(cmd.mMachine.asOutParam());
        /* and get the USB device filters */
        CHECK_ERROR_RET(cmd.mMachine, COMGETTER(USBDeviceFilters)(flts.asOutParam()), RTEXITCODE_FAILURE);
    }

    switch (cmd.mAction)
    {
        case USBFilterCmd::Add:
        {
            if (cmd.mGlobal)
            {
                ComPtr<IHostUSBDeviceFilter> flt;
                CHECK_ERROR_BREAK(host, CreateUSBDeviceFilter(f.mName.raw(),
                                                              flt.asOutParam()));

                if (!f.mActive.isNull())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Active)(f.mActive));
                if (!f.mVendorId.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(VendorId)(f.mVendorId.raw()));
                if (!f.mProductId.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(ProductId)(f.mProductId.raw()));
                if (!f.mRevision.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Revision)(f.mRevision.raw()));
                if (!f.mManufacturer.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Manufacturer)(f.mManufacturer.raw()));
                if (!f.mProduct.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Product)(f.mProduct.raw()));
                if (!f.mPort.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Port)(f.mPort.raw()));
                if (!f.mSerialNumber.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(SerialNumber)(f.mSerialNumber.raw()));
                if (!f.mMaskedInterfaces.isNull())
                    CHECK_ERROR_BREAK(flt, COMSETTER(MaskedInterfaces)(f.mMaskedInterfaces));

                if (f.mAction != USBDeviceFilterAction_Null)
                    CHECK_ERROR_BREAK(flt, COMSETTER(Action)(f.mAction));

                CHECK_ERROR_BREAK(host, InsertUSBDeviceFilter(cmd.mIndex, flt));
            }
            else
            {
                ComPtr<IUSBDeviceFilter> flt;
                CHECK_ERROR_BREAK(flts, CreateDeviceFilter(f.mName.raw(),
                                                          flt.asOutParam()));

                if (!f.mActive.isNull())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Active)(f.mActive));
                if (!f.mVendorId.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(VendorId)(f.mVendorId.raw()));
                if (!f.mProductId.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(ProductId)(f.mProductId.raw()));
                if (!f.mRevision.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Revision)(f.mRevision.raw()));
                if (!f.mManufacturer.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Manufacturer)(f.mManufacturer.raw()));
                if (!f.mProduct.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Product)(f.mProduct.raw()));
                if (!f.mPort.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Port)(f.mPort.raw()));
                if (!f.mRemote.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Remote)(f.mRemote.raw()));
                if (!f.mSerialNumber.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(SerialNumber)(f.mSerialNumber.raw()));
                if (!f.mMaskedInterfaces.isNull())
                    CHECK_ERROR_BREAK(flt, COMSETTER(MaskedInterfaces)(f.mMaskedInterfaces));

                CHECK_ERROR_BREAK(flts, InsertDeviceFilter(cmd.mIndex, flt));
            }
            break;
        }
        case USBFilterCmd::Modify:
        {
            if (cmd.mGlobal)
            {
                SafeIfaceArray <IHostUSBDeviceFilter> coll;
                CHECK_ERROR_BREAK(host, COMGETTER(USBDeviceFilters)(ComSafeArrayAsOutParam(coll)));

                ComPtr<IHostUSBDeviceFilter> flt = coll[cmd.mIndex];

                if (!f.mName.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Name)(f.mName.raw()));
                if (!f.mActive.isNull())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Active)(f.mActive));
                if (!f.mVendorId.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(VendorId)(f.mVendorId.raw()));
                if (!f.mProductId.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(ProductId)(f.mProductId.raw()));
                if (!f.mRevision.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Revision)(f.mRevision.raw()));
                if (!f.mManufacturer.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Manufacturer)(f.mManufacturer.raw()));
                if (!f.mProduct.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Product)(f.mProduct.raw()));
                if (!f.mPort.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Port)(f.mPort.raw()));
                if (!f.mSerialNumber.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(SerialNumber)(f.mSerialNumber.raw()));
                if (!f.mMaskedInterfaces.isNull())
                    CHECK_ERROR_BREAK(flt, COMSETTER(MaskedInterfaces)(f.mMaskedInterfaces));

                if (f.mAction != USBDeviceFilterAction_Null)
                    CHECK_ERROR_BREAK(flt, COMSETTER(Action)(f.mAction));
            }
            else
            {
                SafeIfaceArray <IUSBDeviceFilter> coll;
                CHECK_ERROR_BREAK(flts, COMGETTER(DeviceFilters)(ComSafeArrayAsOutParam(coll)));

                ComPtr<IUSBDeviceFilter> flt = coll[cmd.mIndex];

                if (!f.mName.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Name)(f.mName.raw()));
                if (!f.mActive.isNull())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Active)(f.mActive));
                if (!f.mVendorId.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(VendorId)(f.mVendorId.raw()));
                if (!f.mProductId.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(ProductId)(f.mProductId.raw()));
                if (!f.mRevision.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Revision)(f.mRevision.raw()));
                if (!f.mManufacturer.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Manufacturer)(f.mManufacturer.raw()));
                if (!f.mProduct.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Product)(f.mProduct.raw()));
                if (!f.mPort.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Port)(f.mPort.raw()));
                if (!f.mRemote.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(Remote)(f.mRemote.raw()));
                if (!f.mSerialNumber.isEmpty())
                    CHECK_ERROR_BREAK(flt, COMSETTER(SerialNumber)(f.mSerialNumber.raw()));
                if (!f.mMaskedInterfaces.isNull())
                    CHECK_ERROR_BREAK(flt, COMSETTER(MaskedInterfaces)(f.mMaskedInterfaces));
            }
            break;
        }
        case USBFilterCmd::Remove:
        {
            if (cmd.mGlobal)
            {
                ComPtr<IHostUSBDeviceFilter> flt;
                CHECK_ERROR_BREAK(host, RemoveUSBDeviceFilter(cmd.mIndex));
            }
            else
            {
                ComPtr<IUSBDeviceFilter> flt;
                CHECK_ERROR_BREAK(flts, RemoveDeviceFilter(cmd.mIndex, flt.asOutParam()));
            }
            break;
        }
        default:
            break;
    }

    if (cmd.mMachine)
    {
        if (SUCCEEDED(hrc))
        {
            /* commit the session */
            CHECK_ERROR(cmd.mMachine, SaveSettings());
        }
        /* close the session */
        a->session->UnlockMachine();
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleUSBDevSource(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    /* at least: 0: command, 1: source id */
    if (a->argc < 2)
        return errorSyntax(Usb::tr("Not enough parameters"));

    ComPtr<IHost> host;
    if (!strcmp(a->argv[0], "add"))
    {
        setCurrentSubcommand(HELP_SCOPE_USBDEVSOURCE_ADD);

        Bstr strBackend;
        Bstr strAddress;
        if (a->argc != 6)
            return errorSyntax(Usb::tr("Invalid number of parameters"));

        for (int i = 2; i < a->argc; i++)
        {
            if (!strcmp(a->argv[i], "--backend"))
            {
                i++;
                strBackend = a->argv[i];
            }
            else if (!strcmp(a->argv[i], "--address"))
            {
                i++;
                strAddress = a->argv[i];
            }
            else
                return errorSyntax(Usb::tr("Parameter \"%s\" is invalid"), a->argv[i]);
        }

        SafeArray<BSTR> usbSourcePropNames;
        SafeArray<BSTR> usbSourcePropValues;

        CHECK_ERROR_RET(a->virtualBox, COMGETTER(Host)(host.asOutParam()), RTEXITCODE_FAILURE);
        CHECK_ERROR_RET(host, AddUSBDeviceSource(strBackend.raw(), Bstr(a->argv[1]).raw(), strAddress.raw(),
                                                 ComSafeArrayAsInParam(usbSourcePropNames), ComSafeArrayAsInParam(usbSourcePropValues)),
                        RTEXITCODE_FAILURE);
    }
    else if (!strcmp(a->argv[0], "remove"))
    {
        setCurrentSubcommand(HELP_SCOPE_USBDEVSOURCE_REMOVE);
        CHECK_ERROR_RET(a->virtualBox, COMGETTER(Host)(host.asOutParam()), RTEXITCODE_FAILURE);
        CHECK_ERROR_RET(host, RemoveUSBDeviceSource(Bstr(a->argv[1]).raw()), RTEXITCODE_FAILURE);
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
