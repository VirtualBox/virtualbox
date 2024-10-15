/* $Id: tstCAPIGlue.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file tstCAPIGlue.c
 * Demonstrator program to illustrate use of C bindings of Main API.
 *
 * It has sample code showing how to retrieve all available error information,
 * and how to handle active (event delivery through callbacks) or passive
 * (event delivery through a polling mechanism) event listeners.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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


/** @todo
 * Our appologies for the 256+ missing return code checks in this sample file.
 *
 * We strongly recomment users of the VBoxCAPI to check all return codes!
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxCAPIGlue.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef WIN32
# include <signal.h>
# include <unistd.h>
# include <sys/poll.h>
#endif
#ifdef IPRT_INCLUDED_cdefs_h
# error "not supposed to involve any IPRT or VBox headers here."
#endif

/**
 * Select between active event listener (defined) and passive event listener
 * (undefined). The active event listener case needs much more code, and
 * additionally requires a lot more platform dependent code.
 */
#undef USE_ACTIVE_EVENT_LISTENER


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Set by Ctrl+C handler. */
static volatile int g_fStop = 0;

#ifdef USE_ACTIVE_EVENT_LISTENER
# ifdef WIN32
/** The COM type information for IEventListener, for implementing IDispatch. */
static ITypeInfo *g_pTInfoIEventListener = NULL;
# endif /* WIN32 */
#endif /* USE_ACTIVE_EVENT_LISTENER */

static const char *GetStateName(MachineState_T machineState)
{
    switch (machineState)
    {
        case MachineState_Null:                return "<null>";
        case MachineState_PoweredOff:          return "PoweredOff";
        case MachineState_Saved:               return "Saved";
        case MachineState_Teleported:          return "Teleported";
        case MachineState_Aborted:             return "Aborted";
        case MachineState_AbortedSaved:        return "Aborted-Saved";
        case MachineState_Running:             return "Running";
        case MachineState_Paused:              return "Paused";
        case MachineState_Stuck:               return "Stuck";
        case MachineState_Teleporting:         return "Teleporting";
        case MachineState_LiveSnapshotting:    return "LiveSnapshotting";
        case MachineState_Starting:            return "Starting";
        case MachineState_Stopping:            return "Stopping";
        case MachineState_Saving:              return "Saving";
        case MachineState_Restoring:           return "Restoring";
        case MachineState_TeleportingPausedVM: return "TeleportingPausedVM";
        case MachineState_TeleportingIn:       return "TeleportingIn";
        case MachineState_DeletingSnapshotOnline: return "DeletingSnapshotOnline";
        case MachineState_DeletingSnapshotPaused: return "DeletingSnapshotPaused";
        case MachineState_RestoringSnapshot:   return "RestoringSnapshot";
        case MachineState_DeletingSnapshot:    return "DeletingSnapshot";
        case MachineState_SettingUp:           return "SettingUp";
        default:                               return "no idea";
    }
}

/**
 * Ctrl+C handler, terminate event listener.
 *
 * Remember that most function calls are not allowed in this context (including
 * printf!), so make sure that this does as little as possible.
 *
 * @param  iInfo    Platform dependent detail info (ignored).
 */
#ifdef WIN32
static BOOL VBOX_WINAPI ctrlCHandler(DWORD iInfo)
{
    (void)iInfo;
    g_fStop = 1;
    return TRUE;
}
#else
static void ctrlCHandler(int iInfo)
{
    (void)iInfo;
    g_fStop = 1;
}
#endif

/**
 * Sample event processing function, dumping some event information.
 * Shared between active and passive event demo, to highlight that this part
 * is identical between the two.
 */
