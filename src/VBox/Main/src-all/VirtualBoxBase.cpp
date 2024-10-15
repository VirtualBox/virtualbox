/* $Id: VirtualBoxBase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM base classes implementation
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
#include <iprt/semaphore.h>
#include <iprt/asm.h>
#include <iprt/cpp/exception.h>

#include <typeinfo>

#if !defined(VBOX_WITH_XPCOM)
# include <iprt/win/windows.h>
#else /* !defined(VBOX_WITH_XPCOM) */
/// @todo remove when VirtualBoxErrorInfo goes away from here
# include <nsIServiceManager.h>
# include <nsIExceptionService.h>
#endif /* !defined(VBOX_WITH_XPCOM) */

#include "VirtualBoxBase.h"
#include "AutoCaller.h"
#include "VirtualBoxErrorInfoImpl.h"
#include "VirtualBoxTranslator.h"
#include "Global.h"
#include "LoggingNew.h"

#include "VBox/com/ErrorInfo.h"
#include "VBox/com/MultiResult.h"

////////////////////////////////////////////////////////////////////////////////
//
// VirtualBoxBase
//
////////////////////////////////////////////////////////////////////////////////

CLASSFACTORY_STAT g_aClassFactoryStats[CLASSFACTORYSTATS_MAX] =
{
    { "--- totals ---", 0 },
    { NULL, 0 }
};

RWLockHandle *g_pClassFactoryStatsLock = NULL;


VirtualBoxBase::VirtualBoxBase() :
    mState(this),
    iFactoryStat(~0U)
{
    mObjectLock = NULL;

    if (!g_pClassFactoryStatsLock)
    {
        RWLockHandle *lock = new RWLockHandle(LOCKCLASS_OBJECTSTATE);
        if (!ASMAtomicCmpXchgPtr(&g_pClassFactoryStatsLock, lock, NULL))
            delete lock;
    }
    Assert(g_pClassFactoryStatsLock);
}

VirtualBoxBase::~VirtualBoxBase()
{
    Assert(iFactoryStat == ~0U);
    if (mObjectLock)
        delete mObjectLock;
}

HRESULT VirtualBoxBase::BaseFinalConstruct()
{
    Assert(iFactoryStat == ~0U);
    if (g_pClassFactoryStatsLock)
    {
        AutoWriteLock alock(g_pClassFactoryStatsLock COMMA_LOCKVAL_SRC_POS);
        g_aClassFactoryStats[0].current++;
        g_aClassFactoryStats[0].overall++;
        const char *pszName = getComponentName();
        uint32_t i = 1;
        while (i < CLASSFACTORYSTATS_MAX && g_aClassFactoryStats[i].psz)
        {
            if (g_aClassFactoryStats[i].psz == pszName)
                break;
            i++;
        }
        if (i < CLASSFACTORYSTATS_MAX)
        {
            if (!g_aClassFactoryStats[i].psz)
            {
                g_aClassFactoryStats[i].psz = pszName;
                g_aClassFactoryStats[i].current = 0;
                g_aClassFactoryStats[i].overall = 0;
            }
            iFactoryStat = i;
            g_aClassFactoryStats[i].current++;
            g_aClassFactoryStats[i].overall++;
        }
        else
            AssertMsg(i < CLASSFACTORYSTATS_MAX, ("%u exhausts size of factory housekeeping array\n", i));
    }
    else
        Assert(g_pClassFactoryStatsLock);

#ifdef RT_OS_WINDOWS
    return CoCreateFreeThreadedMarshaler(this, //GetControllingUnknown(),
                                         m_pUnkMarshaler.asOutParam());
#else
    return S_OK;
#endif
}

