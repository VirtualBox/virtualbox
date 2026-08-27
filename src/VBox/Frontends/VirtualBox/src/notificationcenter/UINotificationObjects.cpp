/* $Id: UINotificationObjects.cpp 115134 2026-08-27 15:09:45Z andreas.loeffler@oracle.com $ */
/** @file
 * VBox Qt GUI - Various UINotificationObjects implementations.
 */

/*
 * Copyright (C) 2021-2026 Oracle and/or its affiliates.
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
#include <QDir>
#include <QFileInfo>

/* GUI includes: */
#include "UICommon.h"
#include "UIGlobalSession.h"
#include "UILocalMachineStuff.h"
#include "UINotificationMessage.h"
#include "UINotificationObjects.h"
#include "UITranslator.h"
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
# include "UIDownloaderExtensionPack.h"
# include "UIDownloaderGuestAdditions.h"
#endif

/* COM includes: */
#include "CBooleanFormValue.h"
#include "CChoiceFormValue.h"
#include "CConsole.h"
#include "CHostNetworkInterface.h"
#include "CRangedIntegerFormValue.h"
#include "CRangedInteger64FormValue.h"
#include "CStringFormValue.h"
#include "CVirtualBoxErrorInfo.h"

/* Other VBox stuff: */
#include <iprt/errcore.h>
#include <iprt/string.h>
#ifdef VBOX_WS_NIX
# include <iprt/env.h>
#endif

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumCreate implementation.                                                                     *
*********************************************************************************************************************************/

UINotificationProgressMediumCreate::UINotificationProgressMediumCreate(const CMedium &comTarget,
                                                                       qulonglong uSize,
                                                                       const QVector<KMediumVariant> &variants)
    : m_comTarget(comTarget)
    , m_uSize(uSize)
    , m_variants(variants)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMediumCreate::sltHandleProgressFinished);
}

QString UINotificationProgressMediumCreate::name() const
{
    return UINotificationProgress::tr("Creating medium ...");
}

QString UINotificationProgressMediumCreate::details() const
{
    return UINotificationProgress::tr("<b>Location:</b> %1<br><b>Size:</b> %2").arg(m_strLocation, UITranslator::formatSize(m_uSize));
}

CProgress UINotificationProgressMediumCreate::createProgress(COMResult &comResult)
{
    /* Acquire location: */
    m_strLocation = m_comTarget.GetLocation();
    if (!m_comTarget.isOk())
    {
        /* Store COM result: */
        comResult = m_comTarget;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comTarget.CreateBaseStorage(m_uSize, m_variants);
    /* Store COM result: */
    comResult = m_comTarget;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMediumCreate::sltHandleProgressFinished()
{
    if (m_comTarget.isNotNull() && !m_comTarget.GetId().isNull())
        emit sigMediumCreated(m_comTarget);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumCopy implementation.                                                                       *
*********************************************************************************************************************************/

UINotificationProgressMediumCopy::UINotificationProgressMediumCopy(const CMedium &comSource,
                                                                   const CMedium &comTarget,
                                                                   const QVector<KMediumVariant> &variants,
                                                                   qulonglong uMediumSize)
    : m_comSource(comSource)
    , m_comTarget(comTarget)
    , m_variants(variants)
    , m_uMediumSize(uMediumSize)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMediumCopy::sltHandleProgressFinished);
}

QString UINotificationProgressMediumCopy::name() const
{
    return UINotificationProgress::tr("Copying medium ...");
}

QString UINotificationProgressMediumCopy::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2").arg(m_strSourceLocation, m_strTargetLocation);
}

