/* $Id: COMDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Various COM definitions and COM wrapper class declarations.
 *
 * This header is used in conjunction with the header generated from
 * XIDL expressed interface definitions to provide cross-platform Qt-based
 * interface wrapper classes.
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

#ifndef FEQT_INCLUDED_SRC_globals_COMDefs_h
#define FEQT_INCLUDED_SRC_globals_COMDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @defgroup   grp_QT_COM  Qt-COM Support Layer
 * @{
 *
 * The Qt-COM support layer provides a set of definitions and smart classes for
 * writing simple, clean and platform-independent code to access COM/XPCOM
 * components through exposed COM interfaces. This layer is based on the
 * COM/XPCOM Abstraction Layer library (the VBoxCOM glue library defined in
 * include/VBox/com and implemented in src/VBox/Main/glue).
 *
 * ...
 *
 * @defgroup   grp_QT_COM_arrays    Arrays
 * @{
 *
 * COM/XPCOM arrays are mapped to QVector objects. QVector templates declared
 * with a type that corresponds to the COM type of elements in the array using
 * normal Qt-COM type mapping rules. Here is a code example that demonstrates
 * how to call interface methods that take and return arrays (this example is
 * based on examples given in @ref grp_COM_arrays):
 * @code

    CSomething component;

    // ...

    QVector<LONG> in(3);
    in[0] = -1;
    in[1] = -2;
    in[2] = -3;

    QVector<LONG> out;
    QVector<LONG> ret;

    ret = component.TestArrays(in, out);

    for (size_t i = 0; i < ret.size(); ++ i)
        LogFlow(("*** ret[%u]=%d\n", i, ret[i]));

 * @endcode
 * @}
 */

/* Both VBox/com/assert.h and qglobal.h contain a definition of ASSERT.
 * Either of them can be already included here, so try to shut them up.  */
#undef ASSERT

#include <VBox/com/com.h>
#include <VBox/com/array.h>

#undef ASSERT

/* Qt includes */
#include <QMetaType>
#include <QString>
#include <QUuid>
#include <QVector>

/* GUI includes: */
#include "UILibraryDefs.h"

/*
 * Additional COM / XPCOM defines and includes
 */

#if !defined(VBOX_WITH_XPCOM)

#else /* !defined(VBOX_WITH_XPCOM) */

#include <nsXPCOM.h>
#include <nsMemory.h>
#include <nsIComponentManager.h>

class XPCOMEventQSocketListener;

#endif /* !defined(VBOX_WITH_XPCOM) */

/////////////////////////////////////////////////////////////////////////////

class CVirtualBoxErrorInfo;

/** Represents extended error information */
class SHARED_LIBRARY_STUFF COMErrorInfo
{
public:

    COMErrorInfo()
        : mIsNull(true),
          mIsBasicAvailable(false),
          mIsFullAvailable(false),
          mResultCode(S_OK),
          mResultDetail(0),
          m_pNext(NULL)
    {}

    COMErrorInfo(const COMErrorInfo &info)
    {
        copyFrom(info);
    }

    COMErrorInfo(const CVirtualBoxErrorInfo &info)
    {
        init(info);
    }

    ~COMErrorInfo()
    {
        cleanup();
    }

    COMErrorInfo& operator=(const COMErrorInfo &info)
    {
        cleanup();
        copyFrom(info);
        return *this;
    }

    bool isNull() const RT_NOEXCEPT { return mIsNull; }

    bool isBasicAvailable() const RT_NOEXCEPT { return mIsBasicAvailable; }
    bool isFullAvailable() const RT_NOEXCEPT { return mIsFullAvailable; }

    HRESULT resultCode() const RT_NOEXCEPT { return mResultCode; }
    ULONG resultDetail() const RT_NOEXCEPT { return mResultDetail; }
    QUuid interfaceID() const { return mInterfaceID; }
    QString component() const { return mComponent; }
    QString text() const { return mText; }

    const COMErrorInfo *next() const { return m_pNext; }

    QString interfaceName() const { return mInterfaceName; }
    QUuid calleeIID() const { return mCalleeIID; }
    QString calleeName() const { return mCalleeName; }

private:
    void init(const CVirtualBoxErrorInfo &info);
    void copyFrom(const COMErrorInfo &x);
    void cleanup();

    void fetchFromCurrentThread(IUnknown *callee, const GUID *calleeIID);

    static QString getInterfaceNameFromIID(const QUuid &id);