void VirtualBoxBase::BaseFinalRelease()
{
    if (g_pClassFactoryStatsLock)
    {
        AutoWriteLock alock(g_pClassFactoryStatsLock COMMA_LOCKVAL_SRC_POS);
        g_aClassFactoryStats[0].current--;
        const char *pszName = getComponentName();
        if (iFactoryStat < CLASSFACTORYSTATS_MAX)
        {
            if (g_aClassFactoryStats[iFactoryStat].psz == pszName)
            {
                g_aClassFactoryStats[iFactoryStat].current--;
                iFactoryStat = ~0U;
            }
            else
                AssertMsgFailed(("could not find factory housekeeping array entry for %s (index %u contains %s)\n", pszName, iFactoryStat, g_aClassFactoryStats[iFactoryStat].psz));
        }
        else
            AssertMsgFailed(("factory housekeeping array corruption, index %u is too large\n", iFactoryStat));
    }
    else
        Assert(g_pClassFactoryStatsLock);

#ifdef RT_OS_WINDOWS
     m_pUnkMarshaler.setNull();
#endif
}

void APIDumpComponentFactoryStats()
{
    if (g_pClassFactoryStatsLock)
    {
        AutoReadLock alock(g_pClassFactoryStatsLock COMMA_LOCKVAL_SRC_POS);
        for (uint32_t i = 0; i < CLASSFACTORYSTATS_MAX && g_aClassFactoryStats[i].psz; i++)
            LogRel(("CFS: component %-30s current %-10u total %-10u\n",
                    g_aClassFactoryStats[i].psz, g_aClassFactoryStats[i].current,
                    g_aClassFactoryStats[i].overall));
    }
    else
        Assert(g_pClassFactoryStatsLock);
}

/**
 * This virtual method returns an RWLockHandle that can be used to
 * protect instance data. This RWLockHandle is generally referred to
 * as the "object lock"; its locking class (for lock order validation)
 * must be returned by another virtual method, getLockingClass(), which
 * by default returns LOCKCLASS_OTHEROBJECT but is overridden by several
 * subclasses such as VirtualBox, Host, Machine and others.
 *
 * On the first call this method lazily creates the RWLockHandle.
 *
 * @return
 */
/* virtual */
RWLockHandle *VirtualBoxBase::lockHandle() const
{
    /* lazy initialization */
    if (RT_LIKELY(mObjectLock))
        return mObjectLock;

    AssertCompile(sizeof(RWLockHandle *) == sizeof(void *));

    // getLockingClass() is overridden by many subclasses to return
    // one of the locking classes listed at the top of AutoLock.h
    // getComponentName() returns the class name by default.
    RWLockHandle *objLock = new RWLockHandle(getLockingClass(), getComponentName());
    if (!ASMAtomicCmpXchgPtr(&mObjectLock, objLock, NULL))
    {
        delete objLock;
        objLock = ASMAtomicReadPtrT(&mObjectLock, RWLockHandle *);
    }
    return objLock;
}

/**
 * Handles unexpected exceptions by turning them into COM errors in release
 * builds or by hitting a breakpoint in the release builds.
 *
 * Usage pattern:
 * @code
        try
        {
            // ...
        }
        catch (LaLalA)
        {
            // ...
        }
        catch (...)
        {
            hrc = VirtualBox::handleUnexpectedExceptions(this, RT_SRC_POS);
        }
 * @endcode
 *
 * @param aThis     object where the exception happened
 * @param SRC_POS   "RT_SRC_POS" macro instantiation.
 *  */