CProgress UINotificationProgressMediumCopy::createProgress(COMResult &comResult)
{
    /* Acquire locations: */
    m_strSourceLocation = m_comSource.GetLocation();
    if (!m_comSource.isOk())
    {
        /* Store COM result: */
        comResult = m_comSource;
        /* Return progress-wrapper: */
        return CProgress();
    }
    m_strTargetLocation = m_comTarget.GetLocation();
    if (!m_comTarget.isOk())
    {
        /* Store COM result: */
        comResult = m_comTarget;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comSource.ResizeAndCloneTo(m_comTarget, m_uMediumSize, m_variants, CMedium());
    /* Store COM result: */
    comResult = m_comSource;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMediumCopy::sltHandleProgressFinished()
{
    if (m_comTarget.isNotNull() && !m_comTarget.GetId().isNull())
        emit sigMediumCopied(m_comTarget);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumMove implementation.                                                                       *
*********************************************************************************************************************************/

UINotificationProgressMediumMove::UINotificationProgressMediumMove(const CMedium &comMedium,
                                                                   const QString &strLocation)
    : m_comMedium(comMedium)
    , m_strTo(strLocation)
{
}

QString UINotificationProgressMediumMove::name() const
{
    return UINotificationProgress::tr("Moving medium ...");
}

QString UINotificationProgressMediumMove::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2").arg(m_strFrom, m_strTo);
}

CProgress UINotificationProgressMediumMove::createProgress(COMResult &comResult)
{
    /* Acquire location: */
    m_strFrom = m_comMedium.GetLocation();
    if (!m_comMedium.isOk())
    {
        /* Store COM result: */
        comResult = m_comMedium;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMedium.MoveTo(m_strTo);
    /* Store COM result: */
    comResult = m_comMedium;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumResize implementation.                                                                     *
*********************************************************************************************************************************/

UINotificationProgressMediumResize::UINotificationProgressMediumResize(const CMedium &comMedium,
                                                                       qulonglong uOldSize,
                                                                       qulonglong uNewSize)
    : m_comMedium(comMedium)
    , m_uFrom(uOldSize)
    , m_uTo(uNewSize)
{
}

QString UINotificationProgressMediumResize::name() const
{
    return UINotificationProgress::tr("Resizing medium ...");
}

QString UINotificationProgressMediumResize::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2")
                                      .arg(UITranslator::formatSize(m_uFrom),
                                           UITranslator::formatSize(m_uTo));
}

CProgress UINotificationProgressMediumResize::createProgress(COMResult &comResult)
{
    /* Acquire size: */
    m_uFrom = m_comMedium.GetLogicalSize();
    if (!m_comMedium.isOk())
    {
        /* Store COM result: */
        comResult = m_comMedium;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMedium.Resize(m_uTo);
    /* Store COM result: */
    comResult = m_comMedium;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumDeletingStorage implementation.                                                            *
*********************************************************************************************************************************/

UINotificationProgressMediumDeletingStorage::UINotificationProgressMediumDeletingStorage(const CMedium &comMedium)
    : m_comMedium(comMedium)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMediumDeletingStorage::sltHandleProgressFinished);
}

QString UINotificationProgressMediumDeletingStorage::name() const
{
    return UINotificationProgress::tr("Deleting medium storage ...");
}

QString UINotificationProgressMediumDeletingStorage::details() const
{
    return UINotificationProgress::tr("<b>Location:</b> %1").arg(m_strLocation);
}

CProgress UINotificationProgressMediumDeletingStorage::createProgress(COMResult &comResult)
{
    /* Acquire location: */
    m_strLocation = m_comMedium.GetLocation();
    if (!m_comMedium.isOk())
    {
        /* Store COM result: */
        comResult = m_comMedium;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMedium.DeleteStorage();
    /* Store COM result: */
    comResult = m_comMedium;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMediumDeletingStorage::sltHandleProgressFinished()
{
    if (!error().isEmpty())
        emit sigMediumStorageDeleted(m_comMedium);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachineCopy implementation.                                                                      *
*********************************************************************************************************************************/

UINotificationProgressMachineCopy::UINotificationProgressMachineCopy(const CMachine &comSource,
                                                                     const CMachine &comTarget,
                                                                     const KCloneMode &enmCloneMode,
                                                                     const QVector<KCloneOptions> &options)
    : m_comSource(comSource)
    , m_comTarget(comTarget)
    , m_enmCloneMode(enmCloneMode)
    , m_options(options)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachineCopy::sltHandleProgressFinished);
}

QString UINotificationProgressMachineCopy::name() const
{
    return UINotificationProgress::tr("Copying machine ...");
}

QString UINotificationProgressMachineCopy::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2").arg(m_strSourceName, m_strTargetName);
}

CProgress UINotificationProgressMachineCopy::createProgress(COMResult &comResult)
{
    /* Acquire names: */
    m_strSourceName = m_comSource.GetName();
    if (!m_comSource.isOk())
    {
        /* Store COM result: */
        comResult = m_comSource;
        /* Return progress-wrapper: */
        return CProgress();
    }
    m_strTargetName = m_comTarget.GetName();
    if (!m_comTarget.isOk())
    {
        /* Store COM result: */
        comResult = m_comTarget;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comSource.CloneTo(m_comTarget, m_enmCloneMode, m_options);
    /* Store COM result: */
    comResult = m_comSource;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachineCopy::sltHandleProgressFinished()
{
    if (m_comTarget.isNotNull() && !m_comTarget.GetId().isNull())
    {
        /* Register created machine: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        comVBox.RegisterMachine(m_comTarget);
        if (!comVBox.isOk())
            UINotificationMessage::cannotRegisterMachine(comVBox, m_comTarget.GetName());
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachineLaunch implementation.                                                                    *
*********************************************************************************************************************************/

UINotificationProgressMachineLaunch::UINotificationProgressMachineLaunch(const CMachine &comMachine, UILaunchMode enmLaunchMode)
    : m_comMachine(comMachine)
    , m_enmLaunchMode(enmLaunchMode)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachineLaunch::sltHandleProgressFinished);
}

QString UINotificationProgressMachineLaunch::name() const
{
    return UINotificationProgress::tr("Launch VM ...");
}

QString UINotificationProgressMachineLaunch::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressMachineLaunch::createProgress(COMResult &comResult)
{
    /* Acquire VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Open a session thru which we will modify the machine: */
    m_comSession.createInstance(CLSID_Session);
    if (m_comSession.isNull())
    {
        comResult = m_comSession;
        return CProgress();
    }

    /* Configure environment: */
    QVector<QString> astrEnv;
#ifdef VBOX_WS_WIN
    /* Allow started VM process to be foreground window: */
    AllowSetForegroundWindow(ASFW_ANY);
#endif
#ifdef VBOX_WS_NIX
    /* Make sure VM process will start on the same
     * display as the VirtualBox Manager: */
    const char *pDisplay = RTEnvGet("DISPLAY");
    if (pDisplay)
        astrEnv.append(QString("DISPLAY=%1").arg(pDisplay));
    const char *pXauth = RTEnvGet("XAUTHORITY");
    if (pXauth)
        astrEnv.append(QString("XAUTHORITY=%1").arg(pXauth));
#endif
    QString strType;
    switch (m_enmLaunchMode)
    {
        case UILaunchMode_Default:
            strType = "";
            break;
        case UILaunchMode_Separate:
        {
            switch (uiCommon().uiType())
            {
                /* For Manager UI: */
                case UIType_ManagerUI:
                    strType = "separate";
                    break;
                /* For Runtime UI: */
                case UIType_RuntimeUI:
                    strType = uiCommon().isSeparateProcess() ? "headless" : "separate";
                    break;
                default:
                    break;
            }
            break;
        }
        case UILaunchMode_Headless:
            strType = "headless";
            break;
        default:
            AssertFailedReturn(CProgress());
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.LaunchVMProcess(m_comSession, strType, astrEnv);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachineLaunch::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    m_comSession.UnlockMachine();
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachinePowerUp implementation.                                                                   *
*********************************************************************************************************************************/

UINotificationProgressMachinePowerUp::UINotificationProgressMachinePowerUp(const CConsole &comConsole,
                                                                           const QString &strName)
    : m_comConsole(comConsole)
    , m_strName(strName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachinePowerUp::sltHandleProgressFinished);
}

QString UINotificationProgressMachinePowerUp::name() const
{
    return UINotificationProgress::tr("Power VM up ...");
}

QString UINotificationProgressMachinePowerUp::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressMachinePowerUp::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = uiCommon().shouldStartPaused() ? m_comConsole.PowerUpPaused() : m_comConsole.PowerUp();
    /* Store COM result: */
    comResult = m_comConsole;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachinePowerUp::sltHandleProgressFinished()
{
    /* Notify listeners: */
    emit sigMachinePoweredUp(error().isEmpty());
}


/*********************************************************************************************************************************
*   Class UINotificationProgressDranAndDropSend implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressDranAndDropSend::UINotificationProgressDranAndDropSend(const CDnDTarget &comTarget,
                                                                             ulong uScreenId,
                                                                             const QString &strFormat,
                                                                             const QVector<uint8_t> &vecData)
    : m_comTarget(comTarget)
    , m_uScreenId(uScreenId)
    , m_strFormat(strFormat)
    , m_vecData(vecData)
{
}

QString UINotificationProgressDranAndDropSend::name() const
{
    return UINotificationProgress::tr("Sending D&D data ...");
}

QString UINotificationProgressDranAndDropSend::details() const
{
    return UINotificationProgress::tr("<b>Format:</b> %1<br><b>Size:</b> %2").arg(m_strFormat).arg(m_vecData.size());
}

CProgress UINotificationProgressDranAndDropSend::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comTarget.SendData(m_uScreenId, m_strFormat, m_vecData);
    /* Store COM result: */
    comResult = m_comTarget;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressDranAndDropReceive implementation.                                                               *
*********************************************************************************************************************************/

UINotificationProgressDranAndDropReceive::UINotificationProgressDranAndDropReceive(const CDnDSource &comSource,
                                                                                   const QString &strMIMEType,
                                                                                   KDnDAction enmAction)
    : m_comSource(comSource)
    , m_strMIMEType(strMIMEType)
    , m_enmAction(enmAction)
{
}

QString UINotificationProgressDranAndDropReceive::name() const
{
    return UINotificationProgress::tr("Retrieving D&D data ...");
}

QString UINotificationProgressDranAndDropReceive::details() const
{
    return UINotificationProgress::tr("<b>MIME type:</b> %1<br><b>Action:</b> %2").arg(m_strMIMEType).arg((int)m_enmAction);
}

CProgress UINotificationProgressDranAndDropReceive::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comSource.Drop(m_strMIMEType, m_enmAction);

    // WORKAROUND:
    // We've to notify UIMachine to pass event to guest,
    // is it really so important?
    if (m_comSource.isOk())
        emit sigProgressCreated();

    /* Store COM result: */
    comResult = m_comSource;
    /* Return progress-wrapper: */
    return comProgress;
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS


/*********************************************************************************************************************************
*   Class UINotificationProgressSharedClipboardTransfer implementation.                                                          *
*********************************************************************************************************************************/

/** Returns a translated transfer direction name.
  * @returns Transfer direction name.
  * @param  enmDirection  Brings the direction to name. */
static QString shclTransferDirectionName(KClipboardTransferDirection enmDirection)
{
    switch (enmDirection)
    {
        case KClipboardTransferDirection_ToGuest:
            return UINotificationProgress::tr("to guest");
        case KClipboardTransferDirection_ToHost:
            return UINotificationProgress::tr("to host");
        case KClipboardTransferDirection_Any:
            return UINotificationProgress::tr("any direction");
        default:
            break;
    }
    return UINotificationProgress::tr("unknown direction");
}


/** Returns a translated clipboard source name.
  * @returns Clipboard source name.
  * @param  enmSource  Brings the source to name. */
static QString shclTransferSourceName(KClipboardSource enmSource)
{
    switch (enmSource)
    {
        case KClipboardSource_Host:
            return UINotificationProgress::tr("host");
        case KClipboardSource_Guest:
            return UINotificationProgress::tr("guest");
        case KClipboardSource_Remote:
            return UINotificationProgress::tr("remote");
        case KClipboardSource_Custom:
            return UINotificationProgress::tr("custom");
        default:
            break;
    }
    return UINotificationProgress::tr("unknown");
}


/**
 * Returns the symbolic IPRT result associated with a failed transfer.
 *
 * @returns                     Symbolic IPRT result, or an empty string if no known failure result is
 *                              available.
 * @param   comTransfer         Brings the clipboard transfer to inspect.
 */
static QString shclTransferRuntimeErrorName(const CClipboardTransfer &comTransfer)
{
    if (comTransfer.isNull())
        return QString();

    CProgress comProgress = comTransfer.GetProgress();
    if (!comTransfer.isOk() || comProgress.isNull())
        return QString();

    const BOOL fCompleted = comProgress.GetCompleted();
    if (!comProgress.isOk() || !fCompleted)
        return QString();

    const LONG hrcResult = comProgress.GetResultCode();
    if (!comProgress.isOk() || SUCCEEDED(hrcResult))
        return QString();

    CVirtualBoxErrorInfo comErrorInfo = comProgress.GetErrorInfo();
    if (!comProgress.isOk() || comErrorInfo.isNull())
        return QString();

    const int32_t vrcResult = (int32_t)comErrorInfo.GetResultDetail();
    if (!comErrorInfo.isOk() || !RT_FAILURE(vrcResult) || !RTErrIsKnown(vrcResult))
        return QString();

    char *pszResult = RTStrAPrintf2("%Rrc", vrcResult);
    if (!pszResult)
        return QString();
    const QString strResult = QString::fromUtf8(pszResult);
    RTStrFree(pszResult);
    return strResult;
}


UINotificationProgressSharedClipboardTransfer::UINotificationProgressSharedClipboardTransfer(const CClipboardTransfer &comTransfer)
    : m_comTransfer(comTransfer)
{
}

QString UINotificationProgressSharedClipboardTransfer::name() const
{
    return UINotificationProgress::tr("Shared clipboard transfer ...");
}

QString UINotificationProgressSharedClipboardTransfer::details() const
{
    if (m_comTransfer.isNull())
        return QString();

    const ULONG uId = m_comTransfer.GetId();
    const KClipboardTransferDirection enmDirection = m_comTransfer.GetDirection();
    const KClipboardSource enmSource = m_comTransfer.GetSource();
    if (!m_comTransfer.isOk())
        return UINotificationProgress::tr("<b>Transfer:</b> unavailable");

    QString strDetails = UINotificationProgress::tr("<b>Transfer ID:</b> %1<br><b>Direction:</b> %2<br><b>Source:</b> %3")
                                                     .arg(uId)
                                                     .arg(shclTransferDirectionName(enmDirection))
                                                     .arg(shclTransferSourceName(enmSource));
    const QString strRuntimeError = shclTransferRuntimeErrorName(m_comTransfer);
    if (!strRuntimeError.isEmpty())
        strDetails += UINotificationProgress::tr("<br><b>IPRT Error:</b> %1").arg(strRuntimeError.toHtmlEscaped());
    return strDetails;
}

CProgress UINotificationProgressSharedClipboardTransfer::createProgress(COMResult &comResult)
{
    CProgress comProgress;
    if (m_comTransfer.isNotNull())
        comProgress = m_comTransfer.GetProgress();
    comResult = m_comTransfer;
    return comProgress;
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/*********************************************************************************************************************************
*   Class UINotificationProgressMachineMove implementation.                                                                      *
*********************************************************************************************************************************/

UINotificationProgressMachineMove::UINotificationProgressMachineMove(const QUuid &uId,
                                                                     const QString &strDestination,
                                                                     const QString &strType)
    : m_uId(uId)
    , m_strDestination(QDir::toNativeSeparators(strDestination))
    , m_strType(strType)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachineMove::sltHandleProgressFinished);
}

QString UINotificationProgressMachineMove::name() const
{
    return UINotificationProgress::tr("Moving machine ...");
}

QString UINotificationProgressMachineMove::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2").arg(m_strSource, m_strDestination);
}

CProgress UINotificationProgressMachineMove::createProgress(COMResult &comResult)
{
    /* Open a session thru which we will modify the machine: */
    m_comSession = openSession(m_uId, KLockType_Write);
    if (m_comSession.isNull())
        return CProgress();

    /* Get session machine: */
    CMachine comMachine = m_comSession.GetMachine();
    if (!m_comSession.isOk())
    {
        comResult = m_comSession;
        m_comSession.UnlockMachine();
        return CProgress();
    }

    /* Acquire VM source: */
    const QString strSettingFilePath = comMachine.GetSettingsFilePath();
    if (!comMachine.isOk())
    {
        comResult = comMachine;
        m_comSession.UnlockMachine();
        return CProgress();
    }
    QDir parentDir = QFileInfo(strSettingFilePath).absoluteDir();
    parentDir.cdUp();
    m_strSource = QDir::toNativeSeparators(parentDir.absolutePath());

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.MoveTo(m_strDestination, m_strType);
    /* Store COM result: */
    comResult = comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachineMove::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    m_comSession.UnlockMachine();
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachineSaveState implementation.                                                                 *
*********************************************************************************************************************************/

UINotificationProgressMachineSaveState::UINotificationProgressMachineSaveState(const CMachine &comMachine)
    : m_comMachine(comMachine)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachineSaveState::sltHandleProgressFinished);
}

QString UINotificationProgressMachineSaveState::name() const
{
    return UINotificationProgress::tr("Saving VM state ...");
}

QString UINotificationProgressMachineSaveState::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressMachineSaveState::createProgress(COMResult &comResult)
{
    /* Acquire VM id: */
    const QUuid uId = m_comMachine.GetId();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Prepare machine to save: */
    CMachine comMachine = m_comMachine;

    /* For Manager UI: */
    switch (uiCommon().uiType())
    {
        case UIType_ManagerUI:
        {
            /* Open a session thru which we will modify the machine: */
            m_comSession = openExistingSession(uId);
            if (m_comSession.isNull())
                return CProgress();

            /* Get session machine: */
            comMachine = m_comSession.GetMachine();
            if (!m_comSession.isOk())
            {
                comResult = m_comSession;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            /* Get machine state: */
            const KMachineState enmState = comMachine.GetState();
            if (!comMachine.isOk())
            {
                comResult = comMachine;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            /* If VM isn't yet paused: */
            if (enmState != KMachineState_Paused)
            {
                /* Get session console: */
                CConsole comConsole = m_comSession.GetConsole();
                if (!m_comSession.isOk())
                {
                    comResult = m_comSession;
                    m_comSession.UnlockMachine();
                    return CProgress();
                }

                /* Pause VM first: */
                comConsole.Pause();
                if (!comConsole.isOk())
                {
                    comResult = comConsole;
                    m_comSession.UnlockMachine();
                    return CProgress();
                }
            }

            break;
        }
        default:
            break;
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.SaveState();
    /* Store COM result: */
    comResult = comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachineSaveState::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    if (m_comSession.isNotNull())
        m_comSession.UnlockMachine();

    /* Notifies listeners: */
    emit sigMachineStateSaved(error().isEmpty());
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachinePowerOff implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressMachinePowerOff::UINotificationProgressMachinePowerOff(const CMachine &comMachine,
                                                                             const CConsole &comConsole /* = CConsole() */,
                                                                             bool fIncludingDiscard /* = false */)
    : m_comMachine(comMachine)
    , m_comConsole(comConsole)
    , m_fIncludingDiscard(fIncludingDiscard)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachinePowerOff::sltHandleProgressFinished);
}

QString UINotificationProgressMachinePowerOff::name() const
{
    return UINotificationProgress::tr("Powering VM off ...");
}

QString UINotificationProgressMachinePowerOff::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressMachinePowerOff::createProgress(COMResult &comResult)
{
    /* Prepare machine to power off: */
    CMachine comMachine = m_comMachine;
    /* Prepare console to power off: */
    CConsole comConsole = m_comConsole;

    /* For Manager UI: */
    switch (uiCommon().uiType())
    {
        case UIType_ManagerUI:
        {
            /* Acquire VM id: */
            const QUuid uId = comMachine.GetId();
            if (!comMachine.isOk())
            {
                comResult = comMachine;
                return CProgress();
            }

            /* Open a session thru which we will modify the machine: */
            m_comSession = openExistingSession(uId);
            if (m_comSession.isNull())
                return CProgress();

            /* Get session machine: */
            comMachine = m_comSession.GetMachine();
            if (!m_comSession.isOk())
            {
                comResult = m_comSession;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            /* Get session console: */
            comConsole = m_comSession.GetConsole();
            if (!m_comSession.isOk())
            {
                comResult = m_comSession;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            break;
        }
        default:
            break;
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comConsole.PowerDown();

    /* For Runtime UI: */
    switch (uiCommon().uiType())
    {
        case UIType_RuntimeUI:
        {
            /* Check the console state, it might be already gone: */
            if (!comConsole.isNull())
            {
                /* This can happen if VBoxSVC is not running: */
                COMResult res(comConsole);
                if (FAILED_DEAD_INTERFACE(res.rc()))
                    return CProgress();
            }

            break;
        }
        default:
            break;
    }

    /* Store COM result: */
    comResult = comConsole;

    /* Acquire VM name, no error checks, too late: */
    m_strName = comMachine.GetName();

    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachinePowerOff::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    if (m_comSession.isNotNull())
        m_comSession.UnlockMachine();

    /* Notifies listeners: */
    emit sigMachinePoweredOff(error().isEmpty(), m_fIncludingDiscard);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachineMediaRemove implementation.                                                               *
*********************************************************************************************************************************/

UINotificationProgressMachineMediaRemove::UINotificationProgressMachineMediaRemove(const CMachine &comMachine,
                                                                                   const CMediumVector &media)
    : m_comMachine(comMachine)
    , m_media(media)
{
}

QString UINotificationProgressMachineMediaRemove::name() const
{
    return UINotificationProgress::tr("Removing machine media ...");
}

QString UINotificationProgressMachineMediaRemove::details() const
{
    return UINotificationProgress::tr("<b>Machine Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressMachineMediaRemove::createProgress(COMResult &comResult)
{
    /* Acquire names: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.DeleteConfig(m_media);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressVFSExplorerUpdate implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressVFSExplorerUpdate::UINotificationProgressVFSExplorerUpdate(const CVFSExplorer &comExplorer)
    : m_comExplorer(comExplorer)
{
}

QString UINotificationProgressVFSExplorerUpdate::name() const
{
    return UINotificationProgress::tr("Updating VFS explorer ...");
}

QString UINotificationProgressVFSExplorerUpdate::details() const
{
    return UINotificationProgress::tr("<b>Path:</b> %1").arg(m_strPath);
}

CProgress UINotificationProgressVFSExplorerUpdate::createProgress(COMResult &comResult)
{
    /* Acquire path: */
    m_strPath = m_comExplorer.GetPath();
    if (!m_comExplorer.isOk())
    {
        /* Store COM result: */
        comResult = m_comExplorer;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comExplorer.Update();
    /* Store COM result: */
    comResult = m_comExplorer;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressVFSExplorerFilesRemove implementation.                                                           *
*********************************************************************************************************************************/

UINotificationProgressVFSExplorerFilesRemove::UINotificationProgressVFSExplorerFilesRemove(const CVFSExplorer &comExplorer,
                                                                                           const QVector<QString> &files)
    : m_comExplorer(comExplorer)
    , m_files(files)
{
}

QString UINotificationProgressVFSExplorerFilesRemove::name() const
{
    return UINotificationProgress::tr("Removing VFS explorer files ...");
}

QString UINotificationProgressVFSExplorerFilesRemove::details() const
{
    return UINotificationProgress::tr("<b>Path:</b> %1<br><b>Files:</b> %2")
                                      .arg(m_strPath)
                                      .arg(QStringList(m_files.toList()).join(", "));
}

CProgress UINotificationProgressVFSExplorerFilesRemove::createProgress(COMResult &comResult)
{
    /* Acquire path: */
    m_strPath = m_comExplorer.GetPath();
    if (!m_comExplorer.isOk())
    {
        /* Store COM result: */
        comResult = m_comExplorer;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comExplorer.Remove(m_files);
    /* Store COM result: */
    comResult = m_comExplorer;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressSubnetSelectionVSDFormCreate implementation.                                                     *
*********************************************************************************************************************************/

UINotificationProgressSubnetSelectionVSDFormCreate::UINotificationProgressSubnetSelectionVSDFormCreate(const CCloudClient &comClient,
                                                                                                       const CVirtualSystemDescription &comVSD,
                                                                                                       const QString &strProviderShortName,
                                                                                                       const QString &strProfileName)
    : m_comClient(comClient)
    , m_comVSD(comVSD)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSubnetSelectionVSDFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressSubnetSelectionVSDFormCreate::name() const
{
    return UINotificationProgress::tr("Creating subnet selection VSD form ...");
}

QString UINotificationProgressSubnetSelectionVSDFormCreate::details() const
{
    return UINotificationProgress::tr("<b>Provider:</b> %1<br><b>Profile:</b> %2")
                                      .arg(m_strProviderShortName, m_strProfileName);
}

CProgress UINotificationProgressSubnetSelectionVSDFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.GetSubnetSelectionForm(m_comVSD, m_comVSDForm);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressSubnetSelectionVSDFormCreate::sltHandleProgressFinished()
{
    if (m_comVSDForm.isNotNull())
        emit sigVSDFormCreated(m_comVSDForm);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressLaunchVSDFormCreate implementation.                                                              *
*********************************************************************************************************************************/

UINotificationProgressLaunchVSDFormCreate::UINotificationProgressLaunchVSDFormCreate(const CCloudClient &comClient,
                                                                                     const CVirtualSystemDescription &comVSD,
                                                                                     const QString &strProviderShortName,
                                                                                     const QString &strProfileName)
    : m_comClient(comClient)
    , m_comVSD(comVSD)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressLaunchVSDFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressLaunchVSDFormCreate::name() const
{
    return UINotificationProgress::tr("Creating launch VSD form ...");
}

QString UINotificationProgressLaunchVSDFormCreate::details() const
{
    return UINotificationProgress::tr("<b>Provider:</b> %1<br><b>Profile:</b> %2")
                                      .arg(m_strProviderShortName, m_strProfileName);
}

CProgress UINotificationProgressLaunchVSDFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.GetLaunchDescriptionForm(m_comVSD, m_comVSDForm);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressLaunchVSDFormCreate::sltHandleProgressFinished()
{
    if (m_comVSDForm.isNotNull())
        emit sigVSDFormCreated(m_comVSDForm);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressExportVSDFormCreate implementation.                                                              *
*********************************************************************************************************************************/

UINotificationProgressExportVSDFormCreate::UINotificationProgressExportVSDFormCreate(const CCloudClient &comClient,
                                                                                     const CVirtualSystemDescription &comVSD)
    : m_comClient(comClient)
    , m_comVSD(comVSD)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressExportVSDFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressExportVSDFormCreate::name() const
{
    return UINotificationProgress::tr("Creating export VSD form ...");
}

QString UINotificationProgressExportVSDFormCreate::details() const
{
    return QString();
}

CProgress UINotificationProgressExportVSDFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.GetExportDescriptionForm(m_comVSD, m_comVSDForm);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressExportVSDFormCreate::sltHandleProgressFinished()
{
    if (m_comVSDForm.isNotNull())
        emit sigVSDFormCreated(QVariant::fromValue(m_comVSDForm));
}


/*********************************************************************************************************************************
*   Class UINotificationProgressImportVSDFormCreate implementation.                                                              *
*********************************************************************************************************************************/

UINotificationProgressImportVSDFormCreate::UINotificationProgressImportVSDFormCreate(const CCloudClient &comClient,
                                                                                     const CVirtualSystemDescription &comVSD)
    : m_comClient(comClient)
    , m_comVSD(comVSD)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressImportVSDFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressImportVSDFormCreate::name() const
{
    return UINotificationProgress::tr("Creating import VSD form ...");
}

QString UINotificationProgressImportVSDFormCreate::details() const
{
    return QString();
}

CProgress UINotificationProgressImportVSDFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.GetImportDescriptionForm(m_comVSD, m_comVSDForm);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressImportVSDFormCreate::sltHandleProgressFinished()
{
    if (m_comVSDForm.isNotNull())
        emit sigVSDFormCreated(QVariant::fromValue(m_comVSDForm));
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudImageList implementation.                                                                   *
*********************************************************************************************************************************/

UINotificationProgressCloudImageList::UINotificationProgressCloudImageList(const CCloudClient &comClient,
                                                                           const QVector<KCloudImageState> &cloudImageStates)
    : m_comClient(comClient)
    , m_cloudImageStates(cloudImageStates)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudImageList::sltHandleProgressFinished);
}

QString UINotificationProgressCloudImageList::name() const
{
    return UINotificationProgress::tr("Listing cloud images ...");
}

QString UINotificationProgressCloudImageList::details() const
{
    return QString();
}

CProgress UINotificationProgressCloudImageList::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.ListImages(m_cloudImageStates, m_comNames, m_comIds);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudImageList::sltHandleProgressFinished()
{
    if (m_comNames.isNotNull() && m_comIds.isNotNull())
    {
        emit sigImageNamesReceived(QVariant::fromValue(m_comNames));
        emit sigImageIdsReceived(QVariant::fromValue(m_comIds));
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudSourceBootVolumeList implementation.                                                        *
*********************************************************************************************************************************/

UINotificationProgressCloudSourceBootVolumeList::UINotificationProgressCloudSourceBootVolumeList(const CCloudClient &comClient)
    : m_comClient(comClient)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudSourceBootVolumeList::sltHandleProgressFinished);
}

QString UINotificationProgressCloudSourceBootVolumeList::name() const
{
    return UINotificationProgress::tr("Listing cloud source boot volumes ...");
}

QString UINotificationProgressCloudSourceBootVolumeList::details() const
{
    return QString();
}

CProgress UINotificationProgressCloudSourceBootVolumeList::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.ListSourceBootVolumes(m_comNames, m_comIds);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudSourceBootVolumeList::sltHandleProgressFinished()
{
    if (m_comNames.isNotNull() && m_comIds.isNotNull())
    {
        emit sigImageNamesReceived(QVariant::fromValue(m_comNames));
        emit sigImageIdsReceived(QVariant::fromValue(m_comIds));
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudInstanceList implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressCloudInstanceList::UINotificationProgressCloudInstanceList(const CCloudClient &comClient)
    : m_comClient(comClient)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudInstanceList::sltHandleProgressFinished);
}

QString UINotificationProgressCloudInstanceList::name() const
{
    return UINotificationProgress::tr("Listing cloud instances ...");
}

QString UINotificationProgressCloudInstanceList::details() const
{
    return QString();
}

CProgress UINotificationProgressCloudInstanceList::createProgress(COMResult &comResult)
{
    /* Currently we are interested in Running and Stopped VMs only: */
    const QVector<KCloudMachineState> cloudMachineStates  = QVector<KCloudMachineState>()
                                                         << KCloudMachineState_Running
                                                         << KCloudMachineState_Stopped;

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.ListInstances(cloudMachineStates, m_comNames, m_comIds);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudInstanceList::sltHandleProgressFinished()
{
    if (m_comNames.isNotNull() && m_comIds.isNotNull())
    {
        emit sigImageNamesReceived(QVariant::fromValue(m_comNames));
        emit sigImageIdsReceived(QVariant::fromValue(m_comIds));
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudSourceInstanceList implementation.                                                          *
*********************************************************************************************************************************/

UINotificationProgressCloudSourceInstanceList::UINotificationProgressCloudSourceInstanceList(const CCloudClient &comClient)
    : m_comClient(comClient)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudSourceInstanceList::sltHandleProgressFinished);
}

QString UINotificationProgressCloudSourceInstanceList::name() const
{
    return UINotificationProgress::tr("Listing cloud source instances ...");
}

QString UINotificationProgressCloudSourceInstanceList::details() const
{
    return QString();
}

CProgress UINotificationProgressCloudSourceInstanceList::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.ListSourceInstances(m_comNames, m_comIds);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudSourceInstanceList::sltHandleProgressFinished()
{
    if (m_comNames.isNotNull() && m_comIds.isNotNull())
    {
        emit sigImageNamesReceived(QVariant::fromValue(m_comNames));
        emit sigImageIdsReceived(QVariant::fromValue(m_comIds));
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineAdd implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineAdd::UINotificationProgressCloudMachineAdd(const CCloudClient &comClient,
                                                                             const CCloudMachine &comMachine,
                                                                             const QString &strInstanceName,
                                                                             const QString &strProviderShortName,
                                                                             const QString &strProfileName)
    : m_comClient(comClient)
    , m_comMachine(comMachine)
    , m_strInstanceName(strInstanceName)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudMachineAdd::sltHandleProgressFinished);
}

QString UINotificationProgressCloudMachineAdd::name() const
{
    return UINotificationProgress::tr("Adding cloud VM ...");
}

QString UINotificationProgressCloudMachineAdd::details() const
{
    return UINotificationProgress::tr("<b>Provider:</b> %1<br><b>Profile:</b> %2<br><b>Instance Name:</b> %3")
                                      .arg(m_strProviderShortName, m_strProfileName, m_strInstanceName);
}

CProgress UINotificationProgressCloudMachineAdd::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.AddCloudMachine(m_strInstanceName, m_comMachine);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudMachineAdd::sltHandleProgressFinished()
{
    if (m_comMachine.isNotNull() && !m_comMachine.GetId().isNull())
        emit sigCloudMachineAdded(m_strProviderShortName, m_strProfileName, m_comMachine);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineCreate implementation.                                                               *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineCreate::UINotificationProgressCloudMachineCreate(const CCloudClient &comClient,
                                                                                   const CCloudMachine &comMachine,
                                                                                   const CVirtualSystemDescription &comVSD,
                                                                                   const QString &strProviderShortName,
                                                                                   const QString &strProfileName)
    : m_comClient(comClient)
    , m_comMachine(comMachine)
    , m_comVSD(comVSD)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudMachineCreate::sltHandleProgressFinished);
}

QString UINotificationProgressCloudMachineCreate::name() const
{
    return UINotificationProgress::tr("Creating cloud VM ...");
}

QString UINotificationProgressCloudMachineCreate::details() const
{
    return UINotificationProgress::tr("<b>Provider:</b> %1<br><b>Profile:</b> %2<br><b>VM Name:</b> %3")
                                      .arg(m_strProviderShortName, m_strProfileName, m_strName);
}

CProgress UINotificationProgressCloudMachineCreate::createProgress(COMResult &comResult)
{
    /* Parse cloud VM name: */
    QVector<KVirtualSystemDescriptionType> types;
    QVector<QString> refs, origValues, configValues, extraConfigValues;
    m_comVSD.GetDescriptionByType(KVirtualSystemDescriptionType_Name, types,
                                  refs, origValues, configValues, extraConfigValues);
    if (!origValues.isEmpty())
        m_strName = origValues.first();

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.CreateCloudMachine(m_comVSD, m_comMachine);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudMachineCreate::sltHandleProgressFinished()
{
    if (m_comMachine.isNotNull() && !m_comMachine.GetId().isNull())
        emit sigCloudMachineCreated(m_strProviderShortName, m_strProfileName, m_comMachine);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineRemove implementation.                                                               *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineRemove::UINotificationProgressCloudMachineRemove(const CCloudMachine &comMachine,
                                                                                   bool fFullRemoval,
                                                                                   const QString &strProviderShortName,
                                                                                   const QString &strProfileName)
    : m_comMachine(comMachine)
    , m_fFullRemoval(fFullRemoval)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudMachineRemove::sltHandleProgressFinished);
}

QString UINotificationProgressCloudMachineRemove::name() const
{
    return   m_fFullRemoval
           ? UINotificationProgress::tr("Deleting cloud VM files ...")
           : UINotificationProgress::tr("Removing cloud VM ...");
}

QString UINotificationProgressCloudMachineRemove::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineRemove::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_fFullRemoval
                          ? m_comMachine.Remove()
                          : m_comMachine.Unregister();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudMachineRemove::sltHandleProgressFinished()
{
    if (error().isEmpty())
        emit sigCloudMachineRemoved(m_strProviderShortName, m_strProfileName, m_strName);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineClone implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineClone::UINotificationProgressCloudMachineClone(const CCloudClient &comClient,
                                                                                 const CCloudMachine &comMachine,
                                                                                 const QString &strCloneName)
    : m_comClient(comClient)
    , m_comMachine(comMachine)
    , m_strCloneName(strCloneName)
{
}

QString UINotificationProgressCloudMachineClone::name() const
{
    return UINotificationProgress::tr("Cloning cloud VM ...");
}

QString UINotificationProgressCloudMachineClone::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineClone::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM internal id: */
    m_strId = m_comMachine.GetCloudId();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CCloudMachine comCloneMachine;
    CProgress comProgress = m_comClient.CloneInstance(m_strId, m_strCloneName, comCloneMachine);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineReset implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineReset::UINotificationProgressCloudMachineReset(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachineReset::name() const
{
    return UINotificationProgress::tr("Resetting cloud VM ...");
}

QString UINotificationProgressCloudMachineReset::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineReset::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.Reset();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachinePowerUp implementation.                                                              *
*********************************************************************************************************************************/

UINotificationProgressCloudMachinePowerUp::UINotificationProgressCloudMachinePowerUp(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachinePowerUp::name() const
{
    return   UINotificationProgress::tr("Powering cloud VM up ...");
}

QString UINotificationProgressCloudMachinePowerUp::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachinePowerUp::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.PowerUp();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachinePowerOff implementation.                                                             *
*********************************************************************************************************************************/

UINotificationProgressCloudMachinePowerOff::UINotificationProgressCloudMachinePowerOff(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachinePowerOff::name() const
{
    return   UINotificationProgress::tr("Powering cloud VM off ...");
}

QString UINotificationProgressCloudMachinePowerOff::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachinePowerOff::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.PowerDown();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineShutdown implementation.                                                             *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineShutdown::UINotificationProgressCloudMachineShutdown(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachineShutdown::name() const
{
    return   UINotificationProgress::tr("Shutting cloud VM down ...");
}

QString UINotificationProgressCloudMachineShutdown::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineShutdown::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.Shutdown();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineTerminate implementation.                                                            *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineTerminate::UINotificationProgressCloudMachineTerminate(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachineTerminate::name() const
{
    return   UINotificationProgress::tr("Terminating cloud VM ...");
}

QString UINotificationProgressCloudMachineTerminate::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineTerminate::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.Terminate();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineSettingsFormCreate implementation.                                                   *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineSettingsFormCreate::UINotificationProgressCloudMachineSettingsFormCreate(const CCloudMachine &comMachine,
                                                                                                           const QString &strMachineName)
    : m_comMachine(comMachine)
    , m_strMachineName(strMachineName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudMachineSettingsFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressCloudMachineSettingsFormCreate::name() const
{
    return UINotificationProgress::tr("Creating cloud VM settings form ...");
}

QString UINotificationProgressCloudMachineSettingsFormCreate::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strMachineName);
}

CProgress UINotificationProgressCloudMachineSettingsFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.GetSettingsForm(m_comForm);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudMachineSettingsFormCreate::sltHandleProgressFinished()
{
    if (m_comForm.isNotNull())
        emit sigSettingsFormCreated(QVariant::fromValue(m_comForm));
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineSettingsFormApply implementation.                                                    *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineSettingsFormApply::UINotificationProgressCloudMachineSettingsFormApply(const CForm &comForm,
                                                                                                         const QString &strMachineName)
    : m_comForm(comForm)
    , m_strMachineName(strMachineName)
{
}

QString UINotificationProgressCloudMachineSettingsFormApply::name() const
{
    return UINotificationProgress::tr("Applying cloud VM settings form ...");
}

QString UINotificationProgressCloudMachineSettingsFormApply::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strMachineName);
}

CProgress UINotificationProgressCloudMachineSettingsFormApply::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comForm.Apply();
    /* Store COM result: */
    comResult = m_comForm;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudConsoleConnectionCreate implementation.                                                     *
*********************************************************************************************************************************/

UINotificationProgressCloudConsoleConnectionCreate::UINotificationProgressCloudConsoleConnectionCreate(const CCloudMachine &comMachine,
                                                                                                       const QString &strPublicKey)
    : m_comMachine(comMachine)
    , m_strPublicKey(strPublicKey)
{
}

QString UINotificationProgressCloudConsoleConnectionCreate::name() const
{
    return UINotificationProgress::tr("Creating cloud console connection ...");
}

QString UINotificationProgressCloudConsoleConnectionCreate::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudConsoleConnectionCreate::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.CreateConsoleConnection(m_strPublicKey);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudConsoleConnectionDelete implementation.                                                     *
*********************************************************************************************************************************/

UINotificationProgressCloudConsoleConnectionDelete::UINotificationProgressCloudConsoleConnectionDelete(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudConsoleConnectionDelete::name() const
{
    return UINotificationProgress::tr("Deleting cloud console connection ...");
}

QString UINotificationProgressCloudConsoleConnectionDelete::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudConsoleConnectionDelete::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.DeleteConsoleConnection();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudConsoleLogAcquire implementation.                                                           *
*********************************************************************************************************************************/

UINotificationProgressCloudConsoleLogAcquire::UINotificationProgressCloudConsoleLogAcquire(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudConsoleLogAcquire::sltHandleProgressFinished);
}

QString UINotificationProgressCloudConsoleLogAcquire::name() const
{
    return UINotificationProgress::tr("Acquire cloud console log ...");
}

QString UINotificationProgressCloudConsoleLogAcquire::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudConsoleLogAcquire::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.GetConsoleHistory(m_comStream);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudConsoleLogAcquire::sltHandleProgressFinished()
{
    /* Read the byte array: */
    QVector<BYTE> byteArray;
    while (true)
    {
        const QVector<BYTE> byteChunk = m_comStream.Read(64 * _1K, 0);
        if (byteChunk.size() == 0)
            break;
        byteArray += byteChunk;
    }
    if (byteArray.size() == 0)
        return;

    /* Convert it to string and send away: */
    const QString strLog = QString::fromUtf8(reinterpret_cast<const char *>(byteArray.data()), byteArray.size());
    emit sigLogRead(m_strName, strLog);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressSnapshotTake implementation.                                                                     *
*********************************************************************************************************************************/

UINotificationProgressSnapshotTake::UINotificationProgressSnapshotTake(const CMachine &comMachine,
                                                                       const QString &strSnapshotName,
                                                                       const QString &strSnapshotDescription)
    : m_comMachine(comMachine)
    , m_strSnapshotName(strSnapshotName)
    , m_strSnapshotDescription(strSnapshotDescription)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSnapshotTake::sltHandleProgressFinished);
}

QString UINotificationProgressSnapshotTake::name() const
{
    return UINotificationProgress::tr("Taking snapshot ...");
}

QString UINotificationProgressSnapshotTake::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1<br><b>Snapshot Name:</b> %2").arg(m_strMachineName, m_strSnapshotName);
}

CProgress UINotificationProgressSnapshotTake::createProgress(COMResult &comResult)
{
    /* Acquire VM id: */
    const QUuid uId = m_comMachine.GetId();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire VM name: */
    m_strMachineName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Get session machine: */
    CMachine comMachine;

    /* For Manager UI: */
    switch (uiCommon().uiType())
    {
        case UIType_ManagerUI:
        {
            /* Acquire session state: */
            const KSessionState enmSessionState = m_comMachine.GetSessionState();
            if (!m_comMachine.isOk())
            {
                comResult = m_comMachine;
                return CProgress();
            }

            /* Open a session thru which we will modify the machine: */
            if (enmSessionState != KSessionState_Unlocked)
                m_comSession = openExistingSession(uId);
            else
                m_comSession = openSession(uId);
            if (m_comSession.isNull())
                return CProgress();

            /* Get session machine: */
            comMachine = m_comSession.GetMachine();
            if (!m_comSession.isOk())
            {
                comResult = m_comSession;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            break;
        }
        case UIType_RuntimeUI:
        {
            /* Get passed machine: */
            comMachine = m_comMachine;

            break;
        }
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.TakeSnapshot(m_strSnapshotName,
                                                    m_strSnapshotDescription,
                                                    true, m_uSnapshotId);
    /* Store COM result: */
    comResult = comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressSnapshotTake::sltHandleProgressFinished()
{
    if (m_comSession.isNotNull())
        m_comSession.UnlockMachine();

    if (!m_uSnapshotId.isNull())
        emit sigSnapshotTaken(QVariant::fromValue(m_uSnapshotId));
}


/*********************************************************************************************************************************
*   Class UINotificationProgressSnapshotRestore implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressSnapshotRestore::UINotificationProgressSnapshotRestore(const QUuid &uMachineId,
                                                                             const CSnapshot &comSnapshot /* = CSnapshot() */)
    : m_uMachineId(uMachineId)
    , m_comSnapshot(comSnapshot)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSnapshotRestore::sltHandleProgressFinished);
}

UINotificationProgressSnapshotRestore::UINotificationProgressSnapshotRestore(const CMachine &comMachine,
                                                                             const CSnapshot &comSnapshot /* = CSnapshot() */)
    : m_comMachine(comMachine)
    , m_comSnapshot(comSnapshot)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSnapshotRestore::sltHandleProgressFinished);
}

QString UINotificationProgressSnapshotRestore::name() const
{
    return UINotificationProgress::tr("Restoring snapshot ...");
}

QString UINotificationProgressSnapshotRestore::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1<br><b>Snapshot Name:</b> %2").arg(m_strMachineName, m_strSnapshotName);
}

CProgress UINotificationProgressSnapshotRestore::createProgress(COMResult &comResult)
{
    /* Make sure machine ID defined: */
    if (m_uMachineId.isNull())
    {
        /* Acquire VM id: */
        AssertReturn(m_comMachine.isNotNull(), CProgress());
        m_uMachineId = m_comMachine.GetId();
        if (!m_comMachine.isOk())
        {
            comResult = m_comMachine;
            return CProgress();
        }
    }

    /* Make sure machine defined: */
    if (m_comMachine.isNull())
    {
        /* Acquire VM: */
        AssertReturn(!m_uMachineId.isNull(), CProgress());
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        m_comMachine = comVBox.FindMachine(m_uMachineId.toString());
        if (!comVBox.isOk())
        {
            comResult = comVBox;
            return CProgress();
        }
    }

    /* Make sure snapshot is defined: */
    if (m_comSnapshot.isNull())
        m_comSnapshot = m_comMachine.GetCurrentSnapshot();

    /* Acquire snapshot name: */
    m_strSnapshotName = m_comSnapshot.GetName();
    if (!m_comSnapshot.isOk())
    {
        comResult = m_comSnapshot;
        return CProgress();
    }

    /* Acquire session state: */
    const KSessionState enmSessionState = m_comMachine.GetSessionState();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Open a session thru which we will modify the machine: */
    if (enmSessionState != KSessionState_Unlocked)
        m_comSession = openExistingSession(m_uMachineId);
    else
        m_comSession = openSession(m_uMachineId);
    if (m_comSession.isNull())
        return CProgress();

    /* Get session machine: */
    CMachine comMachine = m_comSession.GetMachine();
    if (!m_comSession.isOk())
    {
        comResult = m_comSession;
        m_comSession.UnlockMachine();
        return CProgress();
    }

    /* Acquire VM name: */
    m_strMachineName = comMachine.GetName();
    if (!comMachine.isOk())
    {
        comResult = comMachine;
        m_comSession.UnlockMachine();
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.RestoreSnapshot(m_comSnapshot);
    /* Store COM result: */
    comResult = comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressSnapshotRestore::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    m_comSession.UnlockMachine();

    /* Notifies listeners: */
    emit sigSnapshotRestored(error().isEmpty());
}


/*********************************************************************************************************************************
*   Class UINotificationProgressSnapshotDelete implementation.                                                                   *
*********************************************************************************************************************************/

UINotificationProgressSnapshotDelete::UINotificationProgressSnapshotDelete(const CMachine &comMachine,
                                                                           const QUuid &uSnapshotId)
    : m_comMachine(comMachine)
    , m_uSnapshotId(uSnapshotId)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSnapshotDelete::sltHandleProgressFinished);
}

QString UINotificationProgressSnapshotDelete::name() const
{
    return UINotificationProgress::tr("Deleting snapshot ...");
}

QString UINotificationProgressSnapshotDelete::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1<br><b>Snapshot Name:</b> %2").arg(m_strMachineName, m_strSnapshotName);
}

CProgress UINotificationProgressSnapshotDelete::createProgress(COMResult &comResult)
{
    /* Acquire VM id: */
    const QUuid uId = m_comMachine.GetId();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire VM name: */
    m_strMachineName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire snapshot: */
    CSnapshot comSnapshot = m_comMachine.FindSnapshot(m_uSnapshotId.toString());
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire snapshot name: */
    m_strSnapshotName = comSnapshot.GetName();
    if (!comSnapshot.isOk())
    {
        comResult = comSnapshot;
        return CProgress();
    }

    /* Acquire session state: */
    const KSessionState enmSessionState = m_comMachine.GetSessionState();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Open a session thru which we will modify the machine: */
    if (enmSessionState != KSessionState_Unlocked)
        m_comSession = openExistingSession(uId);
    else
        m_comSession = openSession(uId);
    if (m_comSession.isNull())
        return CProgress();

    /* Get session machine: */
    CMachine comMachine = m_comSession.GetMachine();
    if (!m_comSession.isOk())
    {
        comResult = m_comSession;
        m_comSession.UnlockMachine();
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.DeleteSnapshot(m_uSnapshotId);
    /* Store COM result: */
    comResult = comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressSnapshotDelete::sltHandleProgressFinished()
{
    m_comSession.UnlockMachine();
}


/*********************************************************************************************************************************
*   Class UINotificationProgressApplianceWrite implementation.                                                                   *
*********************************************************************************************************************************/

UINotificationProgressApplianceWrite::UINotificationProgressApplianceWrite(const CAppliance &comAppliance,
                                                                           const QString &strFormat,
                                                                           const QVector<KExportOptions> &options,
                                                                           const QString &strPath)
    : m_comAppliance(comAppliance)
    , m_strFormat(strFormat)
    , m_options(options)
    , m_strPath(strPath)
{
}

QString UINotificationProgressApplianceWrite::name() const
{
    return UINotificationProgress::tr("Writing appliance ...");
}

QString UINotificationProgressApplianceWrite::details() const
{
    return UINotificationProgress::tr("<b>To:</b> %1").arg(m_strPath);
}

CProgress UINotificationProgressApplianceWrite::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comAppliance.Write(m_strFormat, m_options, m_strPath);
    /* Store COM result: */
    comResult = m_comAppliance;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressApplianceRead implementation.                                                                    *
*********************************************************************************************************************************/

UINotificationProgressApplianceRead::UINotificationProgressApplianceRead(const CAppliance &comAppliance,
                                                                         const QString &strPath)
    : m_comAppliance(comAppliance)
    , m_strPath(strPath)
{
}

QString UINotificationProgressApplianceRead::name() const
{
    return UINotificationProgress::tr("Reading appliance ...");
}

QString UINotificationProgressApplianceRead::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1").arg(m_strPath);
}

CProgress UINotificationProgressApplianceRead::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comAppliance.Read(m_strPath);
    /* Store COM result: */
    comResult = m_comAppliance;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressApplianceImport implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressApplianceImport::UINotificationProgressApplianceImport(const CAppliance &comAppliance,
                                                                             const QVector<KImportOptions> &options)
    : m_comAppliance(comAppliance)
    , m_options(options)
{
}

QString UINotificationProgressApplianceImport::name() const
{
    return UINotificationProgress::tr("Importing appliance ...");
}

QString UINotificationProgressApplianceImport::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1").arg(m_comAppliance.GetPath());
}

CProgress UINotificationProgressApplianceImport::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comAppliance.ImportMachines(m_options);
    /* Store COM result: */
    comResult = m_comAppliance;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressExtensionPackInstall implementation.                                                             *
*********************************************************************************************************************************/

UINotificationProgressExtensionPackInstall::UINotificationProgressExtensionPackInstall(const CExtPackFile &comExtPackFile,
                                                                                       bool fReplace,
                                                                                       const QString &strExtensionPackName,
                                                                                       const QString &strDisplayInfo)
    : m_comExtPackFile(comExtPackFile)
    , m_fReplace(fReplace)
    , m_strExtensionPackName(strExtensionPackName)
    , m_strDisplayInfo(strDisplayInfo)
{
}

QString UINotificationProgressExtensionPackInstall::name() const
{
    return UINotificationProgress::tr("Installing package ...");
}

QString UINotificationProgressExtensionPackInstall::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strExtensionPackName);
}

CProgress UINotificationProgressExtensionPackInstall::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comExtPackFile.Install(m_fReplace, m_strDisplayInfo);
    /* Store COM result: */
    comResult = m_comExtPackFile;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressExtensionPackUninstall implementation.                                                           *
*********************************************************************************************************************************/

UINotificationProgressExtensionPackUninstall::UINotificationProgressExtensionPackUninstall(const CExtPackManager &comExtPackManager,
                                                                                           const QString &strExtensionPackName,
                                                                                           const QString &strDisplayInfo)
    : m_comExtPackManager(comExtPackManager)
    , m_strExtensionPackName(strExtensionPackName)
    , m_strDisplayInfo(strDisplayInfo)
{
}

QString UINotificationProgressExtensionPackUninstall::name() const
{
    return UINotificationProgress::tr("Uninstalling package ...");
}

QString UINotificationProgressExtensionPackUninstall::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strExtensionPackName);
}

CProgress UINotificationProgressExtensionPackUninstall::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comExtPackManager.Uninstall(m_strExtensionPackName,
                                                          false /* forced removal? */,
                                                          m_strDisplayInfo);
    /* Store COM result: */
    comResult = m_comExtPackManager;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressGuestAdditionsInstall implementation.                                                            *
*********************************************************************************************************************************/

UINotificationProgressGuestAdditionsInstall::UINotificationProgressGuestAdditionsInstall(const CGuest &comGuest,
                                                                                         const QString &strSource)
    : m_comGuest(comGuest)
    , m_strSource(strSource)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressGuestAdditionsInstall::sltHandleProgressFinished);
}

QString UINotificationProgressGuestAdditionsInstall::name() const
{
    return UINotificationProgress::tr("Installing image ...");
}

QString UINotificationProgressGuestAdditionsInstall::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strSource);
}

CProgress UINotificationProgressGuestAdditionsInstall::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    QVector<QString> args;
    QVector<KAdditionsUpdateFlag> flags;
    CProgress comProgress = m_comGuest.UpdateGuestAdditions(m_strSource, args, flags);
    /* Store COM result: */
    comResult = m_comGuest;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressGuestAdditionsInstall::sltHandleProgressFinished()
{
    if (!error().isEmpty())
        emit sigGuestAdditionsInstallationFailed(m_strSource);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressHostOnlyNetworkInterfaceCreate implementation.                                                   *
*********************************************************************************************************************************/

UINotificationProgressHostOnlyNetworkInterfaceCreate::UINotificationProgressHostOnlyNetworkInterfaceCreate(const CHost &comHost,
                                                                                                           const CHostNetworkInterface &comInterface)
    : m_comHost(comHost)
    , m_comInterface(comInterface)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressHostOnlyNetworkInterfaceCreate::sltHandleProgressFinished);
}

QString UINotificationProgressHostOnlyNetworkInterfaceCreate::name() const
{
    return UINotificationProgress::tr("Creating host-only network interface ...");
}

QString UINotificationProgressHostOnlyNetworkInterfaceCreate::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg("TBD");
}

CProgress UINotificationProgressHostOnlyNetworkInterfaceCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comHost.CreateHostOnlyNetworkInterface(m_comInterface);
    /* Store COM result: */
    comResult = m_comHost;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressHostOnlyNetworkInterfaceCreate::sltHandleProgressFinished()
{
    if (error().isEmpty())
        emit sigHostOnlyNetworkInterfaceCreated(m_comInterface);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressHostOnlyNetworkInterfaceRemove implementation.                                                   *
*********************************************************************************************************************************/

UINotificationProgressHostOnlyNetworkInterfaceRemove::UINotificationProgressHostOnlyNetworkInterfaceRemove(const CHost &comHost,
                                                                                                           const QUuid &uInterfaceId)
    : m_comHost(comHost)
    , m_uInterfaceId(uInterfaceId)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressHostOnlyNetworkInterfaceRemove::sltHandleProgressFinished);
}

QString UINotificationProgressHostOnlyNetworkInterfaceRemove::name() const
{
    return UINotificationProgress::tr("Removing host-only network interface ...");
}

QString UINotificationProgressHostOnlyNetworkInterfaceRemove::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strInterfaceName);
}

