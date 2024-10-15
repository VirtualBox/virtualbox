<?xml version="1.0"?>
<!-- $Id: capiidl.xsl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ -->

<!--
 *  A template to generate a C header file for all relevant XPCOM interfaces
 *  provided or needed for calling the VirtualBox API. The header file also
 *  works on Windows, by using the C bindings header created by the MS COM IDL
 *  compiler (which simultaneously supports C and C++, unlike XPCOM).
-->
<!--
    Copyright (C) 2008-2024 Oracle and/or its affiliates.

    This file is part of VirtualBox base platform packages, as
    available from https://www.virtualbox.org.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, in version 3 of the
    License.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <https://www.gnu.org/licenses>.

    SPDX-License-Identifier: GPL-3.0-only
-->

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text"/>

<xsl:strip-space elements="*"/>

<!-- Global parameters. -->
<xsl:param name="g_uVBoxCopyrightYear">2024</xsl:param>

<xsl:include href="../idl/typemap-shared.inc.xsl"/>

<!--
//  Keys for more efficiently looking up of types.
/////////////////////////////////////////////////////////////////////////////
-->

<xsl:key name="G_keyInterfacesByName" match="//interface[@name]" use="@name"/>


<!--
//  templates
/////////////////////////////////////////////////////////////////////////////
-->


<!--
 *  not explicitly matched elements and attributes
-->
<xsl:template match="*"/>


<!--
 *  header
-->
<xsl:template match="/idl">
  <xsl:text>/*
 *  DO NOT EDIT! This is a generated file.
 *
 *  Header file which provides C declarations for VirtualBox Main API
 *  (COM interfaces), generated from XIDL (XML interface definition).
 *  On Windows (which uses COM instead of XPCOM) the native C support
 *  is used, and most of this file is not used.
 *
 *  Source    : src/VBox/Main/idl/VirtualBox.xidl
 *  Generator : src/VBox/Main/cbinding/capiidl.xsl
 *
 *  This file contains portions from the following Mozilla XPCOM files:
 *      xpcom/include/xpcom/nsID.h
 *      xpcom/include/nsIException.h
 *      xpcom/include/nsprpub/prtypes.h
 *      xpcom/include/xpcom/nsISupportsBase.h
 *
 * These files were originally triple-licensed (MPL/GPL2/LGPL2.1). Oracle
 * elects to distribute this derived work under the LGPL2.1 only.
 */

/*
 * Copyright (C) 2008-</xsl:text><xsl:value-of select="$g_uVBoxCopyrightYear"/><xsl:text> Oracle and/or its affiliates.
 *
 * This file is part of a free software library; you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General
 * Public License version 2.1 as published by the Free Software
 * Foundation and shipped in the "COPYING.LIB" file with this library.
 * The library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY of any kind.
 *
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if
 * any license choice other than GPL or LGPL is available it will
 * apply instead, Oracle elects to use only the Lesser General Public
 * License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the
 * language indicating that LGPLv2 or any later version may be used,
 * or where a choice of which version of the LGPL is applied is
 * otherwise unspecified.
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#ifndef ___VirtualBox_CAPI_h
#define ___VirtualBox_CAPI_h

#ifdef _WIN32
# ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable:4668 4255) /* -Wall and windows.h */
#  if _MSC_VER >= 1800 /*RT_MSC_VER_VC120*/
#   pragma warning(disable:4005) /* sdk/v7.1/include/sal_supp.h(57) : warning C4005: '__useHeader' : macro redefinition */
#  endif
#  ifdef __cplusplus
#   if _MSC_VER >= 1900 /*RT_MSC_VER_VC140*/
#    pragma warning(disable:5039) /* winbase.h(13179): warning C5039: 'TpSetCallbackCleanupGroup': pointer or reference to potentially throwing function passed to 'extern "C"' function under -EHc. Undefined behavior may occur if this function throws an exception. */
#   endif
#  endif
# endif
# undef COBJMACROS
# define COBJMACROS
# include "Windows.h"
# pragma warning(pop)
#endif /* _WIN32 */

#ifdef WIN32
# ifdef IN_VBOXCAPI
#  define VBOXCAPI_DECL(type) extern __declspec(dllexport) type
# else /* !IN_VBOXCAPI */
#  define VBOXCAPI_DECL(type) __declspec(dllimport) type
# endif /* !IN_VBOXCAPI */
#endif /* WIN32 */

#ifdef __cplusplus
/* The C++ treatment in this file is not meant for SDK users, it only exists
 * so that this file can be used to produce the VBoxCAPI shared library which
 * has to use C++ as it does all the conversion magic. */
# ifdef IN_VBOXCAPI
#  include "VBox/com/VirtualBox.h"
#  ifndef WIN32
#   include "nsIEventQueue.h"
#  endif /* !WIN32 */
# else /* !IN_VBOXCAPI */
#  error Do not include this header file from C++ code
# endif /* !IN_VBOXCAPI */
#endif /* __cplusplus */

#ifdef __GNUC__
# define VBOX_EXTERN_CONST(type, name) extern const type name __attribute__((nocommon))
#else /* !__GNUC__ */
# define VBOX_EXTERN_CONST(type, name) extern const type name
#endif /* !__GNUC__ */

/* Treat WIN32 completely separately, as on Windows VirtualBox uses COM, not
 * XPCOM like on all other platforms. While the code below would also compile
 * on Windows, we need to switch to the native C support provided by the header
 * files produced by the COM IDL compiler. */
#ifdef WIN32
# include "ObjBase.h"
# include "oaidl.h"
# include "VirtualBox.h"

#ifndef __cplusplus
/* Skip this in the C++ case as there's already a definition for CBSTR. */
typedef const BSTR CBSTR;
#endif /* !__cplusplus */

#define VBOX_WINAPI WINAPI

#define ComSafeArrayAsInParam(f) (f)
#define ComSafeArrayAsOutParam(f) (&amp;(f))
#define ComSafeArrayAsOutTypeParam(f,t) (&amp;(f))
#define ComSafeArrayAsOutIfaceParam(f,t) (&amp;(f))

#else /* !WIN32 */

#include &lt;stddef.h&gt;
#include "wchar.h"

#ifdef IN_VBOXCAPI
# define VBOXCAPI_DECL(type) PR_EXPORT(type)
#else /* !IN_VBOXCAPI */
# define VBOXCAPI_DECL(type) PR_IMPORT(type)
#endif /* !IN_VBOXCAPI */

#ifndef __cplusplus

#if defined(WIN32)

#define PR_EXPORT(__type) extern __declspec(dllexport) __type
#define PR_EXPORT_DATA(__type) extern __declspec(dllexport) __type
#define PR_IMPORT(__type) __declspec(dllimport) __type
#define PR_IMPORT_DATA(__type) __declspec(dllimport) __type

#define PR_EXTERN(__type) extern __declspec(dllexport) __type
#define PR_IMPLEMENT(__type) __declspec(dllexport) __type
#define PR_EXTERN_DATA(__type) extern __declspec(dllexport) __type
#define PR_IMPLEMENT_DATA(__type) __declspec(dllexport) __type

#define PR_CALLBACK
#define PR_CALLBACK_DECL
#define PR_STATIC_CALLBACK(__x) static __x

#elif defined(XP_BEOS)

#define PR_EXPORT(__type) extern __declspec(dllexport) __type
#define PR_EXPORT_DATA(__type) extern __declspec(dllexport) __type
#define PR_IMPORT(__type) extern __declspec(dllexport) __type
#define PR_IMPORT_DATA(__type) extern __declspec(dllexport) __type

#define PR_EXTERN(__type) extern __declspec(dllexport) __type
#define PR_IMPLEMENT(__type) __declspec(dllexport) __type
#define PR_EXTERN_DATA(__type) extern __declspec(dllexport) __type
#define PR_IMPLEMENT_DATA(__type) __declspec(dllexport) __type

#define PR_CALLBACK
#define PR_CALLBACK_DECL
#define PR_STATIC_CALLBACK(__x) static __x

#elif defined(WIN16)

#define PR_CALLBACK_DECL        __cdecl

#if defined(_WINDLL)
#define PR_EXPORT(__type) extern __type _cdecl _export _loadds
#define PR_IMPORT(__type) extern __type _cdecl _export _loadds
#define PR_EXPORT_DATA(__type) extern __type _export
#define PR_IMPORT_DATA(__type) extern __type _export

#define PR_EXTERN(__type) extern __type _cdecl _export _loadds
#define PR_IMPLEMENT(__type) __type _cdecl _export _loadds
#define PR_EXTERN_DATA(__type) extern __type _export
#define PR_IMPLEMENT_DATA(__type) __type _export

#define PR_CALLBACK             __cdecl __loadds
#define PR_STATIC_CALLBACK(__x) static __x PR_CALLBACK

#else /* this must be .EXE */
#define PR_EXPORT(__type) extern __type _cdecl _export
#define PR_IMPORT(__type) extern __type _cdecl _export
#define PR_EXPORT_DATA(__type) extern __type _export
#define PR_IMPORT_DATA(__type) extern __type _export

#define PR_EXTERN(__type) extern __type _cdecl _export
#define PR_IMPLEMENT(__type) __type _cdecl _export
#define PR_EXTERN_DATA(__type) extern __type _export
#define PR_IMPLEMENT_DATA(__type) __type _export

#define PR_CALLBACK             __cdecl __loadds
#define PR_STATIC_CALLBACK(__x) __x PR_CALLBACK
#endif /* _WINDLL */

#elif defined(XP_MAC)

#define PR_EXPORT(__type) extern __declspec(export) __type
#define PR_EXPORT_DATA(__type) extern __declspec(export) __type
#define PR_IMPORT(__type) extern __declspec(export) __type
#define PR_IMPORT_DATA(__type) extern __declspec(export) __type

#define PR_EXTERN(__type) extern __declspec(export) __type
#define PR_IMPLEMENT(__type) __declspec(export) __type
#define PR_EXTERN_DATA(__type) extern __declspec(export) __type
#define PR_IMPLEMENT_DATA(__type) __declspec(export) __type

#define PR_CALLBACK
#define PR_CALLBACK_DECL
#define PR_STATIC_CALLBACK(__x) static __x

#elif defined(XP_OS2) &amp;&amp; defined(__declspec)

#define PR_EXPORT(__type) extern __declspec(dllexport) __type
#define PR_EXPORT_DATA(__type) extern __declspec(dllexport) __type
#define PR_IMPORT(__type) __declspec(dllimport) __type
#define PR_IMPORT_DATA(__type) __declspec(dllimport) __type

#define PR_EXTERN(__type) extern __declspec(dllexport) __type
#define PR_IMPLEMENT(__type) __declspec(dllexport) __type
#define PR_EXTERN_DATA(__type) extern __declspec(dllexport) __type
#define PR_IMPLEMENT_DATA(__type) __declspec(dllexport) __type

#define PR_CALLBACK
#define PR_CALLBACK_DECL
#define PR_STATIC_CALLBACK(__x) static __x

#elif defined(XP_OS2_VACPP)

#define PR_EXPORT(__type) extern __type
#define PR_EXPORT_DATA(__type) extern __type
#define PR_IMPORT(__type) extern __type
#define PR_IMPORT_DATA(__type) extern __type

#define PR_EXTERN(__type) extern __type
#define PR_IMPLEMENT(__type) __type
#define PR_EXTERN_DATA(__type) extern __type
#define PR_IMPLEMENT_DATA(__type) __type
#define PR_CALLBACK _Optlink
#define PR_CALLBACK_DECL
#define PR_STATIC_CALLBACK(__x) static __x PR_CALLBACK

#else /* Unix */

# ifdef VBOX_HAVE_VISIBILITY_HIDDEN
#  define PR_EXPORT(__type) __attribute__((visibility("default"))) extern __type
#  define PR_EXPORT_DATA(__type) __attribute__((visibility("default"))) extern __type
#  define PR_IMPORT(__type) extern __type
#  define PR_IMPORT_DATA(__type) extern __type
#  define PR_EXTERN(__type) __attribute__((visibility("default"))) extern __type
#  define PR_IMPLEMENT(__type) __attribute__((visibility("default"))) __type
#  define PR_EXTERN_DATA(__type) __attribute__((visibility("default"))) extern __type
#  define PR_IMPLEMENT_DATA(__type) __attribute__((visibility("default"))) __type
#  define PR_CALLBACK
#  define PR_CALLBACK_DECL
#  define PR_STATIC_CALLBACK(__x) static __x
# else
#  define PR_EXPORT(__type) extern __type
#  define PR_EXPORT_DATA(__type) extern __type
#  define PR_IMPORT(__type) extern __type
#  define PR_IMPORT_DATA(__type) extern __type
#  define PR_EXTERN(__type) extern __type
#  define PR_IMPLEMENT(__type) __type
#  define PR_EXTERN_DATA(__type) extern __type
#  define PR_IMPLEMENT_DATA(__type) __type
#  define PR_CALLBACK
#  define PR_CALLBACK_DECL
#  define PR_STATIC_CALLBACK(__x) static __x
# endif
#endif

#if defined(_NSPR_BUILD_)
#define NSPR_API(__type) PR_EXPORT(__type)
#define NSPR_DATA_API(__type) PR_EXPORT_DATA(__type)
#else
#define NSPR_API(__type) PR_IMPORT(__type)
#define NSPR_DATA_API(__type) PR_IMPORT_DATA(__type)
#endif

typedef unsigned char PRUint8;
#if (defined(HPUX) &amp;&amp; defined(__cplusplus) \
        &amp;&amp; !defined(__GNUC__) &amp;&amp; __cplusplus &lt; 199707L) \
    || (defined(SCO) &amp;&amp; defined(__cplusplus) \
        &amp;&amp; !defined(__GNUC__) &amp;&amp; __cplusplus == 1L)
typedef char PRInt8;
#else
typedef signed char PRInt8;
#endif

#define PR_INT8_MAX 127
#define PR_INT8_MIN (-128)
#define PR_UINT8_MAX 255U

typedef unsigned short PRUint16;
typedef short PRInt16;

#define PR_INT16_MAX 32767
#define PR_INT16_MIN (-32768)
#define PR_UINT16_MAX 65535U

typedef unsigned int PRUint32;
typedef int PRInt32;
#define PR_INT32(x)  x
#define PR_UINT32(x) x ## U

