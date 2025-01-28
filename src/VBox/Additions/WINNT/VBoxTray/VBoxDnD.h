/* $Id: VBoxDnD.h 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxDnD.h - Windows-specific bits of the drag'n drop service.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxDnD_h
#define GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxDnD_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/critsect.h>

#include <iprt/cpp/mtlist.h>
#include <iprt/cpp/ministring.h>

class VBoxDnDWnd;

/**
 * Class for implementing IDataObject for VBoxTray's DnD support.
 */
class VBoxDnDDataObject : public IDataObject
{
public:

    enum Status
    {
        Status_Uninitialized = 0,
        Status_Initialized,
        Status_Dropping,
        Status_Dropped,
        Status_Aborted,
        Status_32Bit_Hack = 0x7fffffff
    };

public:

    VBoxDnDDataObject(LPFORMATETC pFormatEtc = NULL, LPSTGMEDIUM pStgMed = NULL, ULONG cFormats = 0);
    virtual ~VBoxDnDDataObject(void);

public: /* IUnknown methods. */

    STDMETHOD(QueryInterface)(REFIID iid, void ** ppvObject);
    STDMETHOD_(ULONG, AddRef)(void);
    STDMETHOD_(ULONG, Release)(void);

public: /* IDataObject methods. */

    STDMETHOD(GetData)(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium);
    STDMETHOD(GetDataHere)(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium);
    STDMETHOD(QueryGetData)(LPFORMATETC pFormatEtc);
    STDMETHOD(GetCanonicalFormatEtc)(LPFORMATETC pFormatEct,  LPFORMATETC pFormatEtcOut);
    STDMETHOD(SetData)(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium, BOOL fRelease);
    STDMETHOD(EnumFormatEtc)(DWORD dwDirection, IEnumFORMATETC **ppEnumFormatEtc);
    STDMETHOD(DAdvise)(LPFORMATETC pFormatEtc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection);
    STDMETHOD(DUnadvise)(DWORD dwConnection);
    STDMETHOD(EnumDAdvise)(IEnumSTATDATA **ppEnumAdvise);

public:

    static const char* ClipboardFormatToString(CLIPFORMAT fmt);

    int Init(LPFORMATETC pFormatEtc, LPSTGMEDIUM pStgMed, ULONG cFormats);
    int Destroy(void);
    int Abort(void);
    void SetStatus(Status status);
    int Signal(const RTCString &strFormat, const void *pvData, size_t cbData);

protected:

    bool LookupFormatEtc(LPFORMATETC pFormatEtc, ULONG *puIndex);
    void RegisterFormat(LPFORMATETC pFormatEtc, CLIPFORMAT clipFormat, TYMED tyMed = TYMED_HGLOBAL,
                        LONG lindex = -1, DWORD dwAspect = DVASPECT_CONTENT, DVTARGETDEVICE *pTargetDevice = NULL);

    /** Current drag and drop status. */
    Status      m_enmStatus;
    /** Internal reference count of this object. */
    LONG        m_cRefs;
    /** Number of native formats registered. This can be a different number than supplied with m_lstFormats. */
    ULONG       m_cFormats;
    /** Array of registered FORMATETC structs. Matches m_cFormats. */
    LPFORMATETC m_paFormatEtc;
    /** Array of registered STGMEDIUM structs. Matches m_cFormats. */
    LPSTGMEDIUM m_paStgMedium;
    /** Event semaphore used for waiting on status changes. */
    RTSEMEVENT  m_EvtDropped;
    /** Format of currently retrieved data. */
    RTCString   m_strFormat;
    /** The retrieved data as a raw buffer. */
    void       *m_pvData;
    /** Raw buffer size (in bytes). */
    size_t      m_cbData;
};

/**
 * Class for implementing IDropSource for VBoxTray's DnD support.
 */
class VBoxDnDDropSource : public IDropSource
{
public:

    VBoxDnDDropSource(VBoxDnDWnd *pThis);
    virtual ~VBoxDnDDropSource(void);

public:

    VBOXDNDACTION GetCurrentAction(void) { return m_enmActionCurrent; }

public: /* IUnknown methods. */

    STDMETHOD(QueryInterface)(REFIID iid, void ** ppvObject);
    STDMETHOD_(ULONG, AddRef)(void);
    STDMETHOD_(ULONG, Release)(void);

public: /* IDropSource methods. */

    STDMETHOD(QueryContinueDrag)(BOOL fEscapePressed, DWORD dwKeyState);
    STDMETHOD(GiveFeedback)(DWORD dwEffect);

protected:

    /** Reference count of this object. */
    LONG                  m_cRefs;
    /** Pointer to parent proxy window. */
    VBoxDnDWnd           *m_pWndParent;
    /** Current drag effect. */
    DWORD                 m_dwCurEffect;
    /** Current action to perform on the host. */
    VBOXDNDACTION         m_enmActionCurrent;
};

/**
 * Class for implementing IDropTarget for VBoxTray's DnD support.
 */