    bool mIsNull : 1;
    bool mIsBasicAvailable : 1;
    bool mIsFullAvailable : 1;

    HRESULT mResultCode;
    ULONG mResultDetail;
    QUuid mInterfaceID;
    QString mComponent;
    QString mText;

    COMErrorInfo *m_pNext;

    QString mInterfaceName;
    QUuid mCalleeIID;
    QString mCalleeName;

    friend class COMBaseWithEI;
};

/////////////////////////////////////////////////////////////////////////////

/**
 * Base COM class the CInterface template and all wrapper classes are derived
 * from. Provides common functionality for all COM wrappers.
 */
class SHARED_LIBRARY_STUFF COMBase
{
public:

    static HRESULT InitializeCOM(bool fGui);
    static HRESULT CleanupCOM();

    /**
     * Returns the result code of the last interface method called by the
     * wrapper instance or the result of CInterface::createInstance()
     * operation.
     */
    HRESULT lastRC() const { return mRC; }

#if !defined(VBOX_WITH_XPCOM)

    /** Converts a GUID value to QUuid */
    static QUuid ToQUuid(const GUID &id)
    {
        return QUuid(id.Data1, id.Data2, id.Data3,
                     id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3],
                     id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
    }

#else /* !defined(VBOX_WITH_XPCOM) */

    /** Converts a GUID value to QUuid */
    static QUuid ToQUuid(const nsID &id)
    {
        return QUuid(id.m0, id.m1, id.m2,
                     id.m3[0], id.m3[1], id.m3[2], id.m3[3],
                     id.m3[4], id.m3[5], id.m3[6], id.m3[7]);
    }

#endif /* !defined(VBOX_WITH_XPCOM) */

    /* Arrays of arbitrary types */

    template <typename QT, typename CT>
    static void ToSafeArray(const QVector<QT> &aVec, com::SafeArray<CT> &aArr)
    {
        aArr.reset(aVec.size());
        for (int i = 0; i < aVec.size(); ++i)
            aArr[i] = static_cast<CT>(aVec.at(i));
    }

    template <typename CT, typename QT>
    static void FromSafeArray(const com::SafeArray<CT> &aArr, QVector<QT> &aVec)
    {
        aVec.resize(static_cast<int>(aArr.size()));
        for (int i = 0; i < aVec.size(); ++i)
            aVec[i] = static_cast<QT>(aArr[i]);
    }

    template <typename QT, typename CT>
    static void ToSafeArray(const QVector<QT *> &aVec, com::SafeArray<CT *> &aArr)
    {
        Q_UNUSED(aVec);
        Q_UNUSED(aArr);
        AssertMsgFailedReturnVoid(("No conversion!\n"));
    }

    template <typename CT, typename QT>
    static void FromSafeArray(const com::SafeArray<CT *> &aArr, QVector<QT *> &aVec)
    {
        Q_UNUSED(aArr);
        Q_UNUSED(aVec);
        AssertMsgFailedReturnVoid(("No conversion!\n"));
    }

    /* Arrays of equal types */

    template <typename T>
    static void ToSafeArray(const QVector<T> &aVec, com::SafeArray<T> &aArr)
    {
        aArr.reset(aVec.size());
        for (int i = 0; i < aVec.size(); ++i)
            aArr[i] = aVec.at(i);
    }

    template <typename T>
    static void FromSafeArray(const com::SafeArray<T> &aArr, QVector<T> &aVec)
    {
        aVec.resize(static_cast<int>(aArr.size()));
        if (aArr.size())
            memcpy(&aVec[0], aArr.raw(), aArr.size() * sizeof(T));
    }

    /* Arrays of strings */

    static void ToSafeArray(const QVector<QString> &aVec,
                            com::SafeArray<BSTR> &aArr);
    static void FromSafeArray(const com::SafeArray<BSTR> &aArr,
                              QVector<QString> &aVec);

    /* Arrays of GUID */

    static void ToSafeArray(const QVector<QUuid> &aVec,
                            com::SafeGUIDArray &aArr);
    static void FromSafeArray(const com::SafeGUIDArray &aArr,
                              QVector<QUuid> &aVec);

    /* Arrays of GUID as BSTR */

    static void ToSafeArray(const QVector<QUuid> &aVec,
                            com::SafeArray<BSTR> &aArr);
    static void FromSafeArray(const com::SafeArray<BSTR> &aArr,
                              QVector<QUuid> &aVec);