/* static */
HRESULT VirtualBoxBase::handleUnexpectedExceptions(VirtualBoxBase *const aThis, RT_SRC_POS_DECL)
{
    try
    {
        /* re-throw the current exception */
        throw;
    }
    catch (const RTCError &err)      // includes all XML exceptions
    {
        return setErrorInternalF(E_FAIL, aThis->getClassIID(), aThis->getComponentName(),
                                 false /* aWarning */,
                                 true /* aLogIt */,
                                 0 /* aResultDetail */,
                                 "%s.\n%s[%d] (%s)",
                                 err.what(), pszFile, iLine, pszFunction);
    }
    catch (const std::exception &err)
    {
        return setErrorInternalF(E_FAIL, aThis->getClassIID(), aThis->getComponentName(),
                                 false /* aWarning */,
                                 true /* aLogIt */,
                                 0 /* aResultDetail */,
                                 tr("Unexpected exception: %s [%s]\n%s[%d] (%s)"),
                                 err.what(), typeid(err).name(), pszFile, iLine, pszFunction);
    }
    catch (...)
    {
        return setErrorInternalF(E_FAIL, aThis->getClassIID(), aThis->getComponentName(),
                                 false /* aWarning */,
                                 true /* aLogIt */,
                                 0 /* aResultDetail */,
                                 tr("Unknown exception\n%s[%d] (%s)"),
                                 pszFile, iLine, pszFunction);
    }

#ifndef _MSC_VER /* (unreachable) */
    /* should not get here */
    AssertFailed();
    return E_FAIL;
#endif
}


/**
 *  Sets error info for the current thread. This is an internal function that
 *  gets eventually called by all public variants.  If @a aWarning is
 *  @c true, then the highest (31) bit in the @a aResultCode value which
 *  indicates the error severity is reset to zero to make sure the receiver will
 *  recognize that the created error info object represents a warning rather
 *  than an error.
 */
/* static */
HRESULT VirtualBoxBase::setErrorInternalF(HRESULT aResultCode,
                                          const GUID &aIID,
                                          const char *aComponent,
                                          bool aWarning,
                                          bool aLogIt,
                                          LONG aResultDetail,
                                          const char *aText, ...)
{
    va_list va;
    va_start(va, aText);
    HRESULT hres = setErrorInternalV(aResultCode, aIID, aComponent, aText, va,
                                     aWarning, aLogIt, aResultDetail);
    va_end(va);
    return hres;
}

/**
 *  Sets error info for the current thread. This is an internal function that
 *  gets eventually called by all public variants.  If @a aWarning is
 *  @c true, then the highest (31) bit in the @a aResultCode value which
 *  indicates the error severity is reset to zero to make sure the receiver will
 *  recognize that the created error info object represents a warning rather
 *  than an error.
 */
