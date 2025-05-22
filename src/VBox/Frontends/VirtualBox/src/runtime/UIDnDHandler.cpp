/* $Id: UIDnDHandler.cpp 168965 2025-05-22 14:49:38Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDnDHandler class implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <QApplication>
#include <QDrag>
#include <QKeyEvent>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QWidget>

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>

/* GUI includes: */
#include "UIDnDHandler.h"
#ifdef VBOX_WITH_DRAG_AND_DROP_GH
#include "CDnDSource.h"
#ifdef RT_OS_WINDOWS
# include "UIDnDDataObject_win.h"
# include "UIDnDDropSource_win.h"
#endif
#include "UIDnDMIMEData.h"
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */
#include "UIMachine.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UISession.h"

/* COM includes: */
#include "CConsole.h"
#include "CGuest.h"
#include "CGuestDnDSource.h"
#include "CGuestDnDTarget.h"
#include "CSession.h"

#ifdef LOG_GROUP
 #undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/log.h>
#include <iprt/err.h>

#if 0
# ifdef DEBUG
#  include <QTextStream>
#  include <QFile>
/** Enable this to log debug output of a Qt debug build to a file defined by DEBUG_DND_QT_LOGFILE. */
#  define DEBUG_DND_QT
#  ifdef RT_OS_WINDOWS
#   define DEBUG_DND_QT_LOGFILE "c:\\temp\\VBoxQt.log"
#  else
#   define DEBUG_DND_QT_LOGFILE "/var/tmp/vboxqt.log"
#  endif
# endif /* DEBUG */
#endif


UIDnDHandler::UIDnDHandler(UIMachine *pMachine, QWidget *pParent)
    : m_pMachine(pMachine)
    , m_pParent(pParent)
    , m_fDataRetrieved(false)
#ifdef RT_OS_WINDOWS
    , m_dwIntegrityLevel(0)
#else
    , m_pMIMEData(NULL)
#endif
{
    AssertPtr(m_pMachine);
    if (m_pMachine)
    {
        CGuest comGuest = m_pMachine->uisession()->guest();
        if (comGuest.isNotNull())
        {
            m_dndSource = static_cast<CDnDSource>(comGuest.GetDnDSource());
            m_dndTarget = static_cast<CDnDTarget>(comGuest.GetDnDTarget());
        }
   }
}

UIDnDHandler::~UIDnDHandler(void)
{
}

/*
 * Frontend -> Target.
 */

Qt::DropAction UIDnDHandler::dragEnter(ulong screenID, int x, int y,
                                       Qt::DropAction proposedAction, Qt::DropActions possibleActions,
                                       const QMimeData *pMimeData)
{
    LogFlowFunc(("screenID=%RU32, x=%d, y=%d, action=%ld\n", screenID, x, y, toVBoxDnDAction(proposedAction)));

    /* Ask the guest for starting a DnD event. */
    KDnDAction result = m_dndTarget.Enter(screenID,
                                          x,
                                          y,
                                          toVBoxDnDAction(proposedAction),
                                          toVBoxDnDActions(possibleActions),
                                          pMimeData->formats().toVector());
    if (m_dndTarget.isOk())
    {
        return toQtDnDAction(result);
    }

    UINotificationMessage::cannotDropDataToGuest(m_dndTarget);
    return Qt::IgnoreAction;
}

Qt::DropAction UIDnDHandler::dragMove(ulong screenID, int x, int y,
                                      Qt::DropAction proposedAction, Qt::DropActions possibleActions,
                                      const QMimeData *pMimeData)
{
    LogFlowFunc(("screenID=%RU32, x=%d, y=%d, action=%ld\n", screenID, x, y, toVBoxDnDAction(proposedAction)));

    if (!m_dndTarget.isOk()) /* Don't try any further if we got an error before. */
        return Qt::IgnoreAction;

    /* Notify the guest that the mouse has been moved while doing
     * a drag'n drop operation. */
    KDnDAction result = m_dndTarget.Move(screenID,
                                         x,
                                         y,
                                         toVBoxDnDAction(proposedAction),
                                         toVBoxDnDActions(possibleActions),
                                         pMimeData->formats().toVector());
    if (m_dndTarget.isOk())
        return toQtDnDAction(result);

    UINotificationMessage::cannotDropDataToGuest(m_dndTarget);
    return Qt::IgnoreAction;
}