    /* Arrays of enums. Does a cast similar to what ENUMOut does. */

    template <typename QE, typename CE>
    static void ToSafeArray(const QVector<QE> &aVec,
                            com::SafeIfaceArray<CE> &aArr)
    {
        aArr.reset(static_cast<int>(aVec.size()));
        for (int i = 0; i < aVec.size(); ++i)
            aArr[i] = static_cast<CE>(aVec.at(i));
    }

    template <typename CE, typename QE>
    static void FromSafeArray(const com::SafeIfaceArray<CE> &aArr,
                              QVector<QE> &aVec)
    {
        aVec.resize(static_cast<int>(aArr.size()));
        for (int i = 0; i < aVec.size(); ++i)
            aVec[i] = static_cast<QE>(aArr[i]);
    }

    /* Arrays of interface pointers. Note: we need a separate pair of names
     * only because the MSVC8 template matching algorithm is poor and tries to
     * instantiate a com::SafeIfaceArray<BSTR> (!!!) template otherwise for
     * *no* reason and fails. Note that it's also not possible to choose the
     * correct function by specifying template arguments explicitly because then
     * it starts to try to instantiate the com::SafeArray<I> template for
     * *no* reason again and fails too. Definitely, broken. Works in GCC like a
     * charm. */

    template <class CI, class I>
    static void ToSafeIfaceArray(const QVector<CI> &aVec,
                                 com::SafeIfaceArray<I> &aArr)
    {
        aArr.reset(static_cast<int>(aVec.size()));
        for (int i = 0; i < aVec.size(); ++i)
        {
            aArr[i] = aVec.at(i).raw();
            if (aArr[i])
                aArr[i]->AddRef();
        }
    }

    template <class I, class CI>
    static void FromSafeIfaceArray(const com::SafeIfaceArray<I> &aArr,
                                   QVector<CI> &aVec)
    {
        aVec.resize(static_cast<int>(aArr.size()));
        for (int i = 0; i < aVec.size(); ++i)
            aVec[i].attach(aArr[i]);
    }

protected:

    /* no arbitrary instance creations */
    COMBase() : mRC(S_OK) {}

#if defined(VBOX_WITH_XPCOM)
    static XPCOMEventQSocketListener *sSocketListener;
#endif

    /** Adapter to pass QString as input BSTR params */
    class BSTRIn
    {
    public:

        BSTRIn(const QString &s) : bstr(SysAllocString((const OLECHAR *)
            (s.isNull() ? 0 : s.utf16()))) {}

        ~BSTRIn()
        {
            if (bstr)
                SysFreeString(bstr);
        }

        operator BSTR() const { return bstr; }

    private:

        BSTR bstr;
    };

    /** Adapter to pass QString as output BSTR params */
    class BSTROut
    {
    public:

        BSTROut(QString &s) : str(s), bstr(0) {}

        ~BSTROut()
        {
            if (bstr) {
                str = QString::fromUtf16((const char16_t *)bstr);
                SysFreeString(bstr);
            }
        }

        operator BSTR *() { return &bstr; }

    private:

        QString &str;
        BSTR bstr;
    };

    /** Adapter to pass QUuid as input BSTR params */
    class GuidAsBStrIn
    {
    public:

        GuidAsBStrIn(const QUuid &s) : bstr(SysAllocString((const OLECHAR *)
            (s.isNull() ? 0 : s.toString().utf16()))) {}

        ~GuidAsBStrIn()
        {
            if (bstr)
                SysFreeString(bstr);
        }

        operator BSTR() const { return bstr; }

    private:

        BSTR bstr;
    };

    /** Adapter to pass QUuid as output BSTR params */
    class GuidAsBStrOut
    {
    public:

        GuidAsBStrOut(QUuid &s) : uuid(s), bstr(0) {}

        ~GuidAsBStrOut()
        {
            if (bstr) {
                uuid = QUuid(QString::fromUtf16((const char16_t *)bstr));
                SysFreeString(bstr);
            }
        }

        operator BSTR *() { return &bstr; }

    private:

        QUuid &uuid;
        BSTR bstr;
    };

    /**
     * Adapter to pass K* enums as output COM enum params (*_T).
     *
     * @param QE    K* enum.
     * @param CE    COM enum.
     */
    template <typename QE, typename CE>
    class ENUMOut
    {
    public:

