/* $Id: UIMessageCenter.cpp 168965 2025-05-22 14:49:38Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMessageCenter class implementation.
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
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QThread>
#include <QWindow>
#ifdef VBOX_WS_MAC
# include <QPushButton>
#endif

/* GUI includes: */
#include "QIMessageBox.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIErrorString.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIHelpBrowserDialog.h"
#include "UIHostComboEditor.h"
#include "UIIconPool.h"
#include "UIMedium.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINotificationCenter.h"
#include "UIProgressDialog.h"
#include "UITranslator.h"
#include "UIVersion.h"
#include "VBoxAboutDlg.h"
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
# include "UINetworkRequestManager.h"
#endif
#ifdef VBOX_OSE
# include "UINotificationCenter.h"
#endif
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif
#ifdef VBOX_WS_WIN
# include <Htmlhelp.h>
#endif

/* COM includes: */
#include "CAppliance.h"
#include "CBooleanFormValue.h"
#include "CChoiceFormValue.h"
#include "CCloudClient.h"
#include "CCloudMachine.h"
#include "CCloudProfile.h"
#include "CCloudProvider.h"
#include "CCloudProviderManager.h"
#include "CConsole.h"
#include "CDHCPServer.h"
#include "CDisplay.h"
#include "CExtPack.h"
#include "CExtPackFile.h"
#include "CExtPackManager.h"
#include "CForm.h"
#include "CHostNetworkInterface.h"
#include "CMachine.h"
#include "CMediumAttachment.h"
#include "CMediumFormat.h"
#include "CNATEngine.h"
#include "CNATNetwork.h"
#include "CRangedIntegerFormValue.h"
#include "CSerialPort.h"
#include "CSharedFolder.h"
#include "CSnapshot.h"
#include "CStorageController.h"
#include "CStringFormValue.h"
#include "CSystemProperties.h"
#include "CUnattended.h"
#include "CVFSExplorer.h"
#include "CVirtualBoxErrorInfo.h"
#include "CVirtualSystemDescription.h"
#include "CVirtualSystemDescriptionForm.h"
#ifdef VBOX_WITH_DRAG_AND_DROP
# include "CDnDSource.h"
# include "CDnDTarget.h"
# include "CGuest.h"
#endif

/* Other VBox includes: */
#include <iprt/errcore.h>
#include <iprt/param.h>
#include <iprt/path.h>

#include <VBox/version.h>

/* static */
UIMessageCenter *UIMessageCenter::s_pInstance = 0;
UIMessageCenter *UIMessageCenter::instance() { return s_pInstance; }

/* static */
void UIMessageCenter::create()
{
    /* Make sure instance is NOT created yet: */
    if (s_pInstance)
    {
        AssertMsgFailed(("UIMessageCenter instance is already created!"));
        return;
    }

    /* Create instance: */
    new UIMessageCenter;
    /* Prepare instance: */
    s_pInstance->prepare();
}

/* static */
void UIMessageCenter::destroy()
{
    /* Make sure instance is NOT destroyed yet: */
    if (!s_pInstance)
    {
        AssertMsgFailed(("UIMessageCenter instance is already destroyed!"));
        return;
    }

    /* Cleanup instance: */
    s_pInstance->cleanup();
    /* Destroy instance: */
    delete s_pInstance;
}

void UIMessageCenter::setWarningShown(const QString &strWarningName, bool fWarningShown) const
{
    if (fWarningShown && !m_warnings.contains(strWarningName))
        m_warnings.append(strWarningName);
    else if (!fWarningShown && m_warnings.contains(strWarningName))
        m_warnings.removeAll(strWarningName);
}

bool UIMessageCenter::warningShown(const QString &strWarningName) const
{
    return m_warnings.contains(strWarningName);
}

int UIMessageCenter::message(QWidget *pParent, MessageType enmType,
                             const QString &strMessage,
                             const QString &strDetails,
                             const char *pcszAutoConfirmId /* = 0*/,
                             int iButton1 /* = 0*/,
                             int iButton2 /* = 0*/,
                             int iButton3 /* = 0*/,
                             const QString &strButtonText1 /* = QString() */,
                             const QString &strButtonText2 /* = QString() */,
                             const QString &strButtonText3 /* = QString() */,
                             const QString &strHelpKeyword /* = QString() */) const
{
    /* If this is NOT a GUI thread: */
    if (thread() != QThread::currentThread())
    {
        /* We have to throw a blocking signal
         * to show a message-box in the GUI thread: */
        emit sigToShowMessageBox(pParent, enmType,
                                 strMessage, strDetails,
                                 iButton1, iButton2, iButton3,
                                 strButtonText1, strButtonText2, strButtonText3,
                                 QString(pcszAutoConfirmId), strHelpKeyword);
        /* Inter-thread communications are not yet implemented: */
        return 0;
    }
    /* In usual case we can chow a message-box directly: */
    return showMessageBox(pParent, enmType,
                          strMessage, strDetails,
                          iButton1, iButton2, iButton3,
                          strButtonText1, strButtonText2, strButtonText3,
                          QString(pcszAutoConfirmId), strHelpKeyword);
}

void UIMessageCenter::error(QWidget *pParent, MessageType enmType,
                           const QString &strMessage,
                           const QString &strDetails,
                           const char *pcszAutoConfirmId /* = 0*/,
                           const QString &strHelpKeyword /* = QString() */) const
{
    message(pParent, enmType, strMessage, strDetails, pcszAutoConfirmId,
            AlertButton_Ok | AlertButtonOption_Default | AlertButtonOption_Escape, 0 /* Button 2 */, 0 /* Button 3 */,
            QString() /* strButtonText1 */, QString() /* strButtonText2 */, QString() /* strButtonText3 */, strHelpKeyword);
}

bool UIMessageCenter::errorWithQuestion(QWidget *pParent, MessageType enmType,
                                        const QString &strMessage,
                                        const QString &strDetails,
                                        const char *pcszAutoConfirmId /* = 0*/,
                                        const QString &strOkButtonText /* = QString()*/,
                                        const QString &strCancelButtonText /* = QString()*/,
                                        const QString &strHelpKeyword /* = QString()*/) const
{
    return (message(pParent, enmType, strMessage, strDetails, pcszAutoConfirmId,
                    AlertButton_Ok | AlertButtonOption_Default,
                    AlertButton_Cancel | AlertButtonOption_Escape,
                    0 /* third button */,
                    strOkButtonText,
                    strCancelButtonText,
                    QString() /* third button text*/,
                    strHelpKeyword) &
            AlertButtonMask) == AlertButton_Ok;
}

void UIMessageCenter::alert(QWidget *pParent, MessageType enmType,
                           const QString &strMessage,
                           const char *pcszAutoConfirmId /* = 0*/,
                           const QString &strHelpKeyword /* = QString()*/) const
{
    error(pParent, enmType, strMessage, QString(), pcszAutoConfirmId, strHelpKeyword);
}

int UIMessageCenter::question(QWidget *pParent, MessageType enmType,
                              const QString &strMessage,
                              const char *pcszAutoConfirmId/* = 0*/,
                              int iButton1 /* = 0*/,
                              int iButton2 /* = 0*/,
                              int iButton3 /* = 0*/,
                              const QString &strButtonText1 /* = QString()*/,
                              const QString &strButtonText2 /* = QString()*/,
                              const QString &strButtonText3 /* = QString()*/) const
{
    return message(pParent, enmType, strMessage, QString(), pcszAutoConfirmId,
                   iButton1, iButton2, iButton3, strButtonText1, strButtonText2, strButtonText3);
}

bool UIMessageCenter::questionBinary(QWidget *pParent, MessageType enmType,
                                     const QString &strMessage,
                                     const char *pcszAutoConfirmId /* = 0*/,
                                     const QString &strOkButtonText /* = QString()*/,
                                     const QString &strCancelButtonText /* = QString()*/,
                                     bool fDefaultFocusForOk /* = true*/) const
{
    return fDefaultFocusForOk ?
           ((question(pParent, enmType, strMessage, pcszAutoConfirmId,
                      AlertButton_Ok | AlertButtonOption_Default,
                      AlertButton_Cancel | AlertButtonOption_Escape,
                      0 /* third button */,
                      strOkButtonText,
                      strCancelButtonText,
                      QString() /* third button */) &
             AlertButtonMask) == AlertButton_Ok) :
           ((question(pParent, enmType, strMessage, pcszAutoConfirmId,
                      AlertButton_Ok,
                      AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape,
                      0 /* third button */,
                      strOkButtonText,
                      strCancelButtonText,
                      QString() /* third button */) &
             AlertButtonMask) == AlertButton_Ok);
}

int UIMessageCenter::questionTrinary(QWidget *pParent, MessageType enmType,
                                     const QString &strMessage,
                                     const char *pcszAutoConfirmId /* = 0*/,
                                     const QString &strChoice1ButtonText /* = QString()*/,
                                     const QString &strChoice2ButtonText /* = QString()*/,
                                     const QString &strCancelButtonText /* = QString()*/) const
{
    return question(pParent, enmType, strMessage, pcszAutoConfirmId,
                    AlertButton_Choice1,
                    AlertButton_Choice2 | AlertButtonOption_Default,
                    AlertButton_Cancel | AlertButtonOption_Escape,
                    strChoice1ButtonText,
                    strChoice2ButtonText,
                    strCancelButtonText);
}

int UIMessageCenter::messageWithOption(QWidget *pParent, MessageType enmType,
                                       const QString &strMessage,
                                       const QString &strOptionText,
                                       bool fDefaultOptionValue /* = true */,
                                       int iButton1 /* = 0*/,
                                       int iButton2 /* = 0*/,
                                       int iButton3 /* = 0*/,
                                       const QString &strButtonName1 /* = QString() */,
                                       const QString &strButtonName2 /* = QString() */,
                                       const QString &strButtonName3 /* = QString() */) const
{
    /* If no buttons are set, using single 'OK' button: */
    if (iButton1 == 0 && iButton2 == 0 && iButton3 == 0)
        iButton1 = AlertButton_Ok | AlertButtonOption_Default;

    /* Assign corresponding title and icon: */
    QString strTitle;
    AlertIconType icon;
    switch (enmType)
    {
        default:
        case MessageType_Info:
            strTitle = tr("VirtualBox - Information", "msg box title");
            icon = AlertIconType_Information;
            break;
        case MessageType_Question:
            strTitle = tr("VirtualBox - Question", "msg box title");
            icon = AlertIconType_Question;
            break;
        case MessageType_Warning:
            strTitle = tr("VirtualBox - Warning", "msg box title");
            icon = AlertIconType_Warning;
            break;
        case MessageType_Error:
            strTitle = tr("VirtualBox - Error", "msg box title");
            icon = AlertIconType_Critical;
            break;
        case MessageType_Critical:
            strTitle = tr("VirtualBox - Critical Error", "msg box title");
            icon = AlertIconType_Critical;
            break;
        case MessageType_GuruMeditation:
            strTitle = "VirtualBox - Guru Meditation"; /* don't translate this */
            icon = AlertIconType_GuruMeditation;
            break;
    }

    /* Create message-box: */
    QWidget *pBoxParent = windowManager().realParentWindow(pParent ? pParent : windowManager().mainWindowShown());
    QPointer<QIMessageBox> pBox = new QIMessageBox(strTitle, strMessage, icon,
                                                   iButton1, iButton2, iButton3, pBoxParent);
    windowManager().registerNewParent(pBox, pBoxParent);

    /* Load option: */
    if (!strOptionText.isNull())
    {
        pBox->setFlagText(strOptionText);
        pBox->setFlagChecked(fDefaultOptionValue);
    }

    /* Configure button-text: */
    if (!strButtonName1.isNull())
        pBox->setButtonText(0, strButtonName1);
    if (!strButtonName2.isNull())
        pBox->setButtonText(1, strButtonName2);
    if (!strButtonName3.isNull())
        pBox->setButtonText(2, strButtonName3);

    /* Show box: */
    int rc = pBox->exec();

    /* Make sure box still valid: */
    if (!pBox)
        return rc;

    /* Save option: */
    if (pBox->flagChecked())
        rc |= AlertOption_CheckBox;

    /* Delete message-box: */
    if (pBox)
        delete pBox;

    return rc;
}