/* static */
HRESULT VirtualBoxBase::setErrorInternalV(HRESULT aResultCode,
                                          const GUID &aIID,
                                          const char *aComponent,
                                          const char *aText,
                                          va_list aArgs,
                                          bool aWarning,
                                          bool aLogIt,
                                          LONG aResultDetail /* = 0*/)
{
    /* whether multi-error mode is turned on */
    bool preserve = MultiResult::isMultiEnabled();

    com::Utf8Str strText;
    if (aLogIt)
    {
#ifdef VBOX_WITH_MAIN_NLS
        strText = VirtualBoxTranslator::trSource(aText);
#else
        strText = aText;
#endif
        va_list va2;
        va_copy(va2, aArgs);
        LogRel(("%s [COM]: aRC=%Rhrc (%#08x) aIID={%RTuuid} aComponent={%s} aText={%N}, preserve=%RTbool aResultDetail=%d\n",
                aWarning ? "WARNING" : "ERROR",
                aResultCode,
                aResultCode,
                &aIID,
                aComponent,
                strText.c_str(),
                &va2,
                preserve,
                aResultDetail));
        va_end(va2);
    }

    /* these are mandatory, others -- not */
    AssertReturn((!aWarning && FAILED(aResultCode)) ||
                  (aWarning && aResultCode != S_OK),
                  E_FAIL);

    /* reset the error severity bit if it's a warning */
    if (aWarning)
        aResultCode &= ~0x80000000;

    HRESULT hrc = S_OK;

    if (aText == NULL || aText[0] == '\0')
    {
        /* Some default info */
        switch (aResultCode)
        {
            case E_INVALIDARG:                 strText = tr("A parameter has an invalid value"); break;
            case E_POINTER:                    strText = tr("A parameter is an invalid pointer"); break;
            case E_UNEXPECTED:                 strText = tr("The result of the operation is unexpected"); break;
            case E_ACCESSDENIED:               strText = tr("The access to an object is not allowed"); break;
            case E_OUTOFMEMORY:                strText = tr("The allocation of new memory failed"); break;
            case E_NOTIMPL:                    strText = tr("The requested operation is not implemented"); break;
            case E_NOINTERFACE:                strText = tr("The requested interface is not implemented"); break;
            case E_FAIL:                       strText = tr("A general error occurred"); break;
            case E_ABORT:                      strText = tr("The operation was canceled"); break;
            case VBOX_E_OBJECT_NOT_FOUND:      strText = tr("Object corresponding to the supplied arguments does not exist"); break;
            case VBOX_E_INVALID_VM_STATE:      strText = tr("Current virtual machine state prevents the operation"); break;
            case VBOX_E_VM_ERROR:              strText = tr("Virtual machine error occurred attempting the operation"); break;
            case VBOX_E_FILE_ERROR:            strText = tr("File not accessible or erroneous file contents"); break;
            case VBOX_E_IPRT_ERROR:            strText = tr("Runtime subsystem error"); break;
            case VBOX_E_PDM_ERROR:             strText = tr("Pluggable Device Manager error"); break;
            case VBOX_E_INVALID_OBJECT_STATE:  strText = tr("Current object state prohibits operation"); break;
            case VBOX_E_HOST_ERROR:            strText = tr("Host operating system related error"); break;
            case VBOX_E_NOT_SUPPORTED:         strText = tr("Requested operation is not supported"); break;
            case VBOX_E_XML_ERROR:             strText = tr("Invalid XML found"); break;
            case VBOX_E_INVALID_SESSION_STATE: strText = tr("Current session state prohibits operation"); break;
            case VBOX_E_OBJECT_IN_USE:         strText = tr("Object being in use prohibits operation"); break;
            case VBOX_E_PASSWORD_INCORRECT:    strText = tr("Incorrect password provided"); break;
            default:                           strText = tr("Unknown error"); break;
        }
    }
    else
    {
        va_list va2;
        va_copy(va2, aArgs);
        strText = com::Utf8StrFmt("%N", aText, &va2);
        va_end(va2);
    }

    do
    {
        ComObjPtr<VirtualBoxErrorInfo> info;
        hrc = info.createObject();
        if (FAILED(hrc)) break;

#if !defined(VBOX_WITH_XPCOM)

        ComPtr<IVirtualBoxErrorInfo> curInfo;
        if (preserve)
        {
            /* get the current error info if any */
            ComPtr<IErrorInfo> err;
            hrc = ::GetErrorInfo(0, err.asOutParam());
            if (FAILED(hrc)) break;
            hrc = err.queryInterfaceTo(curInfo.asOutParam());
            if (FAILED(hrc))
            {
                /* create a IVirtualBoxErrorInfo wrapper for the native
                 * IErrorInfo object */
                ComObjPtr<VirtualBoxErrorInfo> wrapper;
                hrc = wrapper.createObject();
                if (SUCCEEDED(hrc))
                {
                    hrc = wrapper->init(err);
                    if (SUCCEEDED(hrc))
                        curInfo = wrapper;
                }
            }
        }
        /* On failure, curInfo will stay null */
        Assert(SUCCEEDED(hrc) || curInfo.isNull());

        /* set the current error info and preserve the previous one if any */
        hrc = info->initEx(aResultCode, aResultDetail, aIID, aComponent, strText, curInfo);
        if (FAILED(hrc)) break;

        ComPtr<IErrorInfo> err;
        hrc = info.queryInterfaceTo(err.asOutParam());
        if (SUCCEEDED(hrc))
            hrc = ::SetErrorInfo(0, err);

#else // !defined(VBOX_WITH_XPCOM)

        nsCOMPtr <nsIExceptionService> es;
        es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &hrc);
        if (NS_SUCCEEDED(hrc))
        {
            nsCOMPtr <nsIExceptionManager> em;
            hrc = es->GetCurrentExceptionManager(getter_AddRefs(em));
            if (FAILED(hrc)) break;

            ComPtr<IVirtualBoxErrorInfo> curInfo;
            if (preserve)
            {
                /* get the current error info if any */
                ComPtr<nsIException> ex;
                hrc = em->GetCurrentException(ex.asOutParam());
                if (FAILED(hrc)) break;
                hrc = ex.queryInterfaceTo(curInfo.asOutParam());
                if (FAILED(hrc))
                {
                    /* create a IVirtualBoxErrorInfo wrapper for the native
                     * nsIException object */
                    ComObjPtr<VirtualBoxErrorInfo> wrapper;
                    hrc = wrapper.createObject();
                    if (SUCCEEDED(hrc))
                    {
                        hrc = wrapper->init(ex);
                        if (SUCCEEDED(hrc))
                            curInfo = wrapper;
                    }
                }
            }
            /* On failure, curInfo will stay null */
            Assert(SUCCEEDED(hrc) || curInfo.isNull());

            /* set the current error info and preserve the previous one if any */
            hrc = info->initEx(aResultCode, aResultDetail, aIID, aComponent, Bstr(strText), curInfo);
            if (FAILED(hrc)) break;

            ComPtr<nsIException> ex;
            hrc = info.queryInterfaceTo(ex.asOutParam());
            if (SUCCEEDED(hrc))
                hrc = em->SetCurrentException(ex);
        }
        else if (hrc == NS_ERROR_UNEXPECTED)
        {
            /*
             *  It is possible that setError() is being called by the object
             *  after the XPCOM shutdown sequence has been initiated
             *  (for example, when XPCOM releases all instances it internally
             *  references, which can cause object's FinalConstruct() and then
             *  uninit()). In this case, do_GetService() above will return
             *  NS_ERROR_UNEXPECTED and it doesn't actually make sense to
             *  set the exception (nobody will be able to read it).
             */
            Log1WarningFunc(("Will not set an exception because nsIExceptionService is not available (NS_ERROR_UNEXPECTED). XPCOM is being shutdown?\n"));
            hrc = NS_OK;
        }

#endif // !defined(VBOX_WITH_XPCOM)
    }
    while (0);

    AssertComRC(hrc);

    return SUCCEEDED(hrc) ? aResultCode : hrc;
}