        ENUMOut(QE &e) : qe(e), ce((CE)0) {}
        ~ENUMOut() { qe = (QE)ce; }
        operator CE *() { return &ce; }

    private:

        QE &qe;
        CE ce;
    };

#if !defined(VBOX_WITH_XPCOM)

    /** Adapter to pass QUuid as input GUID params */
    static GUID GUIDIn(const QUuid &uuid) { return uuid; }

    /** Adapter to pass QUuid as output GUID params */
    class GUIDOut
    {
    public:

        GUIDOut(QUuid &id) : uuid(id)
        {
            ::memset(&guid, 0, sizeof(GUID));
        }

        ~GUIDOut()
        {
            uuid = QUuid(
                guid.Data1, guid.Data2, guid.Data3,
                guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        }

        operator GUID *() { return &guid; }

    private:

        QUuid &uuid;
        GUID guid;
    };

#else /* !defined(VBOX_WITH_XPCOM) */

    /** Adapter to pass QUuid as input GUID params */
    static const nsID &GUIDIn(const QUuid &uuid)
    {
        return *(const nsID *) &uuid;
    }

    /** Adapter to pass QUuid as output GUID params */
    class GUIDOut
    {
    public:

        GUIDOut(QUuid &id) : uuid(id), nsid(0) {}

        ~GUIDOut()
        {
            if (nsid)
            {
                uuid = QUuid(
                    nsid->m0, nsid->m1, nsid->m2,
                    nsid->m3[0], nsid->m3[1], nsid->m3[2], nsid->m3[3],
                    nsid->m3[4], nsid->m3[5], nsid->m3[6], nsid->m3[7]);
                nsMemory::Free(nsid);
            }
        }

        operator nsID **() { return &nsid; }

    private:

        QUuid &uuid;
        nsID *nsid;
    };

#endif /* !defined(VBOX_WITH_XPCOM) */

    static void addref(IUnknown *aIface) { if (aIface) aIface->AddRef(); }
    static void release(IUnknown *aIface) { if (aIface) aIface->Release(); }

protected:

    mutable HRESULT mRC;

    friend class COMErrorInfo;
};

/////////////////////////////////////////////////////////////////////////////

/**
 * Alternative base class for the CInterface template that adds the errorInfo()
 * method for providing extended error info about unsuccessful invocation of the
 * last called interface method.
 */
class COMBaseWithEI : public COMBase
{
public:

    /**
     * Returns error info set by the last unsuccessfully invoked interface
     * method. Returned error info is useful only if CInterface::lastRC()
     * represents a failure or a warning (i.e. CInterface::isReallyOk() is
     * false).
     */
    const COMErrorInfo &errorInfo() const { return mErrInfo; }

protected:

    /* no arbitrary instance creation */
    COMBaseWithEI() : COMBase() {};

    void setErrorInfo(const COMErrorInfo &aErrInfo) { mErrInfo = aErrInfo; }

    void fetchErrorInfo(IUnknown *aCallee, const GUID *aCalleeIID) const
    {
        mErrInfo.fetchFromCurrentThread(aCallee, aCalleeIID);
    }

    mutable COMErrorInfo mErrInfo;
};

/////////////////////////////////////////////////////////////////////////////

/**
 * Simple class that encapsulates the result code and COMErrorInfo.
 */
class COMResult
{
public:

    COMResult() : mRC(S_OK) {}

    /**
     * Queries the current result code from the given component.
     */
    explicit COMResult(const COMBase &aComponent)
        : mRC(aComponent.lastRC()) {}

    /**
     * Queries the current result code and error info from the given component.
     */
    COMResult(const COMBaseWithEI &aComponent)
        : mRC(aComponent.lastRC()),
          mErrInfo(aComponent.errorInfo())
    { }

    /**
     * Queries the current result code from the given component.
     */
    COMResult &operator=(const COMBase &aComponent)
    {
        mRC = aComponent.lastRC();
        return *this;
    }

    /**
     * Queries the current result code and error info from the given component.
     */
    COMResult &operator=(const COMBaseWithEI &aComponent)
    {
        mRC = aComponent.lastRC();
        mErrInfo = aComponent.errorInfo();
        return *this;
    }

    bool isNull() const { return mErrInfo.isNull(); }

    /**
     * Returns @c true if the result code represents success (with or without
     * warnings).
     */
    bool isOk() const { return SUCCEEDED(mRC); }

