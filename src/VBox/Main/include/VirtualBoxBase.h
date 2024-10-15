/* $Id: VirtualBoxBase.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM base classes definition
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

#ifndef MAIN_INCLUDED_VirtualBoxBase_h
#define MAIN_INCLUDED_VirtualBoxBase_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/thread.h>

#include <list>
#include <map>

#include "ObjectState.h"

#include "VBox/com/AutoLock.h"
#include "VBox/com/string.h"
#include "VBox/com/Guid.h"

#include "VBox/com/VirtualBox.h"

#include "VirtualBoxTranslator.h"

// avoid including VBox/settings.h and VBox/xml.h; only declare the classes
namespace xml
{
class File;
}

namespace com
{
class ErrorInfo;
}

using namespace com;
using namespace util;

class VirtualBox;
class Machine;
class Medium;
class Host;
typedef std::list<ComObjPtr<Medium> > MediaList;
typedef std::list<Utf8Str> StringsList;

////////////////////////////////////////////////////////////////////////////////
//
// COM helpers
//
////////////////////////////////////////////////////////////////////////////////

#if !defined(VBOX_WITH_XPCOM)

/* use a special version of the singleton class factory,
 * see KB811591 in msdn for more info. */

#undef DECLARE_CLASSFACTORY_SINGLETON
#define DECLARE_CLASSFACTORY_SINGLETON(obj) DECLARE_CLASSFACTORY_EX(CMyComClassFactorySingleton<obj>)

/**
 * @todo r=bird: This CMyComClassFactorySingleton stuff is probably obsoleted by
 *                microatl.h? Right?
 */

template <class T>
class CMyComClassFactorySingleton : public ATL::CComClassFactory
{
public:
    CMyComClassFactorySingleton() :
        m_hrCreate(S_OK), m_spObj(NULL)
    {
    }
    virtual ~CMyComClassFactorySingleton()
    {
        if (m_spObj)
            m_spObj->Release();
    }
    // IClassFactory
    STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, void** ppvObj)
    {
        HRESULT hRes = E_POINTER;
        if (ppvObj != NULL)
        {
            *ppvObj = NULL;
            // no aggregation for singletons
            AssertReturn(pUnkOuter == NULL, CLASS_E_NOAGGREGATION);
            if (m_hrCreate == S_OK && m_spObj == NULL)
            {
                Lock();
                __try
                {
                    // Fix:  The following If statement was moved inside the __try statement.
                    // Did another thread arrive here first?
                    if (m_hrCreate == S_OK && m_spObj == NULL)
                    {
                        // lock the module to indicate activity
                        // (necessary for the monitor shutdown thread to correctly
                        // terminate the module in case when CreateInstance() fails)
                        ATL::_pAtlModule->Lock();
                        ATL::CComObjectCached<T> *p;
                        m_hrCreate = ATL::CComObjectCached<T>::CreateInstance(&p);
                        if (SUCCEEDED(m_hrCreate))
                        {
                            m_hrCreate = p->QueryInterface(IID_IUnknown, (void **)&m_spObj);
                            if (FAILED(m_hrCreate))
                            {
                                delete p;
                            }
                        }
                        ATL::_pAtlModule->Unlock();
                    }
                }
                __finally
                {
                    Unlock();
                }
            }
            if (m_hrCreate == S_OK)
            {
                hRes = m_spObj->QueryInterface(riid, ppvObj);
            }
            else
            {
                hRes = m_hrCreate;
            }
        }
        return hRes;
    }
    HRESULT m_hrCreate;
    IUnknown *m_spObj;
};

#endif /* !defined(VBOX_WITH_XPCOM) */

////////////////////////////////////////////////////////////////////////////////
//
// Macros
//
////////////////////////////////////////////////////////////////////////////////

/**
 *  Special version of the Assert macro to be used within VirtualBoxBase
 *  subclasses.
 *
 *  In the debug build, this macro is equivalent to Assert.
 *  In the release build, this macro uses |setError(E_FAIL, ...)| to set the
 *  error info from the asserted expression.
 *
 *  @see VirtualBoxBase::setError
 *
 *  @param   expr    Expression which should be true.
 */