Qt::DropAction UIDnDHandler::dragDrop(ulong screenID, int x, int y,
                                      Qt::DropAction proposedAction, Qt::DropActions possibleActions,
                                      const QMimeData *pMimeData)
{
    LogFlowFunc(("screenID=%RU32, x=%d, y=%d, action=%ld\n", screenID, x, y, toVBoxDnDAction(proposedAction)));

    if (!m_dndTarget.isOk()) /* Don't try any further if we got an error before. */
        return Qt::IgnoreAction;

    /* The format the guest requests. */
    QString strFormat;
    /* Ask the guest for dropping data. */
    KDnDAction enmResult = m_dndTarget.Drop(screenID,
                                            x,
                                            y,
                                            toVBoxDnDAction(proposedAction),
                                            toVBoxDnDActions(possibleActions),
                                            pMimeData->formats().toVector(), strFormat);
    if (!m_dndTarget.isOk())
    {
        UINotificationMessage::cannotDropDataToGuest(m_dndTarget);
    }
    else if (enmResult != KDnDAction_Ignore) /* Has the guest accepted the drop event? */
    {
        LogRel2(("DnD: Guest requested format '%s'\n", strFormat.toUtf8().constData()));
        LogRel2(("DnD: The host offered %d formats:\n", pMimeData->formats().size()));

#if 0
        QStringList::const_iterator itFmt = pMimeData->formats().constBegin();
        while (itFmt != pMimeData->formats().constEnd())
        {
            LogRel2(("DnD:\t'%s'\n", (*itFmt).toUtf8().constData()));
            itFmt++;
        }
#endif
        QByteArray arrBytes;

        /*
         * Does the host support the format requested by the guest?
         * Lookup the format in the MIME data object.
         */
        AssertPtr(pMimeData);
        if (pMimeData->formats().indexOf(strFormat) >= 0)
        {
            arrBytes = pMimeData->data(strFormat);
            Assert(!arrBytes.isEmpty());
        }
        /*
         * The host does not support the format requested by the guest.
         * This can happen if the host wants to send plan text, for example, but
         * the guest requested something else, e.g. an URI list.
         *
         * In that case dictate the guest to use a fixed format from the host,
         * so instead returning the requested URI list, return the original
         * data format from the host. The guest has to try to deal with that then.
         **/
        else
        {
            if (pMimeData->hasText())
            {
                LogRel2(("DnD: Converting data to text ...\n"));
                arrBytes  = pMimeData->text().toUtf8();
                strFormat = "text/plain;charset=utf-8";
            }
            else
            {
                LogRel(("DnD: Host formats did not offer a matching format for the guest, skipping\n"));
                enmResult = KDnDAction_Ignore;
            }
        }

        if (arrBytes.size()) /* Anything to send? */
        {
            /* Convert data to a vector. */
            QVector<uint8_t> vecData(arrBytes.size()); /** @todo Can this throw or anything? */
            AssertReleaseMsg(vecData.size() == arrBytes.size(), ("Drag and drop format buffer size does not match"));
            memcpy(vecData.data(), arrBytes.constData(), arrBytes.size());

            /* Send data to the guest. */
            LogRel2(("DnD: Host is sending %d bytes of data as '%s'\n", vecData.size(), strFormat.toUtf8().constData()));
            CProgress progress = m_dndTarget.SendData(screenID, strFormat, vecData);

            if (m_dndTarget.isOk())
            {
                msgCenter().showModalProgressDialog(progress,
                                                    tr("Dropping data ..."), ":/progress_dnd_hg_90px.png",
                                                    m_pParent);

                LogFlowFunc(("Transfer fCompleted=%RTbool, fCanceled=%RTbool, hr=%Rhrc\n",
                             progress.GetCompleted(), progress.GetCanceled(), progress.GetResultCode()));

                BOOL fCanceled = progress.GetCanceled();
                if (   !fCanceled
                    && (   !progress.isOk()
                        ||  progress.GetResultCode() != 0))
                {
                    UINotificationMessage::cannotDropDataToGuest(progress);
                    enmResult = KDnDAction_Ignore;
                }
            }
            else
            {
                UINotificationMessage::cannotDropDataToGuest(m_dndTarget);
                enmResult = KDnDAction_Ignore;
            }
        }
        else /* Error. */
            enmResult = KDnDAction_Ignore;
    }

    return toQtDnDAction(enmResult);
}

