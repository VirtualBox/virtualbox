/* $Id: COMDefs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - CInterface implementation.
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

/* Qt includes: */
#include <QSocketNotifier>

/* GUI includes: */
#include "COMDefs.h"
#include "UILoggingDefs.h"

/* COM includes: */
#include "CVirtualBoxErrorInfo.h"

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>

#ifdef VBOX_WITH_XPCOM

/* Other VBox includes: */
# include <nsEventQueueUtils.h>
# include <nsIEventQueue.h>
# include <nsIExceptionService.h>

/* Mac OS X (Carbon mode) and OS/2 will notify the native queue
   internally in plevent.c. Because moc doesn't seems to respect
   #ifdefs, we still have to include the definition of the class.
   very silly. */
# if !defined(Q_OS_MAC)  && !defined(Q_OS_OS2)
XPCOMEventQSocketListener *COMBase::sSocketListener = 0;

# endif

/**
 *  Internal class to asynchronously handle IPC events on the GUI thread
 *  using the event queue socket FD and QSocketNotifier.
 */
class XPCOMEventQSocketListener : public QObject
{
    Q_OBJECT

public:

    XPCOMEventQSocketListener(nsIEventQueue *eq)
    {
        mEventQ = eq;
        mNotifier = new QSocketNotifier(mEventQ->GetEventQueueSelectFD(), QSocketNotifier::Read, this);
        QObject::connect(mNotifier, SIGNAL(activated (int)), this, SLOT(processEvents()));
    }

    virtual ~XPCOMEventQSocketListener()
    {
        delete mNotifier;
    }

public slots:

    void processEvents() { mEventQ->ProcessPendingEvents(); }

private:

    QSocketNotifier *mNotifier;
    nsCOMPtr<nsIEventQueue> mEventQ;
};

#endif /* defined(VBOX_WITH_XPCOM) */

/**
 *  Initializes COM/XPCOM.
 */
HRESULT COMBase::InitializeCOM(bool fGui)
{
    LogFlowFuncEnter();

    HRESULT rc = com::Initialize(fGui ? VBOX_COM_INIT_F_DEFAULT | VBOX_COM_INIT_F_GUI : VBOX_COM_INIT_F_DEFAULT);

#if defined(VBOX_WITH_XPCOM)

# if !defined(RT_OS_DARWIN) && !defined(RT_OS_OS2)

    if (NS_SUCCEEDED(rc))
    {
        nsCOMPtr<nsIEventQueue> eventQ;
        rc = NS_GetMainEventQ(getter_AddRefs(eventQ));
        if (NS_SUCCEEDED(rc))
        {
#  ifdef DEBUG
            BOOL isNative = FALSE;
            eventQ->IsQueueNative(&isNative);
            AssertMsg(isNative, ("The event queue must be native"));
#  endif
            BOOL isOnMainThread = FALSE;
            rc = eventQ->IsOnCurrentThread(&isOnMainThread);
            if (NS_SUCCEEDED(rc) && isOnMainThread)
            {
                sSocketListener = new XPCOMEventQSocketListener(eventQ);
            }
        }
    }

# endif /* !defined(RT_OS_DARWIN) && !defined(RT_OS_OS) */

#endif /* defined(VBOX_WITH_XPCOM) */

    if (FAILED(rc))
        CleanupCOM();

    AssertComRC(rc);

    LogFlowFunc(("rc=%08X\n", rc));
    LogFlowFuncLeave();
    return rc;

}

/**
 *  Cleans up COM/XPCOM.
 */
HRESULT COMBase::CleanupCOM()
{
    LogFlowFuncEnter();

    HRESULT rc = S_OK;

#if defined(VBOX_WITH_XPCOM)

    /* scope the code to make smart references are released before calling
     * com::Shutdown() */
    {
        nsCOMPtr<nsIEventQueue> eventQ;
        rc = NS_GetMainEventQ(getter_AddRefs(eventQ));
        if (NS_SUCCEEDED(rc))
        {
            BOOL isOnMainThread = FALSE;
            rc = eventQ->IsOnCurrentThread(&isOnMainThread);
            if (NS_SUCCEEDED(rc) && isOnMainThread)
            {
# if !defined(RT_OS_DARWIN) && !defined(RT_OS_OS2)
                if (sSocketListener)
                {
                    delete sSocketListener;
                    sSocketListener = NULL;
                }
# endif
            }
        }
    }

#endif /* defined(VBOX_WITH_XPCOM) */

    HRESULT rc2 = com::Shutdown();
    if (SUCCEEDED(rc))
        rc = rc2;

    AssertComRC(rc);

    LogFlowFunc(("rc=%08X\n", rc));
    LogFlowFuncLeave();
    return rc;
}