/**
 * Shortcut instance method to calling the static setErrorInternal with the
 * class interface ID and component name inserted correctly. This uses the
 * virtual getClassIID() and getComponentName() methods which are automatically
 * defined by the VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT macro.
 * @param   aResultCode
 * @return
 */
HRESULT VirtualBoxBase::setError(HRESULT aResultCode)
{
    return setErrorInternalF(aResultCode,
                             this->getClassIID(),
                             this->getComponentName(),
                             false /* aWarning */,
                             true /* aLogIt */,
                             0 /* aResultDetail */,
                             NULL);
}

/**
 * Shortcut instance method to calling the static setErrorInternal with the
 * class interface ID and component name inserted correctly. This uses the
 * virtual getClassIID() and getComponentName() methods which are automatically
 * defined by the VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT macro.
 * @param   aResultCode
 * @param   pcsz
 * @return
 */
HRESULT VirtualBoxBase::setError(HRESULT aResultCode, const char *pcsz, ...)
{
    va_list args;
    va_start(args, pcsz);
    HRESULT hrc = setErrorInternalV(aResultCode,
                                    this->getClassIID(),
                                    this->getComponentName(),
                                    pcsz, args,
                                    false /* aWarning */,
                                    true /* aLogIt */);
    va_end(args);
    return hrc;
}

/**
 * Shortcut instance method to calling the static setErrorInternal with the
 * class interface ID and component name inserted correctly. This uses the
 * virtual getClassIID() and getComponentName() methods which are automatically
 * defined by the VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT macro.
 * @param   ei
 * @return
 */