CProgress UINotificationProgressHostOnlyNetworkInterfaceRemove::createProgress(COMResult &comResult)
{
    /* Acquire interface: */
    CHostNetworkInterface comInterface = m_comHost.FindHostNetworkInterfaceById(m_uInterfaceId);
    if (!m_comHost.isOk())
    {
        comResult = m_comHost;
        return CProgress();
    }

    /* Acquire interface name: */
    m_strInterfaceName = comInterface.GetName();
    if (!comInterface.isOk())
    {
        comResult = comInterface;
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comHost.RemoveHostOnlyNetworkInterface(m_uInterfaceId);
    /* Store COM result: */
    comResult = m_comHost;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressHostOnlyNetworkInterfaceRemove::sltHandleProgressFinished()
{
    if (error().isEmpty())
        emit sigHostOnlyNetworkInterfaceRemoved(m_strInterfaceName);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressVsdFormValueSet implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CBooleanFormValue &comValue,
                                                                             bool fBool)
    : m_enmType(KFormValueType_Boolean)
    , m_comValue(comValue)
    , m_fBool(fBool)
    , m_iChoice(0)
    , m_iInteger(0)
    , m_iInteger64(0)
{
}

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CStringFormValue &comValue,
                                                                             const QString &strString)
    : m_enmType(KFormValueType_String)
    , m_comValue(comValue)
    , m_fBool(false)
    , m_strString(strString)
    , m_iChoice(0)
    , m_iInteger(0)
    , m_iInteger64(0)
{
}

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CChoiceFormValue &comValue,
                                                                             int iChoice)
    : m_enmType(KFormValueType_Choice)
    , m_comValue(comValue)
    , m_fBool(false)
    , m_iChoice(iChoice)
    , m_iInteger(0)
    , m_iInteger64(0)
{
}

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CRangedIntegerFormValue &comValue,
                                                                             int iInteger)
    : m_enmType(KFormValueType_RangedInteger)
    , m_comValue(comValue)
    , m_fBool(false)
    , m_iChoice(0)
    , m_iInteger(iInteger)
    , m_iInteger64(0)
{
}

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CRangedInteger64FormValue &comValue,
                                                                             qlonglong iInteger64)
    : m_enmType(KFormValueType_RangedInteger64)
    , m_comValue(comValue)
    , m_fBool(false)
    , m_iChoice(0)
    , m_iInteger(0)
    , m_iInteger64(iInteger64)
{
}