bool UIMessageCenter::showModalProgressDialog(CProgress &progress,
                                              const QString &strTitle,
                                              const QString &strImage /* = "" */,
                                              QWidget *pParent /* = 0*/,
                                              int cMinDuration /* = 2000 */)
{
    /* Prepare result: */
    bool fRc = false;

    /* Gather suitable dialog parent: */
    QWidget *pDlgParent = windowManager().realParentWindow(pParent ? pParent : windowManager().mainWindowShown());

    /* Prepare pixmap: */
    QPixmap pixmap;
    if (!strImage.isEmpty())
    {
        const qreal fDevicePixelRatio = pDlgParent && pDlgParent->windowHandle() ? pDlgParent->windowHandle()->devicePixelRatio() : 1;
        pixmap = UIIconPool::iconSet(strImage).pixmap(QSize(90, 90), fDevicePixelRatio);
    }

    /* Create progress-dialog: */
    QPointer<UIProgressDialog> pProgressDlg = new UIProgressDialog(progress, strTitle, &pixmap, cMinDuration, pDlgParent);
    if (pProgressDlg)
    {
        /* Register it as new parent: */
        windowManager().registerNewParent(pProgressDlg, pDlgParent);

        /* Run the dialog with the 350 ms refresh interval. */
        pProgressDlg->run(350);

        /* Make sure progress-dialog still valid: */
        if (pProgressDlg)
        {
            /* Delete progress-dialog: */
            delete pProgressDlg;
            fRc = true;
        }
    }

    /* Return result: */
    return fRc;
}

void UIMessageCenter::cannotFindLanguage(const QString &strLangId, const QString &strNlsPath) const
{
    alert(0, MessageType_Error,
          tr("<p>Could not find a language file for the language <b>%1</b> in the directory <b><nobr>%2</nobr></b>.</p>"
             "<p>The language will be temporarily reset to the system default language. "
             "Please go to the <b>Preferences</b> window which you can open from the <b>File</b> menu of the "
             "VirtualBox Manager window, and select one of the existing languages on the <b>Language</b> page.</p>")
             .arg(strLangId).arg(strNlsPath));
}

void UIMessageCenter::cannotLoadLanguage(const QString &strLangFile) const
{
    alert(0, MessageType_Error,
          tr("<p>Could not load the language file <b><nobr>%1</nobr></b>. "
             "<p>The language will be temporarily reset to English (built-in). "
             "Please go to the <b>Preferences</b> window which you can open from the <b>File</b> menu of the "
             "VirtualBox Manager window, and select one of the existing languages on the <b>Language</b> page.</p>")
             .arg(strLangFile));
}

void UIMessageCenter::cannotInitUserHome(const QString &strUserHome) const
{
    error(0, MessageType_Critical,
          tr("<p>Failed to initialize COM because the VirtualBox global "
             "configuration directory <b><nobr>%1</nobr></b> is not accessible. "
             "Please check the permissions of this directory and of its parent directory.</p>"
             "<p>The application will now terminate.</p>")
             .arg(strUserHome),
          UIErrorString::formatErrorInfo(COMErrorInfo()));
}

void UIMessageCenter::cannotInitCOM(HRESULT rc) const
{
    error(0, MessageType_Critical,
          tr("<p>Failed to initialize COM or to find the VirtualBox COM server. "
             "Most likely, the VirtualBox server is not running or failed to start.</p>"
             "<p>The application will now terminate.</p>"),
          UIErrorString::formatErrorInfo(COMErrorInfo(), rc));
}

void UIMessageCenter::cannotHandleRuntimeOption(const QString &strOption) const
{
    alert(0, MessageType_Error,
          tr("<b>%1</b> is an option for the VirtualBox VM runner (VirtualBoxVM) application, not the VirtualBox Manager.")
             .arg(strOption));
}

#ifdef RT_OS_LINUX
void UIMessageCenter::warnAboutWrongUSBMounted() const
{
    alert(0, MessageType_Warning,
          tr("You seem to have the USBFS filesystem mounted at /sys/bus/usb/drivers. "
             "We strongly recommend that you change this, as it is a severe mis-configuration of "
             "your system which could cause USB devices to fail in unexpected ways."),
          "warnAboutWrongUSBMounted");
}
#endif /* RT_OS_LINUX */

void UIMessageCenter::cannotStartSelector() const
{
    alert(0, MessageType_Critical,
          tr("<p>Cannot start the VirtualBox Manager due to local restrictions.</p>"
             "<p>The application will now terminate.</p>"));
}

void UIMessageCenter::cannotStartRuntime() const
{
    /* Prepare error string: */
    const QString strError = tr("<p>You must specify a machine to start, using the command line.</p><p>%1</p>",
                                "There will be a usage text passed as argument.");

    /* Prepare Usage, it can change in future: */
    const QString strTable = QString("<table cellspacing=0 style='white-space:pre'>%1</table>");
    const QString strUsage = tr("<tr>"
                                "<td>Usage: VirtualBoxVM --startvm &lt;name|UUID&gt;</td>"
                                "</tr>"
                                "<tr>"
                                "<td>Starts the VirtualBox virtual machine with the given "
                                "name or unique identifier (UUID).</td>"
                                "</tr>");

    /* Show error: */
    alert(0, MessageType_Error, strError.arg(strTable.arg(strUsage)));
}

void UIMessageCenter::cannotCreateVirtualBoxClient(const CVirtualBoxClient &comClient) const
{
    error(0, MessageType_Critical,
          tr("<p>Failed to create the VirtualBoxClient COM object.</p>"
             "<p>The application will now terminate.</p>"),
          UIErrorString::formatErrorInfo(comClient));
}

void UIMessageCenter::cannotAcquireVirtualBox(const CVirtualBoxClient &comClient) const
{
    QString err = tr("<p>Failed to acquire the VirtualBox COM object.</p>"
                     "<p>The application will now terminate.</p>");
#if defined(VBOX_WS_NIX) || defined(VBOX_WS_MAC)
    if (comClient.lastRC() == NS_ERROR_SOCKET_FAIL)
        err += tr("<p>The reason for this error are most likely wrong permissions of the IPC "
                  "daemon socket due to an installation problem. Please check the permissions of "
                  "<font color=blue>'/tmp'</font> and <font color=blue>'/tmp/.vbox-*-ipc/'</font></p>");
#endif
    error(0, MessageType_Critical, err, UIErrorString::formatErrorInfo(comClient));
}

void UIMessageCenter::cannotFindMachineByName(const CVirtualBox &comVBox, const QString &strName) const
{
    error(0, MessageType_Error,
          tr("There is no virtual machine named <b>%1</b>.")
             .arg(strName),
          UIErrorString::formatErrorInfo(comVBox));
}

void UIMessageCenter::cannotFindMachineById(const CVirtualBox &comVBox, const QUuid &uId) const
{
    error(0, MessageType_Error,
          tr("There is no virtual machine with the identifier <b>%1</b>.")
             .arg(uId.toString()),
          UIErrorString::formatErrorInfo(comVBox));
}

void UIMessageCenter::cannotSetExtraData(const CVirtualBox &comVBox, const QString &strKey, const QString &strValue)
{
    error(0, MessageType_Error,
          tr("Failed to set the global VirtualBox extra data for key <i>%1</i> to value <i>{%2}</i>.")
             .arg(strKey, strValue),
          UIErrorString::formatErrorInfo(comVBox));
}

void UIMessageCenter::cannotOpenMedium(const CVirtualBox &comVBox, const QString &strLocation, QWidget *pParent /* = 0 */) const
{
    /* Show the error: */
    error(pParent, MessageType_Error,
          tr("Failed to open the disk image file <nobr><b>%1</b></nobr>.").arg(strLocation), UIErrorString::formatErrorInfo(comVBox));
}

void UIMessageCenter::cannotOpenSession(const CSession &comSession) const
{
    error(0, MessageType_Error,
          tr("Failed to create a new session."),
          UIErrorString::formatErrorInfo(comSession));
}

void UIMessageCenter::cannotOpenSession(const CMachine &comMachine) const
{
    error(0, MessageType_Error,
          tr("Failed to open a session for the virtual machine <b>%1</b>.")
             .arg(CMachine(comMachine).GetName()),
          UIErrorString::formatErrorInfo(comMachine));
}

void UIMessageCenter::cannotOpenSession(const CProgress &comProgress, const QString &strMachineName) const
{
    error(0, MessageType_Error,
          tr("Failed to open a session for the virtual machine <b>%1</b>.")
             .arg(strMachineName),
          UIErrorString::formatErrorInfo(comProgress));
}

void UIMessageCenter::cannotSetExtraData(const CMachine &machine, const QString &strKey, const QString &strValue)
{
    error(0, MessageType_Error,
          tr("Failed to set the extra data for key <i>%1</i> of machine <i>%2</i> to value <i>{%3}</i>.")
             .arg(strKey, CMachine(machine).GetName(), strValue),
          UIErrorString::formatErrorInfo(machine));
}

void UIMessageCenter::cannotAttachDevice(const CMachine &machine, UIMediumDeviceType enmType,
                                         const QString &strLocation, const StorageSlot &storageSlot,
                                         QWidget *pParent /* = 0*/)
{
    QString strMessage;
    switch (enmType)
    {
        case UIMediumDeviceType_HardDisk:
        {
            strMessage = tr("Failed to attach the hard disk (<nobr><b>%1</b></nobr>) to the slot <i>%2</i> of the machine <b>%3</b>.")
                            .arg(strLocation).arg(gpConverter->toString(storageSlot)).arg(CMachine(machine).GetName());
            break;
        }
        case UIMediumDeviceType_DVD:
        {
            strMessage = tr("Failed to attach the optical drive (<nobr><b>%1</b></nobr>) to the slot <i>%2</i> of the machine <b>%3</b>.")
                            .arg(strLocation).arg(gpConverter->toString(storageSlot)).arg(CMachine(machine).GetName());
            break;
        }
        case UIMediumDeviceType_Floppy:
        {
            strMessage = tr("Failed to attach the floppy drive (<nobr><b>%1</b></nobr>) to the slot <i>%2</i> of the machine <b>%3</b>.")
                            .arg(strLocation).arg(gpConverter->toString(storageSlot)).arg(CMachine(machine).GetName());
            break;
        }
        default:
            break;
    }
    error(pParent, MessageType_Error,
          strMessage, UIErrorString::formatErrorInfo(machine));
}