HRESULT VirtualBoxBase::setError(const com::ErrorInfo &ei)
{
    /* whether multi-error mode is turned on */
    bool preserve = MultiResult::isMultiEnabled();

    HRESULT hrc = S_OK;

    do
    {
        ComObjPtr<VirtualBoxErrorInfo> info;
        hrc = info.createObject();
        if (FAILED(hrc)) break;

#if !defined(VBOX_WITH_XPCOM)

        ComPtr<IVirtualBoxErrorInfo> curInfo;
        if (preserve)
        {
            /* get the current error info if any */
            ComPtr<IErrorInfo> err;
            hrc = ::GetErrorInfo(0, err.asOutParam());
            if (FAILED(hrc)) break;
            hrc = err.queryInterfaceTo(curInfo.asOutParam());
            if (FAILED(hrc))
            {
                /* create a IVirtualBoxErrorInfo wrapper for the native
                 * IErrorInfo object */
                ComObjPtr<VirtualBoxErrorInfo> wrapper;
                hrc = wrapper.createObject();
                if (SUCCEEDED(hrc))
                {
                    hrc = wrapper->init(err);
                    if (SUCCEEDED(hrc))
                        curInfo = wrapper;
                }
            }
        }
        /* On failure, curInfo will stay null */
        Assert(SUCCEEDED(hrc) || curInfo.isNull());

        /* set the current error info and preserve the previous one if any */
        hrc = info->init(ei, curInfo);
        if (FAILED(hrc)) break;

        ComPtr<IErrorInfo> err;
        hrc = info.queryInterfaceTo(err.asOutParam());
        if (SUCCEEDED(hrc))
            hrc = ::SetErrorInfo(0, err);

#else // !defined(VBOX_WITH_XPCOM)

        nsCOMPtr <nsIExceptionService> es;
        es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &hrc);
        if (NS_SUCCEEDED(hrc))
        {
            nsCOMPtr <nsIExceptionManager> em;
            hrc = es->GetCurrentExceptionManager(getter_AddRefs(em));
            if (FAILED(hrc)) break;

            ComPtr<IVirtualBoxErrorInfo> curInfo;
            if (preserve)
            {
                /* get the current error info if any */
                ComPtr<nsIException> ex;
                hrc = em->GetCurrentException(ex.asOutParam());
                if (FAILED(hrc)) break;
                hrc = ex.queryInterfaceTo(curInfo.asOutParam());
                if (FAILED(hrc))
                {
                    /* create a IVirtualBoxErrorInfo wrapper for the native
                     * nsIException object */
                    ComObjPtr<VirtualBoxErrorInfo> wrapper;
                    hrc = wrapper.createObject();
                    if (SUCCEEDED(hrc))
                    {
                        hrc = wrapper->init(ex);
                        if (SUCCEEDED(hrc))
                            curInfo = wrapper;
                    }
                }
            }
            /* On failure, curInfo will stay null */
            Assert(SUCCEEDED(hrc) || curInfo.isNull());

            /* set the current error info and preserve the previous one if any */
            hrc = info->init(ei, curInfo);
            if (FAILED(hrc)) break;

            ComPtr<nsIException> ex;
            hrc = info.queryInterfaceTo(ex.asOutParam());
            if (SUCCEEDED(hrc))
                hrc = em->SetCurrentException(ex);
        }
        else if (hrc == NS_ERROR_UNEXPECTED)
        {
            /*
             *  It is possible that setError() is being called by the object
             *  after the XPCOM shutdown sequence has been initiated
             *  (for example, when XPCOM releases all instances it internally
             *  references, which can cause object's FinalConstruct() and then
             *  uninit()). In this case, do_GetService() above will return
             *  NS_ERROR_UNEXPECTED and it doesn't actually make sense to
             *  set the exception (nobody will be able to read it).
             */
            Log1WarningFunc(("Will not set an exception because nsIExceptionService is not available (NS_ERROR_UNEXPECTED). XPCOM is being shutdown?\n"));
            hrc = NS_OK;
        }

#endif // !defined(VBOX_WITH_XPCOM)
    }
    while (0);

    AssertComRC(hrc);

    return SUCCEEDED(hrc) ? ei.getResultCode() : hrc;
}

