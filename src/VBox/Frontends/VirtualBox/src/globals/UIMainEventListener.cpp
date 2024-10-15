/* $Id: UIMainEventListener.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMainEventListener class implementation.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <QMutex>
#include <QThread>

/* GUI includes: */
#include "UIGlobalSession.h"
#include "UILoggingDefs.h"
#include "UIMainEventListener.h"
#include "UIMousePointerShapeData.h"

/* COM includes: */
#include "CCanShowWindowEvent.h"
#include "CClipboardErrorEvent.h"
#include "CClipboardModeChangedEvent.h"
#include "CCloudProfileChangedEvent.h"
#include "CCloudProfileRegisteredEvent.h"
#include "CCloudProviderListChangedEvent.h"
#include "CCloudProviderUninstallEvent.h"
#include "CCursorPositionChangedEvent.h"
#include "CDnDModeChangedEvent.h"
#include "CEvent.h"
#include "CEventSource.h"
#include "CEventListener.h"
#include "CExtraDataCanChangeEvent.h"
#include "CExtraDataChangedEvent.h"
#include "CExtPackInstalledEvent.h"
#include "CExtPackUninstalledEvent.h"
#include "CGuestMonitorChangedEvent.h"
#include "CGuestProcessIOEvent.h"
#include "CGuestProcessRegisteredEvent.h"
#include "CGuestProcessStateChangedEvent.h"
#include "CGuestSessionRegisteredEvent.h"
#include "CGuestSessionStateChangedEvent.h"
#include "CKeyboardLedsChangedEvent.h"
#include "CMachineDataChangedEvent.h"
#include "CMachineStateChangedEvent.h"
#include "CMachineRegisteredEvent.h"
#include "CMachineGroupsChangedEvent.h"
#include "CMediumChangedEvent.h"
#include "CMediumConfigChangedEvent.h"
#include "CMediumRegisteredEvent.h"
#include "CMouseCapabilityChangedEvent.h"
#include "CMousePointerShapeChangedEvent.h"
#include "CNetworkAdapterChangedEvent.h"
#include "CProgressPercentageChangedEvent.h"
#include "CProgressTaskCompletedEvent.h"
#include "CRuntimeErrorEvent.h"
#include "CSessionStateChangedEvent.h"
#include "CShowWindowEvent.h"
#include "CSnapshotChangedEvent.h"
#include "CSnapshotDeletedEvent.h"
#include "CSnapshotRestoredEvent.h"
#include "CSnapshotTakenEvent.h"
#include "CStateChangedEvent.h"
#include "CStorageControllerChangedEvent.h"
#include "CStorageDeviceChangedEvent.h"
#include "CUSBDevice.h"
#include "CUSBDeviceStateChangedEvent.h"
#include "CVBoxSVCAvailabilityChangedEvent.h"
#include "CVirtualBoxErrorInfo.h"


/** Private QThread extension allowing to listen for Main events in separate thread.
  * This thread listens for a Main events infinitely unless creator calls for #setShutdown. */
class UIMainEventListeningThread : public QThread
{
    Q_OBJECT;

public:

    /** Constructs Main events listener thread redirecting events from @a comSource to @a comListener.
      * @param  comSource         Brings event source we are creating this thread for.
      * @param  comListener       Brings event listener we are creating this thread for.
      * @param  escapeEventTypes  Brings a set of escape event types which commands this thread to finish. */
    UIMainEventListeningThread(const CEventSource &comSource,
                               const CEventListener &comListener,
                               const QSet<KVBoxEventType> &escapeEventTypes);
    /** Destructs Main events listener thread. */
    virtual ~UIMainEventListeningThread() RT_OVERRIDE;

protected:

    /** Contains the thread excution body. */
    virtual void run() RT_OVERRIDE;

    /** Returns whether the thread asked to shutdown prematurely. */
    bool isShutdown() const;
    /** Defines whether the thread asked to @a fShutdown prematurely. */
    void setShutdown(bool fShutdown);

private:

    /** Holds the Main event source reference. */
    CEventSource          m_comSource;
    /** Holds the Main event listener reference. */
    CEventListener        m_comListener;
    /** Holds a set of event types this thread should finish job on. */
    QSet<KVBoxEventType>  m_escapeEventTypes;