void UIMessageCenter::cannotDetachDevice(const CMachine &machine, UIMediumDeviceType enmType,
                                         const QString &strLocation, const StorageSlot &storageSlot,
                                         QWidget *pParent /* = 0*/) const
{
    /* Prepare the message: */
    QString strMessage;
    switch (enmType)
    {
        case UIMediumDeviceType_HardDisk:
        {
            strMessage = tr("Failed to detach the hard disk (<nobr><b>%1</b></nobr>) from the slot <i>%2</i> of the machine <b>%3</b>.")
                            .arg(strLocation, gpConverter->toString(storageSlot), CMachine(machine).GetName());
            break;
        }
        case UIMediumDeviceType_DVD:
        {
            strMessage = tr("Failed to detach the optical drive (<nobr><b>%1</b></nobr>) from the slot <i>%2</i> of the machine <b>%3</b>.")
                            .arg(strLocation, gpConverter->toString(storageSlot), CMachine(machine).GetName());
            break;
        }
        case UIMediumDeviceType_Floppy:
        {
            strMessage = tr("Failed to detach the floppy drive (<nobr><b>%1</b></nobr>) from the slot <i>%2</i> of the machine <b>%3</b>.")
                            .arg(strLocation, gpConverter->toString(storageSlot), CMachine(machine).GetName());
            break;
        }
        default:
            break;
    }
    /* Show the error: */
    error(pParent, MessageType_Error, strMessage, UIErrorString::formatErrorInfo(machine));
}

bool UIMessageCenter::cannotRemountMedium(const CMachine &machine, const UIMedium &medium, bool fMount,
                                          bool fRetry, QWidget *pParent /* = 0*/) const
{
    /* Compose the message: */
    QString strMessage;
    switch (medium.type())
    {
        case UIMediumDeviceType_DVD:
        {
            if (fMount)
            {
                strMessage = tr("<p>Unable to insert the virtual optical disk <nobr><b>%1</b></nobr> into the machine <b>%2</b>.</p>");
                if (fRetry)
                    strMessage += tr("<p>Would you like to try to force insertion of this disk?</p>");
            }
            else
            {
                strMessage = tr("<p>Unable to eject the virtual optical disk <nobr><b>%1</b></nobr> from the machine <b>%2</b>.</p>");
                if (fRetry)
                    strMessage += tr("<p>Would you like to try to force ejection of this disk?</p>");
            }
            break;
        }
        case UIMediumDeviceType_Floppy:
        {
            if (fMount)
            {
                strMessage = tr("<p>Unable to insert the virtual floppy disk <nobr><b>%1</b></nobr> into the machine <b>%2</b>.</p>");
                if (fRetry)
                    strMessage += tr("<p>Would you like to try to force insertion of this disk?</p>");
            }
            else
            {
                strMessage = tr("<p>Unable to eject the virtual floppy disk <nobr><b>%1</b></nobr> from the machine <b>%2</b>.</p>");
                if (fRetry)
                    strMessage += tr("<p>Would you like to try to force ejection of this disk?</p>");
            }
            break;
        }
        default:
            break;
    }
    /* Show the messsage: */
    if (fRetry)
        return errorWithQuestion(pParent, MessageType_Question,
                                 strMessage.arg(medium.isHostDrive() ? medium.name() : medium.location(), CMachine(machine).GetName()),
                                 UIErrorString::formatErrorInfo(machine),
                                 0 /* Auto Confirm ID */,
                                 tr("Force Unmount"));
    error(pParent, MessageType_Error,
          strMessage.arg(medium.isHostDrive() ? medium.name() : medium.location(), CMachine(machine).GetName()),
          UIErrorString::formatErrorInfo(machine));
    return false;
}

void UIMessageCenter::cannotSetHostSettings(const CHost &comHost, QWidget *pParent /* = 0 */) const
{
    error(pParent, MessageType_Critical,
          tr("Failed to set global host settings."),
          UIErrorString::formatErrorInfo(comHost));
}

void UIMessageCenter::cannotSetSystemProperties(const CSystemProperties &properties, QWidget *pParent /* = 0*/) const
{
    error(pParent, MessageType_Critical,
          tr("Failed to set global VirtualBox properties."),
          UIErrorString::formatErrorInfo(properties));
}

void UIMessageCenter::cannotSaveMachineSettings(const CMachine &machine, QWidget *pParent /* = 0*/) const
{
    error(pParent, MessageType_Error,
          tr("Failed to save the settings of the virtual machine <b>%1</b> to <b><nobr>%2</nobr></b>.")
             .arg(CMachine(machine).GetName(), CMachine(machine).GetSettingsFilePath()),
          UIErrorString::formatErrorInfo(machine));
}

void UIMessageCenter::cannotAddDiskEncryptionPassword(const CConsole &console)
{
    error(0, MessageType_Error,
          tr("Bad password or authentication failure."),
          UIErrorString::formatErrorInfo(console));
}

bool UIMessageCenter::confirmResetMachine(const QString &strNames) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Do you really want to reset the following virtual machines?</p>"
                             "<p><b>%1</b></p><p>This will cause any unsaved data "
                             "in applications running inside it to be lost.</p>")
                             .arg(strNames),
                          "confirmResetMachine" /* auto-confirm id */,
                          tr("Reset", "machine"));
}

void UIMessageCenter::cannotSaveSettings(const QString strDetails, QWidget *pParent /* = 0 */) const
{
    error(pParent, MessageType_Error,
          tr("Failed to save the settings."),
          strDetails);
}

void UIMessageCenter::warnAboutUnaccessibleUSB(const COMBaseWithEI &object, QWidget *pParent /* = 0*/) const
{
    /* If IMachine::GetUSBController(), IHost::GetUSBDevices() etc. return
     * E_NOTIMPL, it means the USB support is intentionally missing
     * (as in the OSE version). Don't show the error message in this case. */
    COMResult res(object);
    if (res.rc() == E_NOTIMPL)
        return;
    /* Show the error: */
    error(pParent, res.isWarning() ? MessageType_Warning : MessageType_Error,
          tr("Failed to access the USB subsystem."),
          UIErrorString::formatErrorInfo(res),
          "warnAboutUnaccessibleUSB");
}

void UIMessageCenter::warnAboutStateChange(QWidget *pParent /* = 0*/) const
{
    if (warningShown("warnAboutStateChange"))
        return;
    setWarningShown("warnAboutStateChange", true);

    alert(pParent, MessageType_Warning,
          tr("The virtual machine that you are changing has been started. "
             "Only certain settings can be changed while a machine is running. "
             "All other changes will be lost if you close this window now."));

    setWarningShown("warnAboutStateChange", false);
}

bool UIMessageCenter::confirmSettingsDiscarding(QWidget *pParent /* = 0 */) const
{
    if (   warningShown("confirmSettingsDiscarding")
        || warningShown("confirmSettingsReloading"))
        return false;
    setWarningShown("confirmSettingsDiscarding", true);

    const bool fResult = questionBinary(pParent, MessageType_Question,
                                        tr("<p>The machine settings were changed.</p>"
                                           "<p>Would you like to discard the changed settings or to keep editing them?</p>"),
                                        0 /* auto-confirm id */,
                                        tr("Discard changes"), tr("Keep editing"));

    setWarningShown("confirmSettingsDiscarding", false);

    return fResult;
}

bool UIMessageCenter::confirmSettingsReloading(QWidget *pParent /* = 0 */) const
{
    if (   warningShown("confirmSettingsReloading")
        || warningShown("confirmSettingsDiscarding"))
        return false;
    setWarningShown("confirmSettingsReloading", true);

    const bool fResult = questionBinary(pParent, MessageType_Question,
                                        tr("<p>The machine settings were changed while you were editing them. "
                                           "You currently have unsaved setting changes.</p>"
                                           "<p>Would you like to reload the changed settings or to keep your own changes?</p>"),
                                        0 /* auto-confirm id */,
                                        tr("Reload settings"), tr("Keep changes"));

    setWarningShown("confirmSettingsReloading", false);

    return fResult;
}

int UIMessageCenter::confirmRemovingOfLastDVDDevice(QWidget *pParent /* = 0*/) const
{
    return questionBinary(pParent, MessageType_Info,
                          tr("<p>Are you sure you want to delete the optical drive?</p>"
                             "<p>You will not be able to insert any optical disks or ISO images "
                             "or install the Guest Additions without it!</p>"),
                          0 /* auto-confirm id */,
                          tr("Remove", "medium") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmStorageBusChangeWithOpticalRemoval(QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>This controller has optical devices attached.  You have requested storage bus "
                             "change to type which doesn't support optical devices.</p><p>If you proceed optical "
                             "devices will be removed.</p>"));
}

bool UIMessageCenter::confirmStorageBusChangeWithExcessiveRemoval(QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>This controller has devices attached.  You have requested storage bus change to "
                             "type which supports smaller amount of attached devices.</p><p>If you proceed "
                             "excessive devices will be removed.</p>"));
}

bool UIMessageCenter::warnAboutIncorrectPort(QWidget *pParent /* = 0 */) const
{
    alert(pParent, MessageType_Error,
          tr("The current port forwarding rules are not valid. "
             "None of the host or guest port values may be set to zero."));
    return false;
}

bool UIMessageCenter::warnAboutIncorrectAddress(QWidget *pParent /* = 0 */) const
{
    alert(pParent, MessageType_Error,
          tr("The current port forwarding rules are not valid. "
             "All of the host or guest address values should be correct or empty."));
    return false;
}

bool UIMessageCenter::warnAboutEmptyGuestAddress(QWidget *pParent /* = 0 */) const
{
    alert(pParent, MessageType_Error,
          tr("The current port forwarding rules are not valid. "
             "None of the guest address values may be empty."));
    return false;
}

bool UIMessageCenter::warnAboutNameShouldBeUnique(QWidget *pParent /* = 0 */) const
{
    alert(pParent, MessageType_Error,
          tr("The current port forwarding rules are not valid. "
             "Rule names should be unique."));
    return false;
}

bool UIMessageCenter::warnAboutRulesConflict(QWidget *pParent /* = 0 */) const
{
    alert(pParent, MessageType_Error,
          tr("The current port forwarding rules are not valid. "
             "Few rules have same host ports and conflicting IP addresses."));
    return false;
}

bool UIMessageCenter::confirmCancelingPortForwardingDialog(QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>There are unsaved changes in the port forwarding configuration.</p>"
                             "<p>If you proceed your changes will be discarded.</p>"),
                          0 /* auto-confirm id */,
                          QString() /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmRestoringDefaultKeys(QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>You are going to restore default secure boot keys.</p>"
                             "<p>If you proceed your current keys will be rewritten. "
                             "You may not be able to boot affected VM anymore.</p>"),
                          0 /* auto-confirm id */,
                          QString() /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::warnAboutInaccessibleMedia() const
{
    return questionBinary(0, MessageType_Warning,
                          tr("<p>One or more disk image files are not currently accessible. As a result, you will "
                             "not be able to operate virtual machines that use these files until "
                             "they become accessible later.</p>"
                             "<p>Press <b>Check</b> to open the Virtual Media Manager window and "
                             "see which files are inaccessible, or press <b>Ignore</b> to "
                             "ignore this message.</p>"),
                          "warnAboutInaccessibleMedia",
                          tr("Check", "inaccessible media message box"), tr("Ignore"));
}