/* static */
void COMBase::ToSafeArray(const QVector<QString> &aVec,
                          com::SafeArray<BSTR> &aArr)
{
    aArr.reset(aVec.size());
    for (int i = 0; i < aVec.size(); ++ i)
        aArr [i] = SysAllocString((const OLECHAR *)(aVec.at(i).isNull() ? 0 : aVec.at(i).utf16()));
}

/* static */
void COMBase::FromSafeArray(const com::SafeArray<BSTR> &aArr,
                            QVector<QString> &aVec)
{
    AssertCompile(sizeof(aArr[0][0]) == sizeof(ushort));
    aVec.resize(static_cast<int>(aArr.size()));
    for (int i = 0; i < aVec.size(); ++ i)
        aVec[i] = QString::fromUtf16((const char16_t *)aArr[i]);
}

/* static */
void COMBase::ToSafeArray(const QVector <QUuid> &aVec,
                          com::SafeGUIDArray &aArr)
{
    AssertCompileSize(GUID, sizeof(QUuid));
    aArr.reset(aVec.size());
    for (int i = 0; i < aVec.size(); ++ i)
        aArr[i] = *(GUID *)&aVec[i];
}

/* static */
void COMBase::FromSafeArray(const com::SafeGUIDArray &aArr,
                            QVector<QUuid> &aVec)
{
    AssertCompileSize(GUID, sizeof(QUuid));
    aVec.resize (static_cast<int>(aArr.size()));
    for (int i = 0; i < aVec.size(); ++ i)
    {
#ifdef VBOX_WITH_XPCOM
        aVec[i] = *(QUuid *)&aArr[i];
#else
        /* No by-reference accessor, only by-value. So spell it out to avoid warnings. */
        GUID Tmp = aArr[i];
        aVec[i] = *(QUuid *)&Tmp;
#endif
    }
}

/* static */
void COMBase::ToSafeArray(const QVector<QUuid> &aVec,
                          com::SafeArray<BSTR> &aArr)
{
    aArr.reset(aVec.size());
    for (int i = 0; i < aVec.size(); ++ i)
        aArr[i] = SysAllocString((const OLECHAR *)(aVec.at(i).isNull() ? 0 : aVec.at(i).toString().utf16()));
}

/* static */
void COMBase::FromSafeArray(const com::SafeArray<BSTR> &aArr,
                            QVector<QUuid> &aVec)
{
    AssertCompile(sizeof(aArr[0][0]) == sizeof(ushort));
    aVec.resize(static_cast<int>(aArr.size()));
    for (int i = 0; i < aVec.size(); ++ i)
        aVec[i] = QUuid(QString::fromUtf16((const char16_t *)aArr[i]));
}

////////////////////////////////////////////////////////////////////////////////

void COMErrorInfo::init(const CVirtualBoxErrorInfo &info)
{
    if (info.isNull())
    {
        mIsNull = true;
        mIsBasicAvailable = false;
        mIsFullAvailable = false;
        mResultCode = S_OK;
        mResultDetail = 0;
        m_pNext = NULL;
        AssertMsgFailedReturnVoid(("error info is NULL!\n"));
    }

    bool gotSomething = false;
    bool gotAll = true;

    mResultCode = info.GetResultCode();
    gotSomething |= info.isOk();
    gotAll &= info.isOk();

    mResultDetail = info.GetResultDetail();
    gotSomething |= info.isOk();
    gotAll &= info.isOk();

    mInterfaceID = info.GetInterfaceID();
    gotSomething |= info.isOk();
    gotAll &= info.isOk();
    if (info.isOk())
        mInterfaceName = getInterfaceNameFromIID(mInterfaceID);

    mComponent = info.GetComponent();
    gotSomething |= info.isOk();
    gotAll &= info.isOk();

    mText = info.GetText();
    gotSomething |= info.isOk();
    gotAll &= info.isOk();

    m_pNext = NULL;

    CVirtualBoxErrorInfo next = info.GetNext();
    if (info.isOk() && !next.isNull())
    {
        m_pNext = new COMErrorInfo(next);
        Assert(m_pNext);
    }

    gotSomething |= info.isOk();
    gotAll &= info.isOk();

    mIsBasicAvailable = gotSomething;
    mIsFullAvailable = gotAll;

    mIsNull = !gotSomething;

    AssertMsg(gotSomething, ("Nothing to fetch!\n"));
}

void COMErrorInfo::copyFrom(const COMErrorInfo &x)
{
    mIsNull = x.mIsNull;
    mIsBasicAvailable = x.mIsBasicAvailable;
    mIsFullAvailable = x.mIsFullAvailable;

    mResultCode = x.mResultCode;
    mResultDetail = x.mResultDetail;
    mInterfaceID = x.mInterfaceID;
    mComponent = x.mComponent;
    mText = x.mText;

    if (x.m_pNext)
        m_pNext = new COMErrorInfo(*x.m_pNext);
    else
        m_pNext = NULL;

    mInterfaceName = x.mInterfaceName;
    mCalleeIID = x.mCalleeIID;
    mCalleeName = x.mCalleeName;
}