#define PR_INT32_MAX PR_INT32(2147483647)
#define PR_INT32_MIN (-PR_INT32_MAX - 1)
#define PR_UINT32_MAX PR_UINT32(4294967295)

typedef long PRInt64;
typedef unsigned long PRUint64;
typedef int PRIntn;
typedef unsigned int PRUintn;

typedef double          PRFloat64;
typedef size_t PRSize;

typedef ptrdiff_t PRPtrdiff;

typedef unsigned long PRUptrdiff;

typedef PRIntn PRBool;

#define PR_TRUE 1
#define PR_FALSE 0

typedef PRUint8 PRPackedBool;

/*
** Status code used by some routines that have a single point of failure or
** special status return.
*/
typedef enum { PR_FAILURE = -1, PR_SUCCESS = 0 } PRStatus;

#ifndef __PRUNICHAR__
#define __PRUNICHAR__
#if defined(WIN32) || defined(XP_MAC)
typedef wchar_t PRUnichar;
#else
typedef PRUint16 PRUnichar;
#endif
typedef PRUnichar *BSTR;
typedef const PRUnichar *CBSTR;
#endif

typedef long PRWord;
typedef unsigned long PRUword;

#define nsnull 0
typedef PRUint32 nsresult;

#if defined(__GNUC__) &amp;&amp; (__GNUC__ > 2)
#define NS_LIKELY(x)    (__builtin_expect((x), 1))
#define NS_UNLIKELY(x)  (__builtin_expect((x), 0))
#else
#define NS_LIKELY(x)    (x)
#define NS_UNLIKELY(x)  (x)
#endif

#define NS_FAILED(_nsresult) (NS_UNLIKELY((_nsresult) &amp; 0x80000000))
#define NS_SUCCEEDED(_nsresult) (NS_LIKELY(!((_nsresult) &amp; 0x80000000)))

#ifdef VBOX_WITH_XPCOM_NAMESPACE_CLEANUP
# define PR_IntervalNow VBoxNsprPR_IntervalNow
# define PR_TicksPerSecond VBoxNsprPR_TicksPerSecond
# define PR_SecondsToInterval VBoxNsprPR_SecondsToInterval
# define PR_MillisecondsToInterval VBoxNsprPR_MillisecondsToInterval
# define PR_MicrosecondsToInterval VBoxNsprPR_MicrosecondsToInterval
# define PR_IntervalToSeconds VBoxNsprPR_IntervalToSeconds
# define PR_IntervalToMilliseconds VBoxNsprPR_IntervalToMilliseconds
# define PR_IntervalToMicroseconds VBoxNsprPR_IntervalToMicroseconds
# define PR_EnterMonitor VBoxNsprPR_EnterMonitor
# define PR_ExitMonitor VBoxNsprPR_ExitMonitor
# define PR_Notify VBoxNsprPR_Notify
# define PR_NotifyAll VBoxNsprPR_NotifyAll
# define PR_Wait VBoxNsprPR_Wait
# define PR_NewMonitor VBoxNsprPR_NewMonitor
# define PR_DestroyMonitor VBoxNsprPR_DestroyMonitor
#endif /* VBOX_WITH_XPCOM_NAMESPACE_CLEANUP */

typedef PRUint32 PRIntervalTime;

#define PR_INTERVAL_MIN 1000UL
#define PR_INTERVAL_MAX 100000UL
#define PR_INTERVAL_NO_WAIT 0UL
#define PR_INTERVAL_NO_TIMEOUT 0xffffffffUL

NSPR_API(PRIntervalTime) PR_IntervalNow(void);
NSPR_API(PRUint32) PR_TicksPerSecond(void);
NSPR_API(PRIntervalTime) PR_SecondsToInterval(PRUint32 seconds);
NSPR_API(PRIntervalTime) PR_MillisecondsToInterval(PRUint32 milli);
NSPR_API(PRIntervalTime) PR_MicrosecondsToInterval(PRUint32 micro);
NSPR_API(PRUint32) PR_IntervalToSeconds(PRIntervalTime ticks);
NSPR_API(PRUint32) PR_IntervalToMilliseconds(PRIntervalTime ticks);
NSPR_API(PRUint32) PR_IntervalToMicroseconds(PRIntervalTime ticks);

typedef struct PRMonitor PRMonitor;

NSPR_API(PRMonitor*) PR_NewMonitor(void);
NSPR_API(void) PR_DestroyMonitor(PRMonitor *mon);
NSPR_API(void) PR_EnterMonitor(PRMonitor *mon);
NSPR_API(PRStatus) PR_ExitMonitor(PRMonitor *mon);
NSPR_API(PRStatus) PR_Wait(PRMonitor *mon, PRIntervalTime ticks);
NSPR_API(PRStatus) PR_Notify(PRMonitor *mon);
NSPR_API(PRStatus) PR_NotifyAll(PRMonitor *mon);

#ifdef VBOX_WITH_XPCOM_NAMESPACE_CLEANUP
# define PR_CreateThread VBoxNsprPR_CreateThread
# define PR_JoinThread VBoxNsprPR_JoinThread
# define PR_Sleep VBoxNsprPR_Sleep
# define PR_GetCurrentThread VBoxNsprPR_GetCurrentThread
# define PR_GetThreadState VBoxNsprPR_GetThreadState
# define PR_SetThreadPrivate VBoxNsprPR_SetThreadPrivate
# define PR_GetThreadPrivate VBoxNsprPR_GetThreadPrivate
# define PR_NewThreadPrivateIndex VBoxNsprPR_NewThreadPrivateIndex
# define PR_GetThreadPriority VBoxNsprPR_GetThreadPriority
# define PR_SetThreadPriority VBoxNsprPR_SetThreadPriority
# define PR_Interrupt VBoxNsprPR_Interrupt
# define PR_ClearInterrupt VBoxNsprPR_ClearInterrupt
# define PR_BlockInterrupt VBoxNsprPR_BlockInterrupt
# define PR_UnblockInterrupt VBoxNsprPR_UnblockInterrupt
# define PR_GetThreadScope VBoxNsprPR_GetThreadScope
# define PR_GetThreadType VBoxNsprPR_GetThreadType
#endif /* VBOX_WITH_XPCOM_NAMESPACE_CLEANUP */

typedef struct PRThread PRThread;
typedef struct PRThreadStack PRThreadStack;

typedef enum PRThreadType {
    PR_USER_THREAD,
    PR_SYSTEM_THREAD
} PRThreadType;

typedef enum PRThreadScope {
    PR_LOCAL_THREAD,
    PR_GLOBAL_THREAD,
    PR_GLOBAL_BOUND_THREAD
} PRThreadScope;

typedef enum PRThreadState {
    PR_JOINABLE_THREAD,
    PR_UNJOINABLE_THREAD
} PRThreadState;

typedef enum PRThreadPriority
{
    PR_PRIORITY_FIRST = 0,      /* just a placeholder */
    PR_PRIORITY_LOW = 0,        /* the lowest possible priority */
    PR_PRIORITY_NORMAL = 1,     /* most common expected priority */
    PR_PRIORITY_HIGH = 2,       /* slightly more aggressive scheduling */
    PR_PRIORITY_URGENT = 3,     /* it does little good to have more than one */
    PR_PRIORITY_LAST = 3        /* this is just a placeholder */
} PRThreadPriority;

NSPR_API(PRThread*) PR_CreateThread(PRThreadType type,
                     void (PR_CALLBACK *start)(void *arg),
                     void *arg,
                     PRThreadPriority priority,
                     PRThreadScope scope,
                     PRThreadState state,
                     PRUint32 stackSize);
NSPR_API(PRStatus) PR_JoinThread(PRThread *thread);
NSPR_API(PRThread*) PR_GetCurrentThread(void);
#ifndef NO_NSPR_10_SUPPORT
#define PR_CurrentThread() PR_GetCurrentThread() /* for nspr1.0 compat. */
#endif /* NO_NSPR_10_SUPPORT */
NSPR_API(PRThreadPriority) PR_GetThreadPriority(const PRThread *thread);
NSPR_API(void) PR_SetThreadPriority(PRThread *thread, PRThreadPriority priority);

typedef void (PR_CALLBACK *PRThreadPrivateDTOR)(void *priv);

NSPR_API(PRStatus) PR_NewThreadPrivateIndex(
    PRUintn *newIndex, PRThreadPrivateDTOR destructor);
NSPR_API(PRStatus) PR_SetThreadPrivate(PRUintn tpdIndex, void *priv);
NSPR_API(void*) PR_GetThreadPrivate(PRUintn tpdIndex);
NSPR_API(PRStatus) PR_Interrupt(PRThread *thread);
NSPR_API(void) PR_ClearInterrupt(void);
NSPR_API(void) PR_BlockInterrupt(void);
NSPR_API(void) PR_UnblockInterrupt(void);
NSPR_API(PRStatus) PR_Sleep(PRIntervalTime ticks);
NSPR_API(PRThreadScope) PR_GetThreadScope(const PRThread *thread);
NSPR_API(PRThreadType) PR_GetThreadType(const PRThread *thread);
NSPR_API(PRThreadState) PR_GetThreadState(const PRThread *thread);

#ifdef VBOX_WITH_XPCOM_NAMESPACE_CLEANUP
# define PR_DestroyLock VBoxNsprPR_DestroyLock
# define PR_Lock VBoxNsprPR_Lock
# define PR_NewLock VBoxNsprPR_NewLock
# define PR_Unlock VBoxNsprPR_Unlock
#endif /* VBOX_WITH_XPCOM_NAMESPACE_CLEANUP */

typedef struct PRLock PRLock;

NSPR_API(PRLock*) PR_NewLock(void);
NSPR_API(void) PR_DestroyLock(PRLock *lock);
NSPR_API(void) PR_Lock(PRLock *lock);
NSPR_API(PRStatus) PR_Unlock(PRLock *lock);

#ifdef VBOX_WITH_XPCOM_NAMESPACE_CLEANUP
# define PR_NewCondVar VBoxNsprPR_NewCondVar
# define PR_DestroyCondVar VBoxNsprPR_DestroyCondVar
# define PR_WaitCondVar VBoxNsprPR_WaitCondVar
# define PR_NotifyCondVar VBoxNsprPR_NotifyCondVar
# define PR_NotifyAllCondVar VBoxNsprPR_NotifyAllCondVar
#endif /* VBOX_WITH_XPCOM_NAMESPACE_CLEANUP */

typedef struct PRCondVar PRCondVar;

NSPR_API(PRCondVar*) PR_NewCondVar(PRLock *lock);
NSPR_API(void) PR_DestroyCondVar(PRCondVar *cvar);
NSPR_API(PRStatus) PR_WaitCondVar(PRCondVar *cvar, PRIntervalTime timeout);
NSPR_API(PRStatus) PR_NotifyCondVar(PRCondVar *cvar);
NSPR_API(PRStatus) PR_NotifyAllCondVar(PRCondVar *cvar);

typedef struct PRCListStr PRCList;

struct PRCListStr {
    PRCList *next;
    PRCList *prev;
};

#ifdef VBOX_WITH_XPCOM_NAMESPACE_CLEANUP
# define PL_DestroyEvent VBoxNsplPL_DestroyEvent
# define PL_HandleEvent VBoxNsplPL_HandleEvent
# define PL_InitEvent VBoxNsplPL_InitEvent
# define PL_CreateEventQueue VBoxNsplPL_CreateEventQueue
# define PL_CreateMonitoredEventQueue VBoxNsplPL_CreateMonitoredEventQueue
# define PL_CreateNativeEventQueue VBoxNsplPL_CreateNativeEventQueue
# define PL_DequeueEvent VBoxNsplPL_DequeueEvent
# define PL_DestroyEventQueue VBoxNsplPL_DestroyEventQueue
# define PL_EventAvailable VBoxNsplPL_EventAvailable
# define PL_EventLoop VBoxNsplPL_EventLoop
# define PL_GetEvent VBoxNsplPL_GetEvent
# define PL_GetEventOwner VBoxNsplPL_GetEventOwner
# define PL_GetEventQueueMonitor VBoxNsplPL_GetEventQueueMonitor
# define PL_GetEventQueueSelectFD VBoxNsplPL_GetEventQueueSelectFD
# define PL_MapEvents VBoxNsplPL_MapEvents
# define PL_PostEvent VBoxNsplPL_PostEvent
# define PL_PostSynchronousEvent VBoxNsplPL_PostSynchronousEvent
# define PL_ProcessEventsBeforeID VBoxNsplPL_ProcessEventsBeforeID
# define PL_ProcessPendingEvents VBoxNsplPL_ProcessPendingEvents
# define PL_RegisterEventIDFunc VBoxNsplPL_RegisterEventIDFunc
# define PL_RevokeEvents VBoxNsplPL_RevokeEvents
# define PL_UnregisterEventIDFunc VBoxNsplPL_UnregisterEventIDFunc
# define PL_WaitForEvent VBoxNsplPL_WaitForEvent
# define PL_IsQueueNative VBoxNsplPL_IsQueueNative
# define PL_IsQueueOnCurrentThread VBoxNsplPL_IsQueueOnCurrentThread
# define PL_FavorPerformanceHint VBoxNsplPL_FavorPerformanceHint
#endif /* VBOX_WITH_XPCOM_NAMESPACE_CLEANUP */

typedef struct PLEvent PLEvent;
typedef struct PLEventQueue PLEventQueue;

PR_EXTERN(PLEventQueue*)
PL_CreateEventQueue(const char* name, PRThread* handlerThread);
PR_EXTERN(PLEventQueue *)
    PL_CreateNativeEventQueue(
        const char *name,
        PRThread *handlerThread
    );
PR_EXTERN(PLEventQueue *)
    PL_CreateMonitoredEventQueue(
        const char *name,
        PRThread *handlerThread
    );
PR_EXTERN(void)
PL_DestroyEventQueue(PLEventQueue* self);
PR_EXTERN(PRMonitor*)
PL_GetEventQueueMonitor(PLEventQueue* self);

#define PL_ENTER_EVENT_QUEUE_MONITOR(queue) \
    PR_EnterMonitor(PL_GetEventQueueMonitor(queue))

#define PL_EXIT_EVENT_QUEUE_MONITOR(queue)  \
    PR_ExitMonitor(PL_GetEventQueueMonitor(queue))