bool UIMessageCenter::confirmDiscardSavedState(const QString &strNames) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Are you sure you want to discard the saved state of "
                             "the following virtual machines?</p><p><b>%1</b></p>"
                             "<p>This operation is equivalent to resetting or powering off "
                             "the machine without doing a proper shut down of the guest OS.</p>")
                             .arg(strNames),
                          0 /* auto-confirm id */,
                          tr("Discard", "saved state"));
}

bool UIMessageCenter::confirmTerminateCloudInstance(const QString &strNames) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Are you sure you want to terminate the cloud instance "
                             "of the following virtual machines?</p><p><b>%1</b></p>")
                             .arg(strNames),
                          0 /* auto-confirm id */,
                          tr("Terminate", "cloud instance"));
}

bool UIMessageCenter::confirmACPIShutdownMachine(const QString &strNames) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Shut down these VMs by sending the "
                             "ACPI shutdown signal?</p><p><b>%1</b></p>")
                             .arg(strNames),
                          "confirmACPIShutdownMachine" /* auto-confirm id */,
                          tr("Shut Down", "machine"));
}

bool UIMessageCenter::confirmPowerOffMachine(const QString &strNames) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Close these VMs with no shutdown procedure?</p>"
                             "<p><b>%1</b></p><p>Unsaved data in applications "
                             "running on the VM will be lost.</p>")
                             .arg(strNames),
                          "confirmPowerOffMachine" /* auto-confirm id */,
                          tr("Power Off", "machine"));
}

bool UIMessageCenter::confirmStartMultipleMachines(const QString &strNames) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>You are about to start all of the following virtual machines:</p>"
                             "<p><b>%1</b></p><p>This could take some time and consume a lot of "
                             "host system resources. Do you wish to proceed?</p>").arg(strNames),
                          "confirmStartMultipleMachines" /* auto-confirm id */);
}

bool UIMessageCenter::confirmAutomaticCollisionResolve(const QString &strName, const QString &strGroupName) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>You are trying to move group <nobr><b>%1</b></nobr> to group "
                             "<nobr><b>%2</b></nobr> which already have another item with the same name.</p>"
                             "<p>Would you like to automatically rename it?</p>")
                             .arg(strName, strGroupName),
                          0 /* auto-confirm id */,
                          tr("Rename"));
}

void UIMessageCenter::cannotSetGroups(const CMachine &machine) const
{
    /* Compose machine name: */
    QString strName = CMachine(machine).GetName();
    if (strName.isEmpty())
        strName = QFileInfo(CMachine(machine).GetSettingsFilePath()).baseName();
    /* Show the error: */
    error(0, MessageType_Error,
          tr("Failed to set groups of the virtual machine <b>%1</b>.")
             .arg(strName),
          UIErrorString::formatErrorInfo(machine));
}

bool UIMessageCenter::confirmMachineItemRemoval(const QStringList &names) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Remove these virtual machine items from the machine list?</p>"
                             "<p><b>%1</b></p>")
                             .arg(names.join(", ")),
                          0 /* auto-confirm id */,
                          tr("Remove") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

int UIMessageCenter::confirmMachineRemoval(const QList<CMachine> &machines) const
{
    /* Enumerate the machines: */
    int cInacessibleMachineCount = 0;
    bool fMachineWithHardDiskPresent = false;
    QStringList machineNames;
    foreach (const CMachine &machine, machines)
    {
        /* Prepare machine name: */
        QString strMachineName;
        if (machine.GetAccessible())
        {
            /* Just get machine name: */
            strMachineName = machine.GetName();
            /* Enumerate the attachments: */
            const CMediumAttachmentVector &attachments = machine.GetMediumAttachments();
            foreach (const CMediumAttachment &attachment, attachments)
            {
                /* Check if the medium is a hard disk: */
                if (attachment.GetType() == KDeviceType_HardDisk)
                {
                    /* Check if that hard disk isn't shared.
                     * If hard disk is shared, it will *never* be deleted: */
                    QVector<QUuid> usedMachineList = attachment.GetMedium().GetMachineIds();
                    if (usedMachineList.size() == 1)
                    {
                        fMachineWithHardDiskPresent = true;
                        break;
                    }
                }
            }
        }
        else
        {
            /* Compose machine name: */
            QFileInfo fi(machine.GetSettingsFilePath());
            strMachineName = UICommon::hasAllowedExtension(fi.completeSuffix(), VBoxFileExts) ? fi.completeBaseName() : fi.fileName();
            /* Increment inacessible machine count: */
            ++cInacessibleMachineCount;
        }
        /* Append machine name: */
        machineNames << strMachineName;
    }

    /* Prepare message text: */
    const QString strText = tr("<p>Remove these virtual machines from the machine list?</p>"
                               "<p><b>%1</b></p>")
                               .arg(machineNames.join(", "));

    /* Prepare option text: */
    const QString strOption = fMachineWithHardDiskPresent
                            ? tr("Delete the virtual machine files and virtual hard disks.")
                            : tr("Delete the virtual machine files.");

    /* Prepare message itself: */
    return cInacessibleMachineCount == machines.size() ?
           message(0, MessageType_Question,
                   strText, QString(),
                   0 /* auto-confirm id */,
                   AlertButton_Ok,
                   AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape,
                   0,
                   tr("Remove")) :
           messageWithOption(0, MessageType_Question,
                             strText, strOption,
                             false /* default option value */,
                             AlertButton_Ok,
                             AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape,
                             0,
                             tr("Remove"));
}

int UIMessageCenter::confirmCloudMachineRemoval(const QList<CCloudMachine> &machines) const
{
    /* Enumerate the machines: */
    QStringList machineNames;
    foreach (const CCloudMachine &comMachine, machines)
    {
        /* Append machine name to the full name string: */
        if (comMachine.GetAccessible())
            machineNames << QString("<b>%1</b>").arg(comMachine.GetName());
    }

    /* Prepare message text: */
    QString strText = tr("<p>You are about to remove following cloud virtual machines from the machine list:</p>"
                         "<p>%1</p>"
                         "<p>Would you like to delete the instances and boot volumes of these machines as well?</p>")
                         .arg(machineNames.join(", "));

    /* Prepare message itself: */
    return message(0, MessageType_Question,
                   strText, QString(),
                   0 /* auto-confirm id */,
                   AlertButton_Choice1,
                   AlertButton_Choice2,
                   AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape,
                   tr("Delete everything"),
                   tr("Remove only"));
}

int UIMessageCenter::confirmSnapshotRestoring(const QString &strSnapshotName, bool fAlsoCreateNewSnapshot) const
{
    return fAlsoCreateNewSnapshot ?
           messageWithOption(0, MessageType_Question,
                             tr("<p>You are about to restore snapshot <nobr><b>%1</b></nobr>.</p>"
                                "<p>You can create a snapshot of the current state of the virtual machine first by checking the box below; "
                                "if you do not do this the current state will be permanently lost. Do you wish to proceed?</p>")
                                .arg(strSnapshotName),
                             tr("Create a snapshot of the current machine state"),
                             !gEDataManager->messagesWithInvertedOption().contains("confirmSnapshotRestoring"),
                             AlertButton_Ok,
                             AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape,
                             0 /* 3rd button */,
                             tr("Restore"), tr("Cancel"), QString() /* 3rd button text */) :
           message(0, MessageType_Question,
                   tr("<p>Are you sure you want to restore snapshot <nobr><b>%1</b></nobr>?</p>")
                      .arg(strSnapshotName),
                   QString() /* details */,
                   0 /* auto-confirm id */,
                   AlertButton_Ok,
                   AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape,
                   0 /* 3rd button */,
                   tr("Restore"), tr("Cancel"), QString() /* 3rd button text */);
}

bool UIMessageCenter::confirmSnapshotRemoval(const QString &strSnapshotName) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Deleting the snapshot will cause the state information saved in it to be lost, and storage data spread over "
                             "several image files that VirtualBox has created together with the snapshot will be merged into one file. "
                             "This can be a lengthy process, and the information in the snapshot cannot be recovered.</p>"
                             "</p>Are you sure you want to delete the selected snapshot <b>%1</b>?</p>")
                             .arg(strSnapshotName),
                          0 /* auto-confirm id */,
                          tr("Delete") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::warnAboutSnapshotRemovalFreeSpace(const QString &strSnapshotName,
                                                        const QString &strTargetImageName,
                                                        const QString &strTargetImageMaxSize,
                                                        const QString &strTargetFileSystemFree) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Deleting the snapshot %1 will temporarily need more storage space. In the worst case the size of image %2 will grow by %3, "
                              "however on this filesystem there is only %4 free.</p><p>Running out of storage space during the merge operation can result in "
                              "corruption of the image and the VM configuration, i.e. loss of the VM and its data.</p><p>You may continue with deleting "
                              "the snapshot at your own risk.</p>")
                              .arg(strSnapshotName, strTargetImageName, strTargetImageMaxSize, strTargetFileSystemFree),
                          0 /* auto-confirm id */,
                          tr("Delete") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmInstallExtensionPack(const QString &strPackName, const QString &strPackVersion,
                                                  const QString &strPackDescription, QWidget *pParent /* = 0*/) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>You are about to install a VirtualBox extension pack. "
                             "Extension packs complement the functionality of VirtualBox and can contain system level software "
                             "that could be potentially harmful to your system. Please review the description below and only proceed "
                             "if you have obtained the extension pack from a trusted source.</p>"
                             "<p><table cellpadding=0 cellspacing=5>"
                             "<tr><td><b>Name:&nbsp;&nbsp;</b></td><td>%1</td></tr>"
                             "<tr><td><b>Version:&nbsp;&nbsp;</b></td><td>%2</td></tr>"
                             "<tr><td><b>Description:&nbsp;&nbsp;</b></td><td>%3</td></tr>"
                             "</table></p>")
                             .arg(strPackName).arg(strPackVersion).arg(strPackDescription),
                          0 /* auto-confirm id */,
                          tr("Install", "extension pack"));
}