/**
 * Converts the VBox status code a COM one and sets the error info.
 *
 * The VBox status code is made available to the API user via
 * IVirtualBoxErrorInfo::resultDetail attribute.
 *
 * @param   vrc             The VBox status code.
 * @return  COM status code appropriate for @a vrc.
 *
 * @sa      VirtualBoxBase::setError(HRESULT)
 */
HRESULT VirtualBoxBase::setErrorVrc(int vrc)
{
    return setErrorInternalF(Global::vboxStatusCodeToCOM(vrc),
                             this->getClassIID(),
                             this->getComponentName(),
                             false /* aWarning */,
                             true /* aLogIt */,
                             vrc /* aResultDetail */,
                             Utf8StrFmt("%Rrc", vrc).c_str());
}

/**
 * Converts the VBox status code a COM one and sets the error info.
 *
 * @param   vrc             The VBox status code.
 * @param   pcszMsgFmt      Error message format string.
 * @param   va_args         Error message format string.
 * @return  COM status code appropriate for @a vrc.
 *
 * @sa      VirtualBoxBase::setError(HRESULT, const char *, ...)
 */
HRESULT VirtualBoxBase::setErrorVrcV(int vrc, const char *pcszMsgFmt, va_list va_args)
{
    return setErrorInternalV(Global::vboxStatusCodeToCOM(vrc),
                             this->getClassIID(),
                             this->getComponentName(),
                             pcszMsgFmt, va_args,
                             false /* aWarning */,
                             true /* aLogIt */,
                             vrc /* aResultDetail */);
}

/**
 * Converts the VBox status code a COM one and sets the error info.
 *
 * @param   vrc             The VBox status code.
 * @param   pcszMsgFmt      Error message format string.
 * @param   ...             Argument specified in the @a pcszMsgFmt
 * @return  COM status code appropriate for @a vrc.
 *
 * @sa      VirtualBoxBase::setError(HRESULT, const char *, ...)
 */
HRESULT VirtualBoxBase::setErrorVrc(int vrc, const char *pcszMsgFmt, ...)
{
    va_list va;
    va_start(va, pcszMsgFmt);
    HRESULT hrc = setErrorVrcV(vrc, pcszMsgFmt, va);
    va_end(va);
    return hrc;
}

/**
 * Sets error info with both a COM status and an VBox status code.
 *
 * The VBox status code is made available to the API user via
 * IVirtualBoxErrorInfo::resultDetail attribute.
 *
 * @param   hrc             The COM status code to return.
 * @param   vrc             The VBox status code.
 * @return  Most likely @a hrc, see setErrorInternal.
 *
 * @sa      VirtualBoxBase::setError(HRESULT)
 */
HRESULT VirtualBoxBase::setErrorBoth(HRESULT hrc, int vrc)
{
    return setErrorInternalF(hrc,
                             this->getClassIID(),
                             this->getComponentName(),
                             false /* aWarning */,
                             true /* aLogIt */,
                             vrc /* aResultDetail */,
                             Utf8StrFmt("%Rrc", vrc).c_str());
}

/**
 * Sets error info with a message and both a COM status and an VBox status code.
 *
 * The VBox status code is made available to the API user via
 * IVirtualBoxErrorInfo::resultDetail attribute.
 *
 * @param   hrc             The COM status code to return.
 * @param   vrc             The VBox status code.
 * @param   pcszMsgFmt      Error message format string.
 * @param   ...             Argument specified in the @a pcszMsgFmt
 * @return  Most likely @a hrc, see setErrorInternal.
 *
 * @sa      VirtualBoxBase::setError(HRESULT, const char *, ...)
 */