PR_EXTERN(PRStatus) PL_PostEvent(PLEventQueue* self, PLEvent* event);
PR_EXTERN(void*) PL_PostSynchronousEvent(PLEventQueue* self, PLEvent* event);
PR_EXTERN(PLEvent*) PL_GetEvent(PLEventQueue* self);
PR_EXTERN(PRBool) PL_EventAvailable(PLEventQueue* self);

typedef void (PR_CALLBACK *PLEventFunProc)(PLEvent* event, void* data, PLEventQueue* queue);

PR_EXTERN(void) PL_MapEvents(PLEventQueue* self, PLEventFunProc fun, void* data);
PR_EXTERN(void) PL_RevokeEvents(PLEventQueue* self, void* owner);
PR_EXTERN(void) PL_ProcessPendingEvents(PLEventQueue* self);
PR_EXTERN(PLEvent*) PL_WaitForEvent(PLEventQueue* self);
PR_EXTERN(void) PL_EventLoop(PLEventQueue* self);
PR_EXTERN(PRInt32) PL_GetEventQueueSelectFD(PLEventQueue* self);
PR_EXTERN(PRBool) PL_IsQueueOnCurrentThread( PLEventQueue *queue );
PR_EXTERN(PRBool) PL_IsQueueNative(PLEventQueue *queue);

typedef void* (PR_CALLBACK *PLHandleEventProc)(PLEvent* self);
typedef void (PR_CALLBACK *PLDestroyEventProc)(PLEvent* self);
PR_EXTERN(void)
PL_InitEvent(PLEvent* self, void* owner,
             PLHandleEventProc handler,
             PLDestroyEventProc destructor);
PR_EXTERN(void*) PL_GetEventOwner(PLEvent* self);
PR_EXTERN(void) PL_HandleEvent(PLEvent* self);
PR_EXTERN(void) PL_DestroyEvent(PLEvent* self);
PR_EXTERN(void) PL_DequeueEvent(PLEvent* self, PLEventQueue* queue);
PR_EXTERN(void) PL_FavorPerformanceHint(PRBool favorPerformanceOverEventStarvation, PRUint32 starvationDelay);

struct PLEvent {
    PRCList             link;
    PLHandleEventProc   handler;
    PLDestroyEventProc  destructor;
    void*               owner;
    void*               synchronousResult;
    PRLock*             lock;
    PRCondVar*          condVar;
    PRBool              handled;
#ifdef PL_POST_TIMINGS
    PRIntervalTime      postTime;
#endif
#ifdef XP_UNIX
    unsigned long       id;
#endif /* XP_UNIX */
    /* other fields follow... */
};

#if defined(XP_WIN) || defined(XP_OS2)

PR_EXTERN(HWND)
    PL_GetNativeEventReceiverWindow(
        PLEventQueue *eqp
    );
#endif /* XP_WIN || XP_OS2 */

#ifdef XP_UNIX

PR_EXTERN(PRInt32)
PL_ProcessEventsBeforeID(PLEventQueue *aSelf, unsigned long aID);

typedef unsigned long (PR_CALLBACK *PLGetEventIDFunc)(void *aClosure);

PR_EXTERN(void)
PL_RegisterEventIDFunc(PLEventQueue *aSelf, PLGetEventIDFunc aFunc,
                       void *aClosure);
PR_EXTERN(void) PL_UnregisterEventIDFunc(PLEventQueue *aSelf);

#endif /* XP_UNIX */

/* Standard "it worked" return value */
#define NS_OK                              0

#define NS_ERROR_BASE                      ((nsresult) 0xC1F30000)

/* Returned when an instance is not initialized */
#define NS_ERROR_NOT_INITIALIZED           (NS_ERROR_BASE + 1)

/* Returned when an instance is already initialized */
#define NS_ERROR_ALREADY_INITIALIZED       (NS_ERROR_BASE + 2)

/* Returned by a not implemented function */
#define NS_ERROR_NOT_IMPLEMENTED           ((nsresult) 0x80004001L)

/* Returned when a given interface is not supported. */
#define NS_NOINTERFACE                     ((nsresult) 0x80004002L)
#define NS_ERROR_NO_INTERFACE              NS_NOINTERFACE

#define NS_ERROR_INVALID_POINTER           ((nsresult) 0x80004003L)
#define NS_ERROR_NULL_POINTER              NS_ERROR_INVALID_POINTER

/* Returned when a function aborts */
#define NS_ERROR_ABORT                     ((nsresult) 0x80004004L)

/* Returned when a function fails */
#define NS_ERROR_FAILURE                   ((nsresult) 0x80004005L)

/* Returned when an unexpected error occurs */
#define NS_ERROR_UNEXPECTED                ((nsresult) 0x8000ffffL)

/* Returned when a memory allocation fails */
#define NS_ERROR_OUT_OF_MEMORY             ((nsresult) 0x8007000eL)

/* Returned when an illegal value is passed */
#define NS_ERROR_ILLEGAL_VALUE             ((nsresult) 0x80070057L)
#define NS_ERROR_INVALID_ARG               NS_ERROR_ILLEGAL_VALUE

/* Returned when a class doesn't allow aggregation */
#define NS_ERROR_NO_AGGREGATION            ((nsresult) 0x80040110L)

/* Returned when an operation can't complete due to an unavailable resource */
#define NS_ERROR_NOT_AVAILABLE             ((nsresult) 0x80040111L)

/* Returned when a class is not registered */
#define NS_ERROR_FACTORY_NOT_REGISTERED    ((nsresult) 0x80040154L)

/* Returned when a class cannot be registered, but may be tried again later */
#define NS_ERROR_FACTORY_REGISTER_AGAIN    ((nsresult) 0x80040155L)

/* Returned when a dynamically loaded factory couldn't be found */
#define NS_ERROR_FACTORY_NOT_LOADED        ((nsresult) 0x800401f8L)

/* Returned when a factory doesn't support signatures */
#define NS_ERROR_FACTORY_NO_SIGNATURE_SUPPORT \
                                           (NS_ERROR_BASE + 0x101)

/* Returned when a factory already is registered */
#define NS_ERROR_FACTORY_EXISTS            (NS_ERROR_BASE + 0x100)

/**
 * An "interface id" which can be used to uniquely identify a given
 * interface.
 * A "unique identifier". This is modeled after OSF DCE UUIDs.
 */

struct nsID {
    PRUint32 m0;
    PRUint16 m1;
    PRUint16 m2;
    PRUint8 m3[8];
};

typedef struct nsID nsID;
typedef nsID nsIID;
typedef nsID nsCID;

#endif /* __cplusplus */

#define VBOX_WINAPI

/* Various COM types defined by their XPCOM equivalent */
typedef PRInt64 LONG64;
typedef PRInt32 LONG;
typedef PRInt32 DWORD;
typedef PRInt16 SHORT;
typedef PRUint64 ULONG64;
typedef PRUint32 ULONG;
typedef PRUint16 USHORT;

typedef PRBool BOOL;

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

#define HRESULT nsresult
#define SUCCEEDED NS_SUCCEEDED
#define FAILED NS_FAILED

/* OLE error codes */
#define S_OK                ((nsresult)NS_OK)
#define E_UNEXPECTED        NS_ERROR_UNEXPECTED
#define E_NOTIMPL           NS_ERROR_NOT_IMPLEMENTED
#define E_OUTOFMEMORY       NS_ERROR_OUT_OF_MEMORY
#define E_INVALIDARG        NS_ERROR_INVALID_ARG
#define E_NOINTERFACE       NS_ERROR_NO_INTERFACE
#define E_POINTER           NS_ERROR_NULL_POINTER
#define E_ABORT             NS_ERROR_ABORT
#define E_FAIL              NS_ERROR_FAILURE
/* Note: a better analog for E_ACCESSDENIED would probably be
 * NS_ERROR_NOT_AVAILABLE, but we want binary compatibility for now. */
#define E_ACCESSDENIED      ((nsresult)0x80070005L)

/* Basic vartype for COM compatibility. */
typedef enum VARTYPE
{
    VT_I2 = 2,
    VT_I4 = 3,
    VT_BSTR = 8,
    VT_DISPATCH = 9,
    VT_BOOL = 11,
    VT_UNKNOWN = 13,
    VT_I1 = 16,
    VT_UI1 = 17,
    VT_UI2 = 18,
    VT_UI4 = 19,
    VT_I8 = 20,
    VT_UI8 = 21,
    VT_HRESULT = 25
} VARTYPE;

/* Basic safearray type for COM compatibility. */
typedef struct SAFEARRAY
{
    void *pv;
    ULONG c;
} SAFEARRAY;

#define ComSafeArrayAsInParam(f) ((f) ? (f)->c : 0), ((f) ? (f)->pv : NULL)
#define ComSafeArrayAsOutParam(f) (&amp;((f)->c)), (&amp;((f)->pv))
#define ComSafeArrayAsOutTypeParam(f,t) (&amp;((f)->c)), (t**)(&amp;((f)->pv))
#define ComSafeArrayAsOutIfaceParam(f,t) (&amp;((f)->c)), (t**)(&amp;((f)->pv))

/* Glossing over differences between COM and XPCOM */
#define IErrorInfo nsIException
#define IUnknown nsISupports
#define IDispatch nsISupports

/* Make things as COM compatible as possible */
#define interface struct
#ifdef CONST_VTABLE
# define CONST_VTBL const
#else /* !CONST_VTABLE */
# define CONST_VTBL
#endif /* !CONST_VTABLE */

#ifndef __cplusplus

/** @todo this first batch of forward declarations (and the corresponding ones
 * generated for each interface) are 100% redundant, remove eventually. */
interface nsISupports;   /* forward declaration */
interface nsIException;  /* forward declaration */
interface nsIStackFrame; /* forward declaration */
interface nsIEventTarget;/* forward declaration */
interface nsIEventQueue; /* forward declaration */

typedef interface nsISupports nsISupports;     /* forward declaration */
typedef interface nsIException nsIException;   /* forward declaration */
typedef interface nsIStackFrame nsIStackFrame; /* forward declaration */
typedef interface nsIEventTarget nsIEventTarget;/* forward declaration */
typedef interface nsIEventQueue nsIEventQueue; /* forward declaration */

/* starting interface:    nsISupports */
#define NS_ISUPPORTS_IID_STR "00000000-0000-0000-c000-000000000046"

#define NS_ISUPPORTS_IID \
    { 0x00000000, 0x0000, 0x0000, \
      {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46} }

/**
 * Reference count values
 *
 * This is the return type for AddRef() and Release() in nsISupports.
 * IUnknown of COM returns an unsigned long from equivalent functions.
 * The following ifdef exists to maintain binary compatibility with
 * IUnknown.
 */
#if defined(XP_WIN) &amp;&amp; PR_BYTES_PER_LONG == 4
typedef unsigned long nsrefcnt;
#else
typedef PRUint32 nsrefcnt;
#endif

/**
 * Basic component object model interface. Objects which implement
 * this interface support runtime interface discovery (QueryInterface)
 * and a reference counted memory model (AddRef/Release). This is
 * modelled after the win32 IUnknown API.
 */
#ifndef VBOX_WITH_GLUE
struct nsISupports_vtbl
{
    nsresult (*QueryInterface)(nsISupports *pThis, const nsID *iid, void **resultp);
    nsrefcnt (*AddRef)(nsISupports *pThis);
    nsrefcnt (*Release)(nsISupports *pThis);
};
#else /* !VBOX_WITH_GLUE */
struct nsISupportsVtbl
{
    nsresult (*QueryInterface)(nsISupports *pThis, const nsID *iid, void **resultp);
    nsrefcnt (*AddRef)(nsISupports *pThis);
    nsrefcnt (*Release)(nsISupports *pThis);
};
#define nsISupports_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))
#define nsISupports_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define nsISupports_Release(p) ((p)->lpVtbl->Release(p))
#define IUnknown_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))
#define IUnknown_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define IUnknown_Release(p) ((p)->lpVtbl->Release(p))
#define IDispatch_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))
#define IDispatch_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define IDispatch_Release(p) ((p)->lpVtbl->Release(p))
#endif /* !VBOX_WITH_GLUE */

interface nsISupports
{
#ifndef VBOX_WITH_GLUE
    struct nsISupports_vtbl *vtbl;
#else /* !VBOX_WITH_GLUE */
    CONST_VTBL struct nsISupportsVtbl *lpVtbl;
#endif /* !VBOX_WITH_GLUE */
};

/* starting interface:    nsIException */
#define NS_IEXCEPTION_IID_STR "f3a8d3b4-c424-4edc-8bf6-8974c983ba78"

#define NS_IEXCEPTION_IID \
    {0xf3a8d3b4, 0xc424, 0x4edc, \
      { 0x8b, 0xf6, 0x89, 0x74, 0xc9, 0x83, 0xba, 0x78 }}

#ifndef VBOX_WITH_GLUE
struct nsIException_vtbl
{
    /* Methods from the interface nsISupports */
    struct nsISupports_vtbl nsisupports;

    nsresult (*GetMessage)(nsIException *pThis, PRUnichar * *aMessage);
    nsresult (*GetResult)(nsIException *pThis, nsresult *aResult);
    nsresult (*GetName)(nsIException *pThis, PRUnichar * *aName);
    nsresult (*GetFilename)(nsIException *pThis, PRUnichar * *aFilename);
    nsresult (*GetLineNumber)(nsIException *pThis, PRUint32 *aLineNumber);
    nsresult (*GetColumnNumber)(nsIException *pThis, PRUint32 *aColumnNumber);
    nsresult (*GetLocation)(nsIException *pThis, nsIStackFrame * *aLocation);
    nsresult (*GetInner)(nsIException *pThis, nsIException * *aInner);
    nsresult (*GetData)(nsIException *pThis, nsISupports * *aData);
    nsresult (*ToString)(nsIException *pThis, PRUnichar **_retval);
};
#else /* !VBOX_WITH_GLUE */
struct nsIExceptionVtbl
{
    nsresult (*QueryInterface)(nsIException *pThis, const nsID *iid, void **resultp);
    nsrefcnt (*AddRef)(nsIException *pThis);
    nsrefcnt (*Release)(nsIException *pThis);