bool UIMessageCenter::confirmReplaceExtensionPack(const QString &strPackName, const QString &strPackVersionNew,
                                                  const QString &strPackVersionOld, const QString &strPackDescription,
                                                  QWidget *pParent /* = 0*/) const
{
    /* Prepare initial message: */
    QString strBelehrung = tr("Extension packs complement the functionality of VirtualBox and can contain "
                              "system level software that could be potentially harmful to your system. "
                              "Please review the description below and only proceed if you have obtained "
                              "the extension pack from a trusted source.");

    /* Compare versions: */
    QByteArray  ba1     = strPackVersionNew.toUtf8();
    QByteArray  ba2     = strPackVersionOld.toUtf8();
    int         iVerCmp = RTStrVersionCompare(ba1.constData(), ba2.constData());

    /* Show the question: */
    bool fRc;
    if (iVerCmp > 0)
        fRc = questionBinary(pParent, MessageType_Question,
                             tr("<p>An older version of the extension pack is already installed, would you like to upgrade? "
                                "<p>%1</p>"
                                "<p><table cellpadding=0 cellspacing=5>"
                                "<tr><td><b>Name:&nbsp;&nbsp;</b></td><td>%2</td></tr>"
                                "<tr><td><b>New Version:&nbsp;&nbsp;</b></td><td>%3</td></tr>"
                                "<tr><td><b>Current Version:&nbsp;&nbsp;</b></td><td>%4</td></tr>"
                                "<tr><td><b>Description:&nbsp;&nbsp;</b></td><td>%5</td></tr>"
                                "</table></p>")
                                .arg(strBelehrung).arg(strPackName).arg(strPackVersionNew).arg(strPackVersionOld).arg(strPackDescription),
                             0 /* auto-confirm id */,
                             tr("&Upgrade"));
    else if (iVerCmp < 0)
        fRc = questionBinary(pParent, MessageType_Question,
                             tr("<p>An newer version of the extension pack is already installed, would you like to downgrade? "
                                "<p>%1</p>"
                                "<p><table cellpadding=0 cellspacing=5>"
                                "<tr><td><b>Name:&nbsp;&nbsp;</b></td><td>%2</td></tr>"
                                "<tr><td><b>New Version:&nbsp;&nbsp;</b></td><td>%3</td></tr>"
                                "<tr><td><b>Current Version:&nbsp;&nbsp;</b></td><td>%4</td></tr>"
                                "<tr><td><b>Description:&nbsp;&nbsp;</b></td><td>%5</td></tr>"
                                "</table></p>")
                                .arg(strBelehrung).arg(strPackName).arg(strPackVersionNew).arg(strPackVersionOld).arg(strPackDescription),
                             0 /* auto-confirm id */,
                             tr("&Downgrade"));
    else
        fRc = questionBinary(pParent, MessageType_Question,
                             tr("<p>The extension pack is already installed with the same version, would you like reinstall it? "
                                "<p>%1</p>"
                                "<p><table cellpadding=0 cellspacing=5>"
                                "<tr><td><b>Name:&nbsp;&nbsp;</b></td><td>%2</td></tr>"
                                "<tr><td><b>Version:&nbsp;&nbsp;</b></td><td>%3</td></tr>"
                                "<tr><td><b>Description:&nbsp;&nbsp;</b></td><td>%4</td></tr>"
                                "</table></p>")
                                .arg(strBelehrung).arg(strPackName).arg(strPackVersionOld).arg(strPackDescription),
                             0 /* auto-confirm id */,
                             tr("&Reinstall"));
    return fRc;
}

bool UIMessageCenter::confirmRemoveExtensionPack(const QString &strPackName, QWidget *pParent /* = 0*/) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>You are about to remove the VirtualBox extension pack <b>%1</b>.</p>"
                             "<p>Are you sure you want to proceed?</p>")
                             .arg(strPackName),
                          0 /* auto-confirm id */,
                          tr("Remove") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmMediumRelease(const UIMedium &medium, bool fInduced, QWidget *pParent /* = 0 */) const
{
    /* Prepare the usage: */
    QStringList usage;
    CVirtualBox vbox = gpGlobalSession->virtualBox();
    foreach (const QUuid &uMachineID, medium.curStateMachineIds())
    {
        CMachine machine = vbox.FindMachine(uMachineID.toString());
        if (!vbox.isOk() || machine.isNull())
            continue;
        usage << machine.GetName();
    }
    /* Show the question: */
    return !fInduced
           ? questionBinary(pParent, MessageType_Question,
                            tr("<p>Are you sure you want to release the disk image file <nobr><b>%1</b></nobr>?</p>"
                               "<p>This will detach it from the following virtual machine(s): <b>%2</b>.</p>")
                               .arg(medium.location(), usage.join(", ")),
                            0 /* auto-confirm id */,
                            tr("Release", "detach medium"))
           : questionBinary(pParent, MessageType_Question,
                            tr("<p>The changes you requested require this disk to "
                               "be released from the machines it is attached to.</p>"
                               "<p>Are you sure you want to release the disk image file <nobr><b>%1</b></nobr>?</p>"
                               "<p>This will detach it from the following virtual machine(s): <b>%2</b>.</p>")
                               .arg(medium.location(), usage.join(", ")),
                            0 /* auto-confirm id */,
                            tr("Release", "detach medium"));
}

bool UIMessageCenter::confirmMediumRemoval(const UIMedium &medium, QWidget *pParent /* = 0*/) const
{
    /* Prepare the message: */
    QString strMessage;
    switch (medium.type())
    {
        case UIMediumDeviceType_HardDisk:
        {
            strMessage = tr("<p>Are you sure you want to remove the virtual hard disk "
                            "<nobr><b>%1</b></nobr> from the list of known disk image files?</p>");
            /* Compose capabilities flag: */
            qulonglong caps = 0;
            QVector<KMediumFormatCapabilities> capabilities;
            capabilities = medium.medium().GetMediumFormat().GetCapabilities();
            for (int i = 0; i < capabilities.size(); ++i)
                caps |= capabilities[i];
            /* Check capabilities for additional options: */
            if (caps & KMediumFormatCapabilities_File)
            {
                if (medium.state() == KMediumState_Inaccessible)
                    strMessage += tr("<p>As this hard disk is inaccessible its image file"
                                     " cannot be deleted.</p>");
            }
            break;
        }
        case UIMediumDeviceType_DVD:
        {
            strMessage = tr("<p>Are you sure you want to remove the virtual optical disk "
                            "<nobr><b>%1</b></nobr> from the list of known disk image files?</p>");
            strMessage += tr("<p>Note that the storage unit of this medium will not be "
                             "deleted and that it will be possible to use it later again.</p>");
            break;
        }
        case UIMediumDeviceType_Floppy:
        {
            strMessage = tr("<p>Are you sure you want to remove the virtual floppy disk "
                            "<nobr><b>%1</b></nobr> from the list of known disk image files?</p>");
            strMessage += tr("<p>Note that the storage unit of this medium will not be "
                             "deleted and that it will be possible to use it later again.</p>");
            break;
        }
        default:
            break;
    }
    /* Show the question: */
    return questionBinary(pParent, MessageType_Question,
                          strMessage.arg(medium.location()),
                          0 /* auto-confirm id */,
                          tr("Remove", "medium") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

int UIMessageCenter::confirmDeleteHardDiskStorage(const QString &strLocation, QWidget *pParent /* = 0*/) const
{
    return questionTrinary(pParent, MessageType_Question,
                           tr("<p>Do you want to delete the storage unit of the virtual hard disk "
                              "<nobr><b>%1</b></nobr>?</p>"
                              "<p>If you select <b>Delete</b> then the specified storage unit "
                              "will be permanently deleted. This operation <b>cannot be "
                              "undone</b>.</p>"
                              "<p>If you select <b>Keep</b> then the hard disk will be only "
                              "removed from the list of known hard disks, but the storage unit "
                              "will be left untouched which makes it possible to add this hard "
                              "disk to the list later again.</p>")
                              .arg(strLocation),
                           0 /* auto-confirm id */,
                           tr("Delete", "hard disk storage"),
                           tr("Keep", "hard disk storage"));
}

bool UIMessageCenter::confirmInaccesibleMediaClear(const QStringList &mediaNameList, UIMediumDeviceType enmType, QWidget *pParent /* = 0 */)
{
    if (mediaNameList.isEmpty())
        return false;

    if (enmType != UIMediumDeviceType_DVD && enmType != UIMediumDeviceType_Floppy)
        return false;

    QString strDetails("<!--EOM-->");
    QString strDetailMessage;

    if (enmType == UIMediumDeviceType_DVD)
        strDetailMessage = tr("The list of inaccessible DVDs is as follows:");
    else
        strDetailMessage = tr("The list of inaccessible floppy disks is as follows:");


    if (!strDetailMessage.isEmpty())
        strDetails.prepend(QString("<p>%1.</p>").arg(UITranslator::emphasize(strDetailMessage)));

    strDetails += QString("<table bgcolor=%1 border=0 cellspacing=5 cellpadding=0 width=100%>")
                         .arg(QApplication::palette().color(QPalette::Active, QPalette::Window).name(QColor::HexRgb));
    foreach (const QString &strDVD, mediaNameList)
        strDetails += QString("<tr><td>%1</td></tr>").arg(strDVD);
    strDetails += QString("</table>");

    if (!strDetails.isEmpty())
        strDetails = "<qt>" + strDetails + "</qt>";

    if (enmType == UIMediumDeviceType_DVD)
        return message(pParent,
                       MessageType_Question,
                       tr("<p>This will clear the optical disk list by releasing inaccessible DVDs"
                          " from the virtual machines they are attached to"
                          " and removing them from the list of registered media.<p>"
                          "Are you sure?"),
                       strDetails,
                       0 /* auto-confirm id */,
                       AlertButton_Ok,
                       AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape,
                       0 /* third button */,
                       tr("Clear") /* ok button text */,
                       QString() /* cancel button text */,
                       QString() /* 3rd button text */,
                       QString() /* help keyword */);
    else
        return message(pParent,
                       MessageType_Question,
                       tr("<p>This will clear the floppy disk list by releasing inaccessible disks"
                          " from the virtual machines they are attached to"
                          " and removing them from the list of registered media.<p>"
                          "Are you sure?"),
                       strDetails,
                       0 /* auto-confirm id */,
                       AlertButton_Ok,
                       AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape,
                       0 /* third button */,
                       tr("Clear") /* ok button text */,
                       QString() /* cancel button text */,
                       QString() /* 3rd button text */,
                       QString() /* help keyword */);
}

bool UIMessageCenter::confirmVisoDiscard(QWidget *pParent /* = 0*/) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>To open a VISO file you will have to discard the current content.</p>"
                             "<p>Are you sure you want to proceed?</p>"),
                          0 /* auto-confirm id */,
                          tr("Discard") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmCloudNetworkRemoval(const QString &strName, QWidget *pParent /* = 0*/) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>Do you want to remove the cloud network <nobr><b>%1</b>?</nobr></p>"
                             "<p>If this network is in use by one or more virtual "
                             "machine network adapters these adapters will no longer be "
                             "usable until you correct their settings by either choosing "
                             "a different network name or a different adapter attachment "
                             "type.</p>")
                             .arg(strName),
                          0 /* auto-confirm id */,
                          tr("Remove") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmHostNetworkInterfaceRemoval(const QString &strName, QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>Deleting this host-only network will remove "
                             "the host-only interface this network is based on. Do you want to "
                             "remove the (host-only network) interface <nobr><b>%1</b>?</nobr></p>"
                             "<p><b>Note:</b> this interface may be in use by one or more "
                             "virtual network adapters belonging to one of your VMs. "
                             "After it is removed, these adapters will no longer be usable until "
                             "you correct their settings by either choosing a different interface "
                             "name or a different adapter attachment type.</p>")
                             .arg(strName),
                          0 /* auto-confirm id */,
                          tr("Remove") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmHostOnlyNetworkRemoval(const QString &strName, QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>Do you want to remove the host-only network <nobr><b>%1</b>?</nobr></p>"
                             "<p>If this network is in use by one or more virtual "
                             "machine network adapters these adapters will no longer be "
                             "usable until you correct their settings by either choosing "
                             "a different network name or a different adapter attachment "
                             "type.</p>")
                             .arg(strName),
                          0 /* auto-confirm id */,
                          tr("Remove") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmNATNetworkRemoval(const QString &strName, QWidget *pParent /* = 0*/) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>Do you want to remove the NAT network <nobr><b>%1</b>?</nobr></p>"
                             "<p>If this network is in use by one or more virtual "
                             "machine network adapters these adapters will no longer be "
                             "usable until you correct their settings by either choosing "
                             "a different network name or a different adapter attachment "
                             "type.</p>")
                             .arg(strName),
                          0 /* auto-confirm id */,
                          tr("Remove") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmCloudProfileRemoval(const QString &strName, QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>Do you want to remove the cloud profile <nobr><b>%1</b>?</nobr></p>")
                             .arg(strName),
                          0 /* auto-confirm id */,
                          tr("Remove") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmCloudProfilesImport(QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>Do you want to import cloud profiles from external files?</p>"
                             "<p>VirtualBox cloud profiles will be overwritten and their data will be lost.</p>"),
                          0 /* auto-confirm id */,
                          tr("Import") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

int UIMessageCenter::confirmCloudProfileManagerClosing(QWidget *pParent /* = 0 */) const
{
    return question(pParent, MessageType_Question,
                    tr("<p>Do you want to close the Cloud Profile Manager?</p>"
                       "<p>There seems to be an unsaved changes. "
                       "You can choose to <b>Accept</b> or <b>Reject</b> them automatically "
                       "or cancel to keep the dialog opened.</p>"),
                    0 /* auto-confirm id */,
                    AlertButton_Choice1,
                    AlertButton_Choice2,
                    AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape,
                    tr("Accept", "cloud profile manager changes"),
                    tr("Reject", "cloud profile manager changes"));
}

bool UIMessageCenter::confirmCloudConsoleApplicationRemoval(const QString &strName, QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>Do you want to remove the cloud console application <nobr><b>%1</b>?</nobr></p>")
                             .arg(strName),
                          0 /* auto-confirm id */,
                          tr("Remove") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmCloudConsoleProfileRemoval(const QString &strName, QWidget *pParent /* = 0 */) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("<p>Do you want to remove the cloud console profile <nobr><b>%1</b>?</nobr></p>")
                             .arg(strName),
                          0 /* auto-confirm id */,
                          tr("Remove") /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
bool UIMessageCenter::confirmLookingForGuestAdditions() const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Could not find the <b>VirtualBox Guest Additions</b> disk image file.</p>"
                             "<p>Do you wish to download this disk image file from the Internet?</p>"),
                          0 /* auto-confirm id */,
                          tr("Download"));
}