static HRESULT EventListenerDemoProcessEvent(IEvent *event)
{
    VBoxEventType_T evType;
    HRESULT hrc;

    if (!event)
    {
        printf("event null\n");
        return S_OK;
    }

    evType = VBoxEventType_Invalid;
    hrc = IEvent_get_Type(event, &evType);
    if (FAILED(hrc))
    {
        printf("cannot get event type, hrc=%#x\n", (unsigned)hrc);
        return S_OK;
    }

    switch (evType)
    {
        case VBoxEventType_OnMousePointerShapeChanged:
            printf("OnMousePointerShapeChanged\n");
            break;

        case VBoxEventType_OnMouseCapabilityChanged:
            printf("OnMouseCapabilityChanged\n");
            break;

        case VBoxEventType_OnKeyboardLedsChanged:
            printf("OnMouseCapabilityChanged\n");
            break;

        case VBoxEventType_OnStateChanged:
        {
            IStateChangedEvent *ev = NULL;
            enum MachineState state;
            hrc = IEvent_QueryInterface(event, &IID_IStateChangedEvent, (void **)&ev);
            if (FAILED(hrc))
            {
                printf("cannot get StateChangedEvent interface, hrc=%#x\n", (unsigned)hrc);
                return S_OK;
            }
            if (!ev)
            {
                printf("StateChangedEvent reference null\n");
                return S_OK;
            }
            hrc = IStateChangedEvent_get_State(ev, &state);
            if (FAILED(hrc))
                printf("warning: cannot get state, hrc=%#x\n", (unsigned)hrc);
            IStateChangedEvent_Release(ev);
            printf("OnStateChanged: %s\n", GetStateName(state));

            fflush(stdout);
            if (   state == MachineState_PoweredOff
                || state == MachineState_Saved
                || state == MachineState_Teleported
                || state == MachineState_Aborted
                || state == MachineState_AbortedSaved
               )
                g_fStop = 1;
            break;
        }

        case VBoxEventType_OnAdditionsStateChanged:
            printf("OnAdditionsStateChanged\n");
            break;

        case VBoxEventType_OnNetworkAdapterChanged:
            printf("OnNetworkAdapterChanged\n");
            break;

        case VBoxEventType_OnSerialPortChanged:
            printf("OnSerialPortChanged\n");
            break;

        case VBoxEventType_OnParallelPortChanged:
            printf("OnParallelPortChanged\n");
            break;

        case VBoxEventType_OnStorageControllerChanged:
            printf("OnStorageControllerChanged\n");
            break;

        case VBoxEventType_OnMediumChanged:
            printf("OnMediumChanged\n");
            break;

        case VBoxEventType_OnVRDEServerChanged:
            printf("OnVRDEServerChanged\n");
            break;

        case VBoxEventType_OnUSBControllerChanged:
            printf("OnUSBControllerChanged\n");
            break;

        case VBoxEventType_OnUSBDeviceStateChanged:
            printf("OnUSBDeviceStateChanged\n");
            break;

        case VBoxEventType_OnSharedFolderChanged:
            printf("OnSharedFolderChanged\n");
            break;

        case VBoxEventType_OnRuntimeError:
            printf("OnRuntimeError\n");
            break;

        case VBoxEventType_OnCanShowWindow:
            printf("OnCanShowWindow\n");
            break;
        case VBoxEventType_OnShowWindow:
            printf("OnShowWindow\n");
            break;

        default:
            printf("unknown event: %d\n", evType);
    }

    return S_OK;
}

#ifdef USE_ACTIVE_EVENT_LISTENER

struct IEventListenerDemo;
typedef struct IEventListenerDemo IEventListenerDemo;

typedef struct IEventListenerDemoVtbl
{
    HRESULT (*QueryInterface)(IEventListenerDemo *pThis, REFIID riid, void **ppvObject);
    ULONG (*AddRef)(IEventListenerDemo *pThis);
    ULONG (*Release)(IEventListenerDemo *pThis);
#ifdef WIN32
    HRESULT (*GetTypeInfoCount)(IEventListenerDemo *pThis, UINT *pctinfo);
    HRESULT (*GetTypeInfo)(IEventListenerDemo *pThis, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo);
    HRESULT (*GetIDsOfNames)(IEventListenerDemo *pThis, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId);
    HRESULT (*Invoke)(IEventListenerDemo *pThis, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr);
#endif
    HRESULT (*HandleEvent)(IEventListenerDemo *pThis, IEvent *aEvent);
} IEventListenerDemoVtbl;

typedef struct IEventListenerDemo
{
    struct IEventListenerDemoVtbl *lpVtbl;

    int cRef;

#ifdef WIN32
    /* Active event delivery needs a free threaded marshaler, as the default
     * proxy marshaling cannot deal correctly with this case. */
    IUnknown *pUnkMarshaler;
#endif
} IEventListenerDemo;

/* Defines for easily calling IEventListenerDemo functions. */

/* IUnknown functions. */
#define IEventListenerDemo_QueryInterface(This,riid,ppvObject) \
    ( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define IEventListenerDemo_AddRef(This) \
    ( (This)->lpVtbl->AddRef(This) )

#define IEventListenerDemo_Release(This) \
    ( (This)->lpVtbl->Release(This) )

#ifdef WIN32
/* IDispatch functions. */
#define IEventListenerDemo_GetTypeInfoCount(This,pctinfo) \
    ( (This)->lpVtbl->GetTypeInfoCount(This,pctinfo) )

#define IEventListenerDemo_GetTypeInfo(This,iTInfo,lcid,ppTInfo) \
    ( (This)->lpVtbl->GetTypeInfo(This,iTInfo,lcid,ppTInfo) )

#define IEventListenerDemo_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) \
    ( (This)->lpVtbl->GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) )

#define IEventListenerDemo_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) \
    ( (This)->lpVtbl->Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) )
#endif

/* IEventListener functions. */
#define IEventListenerDemo_HandleEvent(This,aEvent) \
    ( (This)->lpVtbl->HandleEvent(This,aEvent) )


/**
 * Event handler function, for active event processing.
 */
static HRESULT IEventListenerDemoImpl_HandleEvent(IEventListenerDemo *pThis, IEvent *event)
{
    return EventListenerDemoProcessEvent(event);
}

