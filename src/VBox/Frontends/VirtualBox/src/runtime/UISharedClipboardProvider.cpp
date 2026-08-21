/* $Id: UISharedClipboardProvider.cpp 115102 2026-08-21 11:14:19Z andreas.loeffler@oracle.com $ */
/** @file
 * VBox Qt GUI - UISharedClipboardProvider class implementation.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

/* GUI includes: */
#include "UIGlobalSession.h"
#include "UILoggingDefs.h"
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include "UINotificationCenter.h"
# include "UINotificationObjects.h"
#endif
#include "UISharedClipboardProvider.h"
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include "UISharedClipboardTransferMgr.h"
#endif
#include "UISession.h"

/* COM includes: */
#include <VBox/com/VirtualBox.h>

#include "COMDefs.h"
#include "CClipboard.h"
#include "CClipboardDataChangedEvent.h"
#include "CClipboardErrorEvent.h"
#include "CClipboardFormat.h"
#include "CClipboardFormatChangedEvent.h"
#include "CClipboardItem.h"
#include "CClipboardSourceChangedEvent.h"
#include "CConsole.h"
#include "CEvent.h"

/* Other VBox includes: */
#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_GUI
#include <VBox/log.h>


/** Returns the clipboard MIME priority, or UINT32_MAX for unsupported formats.
  * @returns Clipboard MIME priority, or UINT32_MAX for unsupported formats.
  * @param  strMimeType  Brings the MIME type to classify. */
static uint32_t shclGuiMimePriority(const QString &strMimeType)
{
    const QString strType = strMimeType.toLower();
    if (   strType == "text/plain"
        || strType.startsWith("text/plain;"))
        return 0;
    if (   strType == "text/html"
        || strType.startsWith("text/html;"))
        return 1;
    if (   strType == "image/bmp"
        || strType == "image/x-bmp"
        || strType == "application/x-bmp")
        return 2;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (   strType == "text/uri-list"
        || strType == "application/x-virtualbox-shared-clipboard-uri-list")
        return 3;
#endif
    return UINT32_MAX;
}


/** Returns whether two MIME types are equivalent for shared clipboard purposes.
  * @returns @c true if the MIME types are equivalent, @c false otherwise.
  * @param  strRequested  Brings the requested MIME type.
  * @param  strActual     Brings the actual MIME type. */
static bool shclGuiMimeEquivalent(const QString &strRequested, const QString &strActual)
{
    const uint32_t uRequestedPriority = shclGuiMimePriority(strRequested);
    return    uRequestedPriority != UINT32_MAX
           && uRequestedPriority == shclGuiMimePriority(strActual);
}


/** Filters unsupported formats and optionally returns the preferred MIME type.
  * @returns @c true if at least one supported format was found, @c false otherwise.
  * @param  formats                Brings the formats to filter.
  * @param  filteredFormats        Where to return the filtered formats.
  * @param  pstrPreferredMimeType  Where to return the preferred MIME type, optional. */