bool UIMessageCenter::confirmDownloadGuestAdditions(const QString &strUrl, qulonglong uSize) const
{
    return questionBinary(windowManager().mainWindowShown(), MessageType_Question,
                          tr("<p>Are you sure you want to download the <b>VirtualBox Guest Additions</b> disk image file "
                             "from <nobr><a href=\"%1\">%1</a></nobr> (size %2 bytes)?</p>")
                             .arg(strUrl, QLocale(UITranslator::languageId()).toString(uSize)),
                          0 /* auto-confirm id */,
                          tr("Download"));
}

void UIMessageCenter::cannotSaveGuestAdditions(const QString &strURL, const QString &strTarget) const
{
    alert(windowManager().mainWindowShown(), MessageType_Error,
          tr("<p>The <b>VirtualBox Guest Additions</b> disk image file has been successfully downloaded "
             "from <nobr><a href=\"%1\">%1</a></nobr> "
             "but can't be saved locally as <nobr><b>%2</b>.</nobr></p>"
             "<p>Please choose another location for that file.</p>")
             .arg(strURL, strTarget));
}

bool UIMessageCenter::proposeMountGuestAdditions(const QString &strUrl, const QString &strSrc) const
{
    return questionBinary(windowManager().mainWindowShown(), MessageType_Question,
                          tr("<p>The <b>VirtualBox Guest Additions</b> disk image file has been successfully downloaded "
                             "from <nobr><a href=\"%1\">%1</a></nobr> "
                             "and saved locally as <nobr><b>%2</b>.</nobr></p>"
                             "<p>Do you wish to continue with Guest Additions installation?</p>")
                             .arg(strUrl, strSrc),
                          0 /* auto-confirm id */,
                          tr("Continue", "agree with additions installation"));
}

bool UIMessageCenter::confirmLookingForUserManual(const QString &strMissedLocation) const
{
    return questionBinary(0, MessageType_Question,
                          tr("<p>Could not find the <b>VirtualBox User Guide</b> <nobr><b>%1</b>.</nobr></p>"
                             "<p>Do you wish to download this file from the Internet?</p>")
                             .arg(strMissedLocation),
                          0 /* auto-confirm id */,
                          tr("Download"));
}

bool UIMessageCenter::confirmDownloadUserManual(const QString &strURL, qulonglong uSize) const
{
    return questionBinary(windowManager().mainWindowShown(), MessageType_Question,
                          tr("<p>Are you sure you want to download the <b>VirtualBox User Guide</b> "
                             "from <nobr><a href=\"%1\">%1</a></nobr> (size %2 bytes)?</p>")
                             .arg(strURL, QLocale(UITranslator::languageId()).toString(uSize)),
                          0 /* auto-confirm id */,
                          tr("Download"));
}

void UIMessageCenter::cannotSaveUserManual(const QString &strURL, const QString &strTarget) const
{
    alert(windowManager().mainWindowShown(), MessageType_Error,
          tr("<p>The VirtualBox User Guide has been successfully downloaded "
             "from <nobr><a href=\"%1\">%1</a></nobr> "
             "but can't be saved locally as <nobr><b>%2</b>.</nobr></p>"
             "<p>Please choose another location for that file.</p>")
             .arg(strURL, strTarget));
}

bool UIMessageCenter::confirmLookingForExtensionPack(const QString &strExtPackName, const QString &strExtPackVersion) const
{
    return questionBinary(windowManager().mainWindowShown(), MessageType_Question,
                          tr("<p>You have an old version (%1) of the <b><nobr>%2</nobr></b> installed.</p>"
                             "<p>Do you wish to download latest one from the Internet?</p>")
                             .arg(strExtPackVersion).arg(strExtPackName),
                          0 /* auto-confirm id */,
                          tr("Download"));
}

bool UIMessageCenter::confirmDownloadExtensionPack(const QString &strExtPackName, const QString &strURL, qulonglong uSize) const
{
    return questionBinary(windowManager().mainWindowShown(), MessageType_Question,
                          tr("<p>Are you sure you want to download the <b><nobr>%1</nobr></b> "
                             "from <nobr><a href=\"%2\">%2</a></nobr> (size %3 bytes)?</p>")
                             .arg(strExtPackName, strURL, QLocale(UITranslator::languageId()).toString(uSize)),
                          0 /* auto-confirm id */,
                          tr("Download"));
}

void UIMessageCenter::cannotSaveExtensionPack(const QString &strExtPackName, const QString &strFrom, const QString &strTo) const
{
    alert(windowManager().mainWindowShown(), MessageType_Error,
          tr("<p>The <b><nobr>%1</nobr></b> has been successfully downloaded "
             "from <nobr><a href=\"%2\">%2</a></nobr> "
             "but can't be saved locally as <nobr><b>%3</b>.</nobr></p>"
             "<p>Please choose another location for that file.</p>")
             .arg(strExtPackName, strFrom, strTo));
}

bool UIMessageCenter::proposeInstallExtentionPack(const QString &strExtPackName, const QString &strFrom, const QString &strTo) const
{
    return questionBinary(windowManager().mainWindowShown(), MessageType_Question,
                          tr("<p>The <b><nobr>%1</nobr></b> has been successfully downloaded "
                             "from <nobr><a href=\"%2\">%2</a></nobr> "
                             "and saved locally as <nobr><b>%3</b>.</nobr></p>"
                             "<p>Do you wish to install this extension pack?</p>")
                             .arg(strExtPackName, strFrom, strTo),
                          0 /* auto-confirm id */,
                          tr("Install", "extension pack"));
}

bool UIMessageCenter::proposeDeleteExtentionPack(const QString &strTo) const
{
    return questionBinary(windowManager().mainWindowShown(), MessageType_Question,
                          tr("Do you want to delete the downloaded file <nobr><b>%1</b></nobr>?")
                             .arg(strTo),
                          0 /* auto-confirm id */,
                          tr("Delete", "extension pack"));
}

bool UIMessageCenter::proposeDeleteOldExtentionPacks(const QStringList &strFiles) const
{
    return questionBinary(windowManager().mainWindowShown(), MessageType_Question,
                          tr("Do you want to delete following list of files <nobr><b>%1</b></nobr>?")
                             .arg(strFiles.join(",")),
                          0 /* auto-confirm id */,
                          tr("Delete", "extension pack"));
}
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */

bool UIMessageCenter::cannotRestoreSnapshot(const CMachine &machine, const QString &strSnapshotName, const QString &strMachineName) const
{
    error(0, MessageType_Error,
          tr("Failed to restore the snapshot <b>%1</b> of the virtual machine <b>%2</b>.")
             .arg(strSnapshotName, strMachineName),
          UIErrorString::formatErrorInfo(machine));
    return false;
}

bool UIMessageCenter::cannotRestoreSnapshot(const CProgress &progress, const QString &strSnapshotName, const QString &strMachineName) const
{
    error(0, MessageType_Error,
          tr("Failed to restore the snapshot <b>%1</b> of the virtual machine <b>%2</b>.")
             .arg(strSnapshotName, strMachineName),
          UIErrorString::formatErrorInfo(progress));
    return false;
}

void UIMessageCenter::cannotStartMachine(const CConsole &console, const QString &strName) const
{
    error(0, MessageType_Error,
          tr("Failed to start the virtual machine <b>%1</b>.")
             .arg(strName),
          UIErrorString::formatErrorInfo(console));
}

void UIMessageCenter::cannotStartMachine(const CProgress &progress, const QString &strName) const
{
    error(0, MessageType_Error,
          tr("Failed to start the virtual machine <b>%1</b>.")
             .arg(strName),
          UIErrorString::formatErrorInfo(progress));
}

bool UIMessageCenter::warnAboutNetworkInterfaceNotFound(const QString &strMachineName, const QString &strIfNames) const
{
    return questionBinary(0, MessageType_Error,
                          tr("<p>Could not start the machine <b>%1</b> because the following "
                             "physical network interfaces were not found:</p><p><b>%2</b></p>"
                             "<p>You can either change the machine's network settings or stop the machine.</p>")
                             .arg(strMachineName, strIfNames),
                          0 /* auto-confirm id */,
                          tr("Change Network Settings"), tr("Close VM"));
}

void UIMessageCenter::warnAboutVBoxSVCUnavailable() const
{
    alert(0, MessageType_Critical,
          tr("<p>A critical error has occurred while running the virtual "
             "machine and the machine execution should be stopped.</p>"
             ""
             "<p>For help, please see the Community section on "
             "<a href=https://www.virtualbox.org>https://www.virtualbox.org</a> "
             "or your support contract. Please provide the contents of the "
             "log file <tt>VBox.log</tt>, "
             "which you can find in the virtual machine log directory, "
             "as well as a description of what you were doing when this error happened. "
             ""
             "Note that you can also access the above file by selecting <b>Show Log</b> "
             "from the <b>Machine</b> menu of the main VirtualBox window.</p>"
             ""
             "<p>Press <b>OK</b> to power off the machine.</p>"),
          0 /* auto-confirm id */);
}