QString UINotificationProgressVsdFormValueSet::name() const
{
    return UINotificationProgress::tr("Set VSD form value ...");
}

QString UINotificationProgressVsdFormValueSet::details() const
{
    /* Handle known types: */
    switch (m_enmType)
    {
        case KFormValueType_Boolean: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_fBool);
        case KFormValueType_String: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_strString);
        case KFormValueType_Choice: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_iChoice);
        case KFormValueType_RangedInteger: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_iInteger);
        case KFormValueType_RangedInteger64: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_iInteger64);
        default: break;
    }
    /* Null-string by default: */
    return QString();
}

CProgress UINotificationProgressVsdFormValueSet::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress;

    /* Handle known types: */
    switch (m_enmType)
    {
        case KFormValueType_Boolean:
        {
            /* Set value: */
            CBooleanFormValue comValue(m_comValue);
            comProgress = comValue.SetSelected(m_fBool);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        case KFormValueType_String:
        {
            /* Set value: */
            CStringFormValue comValue(m_comValue);
            comProgress = comValue.SetString(m_strString);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        case KFormValueType_Choice:
        {
            /* Set value: */
            CChoiceFormValue comValue(m_comValue);
            comProgress = comValue.SetSelectedIndex(m_iChoice);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        case KFormValueType_RangedInteger:
        {
            /* Set value: */
            CRangedIntegerFormValue comValue(m_comValue);
            comProgress = comValue.SetInteger(m_iInteger);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        case KFormValueType_RangedInteger64:
        {
            /* Set value: */
            CRangedInteger64FormValue comValue(m_comValue);
            comProgress = comValue.SetInteger(m_iInteger64);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        default:
            break;
    }

    /* Return progress-wrapper: */
    return comProgress;
}


#ifdef VBOX_GUI_WITH_NETWORK_MANAGER


/*********************************************************************************************************************************
*   Class UINotificationDownloaderExtensionPack implementation.                                                                  *
*********************************************************************************************************************************/

/* static */
UINotificationDownloaderExtensionPack *UINotificationDownloaderExtensionPack::s_pInstance = 0;

/* static */
UINotificationDownloaderExtensionPack *UINotificationDownloaderExtensionPack::instance(const QString &strPackName)
{
    if (!s_pInstance)
        new UINotificationDownloaderExtensionPack(strPackName);
    return s_pInstance;
}

/* static */
bool UINotificationDownloaderExtensionPack::exists()
{
    return !!s_pInstance;
}

UINotificationDownloaderExtensionPack::UINotificationDownloaderExtensionPack(const QString &strPackName)
    : m_strPackName(strPackName)
{
    s_pInstance = this;
}

UINotificationDownloaderExtensionPack::~UINotificationDownloaderExtensionPack()
{
    s_pInstance = 0;
}

QString UINotificationDownloaderExtensionPack::name() const
{
    return UINotificationDownloader::tr("Downloading Extension Pack ...");
}

QString UINotificationDownloaderExtensionPack::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strPackName);
}

UIDownloader *UINotificationDownloaderExtensionPack::createDownloader()
{
    /* Create and configure the Extension Pack downloader: */
    UIDownloaderExtensionPack *pDownloader = new UIDownloaderExtensionPack;
    if (pDownloader)
    {
        connect(pDownloader, &UIDownloaderExtensionPack::sigDownloadFinished,
                this, &UINotificationDownloaderExtensionPack::sigExtensionPackDownloaded);
        return pDownloader;
    }
    return 0;
}


/*********************************************************************************************************************************
*   Class UINotificationDownloaderGuestAdditions implementation.                                                                 *
*********************************************************************************************************************************/

/* static */
UINotificationDownloaderGuestAdditions *UINotificationDownloaderGuestAdditions::s_pInstance = 0;

/* static */
UINotificationDownloaderGuestAdditions *UINotificationDownloaderGuestAdditions::instance(const QString &strFileName)
{
    if (!s_pInstance)
        new UINotificationDownloaderGuestAdditions(strFileName);
    return s_pInstance;
}

/* static */
bool UINotificationDownloaderGuestAdditions::exists()
{
    return !!s_pInstance;
}

UINotificationDownloaderGuestAdditions::UINotificationDownloaderGuestAdditions(const QString &strFileName)
    : m_strFileName(strFileName)
{
    s_pInstance = this;
}

UINotificationDownloaderGuestAdditions::~UINotificationDownloaderGuestAdditions()
{
    s_pInstance = 0;
}

QString UINotificationDownloaderGuestAdditions::name() const
{
    return UINotificationDownloader::tr("Downloading Guest Additions ...");
}

QString UINotificationDownloaderGuestAdditions::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strFileName);
}