static bool shclGuiFilterSupportedFormats(const QVector<CClipboardFormat> &formats,
                                          QVector<CClipboardFormat> &filteredFormats,
                                          QString *pstrPreferredMimeType = 0)
{
    filteredFormats.clear();
    uint32_t uBestPriority = UINT32_MAX;
    QString strBestMimeType;
    foreach (const CClipboardFormat &comFormat, formats)
    {
        if (comFormat.isNull())
            continue;
        const QString strMimeType = comFormat.GetMimeType();
        if (!comFormat.isOk() || strMimeType.isEmpty())
            continue;
        const uint32_t uPriority = shclGuiMimePriority(strMimeType);
        if (uPriority == UINT32_MAX)
            continue;

        filteredFormats << comFormat;
        if (uPriority < uBestPriority)
        {
            uBestPriority = uPriority;
            strBestMimeType = strMimeType;
        }
    }
    if (pstrPreferredMimeType)
        *pstrPreferredMimeType = strBestMimeType;
    return !filteredFormats.isEmpty();
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Returns whether a list contains a shared clipboard URI-list format.
  * @returns @c true if a URI-list format is present, @c false otherwise.
  * @param  formats  Brings the formats to inspect. */
static bool shclGuiFormatsContainUriList(const QVector<CClipboardFormat> &formats)
{
    foreach (const CClipboardFormat &comFormat, formats)
    {
        if (comFormat.isNull())
            continue;

        const QString strMimeType = comFormat.GetMimeType();
        if (   comFormat.isOk()
            && strMimeType.compare("text/uri-list", Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}
#endif


/** Marks a waitable event as processed.
  * @param  comEventSource    Brings the event source.
  * @param  comEventListener  Brings the event listener.
  * @param  comEvent          Brings the event to acknowledge. */
static void shclGuiMarkEventProcessed(CEventSource &comEventSource,
                                      const CEventListener &comEventListener,
                                      const CEvent &comEvent)
{
    if (comEvent.isNull())
        return;

    const BOOL fWaitable = comEvent.GetWaitable();
    if (!comEvent.isOk())
    {
        LogRel2(("GUI: UISharedClipboardProvider: failed to inspect waitable event state: %Rhrc\n", comEvent.lastRC()));
        return;
    }
    if (!fWaitable)
        return;

    comEventSource.EventProcessed(comEventListener, comEvent);
    if (!comEventSource.isOk())
        LogRel(("GUI: UISharedClipboardProvider: acknowledging waitable event failed: %Rhrc\n", comEventSource.lastRC()));
    else
        LogRel2(("GUI: UISharedClipboardProvider: EventProcessed set for waitable event\n"));
}


/** Private thread class serving session clipboard events. */
class UISharedClipboardProviderThread : public QThread
{
    Q_OBJECT;

signals:

    /** Notifies about a shared clipboard error suitable for the notification center.
      * @param  strMsg  Brings the error message. */
    void sigClipboardError(const QString &strMsg);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Notifies about a shared clipboard transfer progress object suitable for the notification center.
      * @param  comTransfer  Brings the transfer to show. */
    void sigClipboardTransferProgress(const CClipboardTransfer &comTransfer);
#endif

public:

    /** Constructs a shared clipboard provider worker thread.
      * @param  pParent              Brings the parent object.
      * @param  comClipboardSession  Brings the clipboard session to serve.
      * @param  comHostClipboard     Brings the session host clipboard endpoint.
      * @param  comEventSource       Brings the session event source.
      * @param  comEventListener     Brings the passive event listener.
      * @param  comTransferManager   Brings the clipboard transfer manager when transfer support is enabled. */
    UISharedClipboardProviderThread(QObject *pParent,
                                    const CClipboardSession &comClipboardSession,
                                    const CHostClipboard &comHostClipboard,
                                    const CEventSource &comEventSource,
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                                    const CEventListener &comEventListener,
                                    const CClipboardTransferManager &comTransferManager)
#else
                                    const CEventListener &comEventListener)
#endif
        : QThread(pParent)
        , m_comClipboardSession(comClipboardSession)
        , m_comHostClipboard(comHostClipboard)
        , m_comEventSource(comEventSource)
        , m_comEventListener(comEventListener)
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        , m_transferManager(comTransferManager)
#endif
        , m_fShutdown(false)
    {
        setObjectName("UISharedClipboardProviderThread");
    }

    /** Requests thread shutdown. */
    void requestShutdown()
    {
        QMutexLocker locker(&m_mutex);
        m_fShutdown = true;
    }

protected:

    /** Contains the thread execution body. */
    virtual void run() RT_OVERRIDE
    {
        HRESULT hrc = COMBase::InitializeCOM(false);
        if (FAILED(hrc))
        {
            LogRel(("GUI: UISharedClipboardProvider: worker COM initialization failed: %Rhrc\n", hrc));
            emit sigClipboardError(tr("Starting the GUI shared clipboard provider failed."));
            return;
        }

        CClipboardSession comClipboardSession = m_comClipboardSession;
        CHostClipboard comHostClipboard = m_comHostClipboard;
        CEventSource comEventSource = m_comEventSource;
        CEventListener comEventListener = m_comEventListener;

        while (!isShutdown())
        {
            CEvent comEvent = comEventSource.GetEvent(comEventListener, 500);
            hrc = comEventSource.lastRC();
            if (hrc == VBOX_E_OBJECT_NOT_FOUND)
                continue;
            if (FAILED(hrc))
            {
                LogRel(("GUI: UISharedClipboardProvider: waiting for clipboard events failed: %Rhrc\n", hrc));
                continue;
            }
            if (comEvent.isNull())
                continue;

            const KVBoxEventType enmType = comEvent.GetType();
            if (!comEvent.isOk())
            {
                LogRel2(("GUI: UISharedClipboardProvider: failed to inspect clipboard event type: %Rhrc\n", comEvent.lastRC()));
                shclGuiMarkEventProcessed(comEventSource, comEventListener, comEvent);
                continue;
            }

            bool fContinue = true;
            if (gpGlobalSession->comTokenTryLockForRead())
            {
                fContinue = handleEvent(comClipboardSession, comHostClipboard, enmType, comEvent);
                gpGlobalSession->comTokenUnlock();
            }
            else
                LogRel2(("GUI: UISharedClipboardProvider: skipping clipboard event while COM cleanup is pending\n"));

            shclGuiMarkEventProcessed(comEventSource, comEventListener, comEvent);
            if (!fContinue)
                break;
        }

        COMBase::CleanupCOM();
    }

private:

    /** Returns whether shutdown was requested. */
    bool isShutdown() const
    {
        QMutexLocker locker(&m_mutex);
        return m_fShutdown;
    }

    /** Handles a clipboard event.
      * @returns @c true to continue serving, @c false to stop the provider.
      * @param  comClipboardSession  Brings the clipboard session.
      * @param  comHostClipboard     Brings the session host clipboard endpoint.
      * @param  enmType              Brings the event type.
      * @param  comEvent             Brings the event to handle. */
    bool handleEvent(CClipboardSession &comClipboardSession, CHostClipboard &comHostClipboard,
                     KVBoxEventType enmType, const CEvent &comEvent)
    {
        switch (enmType)
        {
            case KVBoxEventType_OnClipboardFormatChanged:
                return handleFormatChanged(comClipboardSession, comHostClipboard, comEvent);
            case KVBoxEventType_OnClipboardDataChanged:
                return handleDataChanged(comClipboardSession, comHostClipboard, comEvent);
            case KVBoxEventType_OnClipboardSourceChanged:
                return handleSourceChanged(comClipboardSession, comHostClipboard, comEvent);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            case KVBoxEventType_OnClipboardTransfer:
            {
                CClipboardTransfer comNotifyTransfer;
                QString strError;
                if (m_transferManager.handleEvent(comEvent, &comNotifyTransfer, &strError))
                    emit sigClipboardTransferProgress(comNotifyTransfer);
                if (!strError.isEmpty())
                    emit sigClipboardError(strError);
                return true;
            }
#endif
            case KVBoxEventType_OnClipboardError:
                handleClipboardError(comEvent);
                return true;
            default:
                LogRel3(("GUI: UISharedClipboardProvider: diagnostic clipboard event %d ignored\n", enmType));
                return true;
        }
    }

    /** Publishes guest-owned data to the native host clipboard.
      * @returns @c true to continue serving, @c false to stop the provider.
      * @param  comHostClipboard  Brings the session host clipboard endpoint.
      * @param  strMimeType       Brings the MIME type to publish.
      * @param  buffer            Brings the data buffer to publish. */
    bool publishGuestData(CHostClipboard &comHostClipboard, const QString &strMimeType, const QVector<BYTE> &buffer)
    {
        comHostClipboard.SetData(KClipboardAction_Copy, KClipboardSource_Guest, strMimeType, buffer);
        if (comHostClipboard.isOk())
            return true;

        LogRel(("GUI: UISharedClipboardProvider: publishing guest clipboard data to host failed: %Rhrc\n",
                comHostClipboard.lastRC()));
        emit sigClipboardError(tr("Publishing guest clipboard data to the host failed."));
        return true;
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Reports guest-owned formats to the native host clipboard for lazy rendering.
      * @returns @c true to continue serving, @c false to stop the provider.
      * @param  comHostClipboard  Brings the session host clipboard endpoint.
      * @param  formats           Brings the formats to report. */
    bool reportGuestFormats(CHostClipboard &comHostClipboard, const QVector<CClipboardFormat> &formats)
    {
        comHostClipboard.ReportFormats(KClipboardAction_Copy, KClipboardSource_Guest, formats);
        if (comHostClipboard.isOk())
            return true;

        LogRel(("GUI: UISharedClipboardProvider: reporting guest clipboard formats to host failed: %Rhrc\n",
                comHostClipboard.lastRC()));
        emit sigClipboardError(tr("Publishing guest clipboard formats to the host failed."));
        return true;
    }
#endif

    /** Reads preferred guest-owned data and publishes it eagerly to the native host clipboard.
      * @returns @c true to continue serving, @c false to stop the provider.
      * @param  comClipboardSession  Brings the clipboard session.
      * @param  comHostClipboard     Brings the session host clipboard endpoint.
      * @param  strMimeType          Brings the preferred MIME type to read. */
    bool readAndPublishGuestData(CClipboardSession &comClipboardSession, CHostClipboard &comHostClipboard,
                                 const QString &strMimeType)
    {
        KClipboardSource enmReadSource = KClipboardSource_Custom;
        QString strReadMimeType;
        QVector<BYTE> buffer = comClipboardSession.ReadDataRaw(KClipboardAction_Copy, strMimeType, enmReadSource, strReadMimeType);
        if (!comClipboardSession.isOk())
        {
            /* A passive GUI listener cannot reliably satisfy native lazy data requests before Main
             * retires them.  Missing data here is therefore logged and left for a later payload event. */
            LogRel2(("GUI: UISharedClipboardProvider: eager guest clipboard data read failed: %Rhrc\n",
                     comClipboardSession.lastRC()));
            return true;
        }
        if (   enmReadSource != KClipboardSource_Guest
            || !shclGuiMimeEquivalent(strMimeType, strReadMimeType)
            || buffer.isEmpty())
            return true;

        return publishGuestData(comHostClipboard, strReadMimeType, buffer);
    }

    /** Handles a format-changed event.
      * @returns @c true to continue serving, @c false to stop the provider.
      * @param  comClipboardSession  Brings the clipboard session.
      * @param  comHostClipboard     Brings the session host clipboard endpoint.
      * @param  comEvent             Brings the format-changed event. */
    bool handleFormatChanged(CClipboardSession &comClipboardSession, CHostClipboard &comHostClipboard, const CEvent &comEvent)
    {
        CClipboardFormatChangedEvent comFormatEvent(comEvent);
        if (comFormatEvent.isNull())
            return true;

        const KClipboardSource enmSource = comFormatEvent.GetClipboardSource();
        QVector<CClipboardFormat> formats = comFormatEvent.GetFormats();
        if (!comFormatEvent.isOk())
        {
            LogRel(("GUI: UISharedClipboardProvider: failed to inspect clipboard format event: %Rhrc\n",
                    comFormatEvent.lastRC()));
            emit sigClipboardError(tr("Inspecting a shared clipboard format event failed."));
            return true;
        }
        if (enmSource != KClipboardSource_Guest)
            return true;

        QVector<CClipboardFormat> filteredFormats;
        QString strPreferredMimeType;
        if (!shclGuiFilterSupportedFormats(formats, filteredFormats, &strPreferredMimeType))
        {
            LogRel3(("GUI: UISharedClipboardProvider: guest format event has no supported formats\n"));
            return true;
        }

        LogRel3(("GUI: UISharedClipboardProvider: guest format event has %zu supported formats\n",
                 (size_t)filteredFormats.size()));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        if (shclGuiFormatsContainUriList(filteredFormats))
            return reportGuestFormats(comHostClipboard, filteredFormats);
#endif
        return readAndPublishGuestData(comClipboardSession, comHostClipboard, strPreferredMimeType);
    }

    /** Handles a source-changed event.
      * @returns @c true to continue serving, @c false to stop the provider.
      * @param  comClipboardSession  Brings the clipboard session.
      * @param  comHostClipboard     Brings the session host clipboard endpoint.
      * @param  comEvent             Brings the source-changed event. */
    bool handleSourceChanged(CClipboardSession &comClipboardSession, CHostClipboard &comHostClipboard, const CEvent &comEvent)
    {
        CClipboardSourceChangedEvent comSourceEvent(comEvent);
        if (comSourceEvent.isNull())
            return true;

        const KClipboardSource enmSource = comSourceEvent.GetClipboardSource();
        if (!comSourceEvent.isOk())
        {
            LogRel(("GUI: UISharedClipboardProvider: failed to inspect clipboard source event: %Rhrc\n",
                    comSourceEvent.lastRC()));
            emit sigClipboardError(tr("Inspecting a shared clipboard source event failed."));
            return true;
        }
        if (enmSource != KClipboardSource_Guest)
            return true;

        QVector<CClipboardFormat> formats = comClipboardSession.ReadFormats();
        if (!comClipboardSession.isOk())
        {
            LogRel(("GUI: UISharedClipboardProvider: reading guest clipboard formats failed: %Rhrc\n",
                    comClipboardSession.lastRC()));
            emit sigClipboardError(tr("Reading guest clipboard formats failed."));
            return true;
        }

        QVector<CClipboardFormat> filteredFormats;
        QString strPreferredMimeType;
        if (!shclGuiFilterSupportedFormats(formats, filteredFormats, &strPreferredMimeType))
        {
            LogRel3(("GUI: UISharedClipboardProvider: guest source event has no supported formats\n"));
            return true;
        }

        LogRel3(("GUI: UISharedClipboardProvider: guest source event has %zu supported formats\n",
                 (size_t)filteredFormats.size()));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        if (shclGuiFormatsContainUriList(filteredFormats))
        {
            /* Guest file-format reports always queue a matching format event.  Publish the
             * delayed-rendering data object there so that one offer is not published twice. */
            return true;
        }
#endif
        return readAndPublishGuestData(comClipboardSession, comHostClipboard, strPreferredMimeType);
    }

    /** Handles a data-changed event.
      * @returns @c true to continue serving, @c false to stop the provider.
      * @param  comClipboardSession  Brings the clipboard session.
      * @param  comHostClipboard     Brings the session host clipboard endpoint.
      * @param  comEvent             Brings the data-changed event. */
    bool handleDataChanged(CClipboardSession &comClipboardSession, CHostClipboard &comHostClipboard, const CEvent &comEvent)
    {
        CClipboardDataChangedEvent comDataEvent(comEvent);
        if (comDataEvent.isNull())
            return true;

        const CClipboardItem comItem = comDataEvent.GetItem();
        if (!comDataEvent.isOk() || comItem.isNull())
        {
            LogRel(("GUI: UISharedClipboardProvider: clipboard data event has no item: %Rhrc\n", comDataEvent.lastRC()));
            emit sigClipboardError(tr("Inspecting a shared clipboard data event failed."));
            return true;
        }

        const KClipboardSource enmSource = comItem.GetSource();
        const CClipboardFormat comFormat = comItem.GetFormat();
        const QString strMimeType = comFormat.GetMimeType();
        const QVector<BYTE> buffer = comItem.GetBuffer();
        if (!comItem.isOk() || !comFormat.isOk())
        {
            LogRel(("GUI: UISharedClipboardProvider: reading clipboard data event payload failed: %Rhrc\n",
                    !comItem.isOk() ? comItem.lastRC() : comFormat.lastRC()));
            emit sigClipboardError(tr("Reading a shared clipboard data event failed."));
            return true;
        }
        if (shclGuiMimePriority(strMimeType) == UINT32_MAX || buffer.isEmpty())
            return true;

        if (enmSource == KClipboardSource_Guest)
            return publishGuestData(comHostClipboard, strMimeType, buffer);

        if (enmSource == KClipboardSource_Host)
        {
            KClipboardSource enmWrittenSource = KClipboardSource_Custom;
            QString strWrittenMimeType;
            comClipboardSession.WriteDataRaw(KClipboardAction_Copy, KClipboardSource_Host, strMimeType, buffer,
                                             enmWrittenSource, strWrittenMimeType);
            if (comClipboardSession.isOk())
                return true;

            LogRel(("GUI: UISharedClipboardProvider: forwarding host clipboard data to guest failed: %Rhrc\n",
                    comClipboardSession.lastRC()));
            emit sigClipboardError(tr("Forwarding host clipboard data to the guest failed."));
            return true;
        }

        return true;
    }

    /** Handles a clipboard error event.
      * @param  comEvent  Brings the clipboard error event. */
    void handleClipboardError(const CEvent &comEvent)
    {
        CClipboardErrorEvent comErrorEvent(comEvent);
        if (comErrorEvent.isNull())
        {
            const QString strMsg = tr("An unspecified shared clipboard error occurred.");
            LogRel(("GUI: UISharedClipboardProvider: %s\n", strMsg.toUtf8().constData()));
            emit sigClipboardError(strMsg);
            return;
        }

        QString strMsg = comErrorEvent.GetMsg();
        if (!comErrorEvent.isOk() || strMsg.isEmpty())
            strMsg = tr("An unspecified shared clipboard error occurred.");
        LogRel(("GUI: UISharedClipboardProvider: %s\n", strMsg.toUtf8().constData()));
        emit sigClipboardError(strMsg);
    }

    /** Holds the clipboard session. */
    CClipboardSession m_comClipboardSession;
    /** Holds the session host clipboard endpoint. */
    CHostClipboard m_comHostClipboard;
    /** Holds the session event source. */
    CEventSource m_comEventSource;
    /** Holds the passive event listener. */
    CEventListener m_comEventListener;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Holds the clipboard transfer event manager. */
    UISharedClipboardTransferMgr m_transferManager;
#endif
    /** Holds the mutex instance which protects thread access. */
    mutable QMutex m_mutex;
    /** Holds whether shutdown was requested. */
    bool m_fShutdown;
};


UISharedClipboardProvider::UISharedClipboardProvider(UISession *pSession)
    : QObject(pSession)
    , m_pSession(pSession)
    , m_pThread(0)
{
}

UISharedClipboardProvider::~UISharedClipboardProvider()
{
    cleanup();
}

void UISharedClipboardProvider::prepare()
{
    prepareSession();
    prepareListener();
    prepareThread();
}

void UISharedClipboardProvider::sltHandleThreadFinished()
{
    UISharedClipboardProviderThread *pThread = qobject_cast<UISharedClipboardProviderThread*>(sender());
    if (!pThread || pThread != m_pThread)
        return;

    LogRel2(("GUI: UISharedClipboardProvider: provider thread finished; cleaning up session\n"));
    m_pThread = 0;
    pThread->deleteLater();
    cleanupListener();
    cleanupSession();
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
void UISharedClipboardProvider::sltHandleTransferProgress(const CClipboardTransfer &comTransfer)
{
    if (comTransfer.isNull() || !gpNotificationCenter)
        return;

    gpNotificationCenter->append(new UINotificationProgressSharedClipboardTransfer(comTransfer));
}
#endif

void UISharedClipboardProvider::prepareSession()
{
    AssertPtrReturnVoid(m_pSession);

    CConsole comConsole = m_pSession->console();
    if (comConsole.isNull() || !comConsole.isOk())
        return;

    CClipboard comClipboard = comConsole.GetClipboard();
    if (comClipboard.isNull() || !comClipboard.isOk())
    {
        emit sigClipboardError(tr("Creating the GUI shared clipboard provider failed: no live clipboard object is available."));
        return;
    }

    QVector<KIClipboardSessionFlag> flags;
    flags << KIClipboardSessionFlag_ExcludeOwnChanges;
    /* Windows filters native self-notifications by clipboard-owner identity.
     * A reflection marker there could suppress the next genuine guest offer. */
#if !defined(RT_OS_WINDOWS) || !defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS)
    flags << KIClipboardSessionFlag_ExcludeReflections;
#endif
    flags << KIClipboardSessionFlag_IncludeInitialState
          << KIClipboardSessionFlag_IncludePayload;
    m_comClipboardSession = comClipboard.CreateSession(flags);
    if (m_comClipboardSession.isNull() || !comClipboard.isOk())
    {
        emit sigClipboardError(tr("Creating the GUI shared clipboard session failed."));
        return;
    }

    m_comHostClipboard = m_comClipboardSession.GetHostClipboard();
    if (m_comHostClipboard.isNull() || !m_comClipboardSession.isOk())
    {
        emit sigClipboardError(tr("Creating the GUI shared clipboard host endpoint failed."));
        cleanupSession();
        return;
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    m_comTransferManager = comClipboard.GetTransfers();
    if (m_comTransferManager.isNull() || !comClipboard.isOk())
    {
        LogRel(("GUI: UISharedClipboardProvider: clipboard transfer manager is unavailable: %Rhrc\n",
                comClipboard.lastRC()));
        m_comTransferManager.detach();
    }
    else
        LogRel2(("GUI: UISharedClipboardProvider: clipboard transfer manager acquired\n"));
#endif
}

void UISharedClipboardProvider::prepareListener()
{
    if (m_comClipboardSession.isNull())
        return;

    m_comEventSource = m_comClipboardSession.GetEventSource();
    if (m_comEventSource.isNull() || !m_comClipboardSession.isOk())
    {
        emit sigClipboardError(tr("Creating the GUI shared clipboard event source failed."));
        return;
    }

    m_comEventListener = m_comEventSource.CreateListener();
    if (m_comEventListener.isNull() || !m_comEventSource.isOk())
    {
        emit sigClipboardError(tr("Creating the GUI shared clipboard event listener failed."));
        return;
    }

    QVector<KVBoxEventType> eventTypes;
    eventTypes << KVBoxEventType_OnClipboardFormatChanged
               << KVBoxEventType_OnClipboardDataChanged
               << KVBoxEventType_OnClipboardError
               << KVBoxEventType_OnClipboardSourceChanged;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (m_comTransferManager.isNotNull())
        eventTypes << KVBoxEventType_OnClipboardTransfer;
#endif
    m_comEventSource.RegisterListener(m_comEventListener, eventTypes, FALSE /* active? */);
    if (!m_comEventSource.isOk())
    {
        emit sigClipboardError(tr("Registering the GUI shared clipboard event listener failed."));
        m_comEventListener.detach();
    }
}

void UISharedClipboardProvider::prepareThread()
{
    if (m_comClipboardSession.isNull() || m_comHostClipboard.isNull() || m_comEventSource.isNull() || m_comEventListener.isNull())
        return;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    qRegisterMetaType<CClipboardTransfer>("CClipboardTransfer");
    m_pThread = new UISharedClipboardProviderThread(this, m_comClipboardSession, m_comHostClipboard, m_comEventSource,
                                                   m_comEventListener, m_comTransferManager);
#else
    m_pThread = new UISharedClipboardProviderThread(this, m_comClipboardSession, m_comHostClipboard, m_comEventSource,
                                                   m_comEventListener);
#endif
    if (!m_pThread)
        return;

    connect(m_pThread, &UISharedClipboardProviderThread::sigClipboardError,
            this, &UISharedClipboardProvider::sigClipboardError,
            Qt::QueuedConnection);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    connect(m_pThread, &UISharedClipboardProviderThread::sigClipboardTransferProgress,
            this, &UISharedClipboardProvider::sltHandleTransferProgress,
            Qt::QueuedConnection);
#endif
    connect(m_pThread, &UISharedClipboardProviderThread::finished,
            this, &UISharedClipboardProvider::sltHandleThreadFinished,
            Qt::QueuedConnection);
    m_pThread->start();
    LogRel2(("GUI: UISharedClipboardProvider: session-aware provider started\n"));
}

bool UISharedClipboardProvider::cleanupThread()
{
    if (!m_pThread)
        return true;

    disconnect(m_pThread, &UISharedClipboardProviderThread::finished,
               this, &UISharedClipboardProvider::sltHandleThreadFinished);
    m_pThread->requestShutdown();
    if (!m_pThread->wait(30000))
    {
        LogRel(("GUI: UISharedClipboardProvider: provider thread did not stop in time; leaving COM session cleanup to thread references\n"));
        disconnect(m_pThread, 0, this, 0);
        m_pThread->setParent(0);
        connect(m_pThread, &UISharedClipboardProviderThread::finished,
                m_pThread, &UISharedClipboardProviderThread::deleteLater);
        m_pThread = 0;
        return false;
    }

    delete m_pThread;
    m_pThread = 0;
    return true;
}

void UISharedClipboardProvider::cleanupListener()
{
    if (m_comEventSource.isNotNull() && m_comEventListener.isNotNull())
        m_comEventSource.UnregisterListener(m_comEventListener);
    m_comEventListener.detach();
    m_comEventSource.detach();
}

void UISharedClipboardProvider::cleanupSession()
{
    if (m_comHostClipboard.isNotNull())
        m_comHostClipboard.Clear();
    m_comHostClipboard.detach();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    m_comTransferManager.detach();
#endif

    if (m_comClipboardSession.isNotNull())
        m_comClipboardSession.Close();
    m_comClipboardSession.detach();
}

void UISharedClipboardProvider::cleanup()
{
    if (!cleanupThread())
        return;
    cleanupListener();
    cleanupSession();
}

#include "UISharedClipboardProvider.moc"