    nsresult (*GetMessage)(nsIException *pThis, PRUnichar * *aMessage);
    nsresult (*GetResult)(nsIException *pThis, nsresult *aResult);
    nsresult (*GetName)(nsIException *pThis, PRUnichar * *aName);
    nsresult (*GetFilename)(nsIException *pThis, PRUnichar * *aFilename);
    nsresult (*GetLineNumber)(nsIException *pThis, PRUint32 *aLineNumber);
    nsresult (*GetColumnNumber)(nsIException *pThis, PRUint32 *aColumnNumber);
    nsresult (*GetLocation)(nsIException *pThis, nsIStackFrame * *aLocation);
    nsresult (*GetInner)(nsIException *pThis, nsIException * *aInner);
    nsresult (*GetData)(nsIException *pThis, nsISupports * *aData);
    nsresult (*ToString)(nsIException *pThis, PRUnichar **_retval);
};
#define nsIException_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))
#define nsIException_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define nsIException_Release(p) ((p)->lpVtbl->Release(p))
#define nsIException_get_Message(p, aMessage) ((p)->lpVtbl->GetMessage(p, aMessage))
#define nsIException_GetMessage(p, aMessage) ((p)->lpVtbl->GetMessage(p, aMessage))
#define nsIException_get_Result(p, aResult) ((p)->lpVtbl->GetResult(p, aResult))
#define nsIException_GetResult(p, aResult) ((p)->lpVtbl->GetResult(p, aResult))
#define nsIException_get_Name(p, aName) ((p)->lpVtbl->GetName(p, aName))
#define nsIException_GetName(p, aName) ((p)->lpVtbl->GetName(p, aName))
#define nsIException_get_Filename(p, aFilename) ((p)->lpVtbl->GetFilename(p, aFilename))
#define nsIException_GetFilename(p, aFilename) ((p)->lpVtbl->GetFilename(p, aFilename))
#define nsIException_get_LineNumber(p, aLineNumber) ((p)->lpVtbl->GetLineNumber(p, aLineNumber))
#define nsIException_GetLineNumber(p, aLineNumber) ((p)->lpVtbl->GetLineNumber(p, aLineNumber))
#define nsIException_get_ColumnNumber(p, aColumnNumber) ((p)->lpVtbl->GetColumnNumber(p, aColumnNumber))
#define nsIException_GetColumnNumber(p, aColumnNumber) ((p)->lpVtbl->GetColumnNumber(p, aColumnNumber))
#define nsIException_get_Inner(p, aInner) ((p)->lpVtbl->GetInner(p, aInner))
#define nsIException_GetInner(p, aInner) ((p)->lpVtbl->GetInner(p, aInner))
#define nsIException_get_Data(p, aData) ((p)->lpVtbl->GetData(p, aData))
#define nsIException_GetData(p, aData) ((p)->lpVtbl->GetData(p, aData))
#define nsIException_ToString(p, retval) ((p)->lpVtbl->ToString(p, retval))
#define IErrorInfo_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))
#define IErrorInfo_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define IErrorInfo_Release(p) ((p)->lpVtbl->Release(p))
#define IErrorInfo_get_Message(p, aMessage) ((p)->lpVtbl->GetMessage(p, aMessage))
#define IErrorInfo_GetMessage(p, aMessage) ((p)->lpVtbl->GetMessage(p, aMessage))
#define IErrorInfo_get_Result(p, aResult) ((p)->lpVtbl->GetResult(p, aResult))
#define IErrorInfo_GetResult(p, aResult) ((p)->lpVtbl->GetResult(p, aResult))
#define IErrorInfo_get_Name(p, aName) ((p)->lpVtbl->GetName(p, aName))
#define IErrorInfo_GetName(p, aName) ((p)->lpVtbl->GetName(p, aName))
#define IErrorInfo_get_Filename(p, aFilename) ((p)->lpVtbl->GetFilename(p, aFilename))
#define IErrorInfo_GetFilename(p, aFilename) ((p)->lpVtbl->GetFilename(p, aFilename))
#define IErrorInfo_get_LineNumber(p, aLineNumber) ((p)->lpVtbl->GetLineNumber(p, aLineNumber))
#define IErrorInfo_GetLineNumber(p, aLineNumber) ((p)->lpVtbl->GetLineNumber(p, aLineNumber))
#define IErrorInfo_get_ColumnNumber(p, aColumnNumber) ((p)->lpVtbl->GetColumnNumber(p, aColumnNumber))
#define IErrorInfo_GetColumnNumber(p, aColumnNumber) ((p)->lpVtbl->GetColumnNumber(p, aColumnNumber))
#define IErrorInfo_get_Inner(p, aInner) ((p)->lpVtbl->GetInner(p, aInner))
#define IErrorInfo_GetInner(p, aInner) ((p)->lpVtbl->GetInner(p, aInner))
#define IErrorInfo_get_Data(p, aData) ((p)->lpVtbl->GetData(p, aData))
#define IErrorInfo_GetData(p, aData) ((p)->lpVtbl->GetData(p, aData))
#define IErrorInfo_ToString(p, retval) ((p)->lpVtbl->ToString(p, retval))
#endif /* !VBOX_WITH_GLUE */

interface nsIException
{
#ifndef VBOX_WITH_GLUE
    struct nsIException_vtbl *vtbl;
#else /* !VBOX_WITH_GLUE */
    CONST_VTBL struct nsIExceptionVtbl *lpVtbl;
#endif /* !VBOX_WITH_GLUE */
};

/* starting interface:    nsIStackFrame */
#define NS_ISTACKFRAME_IID_STR "91d82105-7c62-4f8b-9779-154277c0ee90"

#define NS_ISTACKFRAME_IID \
    {0x91d82105, 0x7c62, 0x4f8b, \
      { 0x97, 0x79, 0x15, 0x42, 0x77, 0xc0, 0xee, 0x90 }}

#ifndef VBOX_WITH_GLUE
struct nsIStackFrame_vtbl
{
    /* Methods from the interface nsISupports */
    struct nsISupports_vtbl nsisupports;

    nsresult (*GetLanguage)(nsIStackFrame *pThis, PRUint32 *aLanguage);
    nsresult (*GetLanguageName)(nsIStackFrame *pThis, PRUnichar * *aLanguageName);
    nsresult (*GetFilename)(nsIStackFrame *pThis, PRUnichar * *aFilename);
    nsresult (*GetName)(nsIStackFrame *pThis, PRUnichar * *aName);
    nsresult (*GetLineNumber)(nsIStackFrame *pThis, PRInt32 *aLineNumber);
    nsresult (*GetSourceLine)(nsIStackFrame *pThis, PRUnichar * *aSourceLine);
    nsresult (*GetCaller)(nsIStackFrame *pThis, nsIStackFrame * *aCaller);
    nsresult (*ToString)(nsIStackFrame *pThis, PRUnichar **_retval);
};
#else /* !VBOX_WITH_GLUE */
struct nsIStackFrameVtbl
{
    nsresult (*QueryInterface)(nsIStackFrame *pThis, const nsID *iid, void **resultp);
    nsrefcnt (*AddRef)(nsIStackFrame *pThis);
    nsrefcnt (*Release)(nsIStackFrame *pThis);

    nsresult (*GetLanguage)(nsIStackFrame *pThis, PRUint32 *aLanguage);
    nsresult (*GetLanguageName)(nsIStackFrame *pThis, PRUnichar * *aLanguageName);
    nsresult (*GetFilename)(nsIStackFrame *pThis, PRUnichar * *aFilename);
    nsresult (*GetName)(nsIStackFrame *pThis, PRUnichar * *aName);
    nsresult (*GetLineNumber)(nsIStackFrame *pThis, PRInt32 *aLineNumber);
    nsresult (*GetSourceLine)(nsIStackFrame *pThis, PRUnichar * *aSourceLine);
    nsresult (*GetCaller)(nsIStackFrame *pThis, nsIStackFrame * *aCaller);
    nsresult (*ToString)(nsIStackFrame *pThis, PRUnichar **_retval);
};
#define nsIStackFrame_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))
#define nsIStackFrame_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define nsIStackFrame_Release(p) ((p)->lpVtbl->Release(p))
#define nsIStackFrame_get_Language(p, aLanguage) ((p)->lpVtbl->GetLanguge(p, aLanguage))
#define nsIStackFrame_GetLanguage(p, aLanguage) ((p)->lpVtbl->GetLanguge(p, aLanguage))
#define nsIStackFrame_get_LanguageName(p, aLanguageName) ((p)->lpVtbl->GetLanguageName(p, aLanguageName))
#define nsIStackFrame_GetLanguageName(p, aLanguageName) ((p)->lpVtbl->GetLanguageName(p, aLanguageName))
#define nsIStackFrame_get_Filename(p, aFilename) ((p)->lpVtbl->GetFilename(p, aFilename))
#define nsIStackFrame_GetFilename(p, aFilename) ((p)->lpVtbl->GetFilename(p, aFilename))
#define nsIStackFrame_get_Name(p, aName) ((p)->lpVtbl->GetName(p, aName))
#define nsIStackFrame_GetName(p, aName) ((p)->lpVtbl->GetName(p, aName))
#define nsIStackFrame_get_LineNumber(p, aLineNumber) ((p)->lpVtbl->GetLineNumber(p, aLineNumber))
#define nsIStackFrame_GetLineNumber(p, aLineNumber) ((p)->lpVtbl->GetLineNumber(p, aLineNumber))
#define nsIStackFrame_get_SourceLine(p, aSourceLine) ((p)->lpVtbl->GetSourceLine(p, aSourceLine))
#define nsIStackFrame_GetSourceLine(p, aSourceLine) ((p)->lpVtbl->GetSourceLine(p, aSourceLine))
#define nsIStackFrame_get_Caller(p, aCaller) ((p)->lpVtbl->GetCaller(p, aCaller))
#define nsIStackFrame_GetCaller(p, aCaller) ((p)->lpVtbl->GetCaller(p, aCaller))
#define nsIStackFrame_ToString(p, retval) ((p)->lpVtbl->ToString(p, retval))
#endif /* !VBOX_WITH_GLUE */

interface nsIStackFrame
{
#ifndef VBOX_WITH_GLUE
    struct nsIStackFrame_vtbl *vtbl;
#else /* !VBOX_WITH_GLUE */
    CONST_VTBL struct nsIStackFrameVtbl *lpVtbl;
#endif /* !VBOX_WITH_GLUE */
};

/* starting interface:    nsIEventTarget */
#define NS_IEVENTTARGET_IID_STR "ea99ad5b-cc67-4efb-97c9-2ef620a59f2a"

#define NS_IEVENTTARGET_IID \
    {0xea99ad5b, 0xcc67, 0x4efb, \
      { 0x97, 0xc9, 0x2e, 0xf6, 0x20, 0xa5, 0x9f, 0x2a }}

#ifndef VBOX_WITH_GLUE
struct nsIEventTarget_vtbl
{
    struct nsISupports_vtbl nsisupports;

    nsresult (*PostEvent)(nsIEventTarget *pThis, PLEvent * aEvent);
    nsresult (*IsOnCurrentThread)(nsIEventTarget *pThis, PRBool *_retval);
};
#else /* !VBOX_WITH_GLUE */
struct nsIEventTargetVtbl
{
    nsresult (*QueryInterface)(nsIEventTarget *pThis, const nsID *iid, void **resultp);
    nsrefcnt (*AddRef)(nsIEventTarget *pThis);
    nsrefcnt (*Release)(nsIEventTarget *pThis);

    nsresult (*PostEvent)(nsIEventTarget *pThis, PLEvent * aEvent);
    nsresult (*IsOnCurrentThread)(nsIEventTarget *pThis, PRBool *_retval);
};
#define nsIEventTarget_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))
#define nsIEventTarget_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define nsIEventTarget_Release(p) ((p)->lpVtbl->Release(p))
#define nsIEventTarget_PostEvent(p, aEvent) ((p)->lpVtbl->PostEvent(p, aEvent))
#define nsIEventTarget_IsOnCurrentThread(p, retval) ((p)->lpVtbl->IsOnCurrentThread(p, retval))
#endif /* !VBOX_WITH_GLUE */

interface nsIEventTarget
{
#ifndef VBOX_WITH_GLUE
    struct nsIEventTarget_vtbl *vtbl;
#else /* !VBOX_WITH_GLUE */
    CONST_VTBL struct nsIEventTargetVtbl *lpVtbl;
#endif /* !VBOX_WITH_GLUE */
};

/* starting interface:    nsIEventQueue */
#define NS_IEVENTQUEUE_IID_STR "176afb41-00a4-11d3-9f2a-00400553eef0"

#define NS_IEVENTQUEUE_IID \
  {0x176afb41, 0x00a4, 0x11d3, \
    { 0x9f, 0x2a, 0x00, 0x40, 0x05, 0x53, 0xee, 0xf0 }}

#ifndef VBOX_WITH_GLUE
struct nsIEventQueue_vtbl
{
    struct nsIEventTarget_vtbl nsieventtarget;

    nsresult (*InitEvent)(nsIEventQueue *pThis, PLEvent * aEvent, void * owner, PLHandleEventProc handler, PLDestroyEventProc destructor);
    nsresult (*PostSynchronousEvent)(nsIEventQueue *pThis, PLEvent * aEvent, void * *aResult);
    nsresult (*PendingEvents)(nsIEventQueue *pThis, PRBool *_retval);
    nsresult (*ProcessPendingEvents)(nsIEventQueue *pThis);
    nsresult (*EventLoop)(nsIEventQueue *pThis);
    nsresult (*EventAvailable)(nsIEventQueue *pThis, PRBool *aResult);
    nsresult (*GetEvent)(nsIEventQueue *pThis, PLEvent * *_retval);
    nsresult (*HandleEvent)(nsIEventQueue *pThis, PLEvent * aEvent);
    nsresult (*WaitForEvent)(nsIEventQueue *pThis, PLEvent * *_retval);
    PRInt32 (*GetEventQueueSelectFD)(nsIEventQueue *pThis);
    nsresult (*Init)(nsIEventQueue *pThis, PRBool aNative);
    nsresult (*InitFromPRThread)(nsIEventQueue *pThis, PRThread * thread, PRBool aNative);
    nsresult (*InitFromPLQueue)(nsIEventQueue *pThis, PLEventQueue * aQueue);
    nsresult (*EnterMonitor)(nsIEventQueue *pThis);
    nsresult (*ExitMonitor)(nsIEventQueue *pThis);
    nsresult (*RevokeEvents)(nsIEventQueue *pThis, void * owner);
    nsresult (*GetPLEventQueue)(nsIEventQueue *pThis, PLEventQueue * *_retval);
    nsresult (*IsQueueNative)(nsIEventQueue *pThis, PRBool *_retval);
    nsresult (*StopAcceptingEvents)(nsIEventQueue *pThis);
};
#else /* !VBOX_WITH_GLUE */
struct nsIEventQueueVtbl
{
    nsresult (*QueryInterface)(nsIEventQueue *pThis, const nsID *iid, void **resultp);
    nsrefcnt (*AddRef)(nsIEventQueue *pThis);
    nsrefcnt (*Release)(nsIEventQueue *pThis);