void COMErrorInfo::cleanup()
{
    if (m_pNext)
    {
        delete m_pNext;
        m_pNext = NULL;
    }
}

/**
 *  Fetches error info from the current thread.
 *  If callee is NULL, then error info is fetched in "interfaceless"
 *  manner (so calleeIID() and calleeName() will return null).
 *
 *  @param  callee
 *      pointer to the interface whose method returned an error
 *  @param  calleeIID
 *      UUID of the callee's interface. Ignored when callee is NULL
 */
void COMErrorInfo::fetchFromCurrentThread(IUnknown *callee, const GUID *calleeIID)
{
    mIsNull = true;
    mIsFullAvailable = mIsBasicAvailable = false;

    AssertReturnVoid(!callee || calleeIID);

    HRESULT rc = E_FAIL;

#if !defined(VBOX_WITH_XPCOM)

    if (callee)
    {
        ComPtr<IUnknown> iface(callee);
        ComPtr<ISupportErrorInfo> serr(iface);
        if (!serr)
            return;
        rc = serr->InterfaceSupportsErrorInfo(*calleeIID);
        if (!SUCCEEDED(rc))
            return;
    }

    ComPtr<IErrorInfo> err;
    rc = ::GetErrorInfo(0, err.asOutParam());
    if (rc == S_OK && err)
    {
        ComPtr<IVirtualBoxErrorInfo> info(err);
        if (info)
            init(CVirtualBoxErrorInfo(info));

        if (!mIsFullAvailable)
        {
            bool gotSomething = false;

            rc = err->GetGUID(COMBase::GUIDOut(mInterfaceID));
            gotSomething |= SUCCEEDED(rc);
            if (SUCCEEDED(rc))
                mInterfaceName = getInterfaceNameFromIID(mInterfaceID);

            rc = err->GetSource(COMBase::BSTROut(mComponent));
            gotSomething |= SUCCEEDED(rc);

            rc = err->GetDescription(COMBase::BSTROut(mText));
            gotSomething |= SUCCEEDED(rc);

            if (gotSomething)
                mIsBasicAvailable = true;

            mIsNull = !gotSomething;

            AssertMsg(gotSomething,("Nothing to fetch!\n"));
        }
    }

#else /* defined(VBOX_WITH_XPCOM) */

    nsCOMPtr<nsIExceptionService> es;
    es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &rc);
    if (NS_SUCCEEDED(rc))
    {
        nsCOMPtr<nsIExceptionManager> em;
        rc = es->GetCurrentExceptionManager(getter_AddRefs(em));
        if (NS_SUCCEEDED(rc))
        {
            nsCOMPtr<nsIException> ex;
            rc = em->GetCurrentException(getter_AddRefs(ex));
            if (NS_SUCCEEDED(rc) && ex)
            {
                nsCOMPtr<IVirtualBoxErrorInfo> info;
                info = do_QueryInterface(ex, &rc);
                if (NS_SUCCEEDED(rc) && info)
                    init(CVirtualBoxErrorInfo(info));

                if (!mIsFullAvailable)
                {
                    bool gotSomething = false;

                    rc = ex->GetResult(&mResultCode);
                    gotSomething |= NS_SUCCEEDED(rc);

                    char *message = NULL; // utf8
                    rc = ex->GetMessage(&message);
                    gotSomething |= NS_SUCCEEDED(rc);
                    if (NS_SUCCEEDED(rc) && message)
                    {
                        mText = QString::fromUtf8(message);
                        nsMemory::Free(message);
                    }

                    if (gotSomething)
                        mIsBasicAvailable = true;

                    mIsNull = !gotSomething;

                    AssertMsg(gotSomething, ("Nothing to fetch!\n"));
                }

                // set the exception to NULL (to emulate Win32 behavior)
                em->SetCurrentException(NULL);

                rc = NS_OK;
            }
        }
    }

    AssertComRC(rc);

#endif /* !defined(VBOX_WITH_XPCOM) */

    if (callee && calleeIID && mIsBasicAvailable)
    {
        mCalleeIID = COMBase::ToQUuid(*calleeIID);
        mCalleeName = getInterfaceNameFromIID(mCalleeIID);
    }
}

// static
QString COMErrorInfo::getInterfaceNameFromIID(const QUuid &id)
{
    QString name;

    com::GetInterfaceNameByIID(COMBase::GUIDIn(id), COMBase::BSTROut(name));

    return name;
}

#if defined (VBOX_WITH_XPCOM)
#include "COMDefs.moc"
#endif