    /**
     * Returns @c true if the result code represents success with one or more
     * warnings.
     */
    bool isWarning() const { return SUCCEEDED_WARNING(mRC); }

    /**
     * Returns @c true if the result code represents success with no warnings.
     */
    bool isReallyOk() const { return mRC == S_OK; }

    COMErrorInfo errorInfo() const { return mErrInfo; }
    HRESULT rc() const { return mRC; }

private:

    HRESULT mRC;
    COMErrorInfo mErrInfo;
};

/////////////////////////////////////////////////////////////////////////////

/**
 * Wrapper template class for all interfaces.
 *
 * All interface methods named as they are in the original, i.e. starting
 * with the capital letter. All utility non-interface methods are named
 * starting with the small letter. Utility methods should be not normally
 * called by the end-user client application.
 *
 * @param  I    Interface class (i.e. derived from IUnknown/nsISupports).
 * @param  B    Base class, either COMBase (by default) or COMBaseWithEI.
 */
template <class I, class B = COMBase>
class CInterface : public B
{
public:

    typedef B Base;
    typedef I Iface;

    // constructors & destructor

    CInterface()
    {
        clear();
    }

    CInterface(const CInterface &that) : B(that)
    {
        clear();
        mIface = that.mIface;
        this->addref((IUnknown*)ptr());
    }

    CInterface(I *aIface)
    {
        clear();
        setPtr(aIface);
        this->addref((IUnknown*)aIface);
    }

    virtual ~CInterface()
    {
        detach();
#ifdef RT_STRICT
        mDead = true;
#endif
    }

#ifdef VBOX_WITH_LESS_VIRTUALBOX_INCLUDING
    virtual IID const &getIID() const = 0;
#else
    IID const &getIID() const { return COM_IIDOF(I); }
#endif

    // utility methods
    void createInstance(const CLSID &aClsId)
    {
        AssertMsg(ptr() == NULL, ("Instance is already non-NULL\n"));
        if (ptr() == NULL)
        {
            I* pObj = NULL;
#if !defined(VBOX_WITH_XPCOM)
            B::mRC = CoCreateInstance(aClsId, NULL, CLSCTX_ALL, getIID(), (void **)&pObj);
#else
            nsCOMPtr<nsIComponentManager> manager;
            B::mRC = NS_GetComponentManager(getter_AddRefs(manager));
            if (SUCCEEDED(B::mRC))
                B::mRC = manager->CreateInstance(aClsId, nsnull, getIID(), (void **)&pObj);
#endif

            if (SUCCEEDED(B::mRC))
               setPtr(pObj);
            else
               setPtr(NULL);

            /* fetch error info, but don't assert if it's missing -- many other
             * reasons can lead to an error (w/o providing error info), not only
             * the instance initialization code (that should always provide it) */
            B::fetchErrorInfo(NULL, NULL);
         }
    }

    /**
     * Attaches to the given foreign interface pointer by querying the own
     * interface on it. The operation may fail.
     */
    template <class OI>
    void attach(OI *aIface)
    {
        Assert(!mDead);
        /* be aware of self assignment */
        I* amIface = ptr();
        this->addref((IUnknown*)aIface);
        this->release((IUnknown*)amIface);
        if (aIface)
        {
            amIface = NULL;
            B::mRC = aIface->QueryInterface(getIID(), (void **)&amIface);
            this->release((IUnknown*)aIface);
            setPtr(amIface);
        }
        else
        {
            setPtr(NULL);
            B::mRC = S_OK;
        }
    };

    /** Specialization of attach() for our own interface I. Never fails. */
    void attach(I *aIface)
    {
        Assert(!mDead);
        /* be aware of self assignment */
        this->addref((IUnknown*)aIface);
        this->release((IUnknown*)ptr());
        setPtr(aIface);
        B::mRC = S_OK;
    };

    /** Detaches from the underlying interface pointer. */
    void detach()
    {
       Assert(!mDead);
       this->release((IUnknown*)ptr());
       setPtr(NULL);
    }

    /** Returns @c true if not attached to any interface pointer. */
    bool isNull() const
    {
       Assert(!mDead);
       return mIface == NULL;
    }

    /** Returns @c true if attached to an interface pointer. */
    bool isNotNull() const
    {
       Assert(!mDead);
       return mIface != NULL;
    }

    /**
     * Returns @c true if the result code represents success (with or without
     * warnings).
     */
    bool isOk() const { return !isNull() && SUCCEEDED(B::mRC); }