    nsresult (*PostEvent)(nsIEventQueue *pThis, PLEvent * aEvent);
    nsresult (*IsOnCurrentThread)(nsIEventQueue *pThis, PRBool *_retval);

    nsresult (*InitEvent)(nsIEventQueue *pThis, PLEvent * aEvent, void * owner, PLHandleEventProc handler, PLDestroyEventProc destructor);
    nsresult (*PostSynchronousEvent)(nsIEventQueue *pThis, PLEvent * aEvent, void * *aResult);
    nsresult (*PendingEvents)(nsIEventQueue *pThis, PRBool *_retval);
    nsresult (*ProcessPendingEvents)(nsIEventQueue *pThis);
    nsresult (*EventLoop)(nsIEventQueue *pThis);
    nsresult (*EventAvailable)(nsIEventQueue *pThis, PRBool *aResult);
    nsresult (*GetEvent)(nsIEventQueue *pThis, PLEvent * *_retval);
    nsresult (*HandleEvent)(nsIEventQueue *pThis, PLEvent * aEvent);
    nsresult (*WaitForEvent)(nsIEventQueue *pThis, PLEvent * *_retval);
    PRInt32 (*GetEventQueueSelectFD)(nsIEventQueue *pThis);
    nsresult (*Init)(nsIEventQueue *pThis, PRBool aNative);
    nsresult (*InitFromPRThread)(nsIEventQueue *pThis, PRThread * thread, PRBool aNative);
    nsresult (*InitFromPLQueue)(nsIEventQueue *pThis, PLEventQueue * aQueue);
    nsresult (*EnterMonitor)(nsIEventQueue *pThis);
    nsresult (*ExitMonitor)(nsIEventQueue *pThis);
    nsresult (*RevokeEvents)(nsIEventQueue *pThis, void * owner);
    nsresult (*GetPLEventQueue)(nsIEventQueue *pThis, PLEventQueue * *_retval);
    nsresult (*IsQueueNative)(nsIEventQueue *pThis, PRBool *_retval);
    nsresult (*StopAcceptingEvents)(nsIEventQueue *pThis);
};
#define nsIEventQueue_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))
#define nsIEventQueue_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define nsIEventQueue_Release(p) ((p)->lpVtbl->Release(p))
#define nsIEventQueue_PostEvent(p, aEvent) ((p)->lpVtbl->PostEvent(p, aEvent))
#define nsIEventQueue_IsOnCurrentThread(p, retval) ((p)->lpVtbl->IsOnCurrentThread(p, retval))
#define nsIEventQueue_InitEvent(p, aEvent, owner, handler, destructor) ((p)->lpVtbl->InitEvent(p, aEvent, owner, handler, destructor))
#define nsIEventQueue_PostSynchronousEvent(p, aEvent, aResult) ((p)->lpVtbl->PostSynchronousEvent(p, aEvent, aResult))
#define nsIEventQueue_ProcessPendingEvents(p) ((p)->lpVtbl->ProcessPendingEvents(p))
#define nsIEventQueue_EventLoop(p) ((p)->lpVtbl->EventLoop(p))
#define nsIEventQueue_EventAvailable(p, aResult) ((p)->lpVtbl->EventAvailable(p, aResult))
#define nsIEventQueue_get_Event(p, aEvent) ((p)->lpVtbl->GetEvent(p, aEvent))
#define nsIEventQueue_GetEvent(p, aEvent) ((p)->lpVtbl->GetEvent(p, aEvent))
#define nsIEventQueue_HandleEvent(p, aEvent) ((p)->lpVtbl->HandleEvent(p, aEvent))
#define nsIEventQueue_WaitForEvent(p, aEvent) ((p)->lpVtbl->WaitForEvent(p, aEvent))
#define nsIEventQueue_GetEventQueueSelectFD(p) ((p)->lpVtbl->GetEventQueueSelectFD(p))
#define nsIEventQueue_Init(p, aNative) ((p)->lpVtbl->Init(p, aNative))
#define nsIEventQueue_InitFromPLQueue(p, aQueue) ((p)->lpVtbl->InitFromPLQueue(p, aQueue))
#define nsIEventQueue_EnterMonitor(p) ((p)->lpVtbl->EnterMonitor(p))
#define nsIEventQueue_ExitMonitor(p) ((p)->lpVtbl->ExitMonitor(p))
#define nsIEventQueue_RevokeEvents(p, owner) ((p)->lpVtbl->RevokeEvents(p, owner))
#define nsIEventQueue_GetPLEventQueue(p, retval) ((p)->lpVtbl->GetPLEventQueue(p, retval))
#define nsIEventQueue_IsQueueNative(p, retval) ((p)->lpVtbl->IsQueueNative(p, retval))
#define nsIEventQueue_StopAcceptingEvents(p) ((p)->lpVtbl->StopAcceptingEvents(p))
#endif /* !VBOX_WITH_GLUE */

interface nsIEventQueue
{
#ifndef VBOX_WITH_GLUE
    struct nsIEventQueue_vtbl *vtbl;
#else /* !VBOX_WITH_GLUE */
    CONST_VTBL struct nsIEventQueueVtbl *lpVtbl;
#endif /* !VBOX_WITH_GLUE */
};
</xsl:text>
 <xsl:call-template name="xsltprocNewlineOutputHack"/>
 <xsl:apply-templates/>
 <xsl:text>

#endif /* __cplusplus */

#endif /* !WIN32 */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */


/**
 * Function table for dynamic linking.
 * Use VBoxGetCAPIFunctions() to obtain the pointer to it.
 */
typedef struct VBOXCAPI
{
    /** The size of the structure. */
    unsigned cb;
    /** The structure version. */
    unsigned uVersion;

    /** Gets the VirtualBox version, major * 1000000 + minor * 1000 + patch. */
    unsigned int (*pfnGetVersion)(void);

    /** Gets the VirtualBox API version, major * 1000 + minor, e.g. 4003. */
    unsigned int (*pfnGetAPIVersion)(void);

    /**
     * New and preferred way to initialize the C bindings for an API client.
     *
     * This way is much more flexible, as it can easily handle multiple
     * sessions (important with more complicated API clients, including
     * multithreaded ones), and even VBoxSVC crashes can be detected and
     * processed appropriately by listening for events from the associated
     * event source in VirtualBoxClient. It is completely up to the client
     * to decide what to do (terminate or continue after getting new
     * object references to server-side objects). Must be called in the
     * primary thread of the client, later API use can be done in any
     * thread.
     *
     * Note that the returned reference is owned by the caller, and thus it's
     * the caller's responsibility to handle the reference count appropriately.
     *
     * @param pszVirtualBoxClientIID    pass IVIRTUALBOXCLIENT_IID_STR
     * @param ppVirtualBoxClient        output parameter for VirtualBoxClient
     *              reference, handled as usual with COM/XPCOM.
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnClientInitialize)(const char *pszVirtualBoxClientIID,
                                   IVirtualBoxClient **ppVirtualBoxClient);
    /**
     * Initialize the use of the C bindings in a non-primary thread.
     *
     * Must be called on any newly created thread which wants to use the
     * VirtualBox API.
     *
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnClientThreadInitialize)(void);
    /**
     * Uninitialize the use of the C bindings in a non-primary thread.
     *
     * Should be called before terminating the thread which initialized the
     * C bindings using pfnClientThreadInitialize.
     *
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnClientThreadUninitialize)(void);
    /**
     * Uninitialize the C bindings for an API client.
     *
     * Should be called when the API client is about to terminate and does
     * not want to use the C bindings any more. It will invalidate all
     * object references. It is possible, however, to change one's mind,
     * and call pfnClientInitialize again to continue using the API, as long
     * as none of the object references from before the re-initialization
     * are used. Must be called from the primary thread of the client.
     */
    void (*pfnClientUninitialize)(void);

    /**
     * Deprecated way to initialize the C bindings and getting important
     * object references. Kept for backwards compatibility.
     *
     * If any returned reference is NULL then the initialization failed.
     * Note that the returned references are owned by the C bindings. The
     * number of calls to Release in the client code must match the number
     * of calls to AddRef, and additionally at no point in time there can
     * be more Release calls than AddRef calls.
     *
     * @param pszVirtualBoxIID      pass IVIRTUALBOX_IID_STR
     * @param ppVirtualBox          output parameter for VirtualBox reference,
     *          owned by C bindings
     * @param pszSessionIID         pass ISESSION_IID_STR
     * @param ppSession             output parameter for Session reference,
     *          owned by C bindings
     */
    void (*pfnComInitialize)(const char *pszVirtualBoxIID,
                             IVirtualBox **ppVirtualBox,
                             const char *pszSessionIID,
                             ISession **ppSession);
    /**
     * Deprecated way to uninitialize the C bindings for an API client.
     * Kept for backwards compatibility and must be used if the C bindings
     * were initialized using pfnComInitialize. */
    void (*pfnComUninitialize)(void);

    /**
     * Free string managed by COM/XPCOM.
     *
     * @param pwsz          pointer to string to be freed
     */
    void (*pfnComUnallocString)(BSTR pwsz);
#ifndef WIN32
    /** Legacy function, was always for freeing strings only. */
#define pfnComUnallocMem(pv) pfnComUnallocString((BSTR)(pv))
#endif /* !WIN32 */

    /**
     * Convert string from UTF-16 encoding to UTF-8 encoding.
     *
     * @param pwszString    input string
     * @param ppszString    output string
     * @returns IPRT status code
     */
    int (*pfnUtf16ToUtf8)(CBSTR pwszString, char **ppszString);
    /**
     * Convert string from UTF-8 encoding to UTF-16 encoding.
     *
     * @param pszString     input string
     * @param ppwszString   output string
     * @returns IPRT status code
     */
    int (*pfnUtf8ToUtf16)(const char *pszString, BSTR *ppwszString);
    /**
     * Free memory returned by pfnUtf16ToUtf8. Do not use for anything else.
     *
     * @param pszString     string to be freed.
     */
    void (*pfnUtf8Free)(char *pszString);
    /**
     * Free memory returned by pfnUtf8ToUtf16. Do not use for anything else.
     *
     * @param pwszString    string to be freed.
     */
    void (*pfnUtf16Free)(BSTR pwszString);

    /**
     * Create a safearray (used for passing arrays to COM/XPCOM)
     *
     * Must be freed by pfnSafeArrayDestroy.
     *
     * @param vt            variant type, defines the size of the elements
     * @param lLbound       lower bound of the index, should be 0
     * @param cElements     number of elements
     * @returns pointer to safearray
     */
    SAFEARRAY *(*pfnSafeArrayCreateVector)(VARTYPE vt, LONG lLbound, ULONG cElements);
    /**
     * Pre-allocate a safearray to be used by an out safearray parameter
     *
     * Must be freed by pfnSafeArrayDestroy.
     *
     * @returns pointer to safearray (system dependent, may be NULL if
     *    there is no need to pre-allocate a safearray)
     */
    SAFEARRAY *(*pfnSafeArrayOutParamAlloc)(void);
    /**
     * Copy a C array into a safearray (for passing as an input parameter)
     *
     * @param psa           pointer to already created safearray.
     * @param pv            pointer to memory block to copy into safearray.
     * @param cb            number of bytes to copy.
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnSafeArrayCopyInParamHelper)(SAFEARRAY *psa, const void *pv, ULONG cb);
    /**
     * Copy a safearray into a C array (for getting an output parameter)
     *
     * @param ppv           output pointer to newly created array, which has to
     *          be freed with pfnArrayOutFree.
     * @param pcb           number of bytes in the output buffer.
     * @param vt            variant type, defines the size of the elements
     * @param psa           pointer to safearray for getting the data
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnSafeArrayCopyOutParamHelper)(void **ppv, ULONG *pcb, VARTYPE vt, SAFEARRAY *psa);
    /**
     * Copy a safearray into a C array (special variant for interface pointers)
     *
     * @param ppaObj        output pointer to newly created array, which has
     *          to be freed with pfnArrayOutFree. Note that it's the caller's
     *          responsibility to call Release() on each non-NULL interface
     *          pointer before freeing.
     * @param pcObj         number of pointers in the output buffer.
     * @param psa           pointer to safearray for getting the data
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnSafeArrayCopyOutIfaceParamHelper)(IUnknown ***ppaObj, ULONG *pcObj, SAFEARRAY *psa);
    /**
     * Free a safearray
     *
     * @param psa           pointer to safearray
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnSafeArrayDestroy)(SAFEARRAY *psa);
    /**
     * Free an out array created by pfnSafeArrayCopyOutParamHelper or
     * pdnSafeArrayCopyOutIfaceParamHelper.
     *
     * @param psa           pointer to memory block
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnArrayOutFree)(void *pv);

#ifndef WIN32
    /**
     * Get XPCOM event queue. Deprecated!
     *
     * @param ppEventQueue      output parameter for nsIEventQueue reference,
     *              owned by C bindings.
     */
    void (*pfnGetEventQueue)(nsIEventQueue **ppEventQueue);
#endif /* !WIN32 */

    /**
     * Get current COM/XPCOM exception.
     *
     * @param ppException       output parameter for exception info reference,
     *              may be @c NULL if no exception object has been created by
     *              a previous COM/XPCOM call.
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnGetException)(IErrorInfo **ppException);
    /**
     * Clears current COM/XPCOM exception.
     *
     * @returns COM/XPCOM error code
     */
    HRESULT (*pfnClearException)(void);

    /**
     * Process the event queue for a given amount of time.
     *
     * Must be called on the primary thread. Typical timeouts are from 200 to
     * 5000 msecs, to allow for checking a volatile variable if the event queue
     * processing should be terminated (,
     * or 0 if only the pending events should be processed, without waiting.
     *
     * @param iTimeoutMS        how long to process the event queue, -1 means
     *              infinitely long
     * @returns status code
     * @retval 0 if at least one event has been processed
     * @retval 1 if any signal interrupted the native system call (or returned
     *      otherwise)
     * @retval 2 if the event queue was explicitly interrupted
     * @retval 3 if the timeout expired
     * @retval 4 if the function was called from the wrong thread
     * @retval 5 for all other (unexpected) errors
     */
    int (*pfnProcessEventQueue)(LONG64 iTimeoutMS);
    /**
     * Interrupt event queue processing.
     *
     * Can be called on any thread. Note that this function is not async-signal
     * safe, so never use it in such a context, instead use a volatile global
     * variable and a sensible timeout.
     * @returns 0 if successful, 1 otherwise.
     */
    int (*pfnInterruptEventQueueProcessing)(void);

    /**
     * Clear memory used by a UTF-8 string. Must be zero terminated.
     * Can be used for any UTF-8 or ASCII/ANSI string.
     *
     * @param pszString     input/output string
     */
    void (*pfnUtf8Clear)(char *pszString);
    /**
     * Clear memory used by a UTF-16 string. Must be zero terminated.
     * Can be used for any UTF-16 or UCS-2 string.
     *
     * @param pwszString    input/output string
     */
     void (*pfnUtf16Clear)(BSTR pwszString);

    /** Tail version, same as uVersion.
     *
     * This should only be accessed if for some reason an API client needs
     * exactly the version it requested, or if cb is used to calculate the
     * address of this field. It may move as the structure before this is
     * allowed to grow as long as all the data from earlier minor versions
     * remains at the same place.
     */
    unsigned uEndVersion;
} VBOXCAPI;
/** Pointer to a const VBOXCAPI function table. */
typedef VBOXCAPI const *PCVBOXCAPI;
#ifndef WIN32
/** Backwards compatibility: Pointer to a const VBOXCAPI function table.
 * Use PCVBOXCAPI instead. */