class VBoxDnDDropTarget : public IDropTarget
{
public:

    VBoxDnDDropTarget(VBoxDnDWnd *pThis);
    virtual ~VBoxDnDDropTarget(void);

public: /* IUnknown methods. */

    STDMETHOD(QueryInterface)(REFIID iid, void ** ppvObject);
    STDMETHOD_(ULONG, AddRef)(void);
    STDMETHOD_(ULONG, Release)(void);

public: /* IDropTarget methods. */

    STDMETHOD(DragEnter)(IDataObject *pDataObject, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
    STDMETHOD(DragLeave)(void);
    STDMETHOD(Drop)(IDataObject *pDataObject, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);

protected:

    static void DumpFormats(IDataObject *pDataObject);
    static DWORD GetDropEffect(DWORD grfKeyState, DWORD dwAllowedEffects);
    void reset(void);

public:

    /** Returns the data as mutable raw. Use with caution! */
    void *DataMutableRaw(void) const { return m_pvData; }

    /** Returns the data size (in bytes). */
    size_t DataSize(void) const { return m_cbData; }

    RTCString Formats(void) const;
    int WaitForDrop(RTMSINTERVAL msTimeout);

protected:

    /** Reference count of this object. */
    LONG                  m_cRefs;
    /** Pointer to parent proxy window. */
    VBoxDnDWnd           *m_pWndParent;
    /** Current drop effect. */
    DWORD                 m_dwCurEffect;
    /** Copy of the data object's current FORMATETC struct.
     *  Note: We don't keep the pointer of the DVTARGETDEVICE here! */
    FORMATETC             m_FormatEtc;
    /** Stringified data object's format currently in use.  */
    RTCString             m_strFormat;
    /** Pointer to actual format data. */
    void                 *m_pvData;
    /** Size (in bytes) of format data. */
    size_t                m_cbData;
    /** Event for waiting on the "drop" event. */
    RTSEMEVENT            m_EvtDrop;
    /** Result of the drop event. */
    int                   m_rcDropped;
};

/**
 * Class for implementing IEnumFORMATETC for VBoxTray's DnD support.
 */
class VBoxDnDEnumFormatEtc : public IEnumFORMATETC
{
public:

    VBoxDnDEnumFormatEtc(LPFORMATETC pFormatEtc, ULONG uIdx, ULONG cToCopy, ULONG cTotal);
    virtual ~VBoxDnDEnumFormatEtc(void);

public:

    STDMETHOD(QueryInterface)(REFIID iid, void ** ppvObject);
    STDMETHOD_(ULONG, AddRef)(void);
    STDMETHOD_(ULONG, Release)(void);

    STDMETHOD(Next)(ULONG cFormats, LPFORMATETC pFormatEtc, ULONG *pcFetched);
    STDMETHOD(Skip)(ULONG cFormats);
    STDMETHOD(Reset)(void);
    STDMETHOD(Clone)(IEnumFORMATETC **ppEnumFormatEtc);

public:

    int Init(LPFORMATETC pFormatEtc, ULONG uIdx, ULONG cToCopy, ULONG cTotal);

public:

    static int     CopyFormat(LPFORMATETC pFormatDest, LPFORMATETC pFormatSource);
    static HRESULT CreateEnumFormatEtc(UINT cFormats, LPFORMATETC pFormatEtc, IEnumFORMATETC **ppEnumFormatEtc);

private:

    /** Reference count of this object. */
    LONG        m_cRefs;
    /** Current index for format iteration. */
    ULONG       m_uIdxCur;
    /** Number of format this object contains. */
    ULONG       m_cFormats;
    /** Array of FORMATETC formats this object contains. Matches m_cFormats. */
    LPFORMATETC m_paFormatEtc;
};

struct VBOXDNDCONTEXT;
class VBoxDnDWnd;

/**
 * A drag'n drop event from the host.
 */
typedef struct VBOXDNDEVENT
{
    /** The actual DnD HGCM event data. */
    PVBGLR3DNDEVENT pVbglR3Event;

} VBOXDNDEVENT, *PVBOXDNDEVENT;

/**
 * DnD context data.
 */
typedef struct VBOXDNDCONTEXT
{
    /** Pointer to the service environment. */
    const VBOXTRAYSVCENV      *pEnv;
    /** Started indicator. */
    bool                       fStarted;
    /** Shutdown indicator. */
    bool                       fShutdown;
    /** The registered window class. */
    ATOM                       wndClass;
    /** The DnD main event queue. */
    RTCMTList<VBOXDNDEVENT>    lstEvtQueue;
    /** Semaphore for waiting on main event queue
     *  events. */
    RTSEMEVENT                 hEvtQueueSem;
    /** List of drag'n drop proxy windows.
     *  Note: At the moment only one window is supported. */
    RTCMTList<VBoxDnDWnd*>     lstWnd;
    /** The DnD command context. */
    VBGLR3GUESTDNDCMDCTX       cmdCtx;

} VBOXDNDCONTEXT, *PVBOXDNDCONTEXT;

/**
 * Everything which is required to successfully start
 * a drag'n drop operation via DoDragDrop().
 */
typedef struct VBOXDNDSTARTUPINFO
{
    /** Our DnD data object, holding
     *  the raw DnD data. */
    VBoxDnDDataObject         *pDataObject;
    /** The drop source for sending the
     *  DnD request to a IDropTarget. */
    VBoxDnDDropSource         *pDropSource;
    /** The DnD effects which are wanted / allowed. */
    DWORD                      dwOKEffects;

} VBOXDNDSTARTUPINFO, *PVBOXDNDSTARTUPINFO;

/**
 * Class for handling a DnD proxy window.
 ** @todo Unify this and VBoxClient's DragInstance!
 */
class VBoxDnDWnd
{
    /**
     * Current state of a DnD proxy
     * window.
     */
    enum State
    {
        Uninitialized = 0,
        Initialized,
        Dragging,
        Dropped,
        Canceled
    };