static HRESULT IEventListenerDemoImpl_QueryInterface(IEventListenerDemo *pThis, const IID *iid, void **resultp)
{
    /* match iid */
    if (    !memcmp(iid, &IID_IEventListener, sizeof(IID))
        ||  !memcmp(iid, &IID_IDispatch, sizeof(IID))
        ||  !memcmp(iid, &IID_IUnknown, sizeof(IID)))
    {
        IEventListenerDemo_AddRef(pThis);
        *resultp = pThis;
        return S_OK;
    }
#ifdef WIN32
    if (!memcmp(iid, &IID_IMarshal, sizeof(IID)))
        return IUnknown_QueryInterface(pThis->pUnkMarshaler, iid, resultp);
#endif

    return E_NOINTERFACE;
}

static HRESULT IEventListenerDemoImpl_AddRef(IEventListenerDemo *pThis)
{
    return ++(pThis->cRef);
}

static HRESULT IEventListenerDemoImpl_Release(IEventListenerDemo *pThis)
{
    HRESULT c;

    c = --(pThis->cRef);
    if (!c)
        free(pThis);
    return c;
}

#ifdef WIN32
static HRESULT IEventListenerDemoImpl_GetTypeInfoCount(IEventListenerDemo *pThis, UINT *pctinfo)
{
    if (!pctinfo)
        return E_POINTER;
    *pctinfo = 1;
    return S_OK;
}

static HRESULT IEventListenerDemoImpl_GetTypeInfo(IEventListenerDemo *pThis, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
    if (!ppTInfo)
        return E_POINTER;
    ITypeInfo_AddRef(g_pTInfoIEventListener);
    *ppTInfo = g_pTInfoIEventListener;
    return S_OK;
}

static HRESULT IEventListenerDemoImpl_GetIDsOfNames(IEventListenerDemo *pThis, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    return ITypeInfo_GetIDsOfNames(g_pTInfoIEventListener, rgszNames, cNames, rgDispId);
}