void UIDnDHandler::dragLeave(ulong screenID)
{
    LogFlowFunc(("screenID=%RU32\n", screenID));

    if (!m_dndTarget.isOk()) /* Don't try any further if we got an error before. */
        return;

    m_dndTarget.Leave(screenID);
    if (m_dndTarget.isOk())
        return;

    UINotificationMessage::cannotDropDataToGuest(m_dndTarget);
    return;
}

#ifdef DEBUG_DND_QT
QTextStream *g_pStrmLogQt = NULL; /* Output stream for Qt debug logging. */

/* static */
void UIDnDHandler::debugOutputQt(QtMsgType type, const QMessageLogContext &context, const QString &strMessage)
{
    QString strMsg;
    switch (type)
    {
    case QtWarningMsg:
        strMsg += "[W]";
        break;
    case QtCriticalMsg:
        strMsg += "[C]";
        break;
    case QtFatalMsg:
        strMsg += "[F]";
        break;
    case QtDebugMsg:
    default:
        strMsg += "[D]";
        break;
    }

    if (g_pStrmLogQt)
        (*g_pStrmLogQt) << strMsg << " " << strMessage << endl;
}
#endif /* DEBUG_DND_QT */

/*
 * Source -> Frontend.
 */

int UIDnDHandler::dragStartInternal(const QStringList &lstFormats,
                                    Qt::DropAction defAction, Qt::DropActions actions)
{
    RT_NOREF(defAction);

    int rc = VINF_SUCCESS;

#ifdef VBOX_WITH_DRAG_AND_DROP_GH

    LogFlowFunc(("defAction=0x%x\n", defAction));
    LogFlowFunc(("Number of formats: %d\n", lstFormats.size()));
# ifdef DEBUG
    for (int i = 0; i < lstFormats.size(); i++)
        LogFlowFunc(("\tFormat %d: %s\n", i, lstFormats.at(i).toUtf8().constData()));
# endif

# ifdef DEBUG_DND_QT
    QFile *pFileDebugQt = new QFile(DEBUG_DND_QT_LOGFILE);
    if (pFileDebugQt->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        g_pStrmLogQt = new QTextStream(pFileDebugQt);

        qInstallMessageHandler(UIDnDHandler::debugOutputQt);
        qDebug("========================================================================");
    }
# endif

# ifdef RT_OS_WINDOWS
    UIDnDDataObject *pDataObject = new UIDnDDataObject(this, lstFormats);
    if (!pDataObject)
        return VERR_NO_MEMORY;
    UIDnDDropSource *pDropSource = new UIDnDDropSource(m_pParent, pDataObject);
    if (!pDropSource)
        return VERR_NO_MEMORY;

    DWORD dwOKEffects = DROPEFFECT_NONE;
    if (actions)
    {
        if (actions & Qt::CopyAction)
            dwOKEffects |= DROPEFFECT_COPY;
        if (actions & Qt::MoveAction)
            dwOKEffects |= DROPEFFECT_MOVE;
        if (actions & Qt::LinkAction)
            dwOKEffects |= DROPEFFECT_LINK;
    }

    DWORD dwEffect;
    LogRel2(("DnD: Starting drag and drop operation\n", dwOKEffects));
    LogRel3(("DnD: DoDragDrop dwOKEffects=0x%x\n", dwOKEffects));
    HRESULT hr = ::DoDragDrop(pDataObject, pDropSource, dwOKEffects, &dwEffect);
    LogRel3(("DnD: DoDragDrop ended with hr=%Rhrc, dwEffect=%RI32\n", hr, dwEffect));

    if (pDropSource)
        pDropSource->Release();
    if (pDataObject)
        pDataObject->Release();

# else /* !RT_OS_WINDOWS */

    QDrag *pDrag = new QDrag(m_pParent);
    if (!pDrag)
        return VERR_NO_MEMORY;

    /* Note: pMData is transferred to the QDrag object, so no need for deletion. */
    m_pMIMEData = new UIDnDMIMEData(this, lstFormats, defAction, actions);
    if (!m_pMIMEData)
    {
        delete pDrag;
        return VERR_NO_MEMORY;
    }

    /* Inform the MIME data object of any changes in the current action. */
    connect(pDrag, &QDrag::actionChanged,
            m_pMIMEData, &UIDnDMIMEData::sltDropActionChanged);

    /* Invoke this handler as data needs to be retrieved by our derived QMimeData class. */
    connect(m_pMIMEData, &UIDnDMIMEData::sigGetData,
            this, &UIDnDHandler::sltGetData);

    /*
     * Set MIME data object and start the (modal) drag'n drop operation on the host.
     * This does not block Qt's event loop, however (on Windows it would).
     */
    pDrag->setMimeData(m_pMIMEData);
    LogFlowFunc(("Executing modal drag'n drop operation ...\n"));

    Qt::DropAction dropAction;
#  ifdef RT_OS_DARWIN
#    ifdef VBOX_WITH_DRAG_AND_DROP_PROMISES
        dropAction = pDrag->exec(actions, defAction);
#    else
        /* Without having VBOX_WITH_DRAG_AND_DROP_PROMISES enabled drag and drop
         * will not work on OS X! It also requires some handcrafted patches within Qt
         * (which also needs VBOX_WITH_DRAG_AND_DROP_PROMISES set there). */
        dropAction = Qt::IgnoreAction;
        rc = VERR_NOT_SUPPORTED;
#    endif
#  else /* !RT_OS_DARWIN */
    dropAction = pDrag->exec(actions, defAction);
#  endif /* RT_OS_DARWIN */
    LogRel3(("DnD: Ended with dropAction=%ld\n", UIDnDHandler::toVBoxDnDAction(dropAction)));

    /* Note: The UIDnDMimeData object will not be not accessible here anymore,
     *       since QDrag had its ownership and deleted it after the (blocking)
     *       QDrag::exec() call. */

    /* pDrag will be cleaned up by Qt automatically. */

# endif /* !RT_OS_WINDOWS */

    reset();

#ifdef DEBUG_DND_QT
    if (g_pStrmLogQt)
    {
        delete g_pStrmLogQt;
        g_pStrmLogQt = NULL;
    }

    if (pFileDebugQt)
    {
        pFileDebugQt->close();
        delete pFileDebugQt;
    }
#endif /* DEBUG_DND_QT */

#else /* !VBOX_WITH_DRAG_AND_DROP_GH */
    rc = VERR_NOT_SUPPORTED;
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Checks whether a G->H drag'n drop operation is pending.
 *
 * @returns VBox status code.
 * @retval  VERR_NO_DATA if no operation is pending or an error has occurred.
 * @retval  VERR_NOT_SUPPORTED if G->H operations are not supported.
 * @param   screenID            Screen ID to check pending status.
 */
int UIDnDHandler::dragCheckPending(ulong screenID)
{
    int rc;
#ifdef VBOX_WITH_DRAG_AND_DROP_GH
    LogFlowFunc(("screenID=%RU32\n", screenID));

    /**
     * How this works: Source is asking the target if there is any DnD
     * operation pending, when the mouse leaves the guest window. On
     * return there is some info about a running DnD operation
     * (or defaultAction is KDnDAction_Ignore if not). With
     * this information we create a Qt QDrag object with our own QMimeType
     * implementation and call exec.
     *
     * Note: This function *blocks* until the actual drag'n drop operation
     *       has been finished (successfully or not)!
     */

    /* Clear our current data set. */
    m_dataSource.lstFormats.clear();
    m_dataSource.vecActions.clear();

    /* Ask the guest if there is a drag and drop operation pending (on the guest). */
    QVector<QString> vecFormats;
    m_dataSource.defaultAction = m_dndSource.DragIsPending(screenID, vecFormats, m_dataSource.vecActions);
    if (!m_dndSource.isOk())
    {
        msgCenter().cannotDropDataToHost(m_dndSource, m_pParent);
        return VERR_NO_DATA;
    }

    LogRelMax3(10, ("DnD: Default action is: 0x%x\n", m_dataSource.defaultAction));
    LogRelMax3(10, ("DnD: Number of supported guest actions: %d\n", m_dataSource.vecActions.size()));
        for (int i = 0; i < m_dataSource.vecActions.size(); i++)
            LogRelMax3(10, ("DnD: \tAction %d: 0x%x\n", i, m_dataSource.vecActions.at(i)));

    LogRelMax3(10, ("DnD: Number of supported guest formats: %d\n", vecFormats.size()));
        for (int i = 0; i < vecFormats.size(); i++)
        {
            const QString &strFmtGuest = vecFormats.at(i);
            LogRelMax3(10, ("DnD: \tFormat %d: %s\n", i, strFmtGuest.toUtf8().constData()));
        }

    LogFlowFunc(("defaultAction=0x%x, vecFormatsSize=%d, vecActionsSize=%d\n",
                 m_dataSource.defaultAction, vecFormats.size(), m_dataSource.vecActions.size()));

    if (   m_dataSource.defaultAction != KDnDAction_Ignore
        && vecFormats.size())
    {
        for (int i = 0; i < vecFormats.size(); i++)
        {
            const QString &strFormat = vecFormats.at(i);
            m_dataSource.lstFormats << strFormat;
        }

        rc = VINF_SUCCESS; /* There's a valid pending drag and drop operation on the guest. */
    }
    else /* No format data from the guest arrived yet. */
        rc = VERR_NO_DATA;

#else /* !VBOX_WITH_DRAG_AND_DROP_GH */
    RT_NOREF(screenID);
    rc = VERR_NOT_SUPPORTED;
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int UIDnDHandler::dragStart(ulong screenID)
{
    int rc;
#ifdef VBOX_WITH_DRAG_AND_DROP_GH

    RT_NOREF(screenID);

    LogFlowFuncEnter();

    /* Sanity checks. */
    if (   !m_dataSource.lstFormats.size()
        ||  m_dataSource.defaultAction == KDnDAction_Ignore
        || !m_dataSource.vecActions.size())
    {
        return VERR_INVALID_PARAMETER;
    }

    rc = dragStartInternal(m_dataSource.lstFormats,
                           toQtDnDAction(m_dataSource.defaultAction), toQtDnDActions(m_dataSource.vecActions));

#else /* !VBOX_WITH_DRAG_AND_DROP_GH */
    RT_NOREF(screenID);
    rc = VERR_NOT_SUPPORTED;
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int UIDnDHandler::dragStop(ulong screenID)
{
    RT_NOREF(screenID);
    int rc;
#ifdef VBOX_WITH_DRAG_AND_DROP_GH
    reset();
    rc = VINF_SUCCESS;
#else /* !VBOX_WITH_DRAG_AND_DROP_GH */
    rc = VERR_NOT_SUPPORTED;
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */
    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Initializes the drag'n drop UI handler.
 *
 * @returns VBox status code.
 */
int UIDnDHandler::init(void)
{
    int vrc;
#ifdef RT_OS_WINDOWS

# define CASE_INTEGRITY_LEVEL(a_Level) \
    case a_Level: \
        LogRel(("DnD: User Interface Privilege Isolation (UIPI) is running with %s\n", #a_Level)); \
        break;

    /*
     * Assign and log the current process integrity interity level, so that we have a better chance of diagnosing issues
     * when it comes to drag'n drop and UIPI (User Interface Privilege Isolation) -- a lower integrity level process
     * cannot drag'n drop stuff to a higher integrity level one.
     */
    vrc = getProcessIntegrityLevel(&m_dwIntegrityLevel);
    if (RT_SUCCESS(vrc))
    {
        switch (m_dwIntegrityLevel)
        {
            CASE_INTEGRITY_LEVEL(SECURITY_MANDATORY_UNTRUSTED_RID);
            CASE_INTEGRITY_LEVEL(SECURITY_MANDATORY_LOW_RID);
            CASE_INTEGRITY_LEVEL(SECURITY_MANDATORY_MEDIUM_RID);
            CASE_INTEGRITY_LEVEL(SECURITY_MANDATORY_HIGH_RID);
            CASE_INTEGRITY_LEVEL(SECURITY_MANDATORY_SYSTEM_RID);
            CASE_INTEGRITY_LEVEL(SECURITY_MANDATORY_PROTECTED_PROCESS_RID);
            default:
                break;
        }

        if (m_dwIntegrityLevel > SECURITY_MANDATORY_MEDIUM_RID)
            LogRel(("DnD: Warning: The VM process' integrity level is higher than most regular processes on the system. "
                    "This means that drag'n drop most likely will not work with other applications!\n"));
    }
    else
        LogRel(("DnD: Unable to retrieve process integrity level (%Rrc) -- please report this bug!\n", vrc));
# undef CASE_INTEGRITY_LEVEL

#else /* ! RT_OS_WINDOWS */
    vrc = VINF_SUCCESS;
#endif /* RT_OS_WINDOWS */

    return vrc;
}

void UIDnDHandler::reset(void)
{
    LogFlowFuncEnter();

    m_fDataRetrieved = false;

#ifdef RT_OS_WINDOWS
    m_dwIntegrityLevel = 0;
#endif
}

#ifdef RT_OS_WINDOWS
/**
 * Returns the process' current integrity level.
 *
 * @returns VBox status code.
 * @param   pdwIntegrityLevel   Where to return the detected process integrity level on success.
 */
/* static */
int UIDnDHandler::getProcessIntegrityLevel(DWORD *pdwIntegrityLevel)
{
    AssertPtrReturn(pdwIntegrityLevel, VERR_INVALID_POINTER);

    int vrc = VINF_SUCCESS;

# define PRINT_AND_ASSIGN_LAST_ERROR(a_Msg) \
    { \
        dwLastErr = GetLastError(); \
        vrc = RTErrConvertFromWin32(dwLastErr); \
        LogRel(("DnD: %s: %Rrc (%#x)\n", a_Msg, vrc, dwLastErr)); \
    }

    DWORD dwLastErr = 0;

    DWORD  cb;
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        PRINT_AND_ASSIGN_LAST_ERROR("OpenProcessToken failed");
        return vrc;
    }

    if (   !GetTokenInformation(hToken, TokenIntegrityLevel, NULL, 0, &cb)
        && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        PSID_AND_ATTRIBUTES pSidAndAttr = (PSID_AND_ATTRIBUTES)RTMemAlloc(cb);
        if (GetTokenInformation(hToken, TokenIntegrityLevel, pSidAndAttr, cb, &cb))
        {
            *pdwIntegrityLevel = *GetSidSubAuthority(pSidAndAttr->Sid, *GetSidSubAuthorityCount(pSidAndAttr->Sid) - 1U);
        }
        else
            PRINT_AND_ASSIGN_LAST_ERROR("GetTokenInformation(2) failed");
        RTMemFree(pSidAndAttr);
    }
    else if (   GetLastError() == ERROR_INVALID_PARAMETER
             || GetLastError() == ERROR_NOT_SUPPORTED)
    {
        /* Should never show, as we at least require Windows 7 nowadays on Windows hosts. */
        PRINT_AND_ASSIGN_LAST_ERROR("Querying process integrity level not supported");
    }
    else
        PRINT_AND_ASSIGN_LAST_ERROR("GetTokenInformation(1) failed");

# undef PRINT_AND_ASSIGN_LAST_ERROR

    CloseHandle(hToken);
    return vrc;
}
#endif /* RT_OS_WINDOWS */

int UIDnDHandler::retrieveData(      Qt::DropAction    dropAction,
                               const QString          &strMIMEType,
                                     QVector<uint8_t> &vecData)
{
    /** @todo r=andy Locking required? */

    if (!strMIMEType.compare("application/x-qt-mime-type-name", Qt::CaseInsensitive))
        return VINF_SUCCESS;

    int rc;
    if (!m_fDataRetrieved)
    {
        /*
         * Retrieve the actual data from the guest.
         */
        rc = retrieveDataInternal(dropAction, strMIMEType, m_vecData);
        if (RT_FAILURE(rc))
            LogRel3(("DnD: Receiving data failed: %Rrc\n", rc));
        else
            m_fDataRetrieved = true;
    }
    else /* Data already received, supply cached version. */
        rc = VINF_SUCCESS;

    if (RT_SUCCESS(rc))
        vecData = m_vecData;

    return rc;
}

int UIDnDHandler::retrieveData(      Qt::DropAction   dropAction,
                               const QString         &strMIMEType,
                                     QMetaType::Type  vaType,
                                     QVariant        &vaData)
{
    QVector<uint8_t> vecData;
    int rc = retrieveData(dropAction, strMIMEType, vecData);
    if (RT_SUCCESS(rc))
    {
        /* If no/an invalid variant is set, try to guess the variant type.
         * This can happen on OS X. */
        if (vaType == QMetaType::UnknownType)
            vaType = UIDnDMIMEData::getMetaType(strMIMEType);

        rc = UIDnDMIMEData::getDataAsVariant(vecData, strMIMEType, vaType, vaData);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int UIDnDHandler::retrieveDataInternal(      Qt::DropAction    dropAction,
                                       const QString          &strMIMEType,
                                             QVector<uint8_t> &vecData)
{
    LogRel3(("DnD: Retrieving data from guest as '%s' (%d)\n", qPrintable(strMIMEType), dropAction));

    int rc = VINF_SUCCESS;

    /* Indicate to the guest that we have dropped the data on the host.
     * The guest then will initiate the actual "drop" operation into our proxy on the guest. */
    Assert(!m_dndSource.isNull());
    CProgress progress = m_dndSource.Drop(strMIMEType,
                                          UIDnDHandler::toVBoxDnDAction(dropAction));
    LogFlowFunc(("Source: isOk=%RTbool\n", m_dndSource.isOk()));
    if (m_dndSource.isOk())
    {
        /* Send a mouse event with released mouse buttons into the guest that triggers
         * the "drop" event in our proxy window on the guest. */
        m_pMachine->putMouseEvent(0, 0, 0, 0, 0);

        msgCenter().showModalProgressDialog(progress,
                                            tr("Retrieving data ..."), ":/progress_dnd_gh_90px.png",
                                            m_pParent);

        LogFlowFunc(("Progress: fCanceled=%RTbool, fCompleted=%RTbool, isOk=%RTbool, hrc=%Rhrc\n",
                     progress.GetCanceled(), progress.GetCompleted(), progress.isOk(), progress.GetResultCode()));

        if (!progress.GetCanceled())
        {
            rc =   (   progress.isOk()
                    && progress.GetResultCode() == 0)
                 ? VINF_SUCCESS : VERR_GENERAL_FAILURE; /** @todo Fudge; do a GetResultCode() to rc translation. */

            if (RT_SUCCESS(rc))
            {
                /* After we successfully retrieved data from the source we query it from Main. */
                vecData = m_dndSource.ReceiveData(); /** @todo QVector.size() is "int" only!? */
                if (m_dndSource.isOk())
                {
                    if (vecData.isEmpty())
                        rc = VERR_NO_DATA;
                }
                else
                {
                    msgCenter().cannotDropDataToHost(m_dndSource, m_pParent);
                    rc = VERR_GENERAL_FAILURE; /** @todo Fudge; do a GetResultCode() to rc translation. */
                }
            }
            else
                msgCenter().cannotDropDataToHost(progress, m_pParent);
        }
        else /* Don't pop up a message. */
            rc = VERR_CANCELLED;
    }
    else
    {
        msgCenter().cannotDropDataToHost(m_dndSource, m_pParent);
        rc = VERR_GENERAL_FAILURE; /** @todo Fudge; do a GetResultCode() to rc translation. */
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int UIDnDHandler::sltGetData(      Qt::DropAction   dropAction,
                             const QString         &strMIMEType,
                                   QMetaType::Type  vaType,
                                   QVariant        &vaData)
{
    int rc = retrieveData(dropAction, strMIMEType, vaType, vaData);
    LogFlowFuncLeaveRC(rc);
    return rc;
}

/*
 * Drag and Drop helper methods
 */

/**
 * Static helper function to convert a Qt drop action to an internal DnD drop action.
 *
 * @returns Converted internal drop action.
 * @param   action              Qt drop action to convert.
 */
/* static */
KDnDAction UIDnDHandler::toVBoxDnDAction(Qt::DropAction action)
{
    if (action == Qt::CopyAction)
        return KDnDAction_Copy;
    if (action == Qt::MoveAction)
        return KDnDAction_Move;
    if (action == Qt::LinkAction)
        return KDnDAction_Link;

    return KDnDAction_Ignore;
}

/**
 * Static helper function to convert Qt drop actions to internal DnD drop actions.
 *
 * @returns Vector of converted internal drop actions.
 * @param   actions             Qt drop actions to convert.
 */
/* static */
QVector<KDnDAction> UIDnDHandler::toVBoxDnDActions(Qt::DropActions actions)
{
    QVector<KDnDAction> vbActions;
    if (actions.testFlag(Qt::IgnoreAction))
        vbActions << KDnDAction_Ignore;
    if (actions.testFlag(Qt::CopyAction))
        vbActions << KDnDAction_Copy;
    if (actions.testFlag(Qt::MoveAction))
        vbActions << KDnDAction_Move;
    if (actions.testFlag(Qt::LinkAction))
        vbActions << KDnDAction_Link;

    return vbActions;
}

/**
 * Static helper function to convert an internal drop action to a Qt drop action.
 *
 * @returns Converted Qt drop action.
 * @param   actions             Internal drop action to convert.
 */
/* static */
Qt::DropAction UIDnDHandler::toQtDnDAction(KDnDAction action)
{
    Qt::DropAction dropAct = Qt::IgnoreAction;
    if (action == KDnDAction_Copy)
        dropAct = Qt::CopyAction;
    if (action == KDnDAction_Move)
        dropAct = Qt::MoveAction;
    if (action == KDnDAction_Link)
        dropAct = Qt::LinkAction;

    LogFlowFunc(("dropAct=0x%x\n", dropAct));
    return dropAct;
}

/**
 * Static helper function to convert a vector of internal drop actions to Qt drop actions.
 *
 * @returns Converted Qt drop actions.
 * @param   vecActions          Internal drop actions to convert.
 */
/* static */
Qt::DropActions UIDnDHandler::toQtDnDActions(const QVector<KDnDAction> &vecActions)
{
    Qt::DropActions dropActs = Qt::IgnoreAction;
    for (int i = 0; i < vecActions.size(); i++)
    {
        switch (vecActions.at(i))
        {
            case KDnDAction_Ignore:
                dropActs |= Qt::IgnoreAction;
                break;
            case KDnDAction_Copy:
                dropActs |= Qt::CopyAction;
                break;
            case KDnDAction_Move:
                dropActs |= Qt::MoveAction;
                break;
            case KDnDAction_Link:
                dropActs |= Qt::LinkAction;
                break;
            default:
                break;
        }
    }

    LogFlowFunc(("dropActions=0x%x\n", int(dropActs)));
    return dropActs;
}