    /** Holds the mutex instance which protects thread access. */
    mutable QMutex m_mutex;
    /** Holds whether the thread asked to shutdown prematurely. */
    bool m_fShutdown;
};


/*********************************************************************************************************************************
*   Class UIMainEventListeningThread implementation.                                                                             *
*********************************************************************************************************************************/

UIMainEventListeningThread::UIMainEventListeningThread(const CEventSource &comSource,
                                                       const CEventListener &comListener,
                                                       const QSet<KVBoxEventType> &escapeEventTypes)
    : m_comSource(comSource)
    , m_comListener(comListener)
    , m_escapeEventTypes(escapeEventTypes)
    , m_fShutdown(false)
{
    setObjectName("UIMainEventListeningThread");
}

UIMainEventListeningThread::~UIMainEventListeningThread()
{
    /* Make a request to shutdown: */
    setShutdown(true);

    /* And wait 30 seconds for run() to finish (1 sec increments to help with
       delays incurred debugging and prevent suicidal use-after-free behaviour): */
    uint32_t i = 30000;
    do
        wait(1000);
    while (i-- > 0 && !isFinished());
}

void UIMainEventListeningThread::run()
{
    /* Initialize COM: */
    COMBase::InitializeCOM(false);

    /* Copy source wrapper to this thread: */
    CEventSource comSource = m_comSource;
    /* Copy listener wrapper to this thread: */
    CEventListener comListener = m_comListener;

    /* While we are not in shutdown: */
    while (!isShutdown())
    {
        /* Fetch the event from the queue: */
        CEvent comEvent = comSource.GetEvent(comListener, 500);
        if (!comEvent.isNull())
        {
            /* Process the event and tell the listener: */
            comListener.HandleEvent(comEvent);
            if (comEvent.GetWaitable())
            {
                comSource.EventProcessed(comListener, comEvent);
                LogRel2(("GUI: UIMainEventListener/ThreadRun: EventProcessed set for waitable event\n"));
            }

            /* Check whether we should finish our job on this event: */
            if (m_escapeEventTypes.contains(comEvent.GetType()))
                setShutdown(true);
        }
    }

    /* Cleanup COM: */
    COMBase::CleanupCOM();
}

bool UIMainEventListeningThread::isShutdown() const
{
    m_mutex.lock();
    bool fShutdown = m_fShutdown;
    m_mutex.unlock();
    return fShutdown;
}

void UIMainEventListeningThread::setShutdown(bool fShutdown)
{
    m_mutex.lock();
    m_fShutdown = fShutdown;
    m_mutex.unlock();
}


/*********************************************************************************************************************************
*   Class UIMainEventListener implementation.                                                                                    *
*********************************************************************************************************************************/

UIMainEventListener::UIMainEventListener()
{
    /* Register meta-types for required enums. */
    qRegisterMetaType<KDeviceType>("KDeviceType");
    qRegisterMetaType<KMachineState>("KMachineState");
    qRegisterMetaType<KSessionState>("KSessionState");
    qRegisterMetaType< QVector<uint8_t> >("QVector<uint8_t>");
    qRegisterMetaType<CNetworkAdapter>("CNetworkAdapter");
    qRegisterMetaType<CMedium>("CMedium");
    qRegisterMetaType<CMediumAttachment>("CMediumAttachment");
    qRegisterMetaType<CUSBDevice>("CUSBDevice");
    qRegisterMetaType<CVirtualBoxErrorInfo>("CVirtualBoxErrorInfo");
    qRegisterMetaType<KGuestMonitorChangedEventType>("KGuestMonitorChangedEventType");
    qRegisterMetaType<CGuestSession>("CGuestSession");
}

void UIMainEventListener::registerSource(const CEventSource &comSource,
                                         const CEventListener &comListener,
                                         const QSet<KVBoxEventType> &escapeEventTypes /* = QSet<KVBoxEventType>() */)
{
    /* Make sure source and listener are valid: */
    AssertReturnVoid(!comSource.isNull());
    AssertReturnVoid(!comListener.isNull());

    /* Create thread for passed source: */
    UIMainEventListeningThread *pThread = new UIMainEventListeningThread(comSource, comListener, escapeEventTypes);
    if (pThread)
    {
        /* Listen for thread finished signal: */
        connect(pThread, &UIMainEventListeningThread::finished,
                this, &UIMainEventListener::sltHandleThreadFinished);
        /* Register & start it: */
        m_threads << pThread;
        pThread->start();
    }
}