static HRESULT IEventListenerDemoImpl_Invoke(IEventListenerDemo *pThis, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    return ITypeInfo_Invoke(g_pTInfoIEventListener, (IDispatch *)pThis, dispIdMember, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT LoadTypeInfo(REFIID riid, ITypeInfo **pTInfo)
{
    HRESULT hrc;
    ITypeLib *pTypeLib;
    hrc = LoadRegTypeLib(&LIBID_VirtualBox, 1 /* major */, 0 /* minor */, 0 /* lcid */, &pTypeLib);
    if (FAILED(hrc))
        return hrc;
    hrc = ITypeLib_GetTypeInfoOfGuid(pTypeLib, riid, pTInfo);

    /* No longer need access to the type lib, release it. */
    ITypeLib_Release(pTypeLib);

    return hrc;
}
#endif

#ifdef __GNUC__
typedef struct IEventListenerDemoVtblInt
{
    ptrdiff_t offset_to_top;
    void *typeinfo;
    IEventListenerDemoVtbl lpVtbl;
} IEventListenerDemoVtblInt;

static IEventListenerDemoVtblInt g_IEventListenerDemoVtblInt =
{
    0,      /* offset_to_top */
    NULL,   /* typeinfo, not vital */
    {
        IEventListenerDemoImpl_QueryInterface,
        IEventListenerDemoImpl_AddRef,
        IEventListenerDemoImpl_Release,
#ifdef WIN32
        IEventListenerDemoImpl_GetTypeInfoCount,
        IEventListenerDemoImpl_GetTypeInfo,
        IEventListenerDemoImpl_GetIDsOfNames,
        IEventListenerDemoImpl_Invoke,
#endif
        IEventListenerDemoImpl_HandleEvent
    }
};
#elif defined(_MSC_VER)
typedef struct IEventListenerDemoVtblInt
{
    IEventListenerDemoVtbl lpVtbl;
} IEventListenerDemoVtblInt;

static IEventListenerDemoVtblInt g_IEventListenerDemoVtblInt =
{
    {
        IEventListenerDemoImpl_QueryInterface,
        IEventListenerDemoImpl_AddRef,
        IEventListenerDemoImpl_Release,
#ifdef WIN32
        IEventListenerDemoImpl_GetTypeInfoCount,
        IEventListenerDemoImpl_GetTypeInfo,
        IEventListenerDemoImpl_GetIDsOfNames,
        IEventListenerDemoImpl_Invoke,
#endif
        IEventListenerDemoImpl_HandleEvent
    }
};
#else
# error Port me!
#endif

/**
 * Register active event listener for the selected VM.
 *
 * @param   virtualBox ptr to IVirtualBox object
 * @param   session    ptr to ISession object
 */
static void registerActiveEventListener(IVirtualBox *virtualBox, ISession *session)
{
    IConsole *console = NULL;
    HRESULT hrc;

    hrc = ISession_get_Console(session, &console);
    if (SUCCEEDED(hrc) && console)
    {
        IEventSource *es = NULL;
        hrc = IConsole_get_EventSource(console, &es);
        if (SUCCEEDED(hrc) && es)
        {
            static const ULONG s_auInterestingEvents[] =
            {
                VBoxEventType_OnMousePointerShapeChanged,
                VBoxEventType_OnMouseCapabilityChanged,
                VBoxEventType_OnKeyboardLedsChanged,
                VBoxEventType_OnStateChanged,
                VBoxEventType_OnAdditionsStateChanged,
                VBoxEventType_OnNetworkAdapterChanged,
                VBoxEventType_OnSerialPortChanged,
                VBoxEventType_OnParallelPortChanged,
                VBoxEventType_OnStorageControllerChanged,
                VBoxEventType_OnMediumChanged,
                VBoxEventType_OnVRDEServerChanged,
                VBoxEventType_OnUSBControllerChanged,
                VBoxEventType_OnUSBDeviceStateChanged,
                VBoxEventType_OnSharedFolderChanged,
                VBoxEventType_OnRuntimeError,
                VBoxEventType_OnCanShowWindow,
                VBoxEventType_OnShowWindow
            };
            SAFEARRAY *interestingEventsSA = NULL;
            IEventListenerDemo *consoleListener = NULL;

            /* The VirtualBox API expects enum values as VT_I4, which in the
             * future can be hopefully relaxed. */
            interestingEventsSA = g_pVBoxFuncs->pfnSafeArrayCreateVector(VT_I4, 0,
                                                                           sizeof(s_auInterestingEvents)
                                                                         / sizeof(s_auInterestingEvents[0]));
            g_pVBoxFuncs->pfnSafeArrayCopyInParamHelper(interestingEventsSA, &s_auInterestingEvents,
                                                        sizeof(s_auInterestingEvents));

            consoleListener = calloc(1, sizeof(IEventListenerDemo));
            if (consoleListener)
            {
                consoleListener->lpVtbl = &(g_IEventListenerDemoVtblInt.lpVtbl);
#ifdef WIN32
                CoCreateFreeThreadedMarshaler((IUnknown *)consoleListener, &consoleListener->pUnkMarshaler);
#endif
                IEventListenerDemo_AddRef(consoleListener);

                hrc = IEventSource_RegisterListener(es, (IEventListener *)consoleListener,
                                                    ComSafeArrayAsInParam(interestingEventsSA),
                                                    1 /* active */);
                if (SUCCEEDED(hrc))
                {
                    /* Just wait here for events, no easy way to do this better
                     * as there's not much to do after this completes. */
                    printf("Entering event loop, PowerOff the machine to exit or press Ctrl-C to terminate\n");
                    fflush(stdout);
#ifdef WIN32
                    SetConsoleCtrlHandler(ctrlCHandler, TRUE);
#else
                    signal(SIGINT, (void (*)(int))ctrlCHandler);
#endif

                    while (!g_fStop)
                        g_pVBoxFuncs->pfnProcessEventQueue(250);

#ifdef WIN32
                    SetConsoleCtrlHandler(ctrlCHandler, FALSE);
#else
                    signal(SIGINT, SIG_DFL);
#endif
                }
                else
                    printf("Failed to register event listener.\n");
                IEventSource_UnregisterListener(es, (IEventListener *)consoleListener);
#ifdef WIN32
                if (consoleListener->pUnkMarshaler)
                    IUnknown_Release(consoleListener->pUnkMarshaler);
#endif
                IEventListenerDemo_Release(consoleListener);
            }
            else
                printf("Failed while allocating memory for console event listener.\n");
            g_pVBoxFuncs->pfnSafeArrayDestroy(interestingEventsSA);
            IEventSource_Release(es);
        }
        else
            printf("Failed to get the event source instance.\n");
        IConsole_Release(console);
    }
}

#else /* !USE_ACTIVE_EVENT_LISTENER */

/**
 * Register passive event listener for the selected VM.
 *
 * @param   virtualBox ptr to IVirtualBox object
 * @param   session    ptr to ISession object
 */
static void registerPassiveEventListener(ISession *session)
{
    IConsole *console = NULL;
    HRESULT hrc;

    hrc = ISession_get_Console(session, &console);
    if (SUCCEEDED(hrc) && console)
    {
        IEventSource *es = NULL;
        hrc = IConsole_get_EventSource(console, &es);
        if (SUCCEEDED(hrc) && es)
        {
            static const ULONG s_auInterestingEvents[] =
            {
                VBoxEventType_OnMousePointerShapeChanged,
                VBoxEventType_OnMouseCapabilityChanged,
                VBoxEventType_OnKeyboardLedsChanged,
                VBoxEventType_OnStateChanged,
                VBoxEventType_OnAdditionsStateChanged,
                VBoxEventType_OnNetworkAdapterChanged,
                VBoxEventType_OnSerialPortChanged,
                VBoxEventType_OnParallelPortChanged,
                VBoxEventType_OnStorageControllerChanged,
                VBoxEventType_OnMediumChanged,
                VBoxEventType_OnVRDEServerChanged,
                VBoxEventType_OnUSBControllerChanged,
                VBoxEventType_OnUSBDeviceStateChanged,
                VBoxEventType_OnSharedFolderChanged,
                VBoxEventType_OnRuntimeError,
                VBoxEventType_OnCanShowWindow,
                VBoxEventType_OnShowWindow
            };
            SAFEARRAY *interestingEventsSA = NULL;
            IEventListener *consoleListener = NULL;

            /* The VirtualBox API expects enum values as VT_I4, which in the
             * future can be hopefully relaxed. */
            interestingEventsSA = g_pVBoxFuncs->pfnSafeArrayCreateVector(VT_I4, 0,
                                                                           sizeof(s_auInterestingEvents)
                                                                         / sizeof(s_auInterestingEvents[0]));
            g_pVBoxFuncs->pfnSafeArrayCopyInParamHelper(interestingEventsSA, &s_auInterestingEvents,
                                                        sizeof(s_auInterestingEvents));

            hrc = IEventSource_CreateListener(es, &consoleListener);
            if (SUCCEEDED(hrc) && consoleListener)
            {
                hrc = IEventSource_RegisterListener(es, consoleListener,
                                                    ComSafeArrayAsInParam(interestingEventsSA),
                                                    0 /* passive */);
                if (SUCCEEDED(hrc))
                {
                    /* Just wait here for events, no easy way to do this better
                     * as there's not much to do after this completes. */
                    printf("Entering event loop, PowerOff the machine to exit or press Ctrl-C to terminate\n");
                    fflush(stdout);
#ifdef WIN32
                    SetConsoleCtrlHandler(ctrlCHandler, TRUE);
#else
                    signal(SIGINT, ctrlCHandler);
#endif

                    while (!g_fStop)
                    {
                        IEvent *ev = NULL;
                        hrc = IEventSource_GetEvent(es, consoleListener, 250, &ev);
                        if (FAILED(hrc))
                        {
                            printf("Failed getting event: %#x\n", (unsigned)hrc);
                            g_fStop = 1;
                            continue;
                        }
                        /* handle timeouts, resulting in NULL events */
                        if (!ev)
                            continue;
                        hrc = EventListenerDemoProcessEvent(ev);
                        if (FAILED(hrc))
                        {
                            printf("Failed processing event: %#x\n", (unsigned)hrc);
                            g_fStop = 1;
                            /* finish processing the event */
                        }
                        hrc = IEventSource_EventProcessed(es, consoleListener, ev);
                        if (FAILED(hrc))
                        {
                            printf("Failed to mark event as processed: %#x\n", (unsigned)hrc);
                            g_fStop = 1;
                            /* continue with event release */
                        }
                        if (ev)
                        {
                            IEvent_Release(ev);
                            ev = NULL;
                        }
                    }

#ifdef WIN32
                    SetConsoleCtrlHandler(ctrlCHandler, FALSE);
#else
                    signal(SIGINT, SIG_DFL);
#endif
                }
                else
                    printf("Failed to register event listener.\n");
                IEventSource_UnregisterListener(es, (IEventListener *)consoleListener);
                IEventListener_Release(consoleListener);
            }
            else
                printf("Failed to create an event listener instance.\n");
            g_pVBoxFuncs->pfnSafeArrayDestroy(interestingEventsSA);
            IEventSource_Release(es);
        }
        else
            printf("Failed to get the event source instance.\n");
        IConsole_Release(console);
    }
}

#endif /* !USE_ACTIVE_EVENT_LISTENER */

/**
 * Print detailed error information if available.
 * @param   pszExecutable   string with the executable name
 * @param   pszErrorMsg     string containing the code location specific error message
 * @param   hrc             COM/XPCOM result code
 */
static void PrintErrorInfo(const char *pszExecutable, const char *pszErrorMsg, HRESULT hrc)
{
    IErrorInfo *ex;
    HRESULT hrc2;
    fprintf(stderr, "%s: %s (hrc=%#010x)\n", pszExecutable, pszErrorMsg, (unsigned)hrc);
    hrc2 = g_pVBoxFuncs->pfnGetException(&ex);
    if (SUCCEEDED(hrc2) && ex)
    {
        IVirtualBoxErrorInfo *ei;
        hrc2 = IErrorInfo_QueryInterface(ex, &IID_IVirtualBoxErrorInfo, (void **)&ei);
        if (SUCCEEDED(hrc2) && ei != NULL)
        {
            /* got extended error info, maybe multiple infos */
            do
            {
                LONG resultCode = S_OK;
                BSTR componentUtf16 = NULL;
                char *component = NULL;
                BSTR textUtf16 = NULL;
                char *text = NULL;
                IVirtualBoxErrorInfo *ei_next = NULL;
                fprintf(stderr, "Extended error info (IVirtualBoxErrorInfo):\n");

                IVirtualBoxErrorInfo_get_ResultCode(ei, &resultCode);
                fprintf(stderr, "  resultCode=%#010x\n", (unsigned)resultCode);

                IVirtualBoxErrorInfo_get_Component(ei, &componentUtf16);
                g_pVBoxFuncs->pfnUtf16ToUtf8(componentUtf16, &component);
                g_pVBoxFuncs->pfnComUnallocString(componentUtf16);
                fprintf(stderr, "  component=%s\n", component);
                g_pVBoxFuncs->pfnUtf8Free(component);

                IVirtualBoxErrorInfo_get_Text(ei, &textUtf16);
                g_pVBoxFuncs->pfnUtf16ToUtf8(textUtf16, &text);
                g_pVBoxFuncs->pfnComUnallocString(textUtf16);
                fprintf(stderr, "  text=%s\n", text);
                g_pVBoxFuncs->pfnUtf8Free(text);

                hrc2 = IVirtualBoxErrorInfo_get_Next(ei, &ei_next);
                if (FAILED(hrc2))
                    ei_next = NULL;
                IVirtualBoxErrorInfo_Release(ei);
                ei = ei_next;
            } while (ei);
        }

        IErrorInfo_Release(ex);
        g_pVBoxFuncs->pfnClearException();
    }
}

/**
 * Start a VM.
 *
 * @param   argv0       executable name
 * @param   virtualBox  ptr to IVirtualBox object
 * @param   session     ptr to ISession object
 * @param   id          identifies the machine to start
 */
static void startVM(const char *argv0, IVirtualBox *virtualBox, ISession *session, BSTR id)
{
    HRESULT hrc;
    IMachine  *machine    = NULL;
    IProgress *progress   = NULL;
    SAFEARRAY *env        = NULL;
    BSTR sessionType;
    SAFEARRAY *groupsSA = g_pVBoxFuncs->pfnSafeArrayOutParamAlloc();

    hrc = IVirtualBox_FindMachine(virtualBox, id, &machine);
    if (FAILED(hrc) || !machine)
    {
        PrintErrorInfo(argv0, "Error: Couldn't get the Machine reference", hrc);
        return;
    }

    hrc = IMachine_get_Groups(machine, ComSafeArrayAsOutTypeParam(groupsSA, BSTR));
    if (SUCCEEDED(hrc))
    {
        BSTR *groups = NULL;
        ULONG cbGroups = 0;
        ULONG i, cGroups;
        g_pVBoxFuncs->pfnSafeArrayCopyOutParamHelper((void **)&groups, &cbGroups, VT_BSTR, groupsSA);
        g_pVBoxFuncs->pfnSafeArrayDestroy(groupsSA);
        cGroups = cbGroups / sizeof(groups[0]);
        for (i = 0; i < cGroups; ++i)
        {
            /* Note that the use of %S might be tempting, but it is not
             * available on all platforms, and even where it is usable it
             * may depend on correct compiler options to make wchar_t a
             * 16 bit number. So better play safe and use UTF-8. */
            char *group;
            g_pVBoxFuncs->pfnUtf16ToUtf8(groups[i], &group);
            printf("Groups[%u]: %s\n", (unsigned)i, group);
            g_pVBoxFuncs->pfnUtf8Free(group);
        }
        for (i = 0; i < cGroups; ++i)
            g_pVBoxFuncs->pfnComUnallocString(groups[i]);
        g_pVBoxFuncs->pfnArrayOutFree(groups);
    }

    g_pVBoxFuncs->pfnUtf8ToUtf16("gui", &sessionType);
    hrc = IMachine_LaunchVMProcess(machine, session, sessionType, ComSafeArrayAsInParam(env), &progress);
    g_pVBoxFuncs->pfnUtf16Free(sessionType);
    if (SUCCEEDED(hrc))
    {
        BOOL completed;
        LONG resultCode;

        printf("Waiting for the remote session to open...\n");
        IProgress_WaitForCompletion(progress, -1);

        hrc = IProgress_get_Completed(progress, &completed);
        if (FAILED(hrc))
            fprintf(stderr, "Error: GetCompleted status failed\n");

        IProgress_get_ResultCode(progress, &resultCode);
        if (FAILED(resultCode))
        {
            IVirtualBoxErrorInfo *errorInfo;
            BSTR textUtf16;
            char *text;

            IProgress_get_ErrorInfo(progress, &errorInfo);
            IVirtualBoxErrorInfo_get_Text(errorInfo, &textUtf16);
            g_pVBoxFuncs->pfnUtf16ToUtf8(textUtf16, &text);
            printf("Error: %s\n", text);

            g_pVBoxFuncs->pfnComUnallocString(textUtf16);
            g_pVBoxFuncs->pfnUtf8Free(text);
            IVirtualBoxErrorInfo_Release(errorInfo);
        }
        else
        {
            fprintf(stderr, "VM process has been successfully started\n");

            /* Kick off the event listener demo part, which is quite separate.
             * Ignore it if you need a more basic sample. */
#ifdef USE_ACTIVE_EVENT_LISTENER
            registerActiveEventListener(virtualBox, session);
#else
            registerPassiveEventListener(session);
#endif
        }
        IProgress_Release(progress);
    }
    else
        PrintErrorInfo(argv0, "Error: LaunchVMProcess failed", hrc);

    /* It's important to always release resources. */
    IMachine_Release(machine);
}

/**
 * List the registered VMs.
 *
 * @param   argv0       executable name
 * @param   virtualBox  ptr to IVirtualBox object
 * @param   session     ptr to ISession object
 */
static void listVMs(const char *argv0, IVirtualBox *virtualBox, ISession *session)
{
    HRESULT hrc;
    SAFEARRAY *machinesSA = g_pVBoxFuncs->pfnSafeArrayOutParamAlloc();
    IMachine **machines = NULL;
    ULONG machineCnt = 0;
    ULONG i;
    unsigned start_id;

    /*
     * Get the list of all registered VMs.
     */
    hrc = IVirtualBox_get_Machines(virtualBox, ComSafeArrayAsOutIfaceParam(machinesSA, IMachine *));
    if (FAILED(hrc))
    {
        PrintErrorInfo(argv0, "could not get list of machines", hrc);
        return;
    }

    /*
     * Extract interface pointers from machinesSA, and update the reference
     * counter of each object, as destroying machinesSA would call Release.
     */
    g_pVBoxFuncs->pfnSafeArrayCopyOutIfaceParamHelper((IUnknown ***)&machines, &machineCnt, machinesSA);
    g_pVBoxFuncs->pfnSafeArrayDestroy(machinesSA);

    if (!machineCnt)
    {
        g_pVBoxFuncs->pfnArrayOutFree(machines);
        printf("\tNo VMs\n");
        return;
    }

    printf("VM List:\n\n");

    /*
     * Iterate through the collection.
     */
    for (i = 0; i < machineCnt; ++i)
    {
        IMachine *machine      = machines[i];
        BOOL      isAccessible = FALSE;

        printf("\tMachine #%u\n", (unsigned)i);

        if (!machine)
        {
            printf("\t(skipped, NULL)\n");
            continue;
        }

        IMachine_get_Accessible(machine, &isAccessible);

        if (isAccessible)
        {
            BSTR machineNameUtf16;
            char *machineName;

            IMachine_get_Name(machine, &machineNameUtf16);
            g_pVBoxFuncs->pfnUtf16ToUtf8(machineNameUtf16,&machineName);
            g_pVBoxFuncs->pfnComUnallocString(machineNameUtf16);
            printf("\tName:        %s\n", machineName);
            g_pVBoxFuncs->pfnUtf8Free(machineName);
        }
        else
            printf("\tName:        <inaccessible>\n");

        {
            BSTR uuidUtf16;
            char      *uuidUtf8;

            IMachine_get_Id(machine, &uuidUtf16);
            g_pVBoxFuncs->pfnUtf16ToUtf8(uuidUtf16, &uuidUtf8);
            g_pVBoxFuncs->pfnComUnallocString(uuidUtf16);
            printf("\tUUID:        %s\n", uuidUtf8);
            g_pVBoxFuncs->pfnUtf8Free(uuidUtf8);
        }

        if (isAccessible)
        {
            {
                BSTR      configFileUtf16;
                char      *configFileUtf8;

                IMachine_get_SettingsFilePath(machine, &configFileUtf16);
                g_pVBoxFuncs->pfnUtf16ToUtf8(configFileUtf16, &configFileUtf8);
                g_pVBoxFuncs->pfnComUnallocString(configFileUtf16);
                printf("\tConfig file: %s\n", configFileUtf8);
                g_pVBoxFuncs->pfnUtf8Free(configFileUtf8);
            }

            {
                ULONG memorySize;

                IMachine_get_MemorySize(machine, &memorySize);
                printf("\tMemory size: %uMB\n", (unsigned)memorySize);
            }

            {
                BSTR typeId;
                BSTR osNameUtf16;
                char *osName;
                IGuestOSType *osType = NULL;

                IMachine_get_OSTypeId(machine, &typeId);
                IVirtualBox_GetGuestOSType(virtualBox, typeId, &osType);
                g_pVBoxFuncs->pfnComUnallocString(typeId);
                IGuestOSType_get_Description(osType, &osNameUtf16);
                g_pVBoxFuncs->pfnUtf16ToUtf8(osNameUtf16,&osName);
                g_pVBoxFuncs->pfnComUnallocString(osNameUtf16);
                printf("\tGuest OS:    %s\n\n", osName);
                g_pVBoxFuncs->pfnUtf8Free(osName);

                IGuestOSType_Release(osType);
            }
        }
    }

    /*
     * Let the user chose a machine to start.
     */
    printf("Type Machine# to start (0 - %u) or 'quit' to do nothing: ",
           (unsigned)(machineCnt - 1));
    fflush(stdout);

    if (scanf("%u", &start_id) == 1 && start_id < machineCnt)
    {
        IMachine *machine = machines[start_id];

        if (machine)
        {
            BSTR uuidUtf16 = NULL;

            IMachine_get_Id(machine, &uuidUtf16);
            startVM(argv0, virtualBox, session, uuidUtf16);
            g_pVBoxFuncs->pfnComUnallocString(uuidUtf16);
        }
    }

    /*
     * Don't forget to release the objects in the array.
     */
    for (i = 0; i < machineCnt; ++i)
    {
        IMachine *machine = machines[i];

        if (machine)
            IMachine_Release(machine);
    }
    g_pVBoxFuncs->pfnArrayOutFree(machines);
}

/* Main - Start the ball rolling. */

int main(int argc, char **argv)
{
    IVirtualBoxClient *vboxclient = NULL;
    IVirtualBox *vbox            = NULL;
    ISession   *session          = NULL;
    ULONG       revision         = 0;
    BSTR        versionUtf16     = NULL;
    BSTR        homefolderUtf16  = NULL;
    HRESULT     hrc;     /* Result code of various function (method) calls. */
    (void)argc;

    printf("Starting main()\n");

    if (VBoxCGlueInit())
    {
        fprintf(stderr, "%s: FATAL: VBoxCGlueInit failed: %s\n",
                argv[0], g_szVBoxErrMsg);
        return EXIT_FAILURE;
    }

    {
        unsigned ver = g_pVBoxFuncs->pfnGetVersion();
        printf("VirtualBox version: %u.%u.%u\n", ver / 1000000, ver / 1000 % 1000, ver % 1000);
        ver = g_pVBoxFuncs->pfnGetAPIVersion();
        printf("VirtualBox API version: %u.%u\n", ver / 1000, ver % 1000);
    }

    g_pVBoxFuncs->pfnClientInitialize(NULL, &vboxclient);
    if (!vboxclient)
    {
        fprintf(stderr, "%s: FATAL: could not get VirtualBoxClient reference\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("----------------------------------------------------\n");

    hrc = IVirtualBoxClient_get_VirtualBox(vboxclient, &vbox);
    if (FAILED(hrc) || !vbox)
    {
        PrintErrorInfo(argv[0], "FATAL: could not get VirtualBox reference", hrc);
        return EXIT_FAILURE;
    }
    hrc = IVirtualBoxClient_get_Session(vboxclient, &session);
    if (FAILED(hrc) || !session)
    {
        PrintErrorInfo(argv[0], "FATAL: could not get Session reference", hrc);
        return EXIT_FAILURE;
    }

#ifdef USE_ACTIVE_EVENT_LISTENER
# ifdef WIN32
    hrc = LoadTypeInfo(&IID_IEventListener, &g_pTInfoIEventListener);
    if (FAILED(hrc) || !g_pTInfoIEventListener)
    {
        PrintErrorInfo(argv[0], "FATAL: could not get type information for IEventListener", hrc);
        return EXIT_FAILURE;
    }
# endif /* WIN32 */
#endif /* USE_ACTIVE_EVENT_LISTENER */

    /*
     * Now ask for revision, version and home folder information of
     * this vbox. Were not using fancy macros here so it
     * remains easy to see how we access C++'s vtable.
     */

    /* 1. Revision */
    hrc = IVirtualBox_get_Revision(vbox, &revision);
    if (SUCCEEDED(hrc))
        printf("\tRevision: %u\n", (unsigned)revision);
    else
        PrintErrorInfo(argv[0], "GetRevision() failed", hrc);

    /* 2. Version */
    hrc = IVirtualBox_get_Version(vbox, &versionUtf16);
    if (SUCCEEDED(hrc))
    {
        char *version = NULL;
        g_pVBoxFuncs->pfnUtf16ToUtf8(versionUtf16, &version);
        printf("\tVersion: %s\n", version);
        g_pVBoxFuncs->pfnUtf8Free(version);
        g_pVBoxFuncs->pfnComUnallocString(versionUtf16);
    }
    else
        PrintErrorInfo(argv[0], "GetVersion() failed", hrc);

    /* 3. Home Folder */
    hrc = IVirtualBox_get_HomeFolder(vbox, &homefolderUtf16);
    if (SUCCEEDED(hrc))
    {
        char *homefolder = NULL;
        g_pVBoxFuncs->pfnUtf16ToUtf8(homefolderUtf16, &homefolder);
        printf("\tHomeFolder: %s\n", homefolder);
        g_pVBoxFuncs->pfnUtf8Free(homefolder);
        g_pVBoxFuncs->pfnComUnallocString(homefolderUtf16);
    }
    else
        PrintErrorInfo(argv[0], "GetHomeFolder() failed", hrc);

    listVMs(argv[0], vbox, session);
    ISession_UnlockMachine(session);

    printf("----------------------------------------------------\n");

    /*
     * Do as mom told us: always clean up after yourself.
     */
#ifdef USE_ACTIVE_EVENT_LISTENER
# ifdef WIN32
    if (g_pTInfoIEventListener)
    {
        ITypeInfo_Release(g_pTInfoIEventListener);
        g_pTInfoIEventListener = NULL;
    }
# endif /* WIN32 */
#endif /* USE_ACTIVE_EVENT_LISTENER */

    if (session)
    {
        ISession_Release(session);
        session = NULL;
    }
    if (vbox)
    {
        IVirtualBox_Release(vbox);
        vbox = NULL;
    }
    if (vboxclient)
    {
        IVirtualBoxClient_Release(vboxclient);
        vboxclient = NULL;
    }

    g_pVBoxFuncs->pfnClientUninitialize();
    VBoxCGlueTerm();
    printf("Finished main()\n");

    return 0;
}
/* vim: set ts=4 sw=4 et: */