UIDownloader *UINotificationDownloaderGuestAdditions::createDownloader()
{
    /* Create and configure the User Manual downloader: */
    UIDownloaderGuestAdditions *pDownloader = new UIDownloaderGuestAdditions;
    if (pDownloader)
    {
        connect(pDownloader, &UIDownloaderGuestAdditions::sigDownloadFinished,
                this, &UINotificationDownloaderGuestAdditions::sigGuestAdditionsDownloaded);
        return pDownloader;
    }
    return 0;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressNewVersionChecker implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressNewVersionChecker::UINotificationProgressNewVersionChecker(bool fForcedCall)
    : m_fForcedCall(fForcedCall)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressNewVersionChecker::sltHandleProgressFinished);

#ifdef VBOX_WITH_UPDATE_AGENT
    CHost comHost = gpGlobalSession->host();
    if (!comHost.isNull())
       m_comUpdateHost = comHost.GetUpdateHost();
#endif /* VBOX_WITH_UPDATE_AGENT */
}

QString UINotificationProgressNewVersionChecker::name() const
{
#ifdef VBOX_WITH_UPDATE_AGENT
    if (m_comUpdateHost.isOk())
        return UINotificationProgress::tr("Checking for new version of %1 ...").arg(m_comUpdateHost.GetName().toLocal8Bit().data());
#endif /* VBOX_WITH_UPDATE_AGENT */
    return UINotificationProgress::tr("Checking for new version ...");
}