    /**
     * Current operation mode of
     * a DnD proxy window.
     */
    enum Mode
    {
        /** Unknown mode. */
        Unknown = 0,
        /** Host to guest. */
        HG,
        /** Guest to host. */
        GH
    };

public:

    VBoxDnDWnd(void);
    virtual ~VBoxDnDWnd(void);

public:

    int Initialize(PVBOXDNDCONTEXT a_pCtx);
    void Destroy(void);

public:

    /** The window's thread for the native message pump and OLE context. */
    static DECLCALLBACK(int) Thread(RTTHREAD hThread, void *pvUser);

public:

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM lParam);
    /** The per-instance wndproc routine. */
    LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

public:

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
    int RegisterAsDropTarget(void);
    int UnregisterAsDropTarget(void);
#endif

public:

    int OnCreate(void);
    void OnDestroy(void);

    int Abort(void);

    /* Host -> Guest */
    int OnHgEnter(const RTCList<RTCString> &formats, VBOXDNDACTIONLIST m_lstActionsAllowed);
    int OnHgMove(uint32_t u32xPos, uint32_t u32yPos, VBOXDNDACTION dndAction);
    int OnHgDrop(void);
    int OnHgLeave(void);
    int OnHgDataReceive(PVBGLR3GUESTDNDMETADATA pMeta);
    int OnHgCancel(void);

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
    /* Guest -> Host */
    int OnGhIsDnDPending(void);
    int OnGhDrop(const RTCString &strFormat, VBOXDNDACTION dndActionDefault);
#endif

    void PostMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    int ProcessEvent(PVBOXDNDEVENT pEvent);

    int Hide(void);
    void Reset(void);

protected:

    int checkForSessionChange(void);
    int makeFullscreen(void);
    int mouseMove(int x, int y, DWORD dwMouseInputFlags);
    int mouseRelease(void);
    int setMode(Mode enmMode);

public: /** @todo Make protected! */

    /** Pointer to DnD context. */
    PVBOXDNDCONTEXT            m_pCtx;
    /** The proxy window's main thread for processing
     *  window messages. */
    RTTHREAD                   m_hThread;
    /** Critical section to serialize access. */
    RTCRITSECT                 m_CritSect;
    /** Event semaphore to wait for new DnD events. */
    RTSEMEVENT                 m_EvtSem;
#ifdef RT_OS_WINDOWS
    /** The window's handle. */
    HWND                       m_hWnd;
    /** List of allowed MIME types this
     *  client can handle. Make this a per-instance
     *  property so that we can selectively allow/forbid
     *  certain types later on runtime. */
    RTCList<RTCString>         m_lstFmtSup;
    /** List of formats for the current
     *  drag'n drop operation. */
    RTCList<RTCString>         m_lstFmtActive;
    /** List of all current drag'n drop actions allowed. */
    VBOXDNDACTIONLIST          m_lstActionsAllowed;
    /** The startup information required
     *  for the actual DoDragDrop() call. */
    VBOXDNDSTARTUPINFO         m_startupInfo;
    /** Is the left mouse button being pressed
     *  currently while being in this window? */
    bool                       m_fMouseButtonDown;
# ifdef VBOX_WITH_DRAG_AND_DROP_GH
    /** Pointer to IDropTarget implementation for
     *  guest -> host support. */
    VBoxDnDDropTarget         *m_pDropTarget;
# endif /* VBOX_WITH_DRAG_AND_DROP_GH */
#else /* !RT_OS_WINDOWS */
    /** @todo Implement me. */
#endif /* !RT_OS_WINDOWS */

    /** The window's own DnD context. */
    VBGLR3GUESTDNDCMDCTX       m_cmdCtx;
    /** The current operation mode. */
    Mode                       m_enmMode;
    /** The current state. */
    State                      m_enmState;
    /** Format being requested. */
    RTCString                  m_strFmtReq;
};

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxDnD_h */