void UIMainEventListener::unregisterSources()
{
    /* Stop listening for thread finished thread signals,
     * we are about to destroy these threads anyway: */
    foreach (UIMainEventListeningThread *pThread, m_threads)
        disconnect(pThread, &UIMainEventListeningThread::finished,
                   this, &UIMainEventListener::sltHandleThreadFinished);

    /* Wipe out the threads: */
    /** @todo r=bird: The use of qDeleteAll here is unsafe because it won't take
     * QThread::wait() timeouts into account, and may delete the QThread object
     * while the thread is still running, causing heap corruption/crashes once
     * the thread awakens and gets on with its termination.
     * Observed with debugger + paged heap.
     *
     * Should use specialized thread list which only deletes the threads after
     * isFinished() returns true, leaving them alone on timeout failures. */
    qDeleteAll(m_threads);
}

STDMETHODIMP UIMainEventListener::HandleEvent(VBoxEventType_T, IEvent *pEvent)
{
    /* Try to acquire COM cleanup protection token first: */
    if (!gpGlobalSession->comTokenTryLockForRead())
        return S_OK;

    CEvent comEvent(pEvent);
    //printf("Event received: %d\n", comEvent.GetType());
    switch (comEvent.GetType())
    {
        case KVBoxEventType_OnVBoxSVCAvailabilityChanged:
        {
            CVBoxSVCAvailabilityChangedEvent comEventSpecific(pEvent);
            emit sigVBoxSVCAvailabilityChange(comEventSpecific.GetAvailable());
            break;
        }

        case KVBoxEventType_OnMachineStateChanged:
        {
            CMachineStateChangedEvent comEventSpecific(pEvent);
            emit sigMachineStateChange(comEventSpecific.GetMachineId(), comEventSpecific.GetState());
            break;
        }
        case KVBoxEventType_OnMachineDataChanged:
        {
            CMachineDataChangedEvent comEventSpecific(pEvent);
            emit sigMachineDataChange(comEventSpecific.GetMachineId());
            break;
        }
        case KVBoxEventType_OnMachineRegistered:
        {
            CMachineRegisteredEvent comEventSpecific(pEvent);
            emit sigMachineRegistered(comEventSpecific.GetMachineId(), comEventSpecific.GetRegistered());
            break;
        }
        case KVBoxEventType_OnMachineGroupsChanged:
        {
            CMachineGroupsChangedEvent comEventSpecific(pEvent);
            emit sigMachineGroupsChange(comEventSpecific.GetMachineId());
            break;
        }
        case KVBoxEventType_OnSessionStateChanged:
        {
            CSessionStateChangedEvent comEventSpecific(pEvent);
            emit sigSessionStateChange(comEventSpecific.GetMachineId(), comEventSpecific.GetState());
            break;
        }
        case KVBoxEventType_OnSnapshotTaken:
        {
            CSnapshotTakenEvent comEventSpecific(pEvent);
            emit sigSnapshotTake(comEventSpecific.GetMachineId(), comEventSpecific.GetSnapshotId());
            break;
        }
        case KVBoxEventType_OnSnapshotDeleted:
        {
            CSnapshotDeletedEvent comEventSpecific(pEvent);
            emit sigSnapshotDelete(comEventSpecific.GetMachineId(), comEventSpecific.GetSnapshotId());
            break;
        }
        case KVBoxEventType_OnSnapshotChanged:
        {
            CSnapshotChangedEvent comEventSpecific(pEvent);
            emit sigSnapshotChange(comEventSpecific.GetMachineId(), comEventSpecific.GetSnapshotId());
            break;
        }
        case KVBoxEventType_OnSnapshotRestored:
        {
            CSnapshotRestoredEvent comEventSpecific(pEvent);
            emit sigSnapshotRestore(comEventSpecific.GetMachineId(), comEventSpecific.GetSnapshotId());
            break;
        }
        case KVBoxEventType_OnCloudProviderListChanged:
        {
            emit sigCloudProviderListChanged();
            break;
        }
        case KVBoxEventType_OnCloudProviderUninstall:
        {
            LogRel(("GUI: UIMainEventListener/HandleEvent: KVBoxEventType_OnCloudProviderUninstall event came\n"));
            CCloudProviderUninstallEvent comEventSpecific(pEvent);
            emit sigCloudProviderUninstall(comEventSpecific.GetId());
            LogRel(("GUI: UIMainEventListener/HandleEvent: KVBoxEventType_OnCloudProviderUninstall event done\n"));
            break;
        }
        case KVBoxEventType_OnCloudProfileRegistered:
        {
            CCloudProfileRegisteredEvent comEventSpecific(pEvent);
            emit sigCloudProfileRegistered(comEventSpecific.GetProviderId(), comEventSpecific.GetName(), comEventSpecific.GetRegistered());
            break;
        }
        case KVBoxEventType_OnCloudProfileChanged:
        {
            CCloudProfileChangedEvent comEventSpecific(pEvent);
            emit sigCloudProfileChanged(comEventSpecific.GetProviderId(), comEventSpecific.GetName());
            break;
        }

        case KVBoxEventType_OnExtraDataCanChange:
        {
            CExtraDataCanChangeEvent comEventSpecific(pEvent);
            /* Has to be done in place to give an answer: */
            bool fVeto = false;
            QString strReason;
            emit sigExtraDataCanChange(comEventSpecific.GetMachineId(), comEventSpecific.GetKey(),
                                       comEventSpecific.GetValue(), fVeto, strReason);
            if (fVeto)
                comEventSpecific.AddVeto(strReason);
            break;
        }
        case KVBoxEventType_OnExtraDataChanged:
        {
            CExtraDataChangedEvent comEventSpecific(pEvent);
            emit sigExtraDataChange(comEventSpecific.GetMachineId(), comEventSpecific.GetKey(), comEventSpecific.GetValue());
            break;
        }

        case KVBoxEventType_OnStorageControllerChanged:
        {
            CStorageControllerChangedEvent comEventSpecific(pEvent);
            emit sigStorageControllerChange(comEventSpecific.GetMachinId(),
                                            comEventSpecific.GetControllerName());
            break;
        }
        case KVBoxEventType_OnStorageDeviceChanged:
        {
            CStorageDeviceChangedEvent comEventSpecific(pEvent);
            emit sigStorageDeviceChange(comEventSpecific.GetStorageDevice(),
                                        comEventSpecific.GetRemoved(),
                                        comEventSpecific.GetSilent());
            break;
        }
        case KVBoxEventType_OnMediumChanged:
        {
            CMediumChangedEvent comEventSpecific(pEvent);
            emit sigMediumChange(comEventSpecific.GetMediumAttachment());
            break;
        }
        case KVBoxEventType_OnMediumConfigChanged:
        {
            CMediumConfigChangedEvent comEventSpecific(pEvent);
            emit sigMediumConfigChange(comEventSpecific.GetMedium());
            break;
        }
        case KVBoxEventType_OnMediumRegistered:
        {
            CMediumRegisteredEvent comEventSpecific(pEvent);
            emit sigMediumRegistered(comEventSpecific.GetMediumId(),
                                     comEventSpecific.GetMediumType(),
                                     comEventSpecific.GetRegistered());
            break;
        }

        case KVBoxEventType_OnMousePointerShapeChanged:
        {
            CMousePointerShapeChangedEvent comEventSpecific(pEvent);
            UIMousePointerShapeData shapeData(comEventSpecific.GetVisible(),
                                              comEventSpecific.GetAlpha(),
                                              QPoint(comEventSpecific.GetXhot(), comEventSpecific.GetYhot()),
                                              QSize(comEventSpecific.GetWidth(), comEventSpecific.GetHeight()),
                                              comEventSpecific.GetShape());
            emit sigMousePointerShapeChange(shapeData);
            break;
        }
        case KVBoxEventType_OnMouseCapabilityChanged:
        {
            CMouseCapabilityChangedEvent comEventSpecific(pEvent);
            emit sigMouseCapabilityChange(comEventSpecific.GetSupportsAbsolute(), comEventSpecific.GetSupportsRelative(),
                                          comEventSpecific.GetSupportsTouchScreen(), comEventSpecific.GetSupportsTouchPad(),
                                          comEventSpecific.GetNeedsHostCursor());
            break;
        }
        case KVBoxEventType_OnCursorPositionChanged:
        {
            CCursorPositionChangedEvent comEventSpecific(pEvent);
            emit sigCursorPositionChange(comEventSpecific.GetHasData(),
                                         (unsigned long)comEventSpecific.GetX(), (unsigned long)comEventSpecific.GetY());
            break;
        }
        case KVBoxEventType_OnKeyboardLedsChanged:
        {
            CKeyboardLedsChangedEvent comEventSpecific(pEvent);
            emit sigKeyboardLedsChange(comEventSpecific.GetNumLock(),
                                       comEventSpecific.GetCapsLock(),
                                       comEventSpecific.GetScrollLock());
            break;
        }
        case KVBoxEventType_OnStateChanged:
        {
            CStateChangedEvent comEventSpecific(pEvent);
            emit sigStateChange(comEventSpecific.GetState());
            break;
        }
        case KVBoxEventType_OnAdditionsStateChanged:
        {
            emit sigAdditionsChange();
            break;
        }
        case KVBoxEventType_OnNetworkAdapterChanged:
        {
            CNetworkAdapterChangedEvent comEventSpecific(pEvent);
            emit sigNetworkAdapterChange(comEventSpecific.GetNetworkAdapter());
            break;
        }
        case KVBoxEventType_OnVRDEServerChanged:
        case KVBoxEventType_OnVRDEServerInfoChanged:
        {
            emit sigVRDEChange();
            break;
        }
        case KVBoxEventType_OnRecordingStateChanged:
        {
            emit sigRecordingChange();
            break;
        }
        case KVBoxEventType_OnUSBControllerChanged:
        {
            emit sigUSBControllerChange();
            break;
        }
        case KVBoxEventType_OnUSBDeviceStateChanged:
        {
            CUSBDeviceStateChangedEvent comEventSpecific(pEvent);
            emit sigUSBDeviceStateChange(comEventSpecific.GetDevice(),
                                         comEventSpecific.GetAttached(),
                                         comEventSpecific.GetError());
            break;
        }
        case KVBoxEventType_OnSharedFolderChanged:
        {
            emit sigSharedFolderChange();
            break;
        }
        case KVBoxEventType_OnCPUExecutionCapChanged:
        {
            emit sigCPUExecutionCapChange();
            break;
        }
        case KVBoxEventType_OnGuestMonitorChanged:
        {
            CGuestMonitorChangedEvent comEventSpecific(pEvent);
            emit sigGuestMonitorChange(comEventSpecific.GetChangeType(), comEventSpecific.GetScreenId(),
                                       QRect(comEventSpecific.GetOriginX(), comEventSpecific.GetOriginY(),
                                             comEventSpecific.GetWidth(), comEventSpecific.GetHeight()));
            break;
        }
        case KVBoxEventType_OnRuntimeError:
        {
            CRuntimeErrorEvent comEventSpecific(pEvent);
            emit sigRuntimeError(comEventSpecific.GetFatal(), comEventSpecific.GetId(), comEventSpecific.GetMessage());
            break;
        }
        case KVBoxEventType_OnCanShowWindow:
        {
            CCanShowWindowEvent comEventSpecific(pEvent);
            /* Has to be done in place to give an answer: */
            bool fVeto = false;
            QString strReason;
            emit sigCanShowWindow(fVeto, strReason);
            if (fVeto)
                comEventSpecific.AddVeto(strReason);
            else
                comEventSpecific.AddApproval(strReason);
            break;
        }
        case KVBoxEventType_OnShowWindow:
        {
            CShowWindowEvent comEventSpecific(pEvent);
            /* Has to be done in place to give an answer: */
            qint64 winId = comEventSpecific.GetWinId();
            if (winId != 0)
                break; /* Already set by some listener. */
            emit sigShowWindow(winId);
            comEventSpecific.SetWinId(winId);
            break;
        }
        case KVBoxEventType_OnAudioAdapterChanged:
        {
            emit sigAudioAdapterChange();
            break;
        }

        case KVBoxEventType_OnProgressPercentageChanged:
        {
            CProgressPercentageChangedEvent comEventSpecific(pEvent);
            emit sigProgressPercentageChange(comEventSpecific.GetProgressId(), (int)comEventSpecific.GetPercent());
            break;
        }
        case KVBoxEventType_OnProgressTaskCompleted:
        {
            CProgressTaskCompletedEvent comEventSpecific(pEvent);
            emit sigProgressTaskComplete(comEventSpecific.GetProgressId());
            break;
        }

        case KVBoxEventType_OnGuestSessionRegistered:
        {
            CGuestSessionRegisteredEvent comEventSpecific(pEvent);
            if (comEventSpecific.GetRegistered())
                emit sigGuestSessionRegistered(comEventSpecific.GetSession());
            else
                emit sigGuestSessionUnregistered(comEventSpecific.GetSession());
            break;
        }
        case KVBoxEventType_OnGuestProcessRegistered:
        {
            CGuestProcessRegisteredEvent comEventSpecific(pEvent);
            if (comEventSpecific.GetRegistered())
                emit sigGuestProcessRegistered(comEventSpecific.GetProcess());
            else
                emit sigGuestProcessUnregistered(comEventSpecific.GetProcess());
            break;
        }
        case KVBoxEventType_OnGuestSessionStateChanged:
        {
            CGuestSessionStateChangedEvent comEventSpecific(pEvent);
            emit sigGuestSessionStatedChanged(comEventSpecific);
            break;
        }
        case KVBoxEventType_OnGuestProcessInputNotify:
        case KVBoxEventType_OnGuestProcessOutput:
        {
            break;
        }
        case KVBoxEventType_OnGuestProcessStateChanged:
        {
            CGuestProcessStateChangedEvent comEventSpecific(pEvent);
            comEventSpecific.GetError();
            emit sigGuestProcessStateChanged(comEventSpecific);
            break;
        }
        case KVBoxEventType_OnGuestFileRegistered:
        case KVBoxEventType_OnGuestFileStateChanged:
        case KVBoxEventType_OnGuestFileOffsetChanged:
        case KVBoxEventType_OnGuestFileRead:
        case KVBoxEventType_OnGuestFileWrite:
        {
            break;
        }
        case KVBoxEventType_OnClipboardModeChanged:
        {
            CClipboardModeChangedEvent comEventSpecific(pEvent);
            emit sigClipboardModeChange(comEventSpecific.GetClipboardMode());
            break;
        }
        case KVBoxEventType_OnClipboardError:
        {
            CClipboardErrorEvent comEventSpecific(pEvent);
            emit sigClipboardError(comEventSpecific.GetMsg());
            break;
        }
        case KVBoxEventType_OnDnDModeChanged:
        {
            CDnDModeChangedEvent comEventSpecific(pEvent);
            emit sigDnDModeChange(comEventSpecific.GetDndMode());
            break;
        }
        case KVBoxEventType_OnExtPackInstalled:
        {
            CExtPackInstalledEvent comEventSpecific(pEvent);
            emit sigExtensionPackInstalled(comEventSpecific.GetName());
            break;
        }
        case KVBoxEventType_OnExtPackUninstalled:
        {
            CExtPackInstalledEvent comEventSpecific(pEvent);
            emit sigExtensionPackUninstalled(comEventSpecific.GetName());
            break;
        }
        default: break;
    }

    /* Unlock COM cleanup protection token: */
    gpGlobalSession->comTokenUnlock();

    return S_OK;
}

void UIMainEventListener::sltHandleThreadFinished()
{
    /* We have received a signal about thread finished, that means we were
     * patiently waiting for it, instead of killing UIMainEventListener object. */
    UIMainEventListeningThread *pSender = qobject_cast<UIMainEventListeningThread*>(sender());
    AssertPtrReturnVoid(pSender);

    /* We should remove corresponding thread from the list: */
    const int iIndex = m_threads.indexOf(pSender);
    delete m_threads.value(iIndex);
    m_threads.removeAt(iIndex);

    /* And notify listeners we have really finished: */
    if (m_threads.isEmpty())
        emit sigListeningFinished();
}

#include "UIMainEventListener.moc"