QString UINotificationProgressNewVersionChecker::details() const
{
    return QString();
}

CProgress UINotificationProgressNewVersionChecker::createProgress(COMResult &comResult)
{
#ifdef VBOX_WITH_UPDATE_AGENT
    if (!m_comUpdateHost.isOk())
        return CProgress();

    CProgress comProgress = m_comUpdateHost.CheckFor();
    comResult = m_comUpdateHost;

    return comProgress;
#else
    return CProgress();
#endif /* VBOX_WITH_UPDATE_AGENT */
}

void UINotificationProgressNewVersionChecker::sltHandleProgressFinished()
{
#ifdef VBOX_WITH_UPDATE_AGENT
    if (m_comUpdateHost.isNull() && !m_comUpdateHost.isOk())
        return;

    KUpdateState enmState = m_comUpdateHost.GetState();
    if (!m_comUpdateHost.isOk())
        return;

    switch (enmState)
    {
        case KUpdateState_Available:
        {
            QString strVersion = m_comUpdateHost.GetVersion();
            if (!m_comUpdateHost.isOk())
                return;
            QString strURL = m_comUpdateHost.GetDownloadUrl();
            if (!m_comUpdateHost.isOk())
                return;
            UINotificationMessage::showUpdateSuccess(strVersion, strURL);
            break;
        }
        case KUpdateState_NotAvailable:
        {
            if (m_fForcedCall)
                UINotificationMessage::showUpdateNotFound();
            break;
        }
        case KUpdateState_Invalid:
        case KUpdateState_Error:
        case KUpdateState_Max:
            /* Error cases are handled not here: */
            break;
        case KUpdateState_Downloading:
        case KUpdateState_Downloaded:
        case KUpdateState_Installing:
        case KUpdateState_Installed:
        case KUpdateState_UserInteraction:
        case KUpdateState_Canceled:
        case KUpdateState_Maintenance:
            /* These cases are not yet implemented in Main: */
            break;
        default:
            break;
    }

#endif /* VBOX_WITH_UPDATE_AGENT */
}

#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