bool UIMessageCenter::warnAboutGuruMeditation(const QString &strLogFolder)
{
    return questionBinary(0, MessageType_GuruMeditation,
                          tr("<p>A critical error has occurred while running the virtual "
                             "machine and the machine execution has been stopped.</p>"
                             ""
                             "<p>For help, please see the Community section on "
                             "<a href=https://www.virtualbox.org>https://www.virtualbox.org</a> "
                             "or your support contract. Please provide the contents of the "
                             "log file <tt>VBox.log</tt> and the image file <tt>VBox.png</tt>, "
                             "which you can find in the <nobr><b>%1</b></nobr> directory, "
                             "as well as a description of what you were doing when this error happened. "
                             ""
                             "Note that you can also access the above files by selecting <b>Show Log</b> "
                             "from the <b>Machine</b> menu of the main VirtualBox window.</p>"
                             ""
                             "<p>Press <b>OK</b> if you want to power off the machine "
                             "or press <b>Ignore</b> if you want to leave it as is for debugging. "
                             "Please note that debugging requires special knowledge and tools, "
                             "so it is recommended to press <b>OK</b> now.</p>")
                             .arg(strLogFolder),
                          0 /* auto-confirm id */,
                          QIMessageBox::tr("OK"),
                          tr("Ignore"));
}

void UIMessageCenter::showRuntimeError(MessageType emnMessageType, const QString &strErrorId, const QString &strErrorMsg) const
{
    /* Gather suitable severity and confirm id: */
    QString strSeverity;
    QByteArray autoConfimId = "showRuntimeError.";
    switch (emnMessageType)
    {
        case MessageType_Warning:
        {
            strSeverity = tr("<nobr>Warning</nobr>", "runtime error info");
            autoConfimId += "warning.";
            break;
        }
        case MessageType_Error:
        {
            strSeverity = tr("<nobr>Non-Fatal Error</nobr>", "runtime error info");
            autoConfimId += "error.";
            break;
        }
        case MessageType_Critical:
        {
            strSeverity = tr("<nobr>Fatal Error</nobr>", "runtime error info");
            autoConfimId += "fatal.";
            break;
        }
        default:
            break;
    }
    autoConfimId += strErrorId.toUtf8();

    /* Format error-details: */
    QString formatted("<!--EOM-->");
    if (!strErrorMsg.isEmpty())
        formatted.prepend(QString("<p>%1.</p>").arg(UITranslator::emphasize(strErrorMsg)));
    if (!strErrorId.isEmpty())
        formatted += QString("<table bgcolor=%1 border=0 cellspacing=5 "
                             "cellpadding=0 width=100%>"
                             "<tr><td>%2</td><td>%3</td></tr>"
                             "<tr><td>%4</td><td>%5</td></tr>"
                             "</table>")
                             .arg(QApplication::palette().color(QPalette::Active, QPalette::Window).name(QColor::HexRgb))
                             .arg(tr("<nobr>Error ID:</nobr>", "runtime error info"), strErrorId)
                             .arg(tr("Severity:", "runtime error info"), strSeverity);
    if (!formatted.isEmpty())
        formatted = "<qt>" + formatted + "</qt>";

    /* Show the error: */
    switch (emnMessageType)
    {
        case MessageType_Warning:
        {
            /** @todo r=bird: This is a very annoying message as it refers to invisible text
             * below.  User have to expand "Details" to see what actually went wrong.
             * Probably a good idea to check strErrorId and see if we can come up with better
             * messages here, at least for common stuff like DvdOrFloppyImageInaccesssible... */
            error(0, emnMessageType,
                  tr("<p>The virtual machine execution ran into a non-fatal problem as described below. "
                     "We suggest that you take appropriate action to prevent the problem from recurring.</p>"),
                  formatted, autoConfimId.data());
            break;
        }
        case MessageType_Error:
        {
            error(0, emnMessageType,
                  tr("<p>An error has occurred during virtual machine execution! "
                     "The error details are shown below. You may try to correct the error "
                     "and resume the virtual machine execution.</p>"),
                  formatted, autoConfimId.data());
            break;
        }
        case MessageType_Critical:
        {
            error(0, emnMessageType,
                  tr("<p>A fatal error has occurred during virtual machine execution! "
                     "The virtual machine will be powered off. Please copy the following error message "
                     "using the clipboard to help diagnose the problem:</p>"),
                  formatted, autoConfimId.data());
            break;
        }
        default:
            break;
    }
}

bool UIMessageCenter::confirmInputCapture(bool &fAutoConfirmed) const
{
    int rc = question(0, MessageType_Info,
                      tr("<p>You have <b>clicked the mouse</b> inside the Virtual Machine display or pressed the <b>host key combo</b>. "
                         "This will cause the Virtual Machine to <b>capture</b> the host mouse pointer (only if the mouse pointer "
                         "integration is not currently supported by the guest OS) and the keyboard, which will make them "
                         "unavailable to other applications running on your host machine.</p>"
                         "<p>You can press the <b>host key combo</b> at any time to <b>uncapture</b> the keyboard and mouse "
                         "(if it is captured) and return them to normal operation. "
                         "The currently assigned host key combo is shown on the status bar at the bottom of the Virtual Machine window, "
                         "next to the&nbsp;<img src=:/hostkey_16px.png/>&nbsp;icon. "
                         "This icon, together with the mouse icon placed nearby, indicate the current keyboard and mouse capture state.</p>") +
                      tr("<p>The host key combo is currently defined as <b>%1</b>.</p>", "additional message box paragraph")
                         .arg(UIHostCombo::toReadableString(gEDataManager->hostKeyCombination())),
                      "confirmInputCapture",
                      AlertButton_Ok | AlertButtonOption_Default,
                      AlertButton_Cancel | AlertButtonOption_Escape,
                      0,
                      tr("Capture", "do input capture"));
    /* Was the message auto-confirmed? */
    fAutoConfirmed = (rc & AlertOption_AutoConfirmed);
    /* True if "Ok" was pressed: */
    return (rc & AlertButtonMask) == AlertButton_Ok;
}

bool UIMessageCenter::confirmGoingFullscreen(const QString &strHotKey) const
{
    return questionBinary(0, MessageType_Info,
                          tr("<p>The virtual machine window will be now switched to <b>full-screen</b> mode. "
                             "You can go back to windowed mode at any time by pressing <b>%1</b>.</p>"
                             "<p>Note that the <i>Host</i> key is currently defined as <b>%2</b>.</p>"
                             "<p>Note that the main menu bar is hidden in full-screen mode. "
                             "You can access it by pressing <b>Host+Home</b>.</p>")
                             .arg(strHotKey, UIHostCombo::toReadableString(gEDataManager->hostKeyCombination())),
                          "confirmGoingFullscreen",
                          tr("Switch"));
}

bool UIMessageCenter::confirmGoingSeamless(const QString &strHotKey) const
{
    return questionBinary(0, MessageType_Info,
                          tr("<p>The virtual machine window will be now switched to <b>Seamless</b> mode. "
                             "You can go back to windowed mode at any time by pressing <b>%1</b>.</p>"
                             "<p>Note that the <i>Host</i> key is currently defined as <b>%2</b>.</p>"
                             "<p>Note that the main menu bar is hidden in seamless mode. "
                             "You can access it by pressing <b>Host+Home</b>.</p>")
                             .arg(strHotKey, UIHostCombo::toReadableString(gEDataManager->hostKeyCombination())),
                          "confirmGoingSeamless",
                          tr("Switch"));
}

bool UIMessageCenter::confirmGoingScale(const QString &strHotKey) const
{
    return questionBinary(0, MessageType_Info,
                          tr("<p>The virtual machine window will be now switched to <b>Scale</b> mode. "
                             "You can go back to windowed mode at any time by pressing <b>%1</b>.</p>"
                             "<p>Note that the <i>Host</i> key is currently defined as <b>%2</b>.</p>"
                             "<p>Note that the main menu bar is hidden in scaled mode. "
                             "You can access it by pressing <b>Host+Home</b>.</p>")
                             .arg(strHotKey, UIHostCombo::toReadableString(gEDataManager->hostKeyCombination())),
                          "confirmGoingScale",
                          tr("Switch"));
}

bool UIMessageCenter::cannotEnterFullscreenMode(ULONG /* uWidth */, ULONG /* uHeight */, ULONG /* uBpp */, ULONG64 uMinVRAM) const
{
    return questionBinary(0, MessageType_Warning,
                          tr("<p>Could not switch the guest display to full-screen mode due to insufficient guest video memory.</p>"
                             "<p>You should configure the virtual machine to have at least <b>%1</b> of video memory.</p>"
                             "<p>Press <b>Ignore</b> to switch to full-screen mode anyway or press <b>Cancel</b> to cancel the operation.</p>")
                             .arg(UITranslator::formatSize(uMinVRAM)),
                          0 /* auto-confirm id */,
                          tr("Ignore"));
}

void UIMessageCenter::cannotEnterSeamlessMode(ULONG /* uWidth */, ULONG /* uHeight */, ULONG /* uBpp */, ULONG64 uMinVRAM) const
{
    alert(0, MessageType_Error,
          tr("<p>Could not enter seamless mode due to insufficient guest "
             "video memory.</p>"
             "<p>You should configure the virtual machine to have at "
             "least <b>%1</b> of video memory.</p>")
             .arg(UITranslator::formatSize(uMinVRAM)));
}

bool UIMessageCenter::cannotSwitchScreenInFullscreen(quint64 uMinVRAM) const
{
    return questionBinary(0, MessageType_Warning,
                          tr("<p>Could not change the guest screen to this host screen due to insufficient guest video memory.</p>"
                             "<p>You should configure the virtual machine to have at least <b>%1</b> of video memory.</p>"
                             "<p>Press <b>Ignore</b> to switch the screen anyway or press <b>Cancel</b> to cancel the operation.</p>")
                             .arg(UITranslator::formatSize(uMinVRAM)),
                          0 /* auto-confirm id */,
                          tr("Ignore"));
}

void UIMessageCenter::cannotSwitchScreenInSeamless(quint64 uMinVRAM) const
{
    alert(0, MessageType_Error,
          tr("<p>Could not change the guest screen to this host screen "
             "due to insufficient guest video memory.</p>"
             "<p>You should configure the virtual machine to have at "
             "least <b>%1</b> of video memory.</p>")
             .arg(UITranslator::formatSize(uMinVRAM)));
}

#ifdef VBOX_WITH_DRAG_AND_DROP
void UIMessageCenter::cannotDropDataToHost(const CDnDSource &dndSource, QWidget *pParent /* = 0 */) const
{
    error(pParent, MessageType_Error,
          tr("Drag and drop operation from guest to host failed."),
          UIErrorString::formatErrorInfo(dndSource));
}

void UIMessageCenter::cannotDropDataToHost(const CProgress &progress, QWidget *pParent /* = 0 */) const
{
    error(pParent, MessageType_Error,
          tr("Drag and drop operation from guest to host failed."),
          UIErrorString::formatErrorInfo(progress));
}
#endif /* VBOX_WITH_DRAG_AND_DROP */

bool UIMessageCenter::confirmHardDisklessMachine(QWidget *pParent /* = 0*/) const
{
    return questionBinary(pParent, MessageType_Warning,
                          tr("You are about to create a new virtual machine without a hard disk. "
                             "You will not be able to install an operating system on the machine "
                             "until you add one. In the mean time you will only be able to start the "
                             "machine using a virtual optical disk or from the network."),
                          0 /* auto-confirm id */,
                          tr("Continue", "agree to create VM with no hard disk attached"),
                          tr("Go Back", "reject to create VM with no hard disk attached"));
}