typedef VBOXCAPI const *PCVBOXXPCOM;
#endif /* !WIN32 */

#ifndef WIN32
/** Backwards compatibility: make sure old code using VBOXXPCOMC still compiles.
 * Use VBOXCAPI instead. */
#define VBOXXPCOMC VBOXCAPI
#endif /* !WIN32 */

/** Extract the C API style major version.
 * Useful for comparing the interface version in VBOXCAPI::uVersion. */
#define VBOX_CAPI_MAJOR(x) (((x) &amp; 0xffff0000U) &gt;&gt; 16)

/** Extract the C API style major version.
 * Useful for comparing the interface version in VBOXCAPI::uVersion. */
#define VBOX_CAPI_MINOR(x) ((x) &amp; 0x0000ffffU)

/** The current interface version.
 * For use with VBoxGetCAPIFunctions and to be found in VBOXCAPI::uVersion. */
#define VBOX_CAPI_VERSION 0x00040001U

#ifndef WIN32
/** Backwards compatibility: The current interface version.
 * Use VBOX_CAPI_VERSION instead. */
#define VBOX_XPCOMC_VERSION VBOX_CAPI_VERSION
#endif /* !WIN32 */

/** VBoxGetCAPIFunctions. */
VBOXCAPI_DECL(PCVBOXCAPI) VBoxGetCAPIFunctions(unsigned uVersion);
#ifndef WIN32
/** Backwards compatibility: VBoxGetXPCOMCFunctions.
 * Use VBoxGetCAPIFunctions instead. */
VBOXCAPI_DECL(PCVBOXCAPI) VBoxGetXPCOMCFunctions(unsigned uVersion);
#endif /* !WIN32 */

/** Typedef for VBoxGetCAPIFunctions. */
typedef PCVBOXCAPI (*PFNVBOXGETCAPIFUNCTIONS)(unsigned uVersion);
#ifndef WIN32
/** Backwards compatibility: Typedef for VBoxGetXPCOMCFunctions.
 * Use PFNVBOXGETCAPIFUNCTIONS instead. */
typedef PCVBOXCAPI (*PFNVBOXGETXPCOMCFUNCTIONS)(unsigned uVersion);
#endif /* !WIN32 */

/** The symbol name of VBoxGetCAPIFunctions. */
#ifdef __OS2__
# define VBOX_GET_CAPI_FUNCTIONS_SYMBOL_NAME "_VBoxGetCAPIFunctions"
#else /* !__OS2__ */
# define VBOX_GET_CAPI_FUNCTIONS_SYMBOL_NAME "VBoxGetCAPIFunctions"
#endif /* !__OS2__ */
#ifndef WIN32
/** Backwards compatibility: The symbol name of VBoxGetXPCOMCFunctions.
 * Use VBOX_GET_CAPI_FUNCTIONS_SYMBOL_NAME instead. */
# ifdef __OS2__
#  define VBOX_GET_XPCOMC_FUNCTIONS_SYMBOL_NAME "_VBoxGetXPCOMCFunctions"
# else /* !__OS2__ */
#  define VBOX_GET_XPCOMC_FUNCTIONS_SYMBOL_NAME "VBoxGetXPCOMCFunctions"
# endif /* !__OS2__ */
#endif /* !WIN32 */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !___VirtualBox_CAPI_h */
</xsl:text>
</xsl:template>

<!--
 *  ignore all |if|s except those for XPIDL target
-->
<xsl:template match="if">
  <xsl:if test="@target='xpidl'">
    <xsl:apply-templates/>
  </xsl:if>
</xsl:template>
<xsl:template match="if" mode="forward">
  <xsl:if test="@target='xpidl'">
    <xsl:apply-templates mode="forward"/>
  </xsl:if>
</xsl:template>
<xsl:template match="if" mode="forwarder">
  <xsl:if test="@target='midl'">
    <xsl:apply-templates mode="forwarder"/>
  </xsl:if>
</xsl:template>


<!--
 *  libraries
-->
<xsl:template match="idl/library">
  <!-- result codes -->
  <xsl:call-template name="xsltprocNewlineOutputHack"/>
  <xsl:for-each select="application/result">
    <xsl:apply-templates select="."/>
  </xsl:for-each>
  <xsl:call-template name="xsltprocNewlineOutputHack"/>
  <xsl:call-template name="xsltprocNewlineOutputHack"/>
  <!-- forward declarations -->
  <xsl:apply-templates select="application/interface | application/if/interface" mode="forward"/>
  <xsl:call-template name="xsltprocNewlineOutputHack"/>
  <!-- typedef'ing the struct declarations -->
  <xsl:apply-templates select="application/interface | application/if/interface" mode="typedef"/>
  <xsl:call-template name="xsltprocNewlineOutputHack"/>
  <!-- all enums go first -->
  <xsl:apply-templates select="application/enum | application/if/enum"/>
  <!-- everything else but result codes and enums
  <xsl:apply-templates select="*[not(self::result or self::enum) and
                                 not(self::if[result] or self::if[enum])]"/> -->
    <!-- the modules (i.e. everything else) -->
  <xsl:apply-templates select="application/interface | application/if[interface]
                                   | application/module | application/if[module]"/>
  <!-- -->
</xsl:template>


<!--
 *  result codes
-->
<xsl:template match="result">
  <xsl:value-of select="concat('#define ',@name,' ((HRESULT)',@value, ')')"/>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>


<!--
 *  forward declarations
-->
<xsl:template match="interface" mode="forward">
  <xsl:if test="not(@internal='yes')">
    <xsl:text>interface </xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text>;&#x0A;</xsl:text>
  </xsl:if>
</xsl:template>


<!--
 *  typedef'ing the struct declarations
-->
<xsl:template match="interface" mode="typedef">
  <xsl:if test="not(@internal='yes')">
    <xsl:text>typedef interface </xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text> </xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text>;&#x0A;</xsl:text>
  </xsl:if>
</xsl:template>


<!--
 *  COBJMACRO style convenience macros for calling methods
-->
<xsl:template match="interface" mode="cobjmacro">
  <xsl:param name="iface"/>

  <xsl:variable name="extends" select="@extends"/>
  <xsl:choose>
    <xsl:when test="$extends='$unknown'">
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_AddRef(p) ((p)->lpVtbl->AddRef(p))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_Release(p) ((p)->lpVtbl->Release(p))&#x0A;</xsl:text>
    </xsl:when>
    <xsl:when test="$extends='$errorinfo'">
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_QueryInterface(p, iid, resultp) ((p)->lpVtbl->QueryInterface(p, iid, resultp))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_AddRef(p) ((p)->lpVtbl->AddRef(p))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_Release(p) ((p)->lpVtbl->Release(p))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_get_Message(p, aMessage) ((p)->lpVtbl->GetMessage(p, aMessage))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_GetMessage(p, aMessage) ((p)->lpVtbl->GetMessage(p, aMessage))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_get_Result(p, aResult) ((p)->lpVtbl->GetResult(p, aResult))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_GetResult(p, aResult) ((p)->lpVtbl->GetResult(p, aResult))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_get_Name(p, aName) ((p)->lpVtbl->GetName(p, aName))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_GetName(p, aName) ((p)->lpVtbl->GetName(p, aName))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_get_Filename(p, aFilename) ((p)->lpVtbl->GetFilename(p, aFilename))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_GetFilename(p, aFilename) ((p)->lpVtbl->GetFilename(p, aFilename))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_get_LineNumber(p, aLineNumber) ((p)->lpVtbl->GetLineNumber(p, aLineNumber))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_GetLineNumber(p, aLineNumber) ((p)->lpVtbl->GetLineNumber(p, aLineNumber))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_get_ColumnNumber(p, aColumnNumber) ((p)->lpVtbl->GetColumnNumber(p, aColumnNumber))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_GetColumnNumber(p, aColumnNumber) ((p)->lpVtbl->GetColumnNumber(p, aColumnNumber))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_get_Location(p, aLocation) ((p)->lpVtbl->GetLocation(p, aLocation))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_GetLocation(p, aLocation) ((p)->lpVtbl->GetLocation(p, aLocation))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_get_Inner(p, aInner) ((p)->lpVtbl->GetInner(p, aInner))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_GetInner(p, aInner) ((p)->lpVtbl->GetInner(p, aInner))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_get_Data(p, aData) ((p)->lpVtbl->GetData(p, aData))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_GetData(p, aData) ((p)->lpVtbl->GetData(p, aData))&#x0A;</xsl:text>
      <xsl:text>#define </xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>_ToString(p, retval) ((p)->lpVtbl->ToString(p, retval))&#x0A;</xsl:text>
    </xsl:when>
    <xsl:otherwise>
      <xsl:apply-templates select="key('G_keyInterfacesByName', $extends)" mode="cobjmacro">
        <xsl:with-param name="iface" select="$iface"/>
      </xsl:apply-templates>
    </xsl:otherwise>
  </xsl:choose>
  <!-- attributes (properties) -->
  <xsl:apply-templates select="attribute | if/attribute" mode="cobjmacro">
    <xsl:with-param name="iface" select="$iface"/>
  </xsl:apply-templates>
  <!-- methods -->
  <xsl:apply-templates select="method | if/method" mode="cobjmacro">
    <xsl:with-param name="iface" select="$iface"/>
  </xsl:apply-templates>
</xsl:template>


<!--
 *  emit flat vtable, compatible with COM
-->
<xsl:template match="interface" mode="vtab_flat">
  <xsl:param name="iface"/>

  <xsl:variable name="name" select="@name"/>
  <xsl:variable name="extends" select="@extends"/>
  <xsl:choose>
    <xsl:when test="$extends='$unknown'">
      <xsl:text>    nsresult (*QueryInterface)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, const nsID *iid, void **resultp);&#x0A;</xsl:text>
      <xsl:text>    nsrefcnt (*AddRef)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis);&#x0A;</xsl:text>
      <xsl:text>    nsrefcnt (*Release)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis);&#x0A;</xsl:text>
    </xsl:when>
    <xsl:when test="$extends='$errorinfo'">
      <xsl:text>    nsresult (*QueryInterface)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, const nsID *iid, void **resultp);&#x0A;</xsl:text>
      <xsl:text>    nsrefcnt (*AddRef)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis);&#x0A;</xsl:text>
      <xsl:text>    nsrefcnt (*Release)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*GetMessage)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, PRUnichar * *aMessage);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*GetResult)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, nsresult *aResult);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*GetName)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text>*pThis, PRUnichar * *aName);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*GetFilename)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, PRUnichar * *aFilename);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*GetLineNumber)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, PRUint32 *aLineNumber);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*GetColumnNumber)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, PRUint32 *aColumnNumber);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*GetLocation)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, nsIStackFrame * *aLocation);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*GetInner)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, nsIException * *aInner);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*GetData)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, nsISupports * *aData);&#x0A;</xsl:text>
      <xsl:text>    nsresult (*ToString)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, PRUnichar **_retval);&#x0A;</xsl:text>
    </xsl:when>
    <xsl:otherwise>
      <xsl:apply-templates select="key('G_keyInterfacesByName', $extends)" mode="vtab_flat">
        <xsl:with-param name="iface" select="$iface"/>
      </xsl:apply-templates>
    </xsl:otherwise>
  </xsl:choose>
  <!-- attributes (properties) -->
  <xsl:apply-templates select="attribute | if/attribute">
    <xsl:with-param name="iface" select="$iface"/>
  </xsl:apply-templates>
  <xsl:variable name="reservedAttributes" select="@reservedAttributes"/>
  <xsl:if test="$reservedAttributes > 0">
    <!-- tricky way to do a "for" loop without recursion -->
    <xsl:for-each select="(//*)[position() &lt;= $reservedAttributes]">
      <xsl:text>    nsresult (*GetInternalAndReservedAttribute</xsl:text>
      <xsl:value-of select="concat(position(), $name)"/>
      <xsl:text>)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis, PRUint32 *reserved);&#x0A;&#x0A;</xsl:text>
    </xsl:for-each>
  </xsl:if>
  <!-- methods -->
  <xsl:apply-templates select="method | if/method">
    <xsl:with-param name="iface" select="$iface"/>
  </xsl:apply-templates>
  <xsl:variable name="reservedMethods" select="@reservedMethods"/>
  <xsl:if test="$reservedMethods > 0">
    <!-- tricky way to do a "for" loop without recursion -->
    <xsl:for-each select="(//*)[position() &lt;= $reservedMethods]">
      <xsl:text>    nsresult (*InternalAndReservedMethod</xsl:text>
      <xsl:value-of select="concat(position(), $name)"/>
      <xsl:text>)(</xsl:text>
      <xsl:value-of select="$iface"/>
      <xsl:text> *pThis);&#x0A;&#x0A;</xsl:text>
    </xsl:for-each>
  </xsl:if>