HRESULT VirtualBoxBase::setErrorBoth(HRESULT hrc, int vrc, const char *pcszMsgFmt, ...)
{
    va_list va;
    va_start(va, pcszMsgFmt);
    hrc = setErrorInternalV(hrc,
                            this->getClassIID(),
                            this->getComponentName(),
                            pcszMsgFmt, va,
                            false /* aWarning */,
                            true /* aLogIt */,
                            vrc /* aResultDetail */);
    va_end(va);
    return hrc;
}

/**
 * Like setError(), but sets the "warning" bit in the call to setErrorInternal().
 * @param aResultCode
 * @param pcsz
 * @return
 */
HRESULT VirtualBoxBase::setWarning(HRESULT aResultCode, const char *pcsz, ...)
{
    va_list args;
    va_start(args, pcsz);
    HRESULT hrc = setErrorInternalV(aResultCode,
                                    this->getClassIID(),
                                    this->getComponentName(),
                                    pcsz, args,
                                    true /* aWarning */,
                                    true /* aLogIt */);
    va_end(args);
    return hrc;
}

/**
 * Like setError(), but disables the "log" flag in the call to setErrorInternal().
 * @param aResultCode
 * @param pcsz
 * @return
 */
HRESULT VirtualBoxBase::setErrorNoLog(HRESULT aResultCode, const char *pcsz, ...)
{
    va_list args;
    va_start(args, pcsz);
    HRESULT hrc = setErrorInternalV(aResultCode,
                                    this->getClassIID(),
                                    this->getComponentName(),
                                    pcsz, args,
                                    false /* aWarning */,
                                    false /* aLogIt */);
    va_end(args);
    return hrc;
}

/**
 * Like setErrorBoth(), but disables the "log" flag in the call to setErrorInternal().
 * @param hrc
 * @param vrc
 * @param pcszMsgFmt
 * @param ...
 * @return
 */
HRESULT VirtualBoxBase::setErrorBothNoLog(HRESULT hrc, int vrc, const char *pcszMsgFmt, ...)
{
    va_list va;
    va_start(va, pcszMsgFmt);
    hrc = setErrorInternalV(hrc,
                            this->getClassIID(),
                            this->getComponentName(),
                            pcszMsgFmt, va,
                            false /* aWarning */,
                            false /* aLogIt */,
                            vrc /* aResultDetail */);
    va_end(va);
    return hrc;
}

/**
 * Clear the current error information.
 */
/*static*/
void VirtualBoxBase::clearError(void)
{
#if !defined(VBOX_WITH_XPCOM)
    ::SetErrorInfo(0, NULL);
#else
    HRESULT hrc = S_OK;
    nsCOMPtr <nsIExceptionService> es;
    es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &hrc);
    if (NS_SUCCEEDED(hrc))
    {
        nsCOMPtr <nsIExceptionManager> em;
        hrc = es->GetCurrentExceptionManager(getter_AddRefs(em));
        if (SUCCEEDED(hrc))
            em->SetCurrentException(NULL);
    }
#endif
}


////////////////////////////////////////////////////////////////////////////////
//
// MultiResult methods
//
////////////////////////////////////////////////////////////////////////////////

RTTLS MultiResult::sCounter = NIL_RTTLS;

/*static*/
void MultiResult::incCounter()
{
    if (sCounter == NIL_RTTLS)
    {
        sCounter = RTTlsAlloc();
        AssertReturnVoid(sCounter != NIL_RTTLS);
    }

    uintptr_t counter = (uintptr_t)RTTlsGet(sCounter);
    ++counter;
    RTTlsSet(sCounter, (void*)counter);
}

/*static*/
void MultiResult::decCounter()
{
    uintptr_t counter = (uintptr_t)RTTlsGet(sCounter);
    AssertReturnVoid(counter != 0);
    --counter;
    RTTlsSet(sCounter, (void*)counter);
}

/*static*/
bool MultiResult::isMultiEnabled()
{
    if (sCounter == NIL_RTTLS)
       return false;

    return ((uintptr_t)RTTlsGet(MultiResult::sCounter)) > 0;
}