    /**
     * Returns @c true if the result code represents success with one or more
     * warnings.
     */
    bool isWarning() const { return !isNull() && SUCCEEDED_WARNING(B::mRC); }

    /**
     * Returns @c true if the result code represents success with no warnings.
     */
    bool isReallyOk() const { return !isNull() && B::mRC == S_OK; }

    /**
     * Returns the last result code of this interface.
     */
    HRESULT rc() const { return B::mRC; }

    // utility operators

    CInterface &operator=(const CInterface &that)
    {
        attach(that.ptr());
        B::operator=(that);
        return *this;
    }

    CInterface &operator=(I *aIface)
    {
        attach(aIface);
        return *this;
    }

    /**
     * Returns the raw interface pointer. Not intended to be used for anything
     * else but in generated wrappers and for debugging. You've been warned.
     */
    I *raw() const
    {
       return ptr();
    }

    bool operator==(const CInterface &that) const { return ptr() == that.ptr(); }
    bool operator!=(const CInterface &that) const { return ptr() != that.ptr(); }

    I *ptr() const
    {
        Assert(!mDead);
        return mIface;
    }

    void setPtr(I* aObj) const
    {
        Assert(!mDead);
        mIface = aObj;
    }

private:
#ifdef RT_STRICT
    bool          mDead;
#endif
    mutable I *   mIface;

    void clear()
    {
       mIface = NULL;
#ifdef RT_STRICT
       mDead = false;
#endif
    }
};

/**
 * Partial specialization for CInterface template class above for a case when B == COMBase.
 *
 * We had to add it because on exporting template to a library at least on Windows there is
 * an implicit instantiation of the createInstance() member (even if it's not used) which
 * in case of base template uses API present in COMBaseWithEI class only, not in COMBase.
 *
 * @param  I  Brings the interface class (i.e. derived from IUnknown/nsISupports).
 */
template <class I>
class CInterface<I, COMBase> : public COMBase
{
public:

    typedef COMBase Base;
    typedef I       Iface;

    // constructors & destructor

    CInterface()
    {
        clear();
    }

    CInterface(const CInterface &that) : COMBase(that)
    {
        clear();
        mIface = that.mIface;
        this->addref((IUnknown*)ptr());
    }

    CInterface(I *pIface)
    {
        clear();
        setPtr(pIface);
        this->addref((IUnknown*)pIface);
    }

    virtual ~CInterface()
    {
        detach();
#ifdef RT_STRICT
        mDead = true;
#endif
    }

#ifdef VBOX_WITH_LESS_VIRTUALBOX_INCLUDING
    virtual IID const &getIID() const = 0;
#else
    IID const &getIID() const { return COM_IIDOF(I); }
#endif

    // utility methods

    void createInstance(const CLSID &clsId)
    {
        AssertMsg(ptr() == NULL, ("Instance is already non-NULL\n"));
        if (ptr() == NULL)
        {
            I* pObj = NULL;
#if !defined(VBOX_WITH_XPCOM)
            COMBase::mRC = CoCreateInstance(clsId, NULL, CLSCTX_ALL, getIID(), (void **)&pObj);
#else
            nsCOMPtr<nsIComponentManager> manager;
            COMBase::mRC = NS_GetComponentManager(getter_AddRefs(manager));
            if (SUCCEEDED(COMBase::mRC))
                COMBase::mRC = manager->CreateInstance(clsId, nsnull, getIID(), (void **)&pObj);
#endif

            if (SUCCEEDED(COMBase::mRC))
               setPtr(pObj);
            else
               setPtr(NULL);
         }
    }

    /**
     * Attaches to the given foreign interface pointer by querying the own
     * interface on it. The operation may fail.
     */
    template <class OI>
    void attach(OI *pIface)
    {
        Assert(!mDead);
        /* Be aware of self assignment: */
        I *pmIface = ptr();
        this->addref((IUnknown*)pIface);
        this->release((IUnknown*)pmIface);
        if (pIface)
        {
            pmIface = NULL;
            COMBase::mRC = pIface->QueryInterface(getIID(), (void **)&pmIface);
            this->release((IUnknown*)pIface);
            setPtr(pmIface);
        }
        else
        {
            setPtr(NULL);
            COMBase::mRC = S_OK;
        }
    };