bool UIMessageCenter::confirmExportMachinesInSaveState(const QStringList &machineNames, QWidget *pParent /* = 0*/) const
{
    return questionBinary(pParent, MessageType_Warning,
                          tr("<p>The %n following virtual machine(s) are currently in a saved state: <b>%1</b></p>"
                             "<p>If you continue the runtime state of the exported machine(s) will be discarded. "
                             "The other machine(s) will not be changed.</p>",
                             "This text is never used with n == 0. Feel free to drop the %n where possible, "
                             "we only included it because of problems with Qt Linguist (but the user can see "
                             "how many machines are in the list and doesn't need to be told).", machineNames.size())
                             .arg(machineNames.join(", ")),
                          0 /* auto-confirm id */,
                          tr("Continue", "agree to export VMs without saved-state"));
}

bool UIMessageCenter::confirmOverridingFile(const QString &strPath, QWidget *pParent /* = 0*/) const
{
    return questionBinary(pParent, MessageType_Question,
                          tr("A file named <b>%1</b> already exists. "
                             "Are you sure you want to replace it?<br /><br />"
                             "Replacing it will overwrite its contents.")
                             .arg(strPath),
                          0 /* auto-confirm id */,
                          QString() /* ok button text */,
                          QString() /* cancel button text */,
                          false /* ok button by default? */);
}

bool UIMessageCenter::confirmOverridingFiles(const QVector<QString> &strPaths, QWidget *pParent /* = 0*/) const
{
    /* If it is only one file use the single question versions above: */
    if (strPaths.size() == 1)
        return confirmOverridingFile(strPaths.at(0), pParent);
    else if (strPaths.size() > 1)
        return questionBinary(pParent, MessageType_Question,
                              tr("The following files already exist:<br /><br />%1<br /><br />"
                                 "Are you sure you want to replace them? "
                                 "Replacing them will overwrite their contents.")
                                 .arg(QStringList(strPaths.toList()).join("<br />")),
                              0 /* auto-confirm id */,
                              QString() /* ok button text */,
                              QString() /* cancel button text */,
                              false /* ok button by default? */);
    else
        return true;
}

void UIMessageCenter::cannotCreateMediumStorage(const CVirtualBox &comVBox, const QString &strLocation, QWidget *pParent /* = 0 */) const
{
    error(pParent, MessageType_Error,
          tr("Failed to create the virtual disk image storage <nobr><b>%1</b>.</nobr>")
             .arg(strLocation),
          UIErrorString::formatErrorInfo(comVBox));
}

void UIMessageCenter::sltShowHelpWebDialog()
{
    uiCommon().openURL("https://www.virtualbox.org");
}

void UIMessageCenter::sltShowBugTracker()
{
    uiCommon().openURL("https://www.virtualbox.org/wiki/Bugtracker");
}

void UIMessageCenter::sltShowForums()
{
    uiCommon().openURL("https://forums.virtualbox.org/");
}

void UIMessageCenter::sltShowOracle()
{
    uiCommon().openURL("https://www.oracle.com/us/technologies/virtualization/virtualbox/overview/index.html");
}

void UIMessageCenter::sltShowOnlineDocumentation()
{
    QString strUrl = QString("https://docs.oracle.com/en/virtualization/virtualbox/%1.%2/user/index.html").arg(VBOX_VERSION_MAJOR).arg(VBOX_VERSION_MINOR);
    uiCommon().openURL(strUrl);
}

void UIMessageCenter::sltShowHelpAboutDialog()
{
    CVirtualBox vbox = gpGlobalSession->virtualBox();
    const QString strFullVersion = UIVersionInfo::brandingIsActive()
                                 ? QString("%1 r%2 - %3").arg(vbox.GetVersion())
                                                         .arg(vbox.GetRevision())
                                                         .arg(UIVersionInfo::brandingGetKey("Name"))
                                 : QString("%1 r%2").arg(vbox.GetVersion())
                                                    .arg(vbox.GetRevision());
    (new VBoxAboutDlg(windowManager().mainWindowShown(), strFullVersion))->show();
}

void UIMessageCenter::sltResetSuppressedMessages()
{
    /* Nullify suppressed message list: */
    gEDataManager->setSuppressedMessages(QStringList());
}

void UIMessageCenter::sltShowMessageBox(QWidget *pParent, MessageType enmType,
                                        const QString &strMessage, const QString &strDetails,
                                        int iButton1, int iButton2, int iButton3,
                                        const QString &strButtonText1, const QString &strButtonText2, const QString &strButtonText3,
                                        const QString &strAutoConfirmId, const QString &strHelpKeyword) const
{
    /* Now we can show a message-box directly: */
    showMessageBox(pParent, enmType,
                   strMessage, strDetails,
                   iButton1, iButton2, iButton3,
                   strButtonText1, strButtonText2, strButtonText3,
                   strAutoConfirmId, strHelpKeyword);
}

UIMessageCenter::UIMessageCenter()
{
    /* Assign instance: */
    s_pInstance = this;
}

UIMessageCenter::~UIMessageCenter()
{
    /* Unassign instance: */
    s_pInstance = 0;
}

void UIMessageCenter::prepare()
{
    /* Register required objects as meta-types: */
    qRegisterMetaType<CProgress>();
    qRegisterMetaType<CHost>();
    qRegisterMetaType<CMachine>();
    qRegisterMetaType<CConsole>();
    qRegisterMetaType<CHostNetworkInterface>();
    qRegisterMetaType<UIMediumDeviceType>();
    qRegisterMetaType<StorageSlot>();

    /* Prepare interthread connection: */
    qRegisterMetaType<MessageType>();
    // Won't go until we are supporting C++11 or at least variadic templates everywhere.
    // connect(this, &UIMessageCenter::sigToShowMessageBox,
    //         this, &UIMessageCenter::sltShowMessageBox,
    connect(this, SIGNAL(sigToShowMessageBox(QWidget*, MessageType,
                                             const QString&, const QString&,
                                             int, int, int,
                                             const QString&, const QString&, const QString&,
                                             const QString&, const QString&)),
            this, SLOT(sltShowMessageBox(QWidget*, MessageType,
                                         const QString&, const QString&,
                                         int, int, int,
                                         const QString&, const QString&, const QString&,
                                         const QString&, const QString&)),
            Qt::BlockingQueuedConnection);

    /* Translations for Main.
     * Please make sure they corresponds to the strings coming from Main one-by-one symbol! */
    tr("Could not load the Host USB Proxy Service (VERR_FILE_NOT_FOUND). The service might not be installed on the host computer");
    tr("VirtualBox is not currently allowed to access USB devices.  You can change this by adding your user to the 'vboxusers' group.  Please see the user guide for a more detailed explanation");
    tr("VirtualBox is not currently allowed to access USB devices.  You can change this by allowing your user to access the 'usbfs' folder and files.  Please see the user guide for a more detailed explanation");
    tr("The USB Proxy Service has not yet been ported to this host");
    tr("Could not load the Host USB Proxy service");
}

void UIMessageCenter::cleanup()
{
     /* Nothing for now... */
}

int UIMessageCenter::showMessageBox(QWidget *pParent, MessageType enmType,
                                    const QString &strMessage, const QString &strDetails,
                                    int iButton1, int iButton2, int iButton3,
                                    const QString &strButtonText1, const QString &strButtonText2, const QString &strButtonText3,
                                    const QString &strAutoConfirmId, const QString &strHelpKeyword) const
{
    /* Choose the 'default' button: */
    if (iButton1 == 0 && iButton2 == 0 && iButton3 == 0)
        iButton1 = AlertButton_Ok | AlertButtonOption_Default;

    /* Check if message-box was auto-confirmed before: */
    QStringList confirmedMessageList;
    if (!strAutoConfirmId.isEmpty())
    {
        const QUuid uID = uiCommon().uiType() == UIType_RuntimeUI
                        ? uiCommon().managedVMUuid()
                        : UIExtraDataManager::GlobalID;
        confirmedMessageList = gEDataManager->suppressedMessages(uID);
        if (   confirmedMessageList.contains(strAutoConfirmId)
            || confirmedMessageList.contains("allMessageBoxes")
            || confirmedMessageList.contains("all") )
        {
            int iResultCode = AlertOption_AutoConfirmed;
            if (iButton1 & AlertButtonOption_Default)
                iResultCode |= (iButton1 & AlertButtonMask);
            if (iButton2 & AlertButtonOption_Default)
                iResultCode |= (iButton2 & AlertButtonMask);
            if (iButton3 & AlertButtonOption_Default)
                iResultCode |= (iButton3 & AlertButtonMask);
            return iResultCode;
        }
    }

    /* Choose title and icon: */
    QString title;
    AlertIconType icon;
    switch (enmType)
    {
        default:
        case MessageType_Info:
            title = tr("VirtualBox - Information", "msg box title");
            icon = AlertIconType_Information;
            break;
        case MessageType_Question:
            title = tr("VirtualBox - Question", "msg box title");
            icon = AlertIconType_Question;
            break;
        case MessageType_Warning:
            title = tr("VirtualBox - Warning", "msg box title");
            icon = AlertIconType_Warning;
            break;
        case MessageType_Error:
            title = tr("VirtualBox - Error", "msg box title");
            icon = AlertIconType_Critical;
            break;
        case MessageType_Critical:
            title = tr("VirtualBox - Critical Error", "msg box title");
            icon = AlertIconType_Critical;
            break;
        case MessageType_GuruMeditation:
            title = "VirtualBox - Guru Meditation"; /* don't translate this */
            icon = AlertIconType_GuruMeditation;
            break;
    }

    /* Create message-box: */
    QWidget *pMessageBoxParent = windowManager().realParentWindow(pParent ? pParent : windowManager().mainWindowShown());
    QPointer<QIMessageBox> pMessageBox = new QIMessageBox(title, strMessage, icon,
                                                          iButton1, iButton2, iButton3,
                                                          pMessageBoxParent, strHelpKeyword);
    windowManager().registerNewParent(pMessageBox, pMessageBoxParent);

    /* Prepare auto-confirmation check-box: */
    if (!strAutoConfirmId.isEmpty())
    {
        pMessageBox->setFlagText(tr("Do not show this message again", "msg box flag"));
        pMessageBox->setFlagChecked(false);
    }

    /* Configure details: */
    if (!strDetails.isEmpty())
        pMessageBox->setDetailsText(strDetails);

    /* Configure button-text: */
    if (!strButtonText1.isNull())
        pMessageBox->setButtonText(0, strButtonText1);
    if (!strButtonText2.isNull())
        pMessageBox->setButtonText(1, strButtonText2);
    if (!strButtonText3.isNull())
        pMessageBox->setButtonText(2, strButtonText3);

    /* Show message-box: */
    int iResultCode = pMessageBox->exec();

    /* Make sure message-box still valid: */
    if (!pMessageBox)
        return iResultCode;

    /* Remember auto-confirmation check-box value: */
    if (!strAutoConfirmId.isEmpty())
    {
        if (pMessageBox->flagChecked())
        {
            confirmedMessageList << strAutoConfirmId;
            gEDataManager->setSuppressedMessages(confirmedMessageList);
        }
    }

    /* Delete message-box: */
    delete pMessageBox;

    /* Return result-code: */
    return iResultCode;
}