</xsl:template>


<!--
 *  interfaces
-->
<xsl:template match="interface">
  <xsl:if test="not(@internal='yes')">
    <xsl:variable name="name" select="@name"/>
    <xsl:text>/* Start of struct </xsl:text>
    <xsl:value-of select="$name"/>
    <xsl:text> declaration */&#x0A;</xsl:text>
    <xsl:text>#define </xsl:text>
    <xsl:call-template name="string-to-upper">
      <xsl:with-param name="str" select="$name"/>
    </xsl:call-template>
    <xsl:value-of select="concat('_IID_STR &quot;',@uuid,'&quot;')"/>
    <xsl:text>&#x0A;</xsl:text>
    <xsl:text>#define </xsl:text>
    <xsl:call-template name="string-to-upper">
      <xsl:with-param name="str" select="$name"/>
    </xsl:call-template>
    <xsl:text>_IID { \&#x0A;</xsl:text>
    <xsl:text>    0x</xsl:text><xsl:value-of select="substring(@uuid,1,8)"/>
    <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,10,4)"/>
    <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,15,4)"/>
    <xsl:text>, \&#x0A;    </xsl:text>
    <xsl:text>{ 0x</xsl:text><xsl:value-of select="substring(@uuid,20,2)"/>
    <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,22,2)"/>
    <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,25,2)"/>
    <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,27,2)"/>
    <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,29,2)"/>
    <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,31,2)"/>
    <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,33,2)"/>
    <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,35,2)"/>
    <xsl:text> } \&#x0A;}&#x0A;</xsl:text>
    <xsl:text>/* COM compatibility */&#x0A;</xsl:text>
    <xsl:text>VBOX_EXTERN_CONST(nsIID, IID_</xsl:text>
    <xsl:value-of select="$name"/>
    <xsl:text>);&#x0A;</xsl:text>
    <xsl:text>#ifndef VBOX_WITH_GLUE&#x0A;</xsl:text>
    <xsl:text>struct </xsl:text>
    <xsl:value-of select="$name"/>
    <xsl:text>_vtbl&#x0A;{&#x0A;</xsl:text>
    <xsl:text>    </xsl:text>
    <xsl:choose>
      <xsl:when test="@extends='$unknown'">struct nsISupports_vtbl nsisupports;</xsl:when>
      <xsl:when test="@extends='$errorinfo'">struct nsIException_vtbl nsiexception;</xsl:when>
      <xsl:otherwise>
        <xsl:text>struct </xsl:text>
        <xsl:value-of select="@extends"/>
        <xsl:text>_vtbl </xsl:text>
        <xsl:call-template name="string-to-lower">
          <xsl:with-param name="str" select="@extends"/>
        </xsl:call-template>
        <xsl:text>;</xsl:text>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:text>&#x0A;&#x0A;</xsl:text>
    <!-- attributes (properties) -->
    <xsl:apply-templates select="attribute | if/attribute"/>
    <xsl:variable name="reservedAttributes" select="@reservedAttributes"/>
    <xsl:if test="$reservedAttributes > 0">
      <!-- tricky way to do a "for" loop without recursion -->
      <xsl:for-each select="(//*)[position() &lt;= $reservedAttributes]">
        <xsl:text>    nsresult (*GetInternalAndReservedAttribute</xsl:text>
        <xsl:value-of select="concat(position(), $name)"/>
        <xsl:text>)(</xsl:text>
        <xsl:value-of select="$name"/>
        <xsl:text> *pThis, PRUint32 *reserved);&#x0A;&#x0A;</xsl:text>
      </xsl:for-each>
    </xsl:if>
    <!-- methods -->
    <xsl:apply-templates select="method | if/method"/>
    <xsl:variable name="reservedMethods" select="@reservedMethods"/>
    <xsl:if test="$reservedMethods > 0">
      <!-- tricky way to do a "for" loop without recursion -->
      <xsl:for-each select="(//*)[position() &lt;= $reservedMethods]">
        <xsl:text>    nsresult (*InternalAndReservedMethod</xsl:text>
        <xsl:value-of select="concat(position(), $name)"/>
        <xsl:text>)(</xsl:text>
        <xsl:value-of select="$name"/>
        <xsl:text> *pThis);&#x0A;&#x0A;</xsl:text>
      </xsl:for-each>
    </xsl:if>
    <!-- -->
    <xsl:text>};&#x0A;</xsl:text>
    <xsl:text>#else /* VBOX_WITH_GLUE */&#x0A;</xsl:text>
    <xsl:text>struct </xsl:text>
    <xsl:value-of select="$name"/>
    <xsl:text>Vtbl&#x0A;{&#x0A;</xsl:text>
    <xsl:apply-templates select="." mode="vtab_flat">
      <xsl:with-param name="iface" select="$name"/>
    </xsl:apply-templates>
    <xsl:text>};&#x0A;</xsl:text>
    <xsl:apply-templates select="." mode="cobjmacro">
      <xsl:with-param name="iface" select="$name"/>
    </xsl:apply-templates>
    <!-- -->
    <xsl:text>#endif /* VBOX_WITH_GLUE */&#x0A;</xsl:text>
    <xsl:text>&#x0A;</xsl:text>
    <xsl:text>interface </xsl:text>
    <xsl:value-of select="$name"/>
    <xsl:text>&#x0A;{&#x0A;</xsl:text>
    <xsl:text>#ifndef VBOX_WITH_GLUE&#x0A;</xsl:text>
    <xsl:text>    struct </xsl:text>
    <xsl:value-of select="$name"/>
    <xsl:text>_vtbl *vtbl;&#x0A;</xsl:text>
    <xsl:text>#else /* VBOX_WITH_GLUE */&#x0A;</xsl:text>
    <xsl:text>    CONST_VTBL struct </xsl:text>
    <xsl:value-of select="$name"/>
    <xsl:text>Vtbl *lpVtbl;&#x0A;</xsl:text>
    <xsl:text>#endif /* VBOX_WITH_GLUE */&#x0A;</xsl:text>
    <xsl:text>};&#x0A;</xsl:text>
    <xsl:text>/* End of struct </xsl:text>
    <xsl:value-of select="$name"/>
    <xsl:text> declaration */&#x0A;&#x0A;</xsl:text>
    <xsl:call-template name="xsltprocNewlineOutputHack"/>
  </xsl:if>
</xsl:template>


<!--
 *  attributes
-->
<xsl:template match="attribute">
  <xsl:param name="iface" select="ancestor::interface/@name"/>

  <xsl:choose>
    <!-- safearray pseudo attribute -->
    <xsl:when test="@safearray='yes'">
      <!-- getter -->
      <xsl:text>    nsresult (*Get</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text>)(</xsl:text>
      <xsl:value-of select="$iface" />
      <xsl:text> *pThis, </xsl:text>
      <!-- array size -->
      <xsl:text>PRUint32 *</xsl:text>
      <xsl:value-of select="@name"/>
      <xsl:text>Size, </xsl:text>
      <!-- array pointer -->
      <xsl:apply-templates select="@type" mode="forwarder"/>
      <xsl:text> **</xsl:text>
      <xsl:value-of select="@name"/>
      <xsl:text>);&#x0A;</xsl:text>
      <!-- setter -->
      <xsl:if test="not(@readonly='yes')">
        <xsl:text>    nsresult (*Set</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text>)(</xsl:text>
        <xsl:value-of select="$iface" />
        <xsl:text> *pThis, </xsl:text>
        <!-- array size -->
        <xsl:text>PRUint32 </xsl:text>
        <xsl:value-of select="@name"/>
        <xsl:text>Size, </xsl:text>
        <!-- array pointer -->
        <xsl:apply-templates select="@type" mode="forwarder"/>
        <xsl:text> *</xsl:text>
        <xsl:value-of select="@name"/>
        <xsl:text>);&#x0A;</xsl:text>
      </xsl:if>
    </xsl:when>
    <!-- normal attribute -->
    <xsl:otherwise>
      <xsl:text>    </xsl:text>
      <xsl:if test="@readonly='yes'">
        <xsl:text>nsresult (*Get</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text>)(</xsl:text>
        <xsl:value-of select="$iface" />
        <xsl:text> *pThis, </xsl:text>
        <xsl:apply-templates select="@type" mode="forwarder"/>
        <xsl:text> *</xsl:text>
        <xsl:value-of select="@name"/>
        <xsl:text>);&#x0A;</xsl:text>
      </xsl:if>
      <xsl:choose>
        <xsl:when test="@readonly='yes'">
        </xsl:when>
        <xsl:otherwise>
          <xsl:text>nsresult (*Get</xsl:text>
          <xsl:call-template name="capitalize">
            <xsl:with-param name="str" select="@name"/>
          </xsl:call-template>
          <xsl:text>)(</xsl:text>
          <xsl:value-of select="$iface" />
          <xsl:text> *pThis, </xsl:text>
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text> *</xsl:text>
          <xsl:value-of select="@name"/>
          <xsl:text>);&#x0A;    </xsl:text>
          <xsl:text>nsresult (*Set</xsl:text>
          <xsl:call-template name="capitalize">
            <xsl:with-param name="str" select="@name"/>
          </xsl:call-template>
          <xsl:text>)(</xsl:text>
          <xsl:value-of select="$iface" />
          <xsl:text> *pThis, </xsl:text>
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text> </xsl:text>
          <xsl:value-of select="@name"/>
          <xsl:text>);&#x0A;</xsl:text>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>

<xsl:template match="attribute" mode="cobjmacro">
  <xsl:param name="iface"/>

  <!-- getter (COM compatible) -->
  <xsl:text>#define </xsl:text>
  <xsl:value-of select="concat($iface, '_get_')"/>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>(p, a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>) ((p)->lpVtbl->Get</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>(p, a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>))&#x0A;</xsl:text>

  <!-- getter (XPCOM compatible) -->
  <xsl:text>#define </xsl:text>
  <xsl:value-of select="concat($iface, '_Get')"/>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>(p, a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>) ((p)->lpVtbl->Get</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>(p, a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>))&#x0A;</xsl:text>

  <xsl:if test="not(@readonly='yes')">
    <!-- setter (COM compatible) -->
    <xsl:text>#define </xsl:text>
    <xsl:value-of select="concat($iface, '_put_')"/>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>(p, a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>) ((p)->lpVtbl->Set</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>(p, a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>))&#x0A;</xsl:text>

    <!-- setter (XPCOM compatible) -->
    <xsl:text>#define </xsl:text>
    <xsl:value-of select="concat($iface, '_Set')"/>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>(p, a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>) ((p)->lpVtbl->Set</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>(p, a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>))&#x0A;</xsl:text>

  </xsl:if>
</xsl:template>

<!--
 *  methods
-->
<xsl:template match="method">
  <xsl:param name="iface" select="ancestor::interface/@name"/>

  <xsl:if test="param/@mod='ptr'">
    <!-- methods using native types must be non-scriptable
    <xsl:text>    [noscript]&#x0A;</xsl:text>-->
  </xsl:if>
  <xsl:text>    nsresult (*</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:if test="param">
    <xsl:text>)(&#x0A;</xsl:text>
    <xsl:text>        </xsl:text>
    <xsl:value-of select="$iface" />
    <xsl:text> *pThis,&#x0A;</xsl:text>
    <xsl:for-each select="param [position() != last()]">
      <xsl:text>        </xsl:text>
      <xsl:apply-templates select="."/>
      <xsl:text>,&#x0A;</xsl:text>
    </xsl:for-each>
    <xsl:text>        </xsl:text>
    <xsl:apply-templates select="param [last()]"/>
    <xsl:text>&#x0A;    );&#x0A;</xsl:text>
  </xsl:if>
  <xsl:if test="not(param)">
    <xsl:text>)(</xsl:text>
    <xsl:value-of select="$iface" />
    <xsl:text> *pThis );&#x0A;</xsl:text>
  </xsl:if>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>

<xsl:template match="method" mode="cobjmacro">
  <xsl:param name="iface"/>

  <xsl:text>#define </xsl:text>
  <xsl:value-of select="concat($iface, '_')"/>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>(p</xsl:text>
  <xsl:for-each select="param">
    <xsl:text>, a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
  </xsl:for-each>
  <xsl:text>) ((p)->lpVtbl-></xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>(p</xsl:text>
  <xsl:for-each select="param">
    <xsl:text>, a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
  </xsl:for-each>
  <xsl:text>))&#x0A;</xsl:text>
</xsl:template>


<!--
 *  modules
-->
<xsl:template match="module">
  <xsl:apply-templates select="class"/>
</xsl:template>


<!--
 *  co-classes