    /** Specialization of attach() for our own interface I. Never fails. */
    void attach(I *pIface)
    {
        Assert(!mDead);
        /* Be aware of self assignment: */
        this->addref((IUnknown*)pIface);
        this->release((IUnknown*)ptr());
        setPtr(pIface);
        COMBase::mRC = S_OK;
    };

    /** Detaches from the underlying interface pointer. */
    void detach()
    {
        Assert(!mDead);
        this->release((IUnknown*)ptr());
        setPtr(NULL);
    }

    /** Returns @c true if not attached to any interface pointer. */
    bool isNull() const
    {
        Assert(!mDead);
        return mIface == NULL;
    }

    /** Returns @c true if attached to an interface pointer. */
    bool isNotNull() const
    {
        Assert(!mDead);
        return mIface != NULL;
    }

    /** Returns @c true if the result code represents success (with or without warnings). */
    bool isOk() const { return !isNull() && SUCCEEDED(COMBase::mRC); }

    /** Returns @c true if the result code represents success with one or more warnings. */
    bool isWarning() const { return !isNull() && SUCCEEDED_WARNING(COMBase::mRC); }

    /** Returns @c true if the result code represents success with no warnings. */
    bool isReallyOk() const { return !isNull() && COMBase::mRC == S_OK; }

    // utility operators

    CInterface &operator=(const CInterface &that)
    {
        attach(that.ptr());
        COMBase::operator=(that);
        return *this;
    }

    CInterface &operator=(I *pIface)
    {
        attach(pIface);
        return *this;
    }

    /**
     * Returns the raw interface pointer. Not intended to be used for anything
     * else but in generated wrappers and for debugging. You've been warned.
     */
    I *raw() const
    {
       return ptr();
    }

    bool operator==(const CInterface &that) const { return ptr() == that.ptr(); }
    bool operator!=(const CInterface &that) const { return ptr() != that.ptr(); }

    I *ptr() const
    {
        Assert(!mDead);
        return mIface;
    }

    void setPtr(I* aObj) const
    {
        Assert(!mDead);
        mIface = aObj;
    }

private:

#ifdef RT_STRICT
    bool       mDead;
#endif
    mutable I *mIface;

    void clear()
    {
       mIface = NULL;
#ifdef RT_STRICT
       mDead = false;
#endif
    }
};

/////////////////////////////////////////////////////////////////////////////

class CUnknown : public CInterface<IUnknown, COMBaseWithEI>
{
public:

    typedef CInterface<IUnknown, COMBaseWithEI> Base;

    CUnknown() {}

    /** Creates an instance given another CInterface-based instance. */
    template <class OI, class OB>
    explicit CUnknown(const CInterface<OI, OB> &that)
    {
        attach(that.ptr());
        if (SUCCEEDED(mRC))
        {
            /* preserve old error info if any */
            mRC = that.lastRC();
            setErrorInfo(that.errorInfo());
        }
    }

    /** Constructor specialization for IUnknown. */
    CUnknown(const CUnknown &that) : Base(that) {}

    /** Creates an instance given a foreign interface pointer. */
    template <class OI>
    explicit CUnknown(OI *aIface)
    {
        attach(aIface);
    }

    /** Constructor specialization for IUnknown. */
    explicit CUnknown(IUnknown *aIface) : Base(aIface) {}

    /** Assigns from another CInterface-based instance. */
    template <class OI, class OB>
    CUnknown &operator=(const CInterface<OI, OB> &that)
    {
        attach(that.ptr());
        if (SUCCEEDED(mRC))
        {
            /* preserve old error info if any */
            mRC = that.lastRC();
            setErrorInfo(that.errorInfo());
        }
        return *this;
    }

    /** Assignment specialization for CUnknown. */
    CUnknown &operator=(const CUnknown &that)
    {
        Base::operator=(that);
        return *this;
    }

    /** Assigns from a foreign interface pointer. */
    template <class OI>
    CUnknown &operator=(OI *aIface)
    {
        attach(aIface);
        return *this;
    }

    /** Assignment specialization for IUnknown. */
    CUnknown &operator=(IUnknown *aIface)
    {
        Base::operator=(aIface);
        return *this;
    }

#ifdef VBOX_WITH_LESS_VIRTUALBOX_INCLUDING
    IID const &getIID() const RT_OVERRIDE { return COM_IIDOF(IUnknown); }
#else
    IID const &getIID() const { return COM_IIDOF(IUnknown); }
#endif
};

/** @} */

#endif /* !FEQT_INCLUDED_SRC_globals_COMDefs_h */