#define ComAssert(expr) \
    do { \
        if (RT_LIKELY(!!(expr))) \
        { /* likely */ } \
        else \
        { \
            AssertMsgFailed(("%s\n", #expr)); \
            setError(E_FAIL, \
                     VirtualBoxBase::tr("Assertion failed: [%s] at '%s' (%d) in %s.\nPlease contact the product vendor!"), \
                     #expr, __FILE__, __LINE__, __PRETTY_FUNCTION__); \
        } \
    } while (0)

/**
 *  Special version of the AssertFailed macro to be used within VirtualBoxBase
 *  subclasses.
 *
 *  In the debug build, this macro is equivalent to AssertFailed.
 *  In the release build, this macro uses |setError(E_FAIL, ...)| to set the
 *  error info from the asserted expression.
 *
 *  @see VirtualBoxBase::setError
 *
 */
#define ComAssertFailed() \
    do { \
        AssertFailed(); \
        setError(E_FAIL, \
                 VirtualBoxBase::tr("Assertion failed: at '%s' (%d) in %s.\nPlease contact the product vendor!"), \
                 __FILE__, __LINE__, __PRETTY_FUNCTION__); \
    } while (0)

/**
 *  Special version of the AssertMsg macro to be used within VirtualBoxBase
 *  subclasses.
 *
 *  See ComAssert for more info.
 *
 *  @param   expr    Expression which should be true.
 *  @param   a       printf argument list (in parenthesis).
 */
#define ComAssertMsg(expr, a) \
    do { \
        if (RT_LIKELY(!!(expr))) \
        { /* likely */ } \
        else \
        { \
            Utf8StrFmt MyAssertMsg a; /* may throw bad_alloc */ \
            AssertMsgFailed(("%s\n", MyAssertMsg.c_str())); \
            setError(E_FAIL, \
                     VirtualBoxBase::tr("Assertion failed: [%s] at '%s' (%d) in %s.\n%s.\nPlease contact the product vendor!"), \
                     #expr, __FILE__, __LINE__, __PRETTY_FUNCTION__, MyAssertMsg.c_str()); \
        } \
    } while (0)

/**
 *  Special version of the AssertMsgFailed macro to be used within VirtualBoxBase
 *  subclasses.
 *
 *  See ComAssert for more info.
 *
 *  @param   a       printf argument list (in parenthesis).
 */
#define ComAssertMsgFailed(a) \
    do { \
        Utf8StrFmt MyAssertMsg a; /* may throw bad_alloc */ \
        AssertMsgFailed(("%s\n", MyAssertMsg.c_str())); \
        setError(E_FAIL, \
                 VirtualBoxBase::tr("Assertion failed: at '%s' (%d) in %s.\n%s.\nPlease contact the product vendor!"), \
                 __FILE__, __LINE__, __PRETTY_FUNCTION__, MyAssertMsg.c_str()); \
    } while (0)

/**
 *  Special version of the AssertRC macro to be used within VirtualBoxBase
 *  subclasses.
 *
 *  See ComAssert for more info.
 *
 * @param   vrc     VBox status code.
 */
#define ComAssertRC(vrc)            ComAssertMsgRC(vrc, ("%Rra", vrc))

/**
 *  Special version of the AssertMsgRC macro to be used within VirtualBoxBase
 *  subclasses.
 *
 *  See ComAssert for more info.
 *
 *  @param   vrc    VBox status code.
 *  @param   msg    printf argument list (in parenthesis).
 */
#define ComAssertMsgRC(vrc, msg)    ComAssertMsg(RT_SUCCESS(vrc), msg)

/**
 *  Special version of the AssertComRC macro to be used within VirtualBoxBase
 *  subclasses.
 *
 *  See ComAssert for more info.
 *
 *  @param hrc  COM result code
 */
#define ComAssertComRC(hrc)         ComAssertMsg(SUCCEEDED(hrc), ("COM RC=%Rhrc (0x%08X)", (hrc), (hrc)))


/** Special version of ComAssert that returns ret if expr fails */
#define ComAssertRet(expr, ret)             \
    do { ComAssert(expr); if (!(expr)) return (ret); } while (0)
/** Special version of ComAssertMsg that returns ret if expr fails */
#define ComAssertMsgRet(expr, a, ret)       \
    do { ComAssertMsg(expr, a); if (!(expr)) return (ret); } while (0)
/** Special version of ComAssertRC that returns ret if vrc does not succeed */
#define ComAssertRCRet(vrc, ret)            \
    do { ComAssertRC(vrc); if (!RT_SUCCESS(vrc)) return (ret); } while (0)
/** Special version of ComAssertComRC that returns ret if rc does not succeed */
#define ComAssertComRCRet(rc, ret)          \
    do { ComAssertComRC(rc); if (!SUCCEEDED(rc)) return (ret); } while (0)
/** Special version of ComAssertComRC that returns rc if rc does not succeed */
#define ComAssertComRCRetRC(rc)             \
    do { ComAssertComRC(rc); if (!SUCCEEDED(rc)) return (rc); } while (0)
/** Special version of ComAssertFailed that returns ret */
#define ComAssertFailedRet(ret)             \
    do { ComAssertFailed(); return (ret); } while (0)
/** Special version of ComAssertMsgFailed that returns ret */
#define ComAssertMsgFailedRet(msg, ret)     \
    do { ComAssertMsgFailed(msg); return (ret); } while (0)


/** Special version of ComAssert that returns void if expr fails */
#define ComAssertRetVoid(expr)                  \
    do { ComAssert(expr); if (!(expr)) return; } while (0)
/** Special version of ComAssertMsg that returns void if expr fails */
#define ComAssertMsgRetVoid(expr, a)            \
    do { ComAssertMsg(expr, a); if (!(expr)) return; } while (0)
/** Special version of ComAssertRC that returns void if vrc does not succeed */
#define ComAssertRCRetVoid(vrc)                 \
    do { ComAssertRC(vrc); if (!RT_SUCCESS(vrc)) return; } while (0)
/** Special version of ComAssertComRC that returns void if rc does not succeed */
#define ComAssertComRCRetVoid(rc)               \
    do { ComAssertComRC(rc); if (!SUCCEEDED(rc)) return; } while (0)
/** Special version of ComAssertFailed that returns void */
#define ComAssertFailedRetVoid()                \
    do { ComAssertFailed(); return; } while (0)
/** Special version of ComAssertMsgFailed that returns void */
#define ComAssertMsgFailedRetVoid(msg)          \
    do { ComAssertMsgFailed(msg); return; } while (0)


/** Special version of ComAssert that evaluates eval and breaks if expr fails */
#define ComAssertBreak(expr, eval)                \
    if (1) { ComAssert(expr); if (!(expr)) { eval; break; } } else do {} while (0)
/** Special version of ComAssertMsg that evaluates eval and breaks if expr fails */
#define ComAssertMsgBreak(expr, a, eval)          \
    if (1) { ComAssertMsg(expr, a); if (!(expr)) { eval; break; } } else do {} while (0)
/** Special version of ComAssertRC that evaluates eval and breaks if vrc does not succeed */
#define ComAssertRCBreak(vrc, eval)               \
    if (1) { ComAssertRC(vrc); if (!RT_SUCCESS(vrc)) { eval; break; } } else do {} while (0)
/** Special version of ComAssertFailed that evaluates eval and breaks */
#define ComAssertFailedBreak(eval)                \
    if (1) { ComAssertFailed(); { eval; break; } } else do {} while (0)
/** Special version of ComAssertMsgFailed that evaluates eval and breaks */
#define ComAssertMsgFailedBreak(msg, eval)        \
    if (1) { ComAssertMsgFailed (msg); { eval; break; } } else do {} while (0)
/** Special version of ComAssertComRC that evaluates eval and breaks if rc does not succeed */
#define ComAssertComRCBreak(rc, eval)             \
    if (1) { ComAssertComRC(rc); if (!SUCCEEDED(rc)) { eval; break; } } else do {} while (0)
/** Special version of ComAssertComRC that just breaks if rc does not succeed */
#define ComAssertComRCBreakRC(rc)                 \
    if (1) { ComAssertComRC(rc); if (!SUCCEEDED(rc)) { break; } } else do {} while (0)


/** Special version of ComAssert that evaluates eval and throws it if expr fails */
#define ComAssertThrow(expr, eval)                \
    do { ComAssert(expr); if (!(expr)) { throw (eval); } } while (0)
/** Special version of ComAssertRC that evaluates eval and throws it if vrc does not succeed */
#define ComAssertRCThrow(vrc, eval)               \
    do { ComAssertRC(vrc); if (!RT_SUCCESS(vrc)) { throw (eval); } } while (0)
/** Special version of ComAssertComRC that evaluates eval and throws it if rc does not succeed */
#define ComAssertComRCThrow(rc, eval)             \
    do { ComAssertComRC(rc); if (!SUCCEEDED(rc)) { throw (eval); } } while (0)
/** Special version of ComAssertComRC that just throws rc if rc does not succeed */
#define ComAssertComRCThrowRC(rc)                 \
    do { ComAssertComRC(rc); if (!SUCCEEDED(rc)) { throw rc; } } while (0)
/** Special version of ComAssert that throws eval */
#define ComAssertFailedThrow(eval)                \
    do { ComAssertFailed(); { throw (eval); } } while (0)

////////////////////////////////////////////////////////////////////////////////

/**
 * Checks that the pointer argument is not NULL and returns E_INVALIDARG +
 * extended error info on failure.
 * @param arg   Input pointer-type argument (strings, interface pointers...)
 */
#define CheckComArgNotNull(arg) \
    do { \
        if (RT_LIKELY((arg) != NULL)) \
        { /* likely */ }\
        else \
            return setError(E_INVALIDARG, VirtualBoxBase::tr("Argument %s is NULL"), #arg); \
    } while (0)

/**
 * Checks that the pointer argument is a valid pointer or NULL and returns
 * E_INVALIDARG + extended error info on failure.
 * @param arg   Input pointer-type argument (strings, interface pointers...)
 */
#define CheckComArgMaybeNull(arg) \
    do { \
        if (RT_LIKELY(RT_VALID_PTR(arg) || (arg) == NULL)) \
        { /* likely */ }\
        else \
            return setError(E_INVALIDARG, \
                            VirtualBoxBase::tr("Argument %s is an invalid pointer"), #arg); \
    } while (0)

/**
 * Checks that the given pointer to an argument is valid and returns
 * E_POINTER + extended error info otherwise.
 * @param arg   Pointer argument.
 */
#define CheckComArgPointerValid(arg) \
    do { \
        if (RT_LIKELY(RT_VALID_PTR(arg))) \
        { /* likely */ }\
        else \
            return setError(E_POINTER, \
                VirtualBoxBase::tr("Argument %s points to invalid memory location (%p)"), \
                #arg, (void *)(arg)); \
    } while (0)

/**
 * Checks that safe array argument is not NULL and returns E_INVALIDARG +
 * extended error info on failure.
 * @param arg   Input safe array argument (strings, interface pointers...)
 */
#define CheckComArgSafeArrayNotNull(arg) \
    do { \
        if (RT_LIKELY(!ComSafeArrayInIsNull(arg))) \
        { /* likely */ }\
        else \
            return setError(E_INVALIDARG, \
                            VirtualBoxBase::tr("Argument %s is NULL"), #arg); \
    } while (0)

/**
 * Checks that a string input argument is valid (not NULL or obviously invalid
 * pointer), returning E_INVALIDARG + extended error info if invalid.
 * @param a_bstrIn  Input string argument (IN_BSTR).
 */
#define CheckComArgStr(a_bstrIn) \
    do { \
        IN_BSTR const bstrInCheck = (a_bstrIn); /* type check */ \
        if (RT_LIKELY(RT_VALID_PTR(bstrInCheck))) \
        { /* likely */ }\
        else \
            return setError(E_INVALIDARG, \
                            VirtualBoxBase::tr("Argument %s is an invalid pointer"), #a_bstrIn); \
    } while (0)
/**
 * Checks that the string argument is not a NULL, a invalid pointer or an empty
 * string, returning E_INVALIDARG + extended error info on failure.
 * @param a_bstrIn  Input string argument (BSTR etc.).
 */
#define CheckComArgStrNotEmptyOrNull(a_bstrIn) \
    do { \
        IN_BSTR const bstrInCheck = (a_bstrIn); /* type check */ \
        if (RT_LIKELY(RT_VALID_PTR(bstrInCheck) && *(bstrInCheck) != '\0')) \
        { /* likely */ }\
        else \
            return setError(E_INVALIDARG, \
                            VirtualBoxBase::tr("Argument %s is empty or an invalid pointer"), \
                            #a_bstrIn); \
    } while (0)

/**
 * Converts the Guid input argument (string) to a Guid object, returns with
 * E_INVALIDARG and error message on failure.
 *
 * @param a_Arg     Argument.
 * @param a_GuidVar The Guid variable name.
 */
#define CheckComArgGuid(a_Arg, a_GuidVar) \
    do { \
        Guid tmpGuid(a_Arg); \
        (a_GuidVar) = tmpGuid; \
        if (RT_LIKELY((a_GuidVar).isValid())) \
        { /* likely */ }\
        else \
            return setError(E_INVALIDARG, \
                            VirtualBoxBase::tr("GUID argument %s is not valid (\"%ls\")"), \
                            #a_Arg, Bstr(a_Arg).raw()); \
    } while (0)

/**
 * Checks that the given expression (that must involve the argument) is true and
 * returns E_INVALIDARG + extended error info on failure.
 * @param arg   Argument.
 * @param expr  Expression to evaluate.
 */
#define CheckComArgExpr(arg, expr) \
    do { \
        if (RT_LIKELY(!!(expr))) \
        { /* likely */ }\
        else \
            return setError(E_INVALIDARG, \
                            VirtualBoxBase::tr("Argument %s is invalid (must be %s)"), \
                            #arg, #expr); \
    } while (0)

/**
 * Checks that the given expression (that must involve the argument) is true and
 * returns E_INVALIDARG + extended error info on failure. The error message must
 * be customized.
 * @param arg   Argument.
 * @param expr  Expression to evaluate.
 * @param msg   Parenthesized printf-like expression (must start with a verb,
 *              like "must be one of...", "is not within...").
 */
#define CheckComArgExprMsg(arg, expr, msg) \
    do { \
        if (RT_LIKELY(!!(expr))) \
        { /* likely */ }\
        else \
            return setError(E_INVALIDARG, VirtualBoxBase::tr("Argument %s %s"), \
                            #arg, Utf8StrFmt msg .c_str()); \
    } while (0)

/**
 * Checks that the given pointer to an output argument is valid and returns
 * E_POINTER + extended error info otherwise.
 * @param arg   Pointer argument.
 */
#define CheckComArgOutPointerValid(arg) \
    do { \
        if (RT_LIKELY(RT_VALID_PTR(arg))) \
        { /* likely */ }\
        else \
            return setError(E_POINTER, \
                            VirtualBoxBase::tr("Output argument %s points to invalid memory location (%p)"), \
                            #arg, (void *)(arg)); \
    } while (0)

/**
 * Checks that the given pointer to an output safe array argument is valid and
 * returns E_POINTER + extended error info otherwise.
 * @param arg   Safe array argument.
 */
#define CheckComArgOutSafeArrayPointerValid(arg) \
    do { \
        if (RT_LIKELY(!ComSafeArrayOutIsNull(arg))) \
        { /* likely */ }\
        else \
            return setError(E_POINTER, \
                            VirtualBoxBase::tr("Output argument %s points to invalid memory location (%p)"), \
                            #arg, (void*)(arg)); \
    } while (0)

/**
 * Sets the extended error info and returns E_NOTIMPL.
 */
#define ReturnComNotImplemented() \
    do { \
        return setError(E_NOTIMPL, VirtualBoxBase::tr("Method %s is not implemented"), __FUNCTION__); \
    } while (0)

/**
 *  Declares an empty constructor and destructor for the given class.
 *  This is useful to prevent the compiler from generating the default
 *  ctor and dtor, which in turn allows to use forward class statements
 *  (instead of including their header files) when declaring data members of
 *  non-fundamental types with constructors (which are always called implicitly
 *  by constructors and by the destructor of the class).
 *
 *  This macro is to be placed within (the public section of) the class
 *  declaration. Its counterpart, DEFINE_EMPTY_CTOR_DTOR, must be placed
 *  somewhere in one of the translation units (usually .cpp source files).
 *
 *  @param      cls     class to declare a ctor and dtor for
 */
#define DECLARE_EMPTY_CTOR_DTOR(cls) cls(); virtual ~cls();

/**
 *  Defines an empty constructor and destructor for the given class.
 *  See DECLARE_EMPTY_CTOR_DTOR for more info.
 */
#define DEFINE_EMPTY_CTOR_DTOR(cls) \
    cls::cls()  { /*empty*/ } \
    cls::~cls() { /*empty*/ }

/**
 *  A variant of 'throw' that hits a debug breakpoint first to make
 *  finding the actual thrower possible.
 */
#ifdef DEBUG
# define DebugBreakThrow(a) \
    do { \
        RTAssertDebugBreak(); \
        throw (a); \
    } while (0)
#else
# define DebugBreakThrow(a) throw (a)
#endif

////////////////////////////////////////////////////////////////////////////////
//
// VirtualBoxBase
//
////////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_WITH_MAIN_NLS
# define DECLARE_TRANSLATE_METHODS(cls) \
    static inline const char *tr(const char  *aSourceText, \
                                 const char  *aComment = NULL, \
                                 const size_t aNum = ~(size_t)0) \
    { \
        return VirtualBoxTranslator::translate(NULL, #cls, aSourceText, aComment, aNum); \
    }
#else
# define DECLARE_TRANSLATE_METHODS(cls) \
    static inline const char *tr(const char *aSourceText, \
                                 const char *aComment = NULL, \
                                 const size_t aNum = ~(size_t)0) \
    { \
        RT_NOREF(aComment, aNum); \
        return aSourceText; \
    }
#endif

#define DECLARE_COMMON_CLASS_METHODS(cls) \
    DECLARE_EMPTY_CTOR_DTOR(cls) \
    DECLARE_TRANSLATE_METHODS(cls)

#define VIRTUALBOXBASE_ADD_VIRTUAL_COMPONENT_METHODS(cls, iface) \
    virtual const IID &getClassIID() const RT_OVERRIDE \
    { \
        return cls::getStaticClassIID(); \
    } \
    static const IID &getStaticClassIID() \
    { \
        return COM_IIDOF(iface); \
    } \
    virtual const char *getComponentName() const RT_OVERRIDE \
    { \
        return cls::getStaticComponentName(); \
    } \
    static const char *getStaticComponentName() \
    { \
        return #cls; \
    }

/**
 * VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT:
 * This macro must be used once in the declaration of any class derived
 * from VirtualBoxBase. It implements the pure virtual getClassIID() and
 * getComponentName() methods. If this macro is not present, instances
 * of a class derived from VirtualBoxBase cannot be instantiated.
 *
 * @param cls The class name, e.g. "Class".
 * @param iface The interface name which this class implements, e.g. "IClass".
 */
#ifdef VBOX_WITH_XPCOM
  #define VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(cls, iface) \
    VIRTUALBOXBASE_ADD_VIRTUAL_COMPONENT_METHODS(cls, iface)
#else // !VBOX_WITH_XPCOM
  #define VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(cls, iface) \
    VIRTUALBOXBASE_ADD_VIRTUAL_COMPONENT_METHODS(cls, iface) \
    STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid) RT_OVERRIDE \
    { \
        const ATL::_ATL_INTMAP_ENTRY* pEntries = cls::_GetEntries(); \
        Assert(pEntries); \
        if (!pEntries) \
            return S_FALSE; \
        BOOL bSupports = FALSE; \
        BOOL bISupportErrorInfoFound = FALSE; \
        while (pEntries->pFunc != NULL && !bSupports) \
        { \
            if (!bISupportErrorInfoFound) \
                bISupportErrorInfoFound = InlineIsEqualGUID(*(pEntries->piid), IID_ISupportErrorInfo); \
            else \
                bSupports = InlineIsEqualGUID(*(pEntries->piid), riid); \
            pEntries++; \
        } \
        Assert(bISupportErrorInfoFound); \
        return bSupports ? S_OK : S_FALSE; \
    }
#endif // !VBOX_WITH_XPCOM

/**
 * VBOX_TWEAK_INTERFACE_ENTRY:
 * Macro for defining magic interface entries needed for all interfaces
 * implemented by any subclass of VirtualBoxBase.
 */
#ifdef VBOX_WITH_XPCOM
#define VBOX_TWEAK_INTERFACE_ENTRY(iface)
#else // !VBOX_WITH_XPCOM
#define VBOX_TWEAK_INTERFACE_ENTRY(iface)                                   \
        COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_pUnkMarshaler.m_p)
#endif // !VBOX_WITH_XPCOM


/**
 * Abstract base class for all component classes implementing COM
 * interfaces of the VirtualBox COM library.
 *
 * Declares functionality that should be available in all components.
 *
 * The object state logic is documented in ObjectState.h.
 */
class ATL_NO_VTABLE VirtualBoxBase
    : public Lockable
    , public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
#if !defined (VBOX_WITH_XPCOM)
    , public ISupportErrorInfo
#endif
{
protected:
#ifdef RT_OS_WINDOWS
     ComPtr<IUnknown> m_pUnkMarshaler;
#endif

     HRESULT BaseFinalConstruct();
     void BaseFinalRelease();

public:
    DECLARE_COMMON_CLASS_METHODS(VirtualBoxBase)

    /**
     * Uninitialization method.
     *
     * Must be called by all final implementations (component classes) when the
     * last reference to the object is released, before calling the destructor.
     *
     * @note Never call this method the AutoCaller scope or after the
     *       ObjectState::addCaller() call not paired by
     *       ObjectState::releaseCaller() because it is a guaranteed deadlock.
     *       See AutoUninitSpan and AutoCaller.h/ObjectState.h for details.
     */
    virtual void uninit()
    { }

    /**
     */
    ObjectState &getObjectState()
    {
        return mState;
    }

    /**
     * Pure virtual method for simple run-time type identification without
     * having to enable C++ RTTI.
     *
     * This *must* be implemented by every subclass deriving from VirtualBoxBase;
     * use the VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT macro to do that most easily.
     */
    virtual const IID& getClassIID() const = 0;

    /**
     * Pure virtual method for simple run-time type identification without
     * having to enable C++ RTTI.
     *
     * This *must* be implemented by every subclass deriving from VirtualBoxBase;
     * use the VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT macro to do that most easily.
     */
    virtual const char* getComponentName() const = 0;

    /**
     * Virtual method which determines the locking class to be used for validating
     * lock order with the standard member lock handle. This method is overridden
     * in a number of subclasses.
     */
    virtual VBoxLockingClass getLockingClass() const
    {
        return LOCKCLASS_OTHEROBJECT;
    }

    virtual RWLockHandle *lockHandle() const;

    static HRESULT handleUnexpectedExceptions(VirtualBoxBase *const aThis, RT_SRC_POS_DECL);

    static HRESULT setErrorInternalF(HRESULT aResultCode,
                                     const GUID &aIID,
                                     const char *aComponent,
                                     bool aWarning,
                                     bool aLogIt,
                                     LONG aResultDetail,
                                     const char *aText, ...);
    static HRESULT setErrorInternalV(HRESULT aResultCode,
                                     const GUID &aIID,
                                     const char *aComponent,
                                     const char *aText,
                                     va_list aArgs,
                                     bool aWarning,
                                     bool aLogIt,
                                     LONG aResultDetail = 0);
    static void clearError(void);

    HRESULT setError(HRESULT aResultCode);
    HRESULT setError(HRESULT aResultCode, const char *pcsz, ...);
    HRESULT setError(const ErrorInfo &ei);
    HRESULT setErrorVrcV(int vrc, const char *pcszMsgFmt, va_list va_args);
    HRESULT setErrorVrc(int vrc);
    HRESULT setErrorVrc(int vrc, const char *pcszMsgFmt, ...);
    HRESULT setErrorBoth(HRESULT hrc, int vrc);
    HRESULT setErrorBoth(HRESULT hrc, int vrc, const char *pcszMsgFmt, ...);
    HRESULT setWarning(HRESULT aResultCode, const char *pcsz, ...);
    HRESULT setErrorNoLog(HRESULT aResultCode, const char *pcsz, ...);
    HRESULT setErrorBothNoLog(HRESULT hrc, int vrc, const char *pcszMsgFmt, ...);


    /** Initialize COM for a new thread. */
    static HRESULT initializeComForThread(void)
    {
#ifndef VBOX_WITH_XPCOM
        HRESULT hrc = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY);
        AssertComRCReturn(hrc, hrc);
#endif
        return S_OK;
    }

    /** Uninitializes COM for a dying thread. */
    static void uninitializeComForThread(void)
    {
#ifndef VBOX_WITH_XPCOM
        CoUninitialize();
#endif
    }


private:
    /** Object for representing object state */
    ObjectState mState;

    /** User-level object lock for subclasses */
    RWLockHandle *mObjectLock;

    /** Slot of this object in the g_aClassFactoryStats array */
    uint32_t iFactoryStat;

private:
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(VirtualBoxBase); /* Shuts up MSC warning C4625. */
};

/** Structure for counting the currently existing and ever created objects
 * for each component name. */
typedef struct CLASSFACTORY_STAT
{
    const char *psz;
    uint64_t current;
    uint64_t overall;
} CLASSFACTORY_STAT;

/** Maximum number of component names to deal with. There will be debug
 * assertions if the value is too low. Since the table is global and its
 * entries are reasonably small, it's not worth squeezing out the last bit. */
#define CLASSFACTORYSTATS_MAX 128

/* global variables (defined in VirtualBoxBase.cpp) */
extern CLASSFACTORY_STAT g_aClassFactoryStats[CLASSFACTORYSTATS_MAX];
extern RWLockHandle *g_pClassFactoryStatsLock;

extern void APIDumpComponentFactoryStats();

/**
 * Dummy macro that is used to shut down Qt's lupdate tool warnings in some
 * situations. This macro needs to be present inside (better at the very
 * beginning) of the declaration of the class that uses translation, to make
 * lupdate happy.
 */
#define Q_OBJECT

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////


/**
 *  Simple template that manages data structure allocation/deallocation
 *  and supports data pointer sharing (the instance that shares the pointer is
 *  not responsible for memory deallocation as opposed to the instance that
 *  owns it).
 */
template <class D>
class Shareable
{
public:

    Shareable() : mData(NULL), mIsShared(FALSE) {}
    virtual ~Shareable() { free(); }

    void allocate() { attach(new D); }

    virtual void free() {
        if (mData) {
            if (!mIsShared)
                delete mData;
            mData = NULL;
            mIsShared = false;
        }
    }

    void attach(D *d) {
        AssertMsg(d, ("new data must not be NULL"));
        if (d && mData != d) {
            if (mData && !mIsShared)
                delete mData;
            mData = d;
            mIsShared = false;
        }
    }

    void attach(Shareable &d) {
        AssertMsg(
            d.mData == mData || !d.mIsShared,
            ("new data must not be shared")
        );
        if (this != &d && !d.mIsShared) {
            attach(d.mData);
            d.mIsShared = true;
        }
    }

    void share(D *d) {
        AssertMsg(d, ("new data must not be NULL"));
        if (mData != d) {
            if (mData && !mIsShared)
                delete mData;
            mData = d;
            mIsShared = true;
        }
    }

    void share(const Shareable &d) { share(d.mData); }

    void attachCopy(const D *d) {
        AssertMsg(d, ("data to copy must not be NULL"));
        if (d)
            attach(new D(*d));
    }

    void attachCopy(const Shareable &d) {
        attachCopy(d.mData);
    }

    virtual D *detach() {
        D *d = mData;
        mData = NULL;
        mIsShared = false;
        return d;
    }

    D *data() const {
        return mData;
    }

    D *operator->() const {
        AssertMsg(mData, ("data must not be NULL"));
        return mData;
    }

    bool isNull() const { return mData == NULL; }
    bool operator!() const { return isNull(); }

    bool isShared() const { return mIsShared; }

protected:

    D *mData;
    bool mIsShared;
};

/**
 *  Simple template that enhances Shareable<> and supports data
 *  backup/rollback/commit (using the copy constructor of the managed data
 *  structure).
 */
template<class D>
class Backupable : public Shareable<D>
{
public:

    Backupable() : Shareable<D>(), mBackupData(NULL) {}

    void free()
    {
        AssertMsg(this->mData || !mBackupData, ("backup must be NULL if data is NULL"));
        rollback();
        Shareable<D>::free();
    }

    D *detach()
    {
        AssertMsg(this->mData || !mBackupData, ("backup must be NULL if data is NULL"));
        rollback();
        return Shareable<D>::detach();
    }

    void share(const Backupable &d)
    {
        AssertMsg(!d.isBackedUp(), ("data to share must not be backed up"));
        if (!d.isBackedUp())
            Shareable<D>::share(d.mData);
    }

    /**
     *  Stores the current data pointer in the backup area, allocates new data
     *  using the copy constructor on current data and makes new data active.
     *
     *  @deprecated Use backupEx to avoid throwing wild out-of-memory exceptions.
     */
    void backup()
    {
        AssertMsg(this->mData, ("data must not be NULL"));
        if (this->mData && !mBackupData)
        {
            D *pNewData = new D(*this->mData);
            mBackupData = this->mData;
            this->mData = pNewData;
        }
    }

    /**
     *  Stores the current data pointer in the backup area, allocates new data
     *  using the copy constructor on current data and makes new data active.
     *
     *  @returns S_OK, E_OUTOFMEMORY or E_FAIL (internal error).
     */
    HRESULT backupEx()
    {
        AssertMsgReturn(this->mData, ("data must not be NULL"), E_FAIL);
        if (this->mData && !mBackupData)
        {
            try
            {
                D *pNewData = new D(*this->mData);
                mBackupData = this->mData;
                this->mData = pNewData;
            }
            catch (std::bad_alloc &)
            {
                return E_OUTOFMEMORY;
            }
        }
        return S_OK;
    }

    /**
     *  Deletes new data created by #backup() and restores previous data pointer
     *  stored in the backup area, making it active again.
     */
    void rollback()
    {
        if (this->mData && mBackupData)
        {
            delete this->mData;
            this->mData = mBackupData;
            mBackupData = NULL;
        }
    }

    /**
     *  Commits current changes by deleting backed up data and clearing up the
     *  backup area. The new data pointer created by #backup() remains active
     *  and becomes the only managed pointer.
     *
     *  This method is much faster than #commitCopy() (just a single pointer
     *  assignment operation), but makes the previous data pointer invalid
     *  (because it is freed). For this reason, this method must not be
     *  used if it's possible that data managed by this instance is shared with
     *  some other Shareable instance. See #commitCopy().
     */
    void commit()
    {
        if (this->mData && mBackupData)
        {
            if (!this->mIsShared)
                delete mBackupData;
            mBackupData = NULL;
            this->mIsShared = false;
        }
    }

    /**
     *  Commits current changes by assigning new data to the previous data
     *  pointer stored in the backup area using the assignment operator.
     *  New data is deleted, the backup area is cleared and the previous data
     *  pointer becomes active and the only managed pointer.
     *
     *  This method is slower than #commit(), but it keeps the previous data
     *  pointer valid (i.e. new data is copied to the same memory location).
     *  For that reason it's safe to use this method on instances that share
     *  managed data with other Shareable instances.
     */
    void commitCopy()
    {
        if (this->mData && mBackupData)
        {
            *mBackupData = *(this->mData);
            delete this->mData;
            this->mData = mBackupData;
            mBackupData = NULL;
        }
    }

    void assignCopy(const D *pData)
    {
        AssertMsg(this->mData, ("data must not be NULL"));
        AssertMsg(pData, ("data to copy must not be NULL"));
        if (this->mData && pData)
        {
            if (!mBackupData)
            {
                D *pNewData = new D(*pData);
                mBackupData = this->mData;
                this->mData = pNewData;
            }
            else
                *this->mData = *pData;
        }
    }

    void assignCopy(const Backupable &d)
    {
        assignCopy(d.mData);
    }

    bool isBackedUp() const
    {
        return mBackupData != NULL;
    }

    D *backedUpData() const
    {
        return mBackupData;
    }

protected:

    D *mBackupData;
};

#endif /* !MAIN_INCLUDED_VirtualBoxBase_h */