-->
<xsl:template match="module/class">
  <!-- class and contract id -->
  <xsl:text>&#x0A;</xsl:text>
  <xsl:text>#define NS_</xsl:text>
  <xsl:call-template name="string-to-upper">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_CID { \&#x0A;</xsl:text>
  <xsl:text>    0x</xsl:text><xsl:value-of select="substring(@uuid,1,8)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,10,4)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,15,4)"/>
  <xsl:text>, \&#x0A;    </xsl:text>
  <xsl:text>{ 0x</xsl:text><xsl:value-of select="substring(@uuid,20,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,22,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,25,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,27,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,29,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,31,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,33,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,35,2)"/>
  <xsl:text> } \&#x0A;}&#x0A;</xsl:text>
  <xsl:text>#define NS_</xsl:text>
  <xsl:call-template name="string-to-upper">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <!-- Contract ID -->
  <xsl:text>_CONTRACTID &quot;@</xsl:text>
  <xsl:value-of select="@namespace"/>
  <xsl:text>/</xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>;1&quot;&#x0A;</xsl:text>
  <!-- CLSID_xxx declarations for XPCOM, for compatibility with Win32 -->
  <xsl:text>/* COM compatibility */&#x0A;</xsl:text>
  <xsl:text>VBOX_EXTERN_CONST(nsCID, CLSID_</xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>);&#x0A;</xsl:text>
  <xsl:text>&#x0A;&#x0A;</xsl:text>
</xsl:template>


<!--
 *  enums
-->
<xsl:template match="enum">
  <xsl:text>/* Start of enum </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text> declaration */&#x0A;</xsl:text>
  <xsl:text>#define </xsl:text>
  <xsl:call-template name="string-to-upper">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:value-of select="concat('_IID_STR &quot;',@uuid,'&quot;')"/>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:text>#define </xsl:text>
  <xsl:call-template name="string-to-upper">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_IID { \&#x0A;</xsl:text>
  <xsl:text>    0x</xsl:text><xsl:value-of select="substring(@uuid,1,8)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,10,4)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,15,4)"/>
  <xsl:text>, \&#x0A;    </xsl:text>
  <xsl:text>{ 0x</xsl:text><xsl:value-of select="substring(@uuid,20,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,22,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,25,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,27,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,29,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,31,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,33,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,35,2)"/>
  <xsl:text> } \&#x0A;}&#x0A;</xsl:text>
  <xsl:text>typedef enum </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>&#x0A;{&#x0A;</xsl:text>
  <xsl:variable name="this" select="."/>
  <xsl:for-each select="const">
    <xsl:text>    </xsl:text>
    <xsl:value-of select="$this/@name"/>
    <xsl:text>_</xsl:text>
    <xsl:value-of select="@name"/> = <xsl:value-of select="@value"/>
    <xsl:if test="position() != last()">
      <xsl:text>,</xsl:text>
    </xsl:if>
    <xsl:text>&#x0A;</xsl:text>
  </xsl:for-each>
  <xsl:text>} </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>;&#x0A;</xsl:text>
  <xsl:text>/* End of enum </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text> declaration */
#define </xsl:text>
  <xsl:value-of select="concat(@name, '_T PRUint32&#x0A;&#x0A;&#x0A;')"/>
</xsl:template>


<!--
 *  method parameters
-->
<xsl:template match="method/param">
  <xsl:choose>
    <!-- safearray parameters -->
    <xsl:when test="@safearray='yes'">
      <!-- array size -->
      <xsl:choose>
        <xsl:when test="@dir='in'">
          <xsl:text>PRUint32 </xsl:text>
          <xsl:value-of select="@name"/>
          <xsl:text>Size,&#x0A;</xsl:text>
        </xsl:when>
        <xsl:when test="@dir='out'">
          <xsl:text>PRUint32 *</xsl:text>
          <xsl:value-of select="@name"/>
          <xsl:text>Size,&#x0A;</xsl:text>
        </xsl:when>
        <xsl:when test="@dir='return'">
          <xsl:text>PRUint32 *</xsl:text>
          <xsl:value-of select="@name"/>
          <xsl:text>Size,&#x0A;</xsl:text>
        </xsl:when>
        <xsl:otherwise>
          <xsl:text>PRUint32 </xsl:text>
          <xsl:value-of select="@name"/>
          <xsl:text>Size,&#x0A;</xsl:text>
        </xsl:otherwise>
      </xsl:choose>
      <!-- array pointer -->
      <xsl:text>        </xsl:text>
      <xsl:choose>
        <xsl:when test="@dir='in'">
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text>*</xsl:text>
        </xsl:when>
        <xsl:when test="@dir='out'">
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text>**</xsl:text>
        </xsl:when>
        <xsl:when test="@dir='return'">
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text>**</xsl:text>
        </xsl:when>
        <xsl:otherwise>
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text>*</xsl:text>
        </xsl:otherwise>
      </xsl:choose>
      <xsl:text> </xsl:text>
      <xsl:value-of select="@name"/>
    </xsl:when>
    <!-- normal and array parameters -->
    <xsl:otherwise>
      <xsl:choose>
        <xsl:when test="@dir='in'">
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text></xsl:text>
        </xsl:when>
        <xsl:when test="@dir='out'">
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text> *</xsl:text>
        </xsl:when>
        <xsl:when test="@dir='return'">
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text> *</xsl:text>
        </xsl:when>
        <xsl:otherwise>
          <xsl:apply-templates select="@type" mode="forwarder"/>
          <xsl:text></xsl:text>
        </xsl:otherwise>
      </xsl:choose>
      <xsl:text> </xsl:text>
      <xsl:value-of select="@name"/>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="method/param" mode="forwarder">
  <xsl:if test="@safearray='yes'">
    <xsl:text>PRUint32</xsl:text>
    <xsl:if test="@dir='out' or @dir='return'">
      <xsl:text> *</xsl:text>
    </xsl:if>
    <xsl:text> a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>Size, </xsl:text>
  </xsl:if>
  <xsl:apply-templates select="@type" mode="forwarder"/>
  <xsl:if test="@dir='out' or @dir='return'">
    <xsl:text> *</xsl:text>
  </xsl:if>
  <xsl:if test="@safearray='yes'">
    <xsl:text> *</xsl:text>
  </xsl:if>
  <xsl:text> a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
</xsl:template>


<!--
 *  attribute/parameter type conversion
-->
<xsl:template match="attribute/@type | param/@type">
  <xsl:variable name="self_target" select="current()/ancestor::if/@target"/>

  <xsl:choose>
    <!-- modifiers -->
    <xsl:when test="name(current())='type' and ../@mod">
      <xsl:choose>
        <xsl:when test="../@mod='ptr'">
          <xsl:choose>
            <!-- standard types -->
            <!--xsl:when test=".='result'">??</xsl:when-->
            <xsl:when test=".='boolean'">booleanPtr</xsl:when>
            <xsl:when test=".='octet'">octetPtr</xsl:when>
            <xsl:when test=".='short'">shortPtr</xsl:when>
            <xsl:when test=".='unsigned short'">ushortPtr</xsl:when>
            <xsl:when test=".='long'">longPtr</xsl:when>
            <xsl:when test=".='long long'">llongPtr</xsl:when>
            <xsl:when test=".='unsigned long'">ulongPtr</xsl:when>
            <xsl:when test=".='unsigned long long'">ullongPtr</xsl:when>
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
                <xsl:text>attribute 'mod=</xsl:text>
                <xsl:value-of select="concat('&quot;',../@mod,'&quot;')"/>
                <xsl:text>' cannot be used with type </xsl:text>
                <xsl:value-of select="concat('&quot;',current(),'&quot;!')"/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:when>
        <xsl:when test="../@mod='string'">
          <xsl:choose>
            <!-- standard types -->
            <!--xsl:when test=".='result'">??</xsl:when-->
            <xsl:when test=".='uuid'">wstring</xsl:when>
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
                <xsl:text>attribute 'mod=</xsl:text>
                <xsl:value-of select="concat('&quot;',../@mod,'&quot;')"/>
                <xsl:text>' cannot be used with type </xsl:text>
                <xsl:value-of select="concat('&quot;',current(),'&quot;!')"/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:when>
        <xsl:otherwise>
          <xsl:message terminate="yes">
            <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
            <xsl:value-of select="concat('value &quot;',../@mod,'&quot; ')"/>
            <xsl:text>of attribute 'mod' is invalid!</xsl:text>
          </xsl:message>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:when>
    <!-- no modifiers -->
    <xsl:otherwise>
      <xsl:choose>
        <!-- standard types -->
        <xsl:when test=".='result'">nsresult</xsl:when>
        <xsl:when test=".='boolean'">boolean</xsl:when>
        <xsl:when test=".='octet'">octet</xsl:when>
        <xsl:when test=".='short'">short</xsl:when>
        <xsl:when test=".='unsigned short'">unsigned short</xsl:when>
        <xsl:when test=".='long'">long</xsl:when>
        <xsl:when test=".='long long'">long long</xsl:when>
        <xsl:when test=".='unsigned long'">unsigned long</xsl:when>
        <xsl:when test=".='unsigned long long'">unsigned long long</xsl:when>
        <xsl:when test=".='char'">char</xsl:when>
        <xsl:when test=".='wchar'">wchar</xsl:when>
        <xsl:when test=".='string'">string</xsl:when>
        <xsl:when test=".='wstring'">wstring</xsl:when>
        <!-- UUID type -->
        <xsl:when test=".='uuid'">
          <xsl:choose>
            <xsl:when test="name(..)='attribute'">
              <xsl:choose>
                <xsl:when test="../@readonly='yes'">
                  <xsl:text>nsIDPtr</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:message terminate="yes">
                    <xsl:value-of select="../@name"/>
                    <xsl:text>: Non-readonly uuid attributes are not supported!</xsl:text>
                  </xsl:message>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:when>
            <xsl:when test="name(..)='param'">
              <xsl:choose>
                <xsl:when test="../@dir='in' and not(../@safearray='yes')">
                  <xsl:text>nsIDRef</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:text>nsIDPtr</xsl:text>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:when>
          </xsl:choose>
        </xsl:when>
        <!-- system interface types -->
        <xsl:when test=".='$unknown'">nsISupports</xsl:when>
        <xsl:otherwise>
          <xsl:choose>
            <!-- enum types -->
            <xsl:when test="
              (ancestor::library/application/enum[@name=current()]) or
              (ancestor::library/application/if[@target=$self_target]/enum[@name=current()])
            ">
              <xsl:text>PRUint32</xsl:text>
            </xsl:when>
            <!-- custom interface types -->
            <xsl:when test="
              (ancestor::library/application/interface[@name=current()]) or
              (ancestor::library/application/if[@target=$self_target]/interface[@name=current()])
            ">
              <xsl:value-of select="."/>
            </xsl:when>
            <!-- other types -->
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:text>Unknown parameter type: </xsl:text>
                <xsl:value-of select="."/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="attribute/@type | param/@type" mode="forwarder">

  <xsl:variable name="self_target" select="current()/ancestor::if/@target"/>

  <xsl:choose>
    <!-- modifiers -->
    <xsl:when test="name(current())='type' and ../@mod">
      <xsl:choose>
        <xsl:when test="../@mod='ptr'">
          <xsl:choose>
            <!-- standard types -->
            <!--xsl:when test=".='result'">??</xsl:when-->
            <xsl:when test=".='boolean'">PRBool *</xsl:when>
            <xsl:when test=".='octet'">PRUint8 *</xsl:when>
            <xsl:when test=".='short'">PRInt16 *</xsl:when>
            <xsl:when test=".='unsigned short'">PRUint16 *</xsl:when>
            <xsl:when test=".='long'">PRInt32 *</xsl:when>
            <xsl:when test=".='long long'">PRInt64 *</xsl:when>
            <xsl:when test=".='unsigned long'">PRUint32 *</xsl:when>
            <xsl:when test=".='unsigned long long'">PRUint64 *</xsl:when>
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
                <xsl:text>attribute 'mod=</xsl:text>
                <xsl:value-of select="concat('&quot;',../@mod,'&quot;')"/>
                <xsl:text>' cannot be used with type </xsl:text>
                <xsl:value-of select="concat('&quot;',current(),'&quot;!')"/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:when>
        <xsl:when test="../@mod='string'">
          <xsl:choose>
            <!-- standard types -->
            <!--xsl:when test=".='result'">??</xsl:when-->
            <xsl:when test=".='uuid'">PRUnichar *</xsl:when>
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
                <xsl:text>attribute 'mod=</xsl:text>
                <xsl:value-of select="concat('&quot;',../@mod,'&quot;')"/>
                <xsl:text>' cannot be used with type </xsl:text>
                <xsl:value-of select="concat('&quot;',current(),'&quot;!')"/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:when>
      </xsl:choose>
    </xsl:when>
    <!-- no modifiers -->
    <xsl:otherwise>
      <xsl:choose>
        <!-- standard types -->
        <xsl:when test=".='result'">nsresult</xsl:when>
        <xsl:when test=".='boolean'">PRBool</xsl:when>
        <xsl:when test=".='octet'">PRUint8</xsl:when>
        <xsl:when test=".='short'">PRInt16</xsl:when>
        <xsl:when test=".='unsigned short'">PRUint16</xsl:when>
        <xsl:when test=".='long'">PRInt32</xsl:when>
        <xsl:when test=".='long long'">PRInt64</xsl:when>
        <xsl:when test=".='unsigned long'">PRUint32</xsl:when>
        <xsl:when test=".='unsigned long long'">PRUint64</xsl:when>
        <xsl:when test=".='char'">char</xsl:when>
        <xsl:when test=".='wchar'">PRUnichar</xsl:when>
        <!-- string types -->
        <xsl:when test=".='string'">char *</xsl:when>
        <xsl:when test=".='wstring'">PRUnichar *</xsl:when>
        <!-- UUID type -->
        <xsl:when test=".='uuid'">
          <xsl:choose>
            <xsl:when test="name(..)='attribute'">
              <xsl:choose>
                <xsl:when test="../@readonly='yes'">
                  <xsl:text>nsID *</xsl:text>
                </xsl:when>
              </xsl:choose>
            </xsl:when>
            <xsl:when test="name(..)='param'">
              <xsl:choose>
                <xsl:when test="../@dir='in' and not(../@safearray='yes')">
                  <xsl:text>const nsID *</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:text>nsID *</xsl:text>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:when>
          </xsl:choose>
        </xsl:when>
        <!-- system interface types -->
        <xsl:when test=".='$unknown'">nsISupports *</xsl:when>
        <xsl:otherwise>
          <xsl:choose>
            <!-- enum types -->
            <xsl:when test="
              (ancestor::library/application/enum[@name=current()]) or
              (ancestor::library/application/if[@target=$self_target]/enum[@name=current()])
            ">
              <xsl:text>PRUint32</xsl:text>
            </xsl:when>
            <!-- custom interface types -->
            <xsl:when test="
              (ancestor::library/application/interface[@name=current()]) or
              (ancestor::library/application/if[@target=$self_target]/interface[@name=current()])
            ">
              <xsl:value-of select="."/>
              <xsl:text> *</xsl:text>
            </xsl:when>
            <!-- other types -->
          </xsl:choose>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>


<xsl:template match="application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']//module/class" />

<xsl:template match="application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']/if//interface
| application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']//interface" />

<xsl:template match="application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']//interface" mode="forward" />


</xsl:stylesheet>
