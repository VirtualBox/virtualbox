/* $Id: UIVirtualBoxManager.cpp 168992 2025-05-26 13:30:39Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxManager class implementation.
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
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMenuBar>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWindow>
#ifndef VBOX_WS_WIN
# include <QRegularExpression>
#endif

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIFileDialog.h"
#include "QILineEdit.h"
#include "QIRichTextLabel.h"
#include "UIActionPoolManager.h"
#include "UIAdvancedSettingsDialogSpecific.h"
#include "UICloudConsoleManager.h"
#include "UICloudMachineManager.h"
#include "UICloudNetworkingStuff.h"
#include "UICloudProfileManager.h"
#include "UICommon.h"
#include "UIDesktopServices.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIErrorString.h"
#include "UIExtension.h"
#include "UIExtensionPackManager.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIHelpBrowserDialog.h"
#include "UIIconPool.h"
#include "UILocalMachineStuff.h"
#include "UILoggingDefs.h"
#include "UIMedium.h"
#include "UIMediumEnumerator.h"
#include "UIMediumTools.h"
#include "UIMediumManager.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINetworkManager.h"
#include "UINotificationCenter.h"
#include "UIQObjectStuff.h"
#include "UITranslationEventListener.h"
#include "UIVirtualBoxManager.h"
#include "UIVirtualBoxWidget.h"
#include "UIVirtualMachineItemCloud.h"
#include "UIVirtualMachineItemLocal.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVMLogViewerDialog.h"
#include "UIWizardAddCloudVM.h"
#include "UIWizardCloneVM.h"
#include "UIWizardExportApp.h"
#include "UIWizardImportApp.h"
#include "UIWizardNewCloudVM.h"
#include "UIWizardNewVD.h"
#include "UIWizardNewVM.h"
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
# include "UIUpdateManager.h"
#endif
#ifdef VBOX_WS_MAC
# include "UIImageTools.h"
# include "UIWindowMenuManager.h"
# include "UIVersion.h"
# include "VBoxUtils.h"
#else
# include "UIMenuBar.h"
#endif
#ifdef VBOX_WS_NIX
# include "UIDesktopWidgetWatchdog.h"
#endif

/* COM includes: */
#include "CHostUSBDevice.h"
#include "CGuestOSType.h"
#include "CSystemProperties.h"
#include "CUnattended.h"
#include "CVirtualBoxErrorInfo.h"
#ifdef VBOX_WS_MAC
# include "CVirtualBox.h"
#endif

/* Other VBox stuff: */
#include <iprt/buildconfig.h>
#include <VBox/version.h>

/** QDialog extension used to ask for a public key for console connection needs. */
class UIAcquirePublicKeyDialog : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs dialog passing @a pParent to the base-class. */
    UIAcquirePublicKeyDialog(QWidget *pParent = 0);

    /** Return public key. */
    QString publicKey() const;

private slots:

    /** Handles help-viewer @a link click. */
    void sltHandleHelpViewerLinkClick(const QUrl &link);

    /** Handles abstract @a pButton click. */
    void sltHandleButtonClicked(QAbstractButton *pButton);
    /** Handles Open button click. */
    void sltHandleOpenButtonClick();

    /** Performs revalidation. */
    void sltRevalidate();

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepare editor contents. */
    void prepareEditorContents();

    /** Returns a list of default key folders. */
    QStringList defaultKeyFolders() const;
    /** Returns a list of key generation tools. */
    QStringList keyGenerationTools() const;

    /** Loads file contents.
      * @returns Whether file was really loaded. */
    bool loadFileContents(const QString &strPath, bool fIgnoreErrors = false);

    /** Holds the help-viewer instance. */
    QIRichTextLabel   *m_pHelpViewer;
    /** Holds the text-editor instance. */
    QTextEdit         *m_pTextEditor;
    /** Holds the button-box instance. */
    QIDialogButtonBox *m_pButtonBox;
};

/** QDialog extension used to ask for a cloud machine clone name. */
class UIAcquireCloudMachineCloneNameDialog : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs dialog passing @a pParent to the base-class.
      * @param  strName  Brings the clone name by default. */
    UIAcquireCloudMachineCloneNameDialog(QWidget *pParent, const QString &strName);

    /** Returns value. */
    QString value() const;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

    /** Performs revalidation. */
    void sltRevalidate();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();

    /** Holds the clone name by default. */
    QString  m_strName;

    /** Holds the text-editor instance. */
    QILineEdit        *m_pEditor;
    /** Holds the button-box instance. */
    QIDialogButtonBox *m_pButtonBox;
};


/*********************************************************************************************************************************
*   Class UIAcquirePublicKeyDialog implementation.                                                                               *
*********************************************************************************************************************************/

UIAcquirePublicKeyDialog::UIAcquirePublicKeyDialog(QWidget *pParent /* = 0 */)
    : QDialog(pParent)
    , m_pHelpViewer(0)
    , m_pTextEditor(0)
    , m_pButtonBox(0)
{
    prepare();
    sltRevalidate();
}

QString UIAcquirePublicKeyDialog::publicKey() const
{
    return m_pTextEditor->toPlainText();
}

void UIAcquirePublicKeyDialog::sltHandleHelpViewerLinkClick(const QUrl &link)
{
    /* Parse the link meta and use it to get tool path to copy to clipboard: */
    bool fOk = false;
    const uint uToolNumber = link.toString().section('#', 1, 1).toUInt(&fOk);
    if (fOk)
        QApplication::clipboard()->setText(keyGenerationTools().value(uToolNumber), QClipboard::Clipboard);
}

void UIAcquirePublicKeyDialog::sltHandleButtonClicked(QAbstractButton *pButton)
{
    const QDialogButtonBox::StandardButton enmStandardButton = m_pButtonBox->standardButton(pButton);
    switch (enmStandardButton)
    {
        case QDialogButtonBox::Ok:     return accept();
        case QDialogButtonBox::Cancel: return reject();
        case QDialogButtonBox::Open:   return sltHandleOpenButtonClick();
        default: break;
    }
}

void UIAcquirePublicKeyDialog::sltHandleOpenButtonClick()
{
    CVirtualBox comVBox = gpGlobalSession->virtualBox();
    const QString strFileName = QIFileDialog::getOpenFileName(comVBox.GetHomeFolder(), QString(),
                                                              this, tr("Choose a public key file"));
    if (!strFileName.isEmpty())
    {
        gEDataManager->setCloudConsolePublicKeyPath(strFileName);
        loadFileContents(strFileName);
    }
}

void UIAcquirePublicKeyDialog::sltRevalidate()
{
    m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(!m_pTextEditor->toPlainText().isEmpty());
}

void UIAcquirePublicKeyDialog::sltRetranslateUI()
{
    setWindowTitle(tr("Public key"));

    /* Generating help-viewer text: */
    QStringList folders;
    foreach (const QString &strFolder, defaultKeyFolders())
        folders << QString("&nbsp;%1").arg(strFolder);
    const QStringList initialTools = keyGenerationTools();
    QStringList tools;
    foreach (const QString &strTool, initialTools)
        tools << QString("&nbsp;<a href=#%1><img src='manager://copy'/></a>&nbsp;&nbsp;%2")
                         .arg(initialTools.indexOf(strTool))
                         .arg(strTool);
#ifdef VBOX_WS_WIN
    m_pHelpViewer->setText(tr("We haven't found public key id_rsa[.pub] in suitable locations. "
                              "If you have one, please put it under one of those folders OR copy "
                              "content to the edit box below:<br><br>"
                              "%1<br><br>"
                              "If you don't have one, please consider using one of the following "
                              "tools to generate it:<br><br>"
                              "%2")
                           .arg(folders.join("<br>"))
                           .arg(tools.join("<br>")));
#else
    m_pHelpViewer->setText(tr("We haven't found public key id_rsa[.pub] in suitable location. "
                              "If you have one, please put it under specified folder OR copy "
                              "content to the edit box below:<br><br>"
                              "%1<br><br>"
                              "If you don't have one, please consider using the following "
                              "tool to generate it:<br><br>"
                              "%2")
                           .arg(folders.join("<br>"))
                           .arg(tools.join("<br>")));
#endif

    m_pTextEditor->setPlaceholderText(tr("Paste public key"));
    m_pButtonBox->button(QDialogButtonBox::Open)->setText(tr("Browse"));
}

void UIAcquirePublicKeyDialog::prepare()
{
    /* Prepare widgets: */
    prepareWidgets();
    /* Prepare editor contents: */
    prepareEditorContents();
    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIAcquirePublicKeyDialog::sltRetranslateUI);

    /* Resize to suitable size: */
    const int iMinimumHeightHint = minimumSizeHint().height();
    resize(iMinimumHeightHint * 1.618, iMinimumHeightHint);
}

void UIAcquirePublicKeyDialog::prepareWidgets()
{
    /* Prepare layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Create help-viewer: */
        m_pHelpViewer = new QIRichTextLabel(this);
        if (m_pHelpViewer)
        {
            /* Prepare icon and size as well: */
            const QIcon icon = UIIconPool::iconSet(":/file_manager_copy_16px.png");
            const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) * 2 / 3;

            /* Configure help-viewer: */
            m_pHelpViewer->setHidden(true);
            m_pHelpViewer->setMinimumTextWidth(gpDesktop->screenGeometry(window()).width() / 5);
            const qreal fDevicePixelRatio = windowHandle() ? windowHandle()->devicePixelRatio() : 1;
            m_pHelpViewer->registerPixmap(icon.pixmap(QSize(iMetric, iMetric), fDevicePixelRatio), "manager://copy");
            connect(m_pHelpViewer, &QIRichTextLabel::sigLinkClicked, this, &UIAcquirePublicKeyDialog::sltHandleHelpViewerLinkClick);
            pLayout->addWidget(m_pHelpViewer, 2);
        }

        /* Prepare text-editor: */
        m_pTextEditor = new QTextEdit(this);
        if (m_pTextEditor)
        {
            connect(m_pTextEditor, &QTextEdit::textChanged, this, &UIAcquirePublicKeyDialog::sltRevalidate);
            pLayout->addWidget(m_pTextEditor, 1);
        }

        /* Prepare button-box: */
        m_pButtonBox = new QIDialogButtonBox(this);
        if (m_pButtonBox)
        {
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Open);
            connect(m_pButtonBox, &QIDialogButtonBox::clicked, this, &UIAcquirePublicKeyDialog::sltHandleButtonClicked);
            pLayout->addWidget(m_pButtonBox);
        }
    }
}

void UIAcquirePublicKeyDialog::prepareEditorContents()
{
    /* Check whether we were able to load key file: */
    bool fFileLoaded = false;

    /* Try to load last remembered file contents: */
    fFileLoaded = loadFileContents(gEDataManager->cloudConsolePublicKeyPath(), true /* ignore errors */);
    if (!fFileLoaded)
    {
        /* We have failed to load file mentioned in extra-data, now we have
         * to check whether file present in one of default paths: */
        QString strAbsoluteFilePathWeNeed;
        foreach (const QString &strPath, defaultKeyFolders())
        {
            /* Gather possible file names, there can be few of them: */
            const QStringList fileNames = QStringList() << "id_rsa.pub" << "id_rsa";
            /* For each file name we have to: */
            foreach (const QString &strFileName, fileNames)
            {
                /* Compose absolute file path: */
                const QString strAbsoluteFilePath = QDir(strPath).absoluteFilePath(strFileName);
                /* If that file exists, we are referring it: */
                if (QFile::exists(strAbsoluteFilePath))
                {
                    strAbsoluteFilePathWeNeed = strAbsoluteFilePath;
                    break;
                }
            }
            /* Break early if we have found something: */
            if (!strAbsoluteFilePathWeNeed.isEmpty())
                break;
        }

        /* Try to open file if it was really found: */
        if (!strAbsoluteFilePathWeNeed.isEmpty())
            fFileLoaded = loadFileContents(strAbsoluteFilePathWeNeed, true /* ignore errors */);
    }

    /* Show/hide help-viewer depending on
     * whether we were able to load the file: */
    m_pHelpViewer->setHidden(fFileLoaded);
}

QStringList UIAcquirePublicKeyDialog::defaultKeyFolders() const
{
    QStringList folders;
#ifdef VBOX_WS_WIN
    // WORKAROUND:
    // There is additional default folder on Windows:
    folders << QDir::toNativeSeparators(QDir(QDir::homePath()).absoluteFilePath("oci"));
#endif
    folders << QDir::toNativeSeparators(QDir(QDir::homePath()).absoluteFilePath(".ssh"));
    return folders;
}

QStringList UIAcquirePublicKeyDialog::keyGenerationTools() const
{
    QStringList tools;
#ifdef VBOX_WS_WIN
    // WORKAROUND:
    // There is additional key generation tool on Windows:
    tools << "puttygen.exe";
    tools << "ssh-keygen.exe -m PEM -t rsa -b 4096";
#else
    tools << "ssh-keygen -m PEM -t rsa -b 4096";
#endif
    return tools;
}

bool UIAcquirePublicKeyDialog::loadFileContents(const QString &strPath, bool fIgnoreErrors /* = false */)
{
    /* Make sure file path isn't empty: */
    if (strPath.isEmpty())
    {
        if (!fIgnoreErrors)
            UINotificationMessage::warnAboutPublicKeyFilePathIsEmpty();
        return false;
    }

    /* Make sure file exists and is of suitable size: */
    QFileInfo fi(strPath);
    if (!fi.exists())
    {
        if (!fIgnoreErrors)
            UINotificationMessage::warnAboutPublicKeyFileDoesntExist(strPath);
        return false;
    }
    if (fi.size() > 10 * _1K)
    {
        if (!fIgnoreErrors)
            UINotificationMessage::warnAboutPublicKeyFileIsOfTooLargeSize(strPath);
        return false;
    }

    /* Make sure file can be opened: */
    QFile file(strPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (!fIgnoreErrors)
            UINotificationMessage::warnAboutPublicKeyFileIsntReadable(strPath);
        return false;
    }

    /* File opened and read, filling editor: */
    m_pTextEditor->setPlainText(file.readAll());
    return true;
}


/*********************************************************************************************************************************
*   Class UIAcquireCloudMachineCloneNameDialog implementation.                                                                   *
*********************************************************************************************************************************/

UIAcquireCloudMachineCloneNameDialog::UIAcquireCloudMachineCloneNameDialog(QWidget *pParent, const QString &strName)
    : QDialog(pParent)
    , m_strName(strName)
    , m_pEditor(0)
    , m_pButtonBox(0)
{
    prepare();
    sltRevalidate();
}

QString UIAcquireCloudMachineCloneNameDialog::value() const
{
    return m_pEditor ? m_pEditor->text() : QString();
}

void UIAcquireCloudMachineCloneNameDialog::prepare()
{
    /* Prepare widgets: */
    prepareWidgets();
    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIAcquireCloudMachineCloneNameDialog::sltRetranslateUI);

    /* Resize to suitable size: */
    const int iMinimumHeightHint = minimumSizeHint().height();
    resize(iMinimumHeightHint * 1.618, iMinimumHeightHint);
}

void UIAcquireCloudMachineCloneNameDialog::prepareWidgets()
{
    /* Prepare layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare editor: */
        m_pEditor = new QILineEdit(this);
        if (m_pEditor)
        {
            m_pEditor->setText(m_strName);
            m_pEditor->setMinimumWidthByText(QString().fill('0', 20));
            connect(m_pEditor, &QILineEdit::textChanged, this, &UIAcquireCloudMachineCloneNameDialog::sltRevalidate);
            pLayout->addWidget(m_pEditor);
        }

        /* Intermediate stretch: */
        pLayout->addStretch();

        /* Prepare button-box: */
        m_pButtonBox = new QIDialogButtonBox(this);
        if (m_pButtonBox)
        {
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            connect(m_pButtonBox, &QIDialogButtonBox::accepted, this, &UIAcquireCloudMachineCloneNameDialog::accept);
            connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &UIAcquireCloudMachineCloneNameDialog::reject);
            pLayout->addWidget(m_pButtonBox);
        }
    }
}

void UIAcquireCloudMachineCloneNameDialog::sltRetranslateUI()
{
    setWindowTitle(tr("Clone Virtual Machine"));
    m_pEditor->setPlaceholderText(tr("Enter machine name..."));
}

void UIAcquireCloudMachineCloneNameDialog::sltRevalidate()
{
    m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(!m_pEditor->text().isEmpty());
}


/*********************************************************************************************************************************
*   Class UIVirtualBoxManager implementation.                                                                                    *
*********************************************************************************************************************************/

/* static */
UIVirtualBoxManager *UIVirtualBoxManager::s_pInstance = 0;

/* static */
void UIVirtualBoxManager::create()
{
    /* Make sure VirtualBox Manager isn't created: */
    AssertReturnVoid(s_pInstance == 0);

    /* Create VirtualBox Manager: */
    new UIVirtualBoxManager;
    /* Prepare VirtualBox Manager: */
    s_pInstance->prepare();
    /* Show VirtualBox Manager: */
    s_pInstance->show();
}

/* static */
void UIVirtualBoxManager::destroy()
{
    /* Make sure VirtualBox Manager is created: */
    if (!s_pInstance)
        return;

    /* Unregister in the modal window manager: */
    windowManager().setMainWindowShown(0);
    /* Cleanup VirtualBox Manager: */
    s_pInstance->cleanup();
    /* Destroy machine UI: */
    delete s_pInstance;
}

UIVirtualBoxManager::UIVirtualBoxManager()
    : m_fPolished(false)
    , m_fFirstMediumEnumerationHandled(false)
    , m_pActionPool(0)
    , m_pWidget(0)
    , m_iGeometrySaveTimerId(-1)
    , m_fSnapshotCloneByDefault(false)
    , m_fImportFromOCI(false)
    , m_fExportToOCI(false)
{
    s_pInstance = this;
    setAcceptDrops(true);
}

UIVirtualBoxManager::~UIVirtualBoxManager()
{
    s_pInstance = 0;
}

bool UIVirtualBoxManager::shouldBeMaximized() const
{
    return gEDataManager->selectorWindowShouldBeMaximized();
}

#ifdef VBOX_WS_MAC
bool UIVirtualBoxManager::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Ignore for non-active window except for FileOpen event which should be always processed: */
    if (!isActiveWindow() && pEvent->type() != QEvent::FileOpen)
        return QMainWindowWithRestorableGeometry::eventFilter(pObject, pEvent);

    /* Ignore for other objects: */
    if (qobject_cast<QWidget*>(pObject) &&
        qobject_cast<QWidget*>(pObject)->window() != this)
        return QMainWindowWithRestorableGeometry::eventFilter(pObject, pEvent);

    /* Which event do we have? */
    switch (pEvent->type())
    {
        case QEvent::FileOpen:
        {
            sltHandleOpenUrlCall(QList<QUrl>() << static_cast<QFileOpenEvent*>(pEvent)->url());
            pEvent->accept();
            return true;
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QMainWindowWithRestorableGeometry::eventFilter(pObject, pEvent);
}
#endif /* VBOX_WS_MAC */

void UIVirtualBoxManager::sltRetranslateUI()
{
    /* Set window title: */
    QString strTitle(VBOX_PRODUCT);
    strTitle += " " + tr("Manager", "Note: main window title which is prepended by the product name.");
#ifdef VBOX_BLEEDING_EDGE
    strTitle += QString(" EXPERIMENTAL build ")
             +  QString(RTBldCfgVersion())
             +  QString(" r")
             +  QString(RTBldCfgRevisionStr())
             +  QString(" - " VBOX_BLEEDING_EDGE);
#endif /* VBOX_BLEEDING_EDGE */
    setWindowTitle(strTitle);
}

bool UIVirtualBoxManager::event(QEvent *pEvent)
{
    /* Which event do we have? */
    switch (pEvent->type())
    {
        /* Handle every ScreenChangeInternal event to notify listeners: */
        case QEvent::ScreenChangeInternal:
        {
            emit sigWindowRemapped();
            break;
        }
        /* Handle move/resize geometry changes: */
        case QEvent::Move:
        case QEvent::Resize:
        {
            if (m_iGeometrySaveTimerId != -1)
                killTimer(m_iGeometrySaveTimerId);
            m_iGeometrySaveTimerId = startTimer(300);
            break;
        }
        /* Handle timer event started above: */
        case QEvent::Timer:
        {
            QTimerEvent *pTimerEvent = static_cast<QTimerEvent*>(pEvent);
            if (pTimerEvent->timerId() == m_iGeometrySaveTimerId)
            {
                killTimer(m_iGeometrySaveTimerId);
                m_iGeometrySaveTimerId = -1;
                const QRect geo = currentGeometry();
                LogRel2(("GUI: UIVirtualBoxManager: Saving geometry as: Origin=%dx%d, Size=%dx%d\n",
                         geo.x(), geo.y(), geo.width(), geo.height()));
                gEDataManager->setSelectorWindowGeometry(geo, isCurrentlyMaximized());
            }
            break;
        }
        default:
            break;
    }
    /* Call to base-class: */
    return QMainWindowWithRestorableGeometry::event(pEvent);
}

void UIVirtualBoxManager::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QMainWindowWithRestorableGeometry::showEvent(pEvent);

    /* Is polishing required? */
    if (!m_fPolished)
    {
        /* Pass the show-event to polish-event: */
        polishEvent(pEvent);
        /* Mark as polished: */
        m_fPolished = true;
    }
}

void UIVirtualBoxManager::polishEvent(QShowEvent *)
{
    /* Register in the modal window manager: */
    windowManager().setMainWindowShown(s_pInstance);

    /* Make sure user warned about inaccessible media: */
    QMetaObject::invokeMethod(this, "sltHandleMediumEnumerationFinish", Qt::QueuedConnection);
}

void UIVirtualBoxManager::closeEvent(QCloseEvent *pEvent)
{
    /* Call to base-class: */
    QMainWindowWithRestorableGeometry::closeEvent(pEvent);

    /* Quit application: */
    QApplication::quit();
}

void UIVirtualBoxManager::dragEnterEvent(QDragEnterEvent *pEvent)
{
    if (pEvent->mimeData()->hasUrls())
        pEvent->acceptProposedAction();
}

void UIVirtualBoxManager::dropEvent(QDropEvent *pEvent)
{
    if (!pEvent->mimeData()->hasUrls())
        return;
    sltHandleOpenUrlCall(pEvent->mimeData()->urls());
    pEvent->acceptProposedAction();
}

#ifdef VBOX_WS_NIX
void UIVirtualBoxManager::sltHandleHostScreenAvailableAreaChange()
{
    /* Prevent handling if fake screen detected: */
    if (UIDesktopWidgetWatchdog::isFakeScreenDetected())
        return;

    /* Restore the geometry cached by the window: */
    const QRect geo = currentGeometry();
    resize(geo.size());
    move(geo.topLeft());
}
#endif /* VBOX_WS_NIX */

void UIVirtualBoxManager::sltHandleCommitData()
{
    /* Close the sub-dialogs first: */
    sltCloseManagerWindow(UIToolType_Extensions);
    sltCloseManagerWindow(UIToolType_Media);
    sltCloseManagerWindow(UIToolType_Network);
    sltCloseManagerWindow(UIToolType_Cloud);
    sltCloseManagerWindow(UIToolType_CloudConsole);
    sltCloseSettingsDialog();
    sltClosePreferencesDialog();

    // WORKAROUND:
    // This will be fixed proper way during session management cleanup for Qt6.
    // But for now we will just cleanup connections which is Ok anyway.
    cleanupConnections();
}

void UIVirtualBoxManager::sltHandleMediumEnumerationFinish()
{
#if 0 // ohh, come on!
    /* To avoid annoying the user, we check for inaccessible media just once, after
     * the first media emumeration [started from main() at startup] is complete. */
    if (m_fFirstMediumEnumerationHandled)
        return;
    m_fFirstMediumEnumerationHandled = true;

    /* Make sure MM window/tool is not opened,
     * otherwise user sees everything himself: */
    if (   m_pManagerVirtualMedia
        || m_pWidget->isGlobalToolOpened(UIToolType_Media))
        return;

    /* Look for at least one inaccessible medium: */
    bool fIsThereAnyInaccessibleMedium = false;
    foreach (const QUuid &uMediumID, gpMediumEnumerator->mediumIDs())
    {
        if (gpMediumEnumerator->medium(uMediumID).state() == KMediumState_Inaccessible)
        {
            fIsThereAnyInaccessibleMedium = true;
            break;
        }
    }
    /* Warn the user about inaccessible medium, propose to open MM window/tool: */
    if (fIsThereAnyInaccessibleMedium && msgCenter().warnAboutInaccessibleMedia())
    {
        /* Open the MM window: */
        sltOpenVirtualMediumManagerWindow();
    }
#endif
}

void UIVirtualBoxManager::sltHandleOpenUrlCall(QList<QUrl> list /* = QList<QUrl>() */)
{
    /* If passed list is empty, we take the one from UICommon: */
    if (list.isEmpty())
        list = uiCommon().takeArgumentUrls();

    /* Check if we are can handle the dropped urls: */
    for (int i = 0; i < list.size(); ++i)
    {
#ifdef VBOX_WS_MAC
        const QString strFile = ::darwinResolveAlias(list.at(i).toLocalFile());
#else
        const QString strFile = list.at(i).toLocalFile();
#endif
        const QStringList isoExtensionList = QStringList() << "iso";
        /* If there is such file exists: */
        if (!strFile.isEmpty() && QFile::exists(strFile))
        {
            /* And has allowed VBox config file extension: */
            if (UICommon::hasAllowedExtension(strFile, VBoxFileExts))
            {
                /* Handle VBox config file: */
                CVirtualBox comVBox = gpGlobalSession->virtualBox();
                CMachine comMachine = comVBox.FindMachine(strFile);
                if (comVBox.isOk() && comMachine.isNotNull())
                    launchMachine(comMachine);
                else
                    openAddMachineDialog(strFile);
            }
            /* And has allowed VBox OVF file extension: */
            else if (UICommon::hasAllowedExtension(strFile, OVFFileExts))
            {
                /* Allow only one file at the time: */
                openImportApplianceWizard(strFile);
                break;
            }
            /* And has allowed VBox extension pack file extension: */
            else if (UICommon::hasAllowedExtension(strFile, VBoxExtPackFileExts))
            {
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
                /* Prevent update manager from proposing us to update EP: */
                gUpdateManager->setEPInstallationRequested(true);
#endif
                /* Propose the user to install EP described by the arguments @a list. */
                UIExtension::install(strFile, QString(), this, NULL);
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
                /* Allow update manager to propose us to update EP: */
                gUpdateManager->setEPInstallationRequested(false);
#endif
            }
            else if (UICommon::hasAllowedExtension(strFile, isoExtensionList))
            {
                openNewMachineWizard(strFile);
            }
        }
    }
}

void UIVirtualBoxManager::sltCheckUSBAccesibility()
{
    CHost comHost = gpGlobalSession->host();
    if (!comHost.isOk())
        return;
    if (comHost.GetUSBDevices().isEmpty() && comHost.isWarning())
        UINotificationMessage::cannotEnumerateHostUSBDevices(comHost);
}

void UIVirtualBoxManager::sltHandleChooserPaneSelectionChange()
{
    // WORKAROUND:
    // These menus are dynamical since local and cloud VMs have different menu contents.
    // Yet .. we have to prepare Machine/Group menus beforehand, they contains shortcuts.
    updateMenuGroup(actionPool()->action(UIActionIndexMN_M_Group)->menu());
    updateMenuMachine(actionPool()->action(UIActionIndexMN_M_Machine)->menu());

    /* Update actions stuff: */
    updateActionsVisibility();
    updateActionsAppearance();

    /* Special handling for opened settings dialog: */
    if (   m_pWidget->isLocalMachineItemSelected()
        && m_settings.contains(UIAdvancedSettingsDialog::Type_Machine))
    {
        /* Cast dialog to required type: */
        UIAdvancedSettingsDialogMachine *pDialog =
            qobject_cast<UIAdvancedSettingsDialogMachine*>(m_settings.value(UIAdvancedSettingsDialog::Type_Machine));
        AssertPtrReturnVoid(pDialog);

        /* Get current item: */
        UIVirtualMachineItem *pItem = currentItem();
        AssertPtrReturnVoid(pItem);

        /* Update machine stuff: */
        pDialog->setNewMachineId(pItem->id());
    }
    else if (   m_pWidget->isCloudMachineItemSelected()
             && m_pCloudSettings)
    {
        /* Get current item: */
        UIVirtualMachineItem *pItem = currentItem();
        AssertPtrReturnVoid(pItem);
        UIVirtualMachineItemCloud *pItemCloud = pItem->toCloud();
        AssertPtrReturnVoid(pItemCloud);

        /* Update machine stuff: */
        m_pCloudSettings->setCloudMachine(pItemCloud->machine());
    }
}

void UIVirtualBoxManager::sltHandleGlobalToolTypeChange()
{
    /* Update actions stuff: */
    updateActionsVisibility();
    updateActionsAppearance();

    /* Make sure separate dialog closed when corresponding tool opened: */
    sltCloseManagerWindow(m_pWidget->toolsTypeGlobal());
}

void UIVirtualBoxManager::sltHandleMachineToolTypeChange()
{
    /* Update actions stuff: */
    updateActionsVisibility();
    updateActionsAppearance();

    /* Make sure separate dialog closed when corresponding tool opened: */
    sltCloseManagerWindow(m_pWidget->toolsTypeMachine());
}

void UIVirtualBoxManager::sltExecuteHomeTask(HomeTask enmTask)
{
    switch (enmTask)
    {
        case HomeTask_Configure:
            sltOpenPreferencesDialog();
            break;
        case HomeTask_Create:
            sltOpenNewMachineWizard();
            break;
        case HomeTask_Open:
            sltOpenAddMachineDialog();
            break;
        case HomeTask_Import:
            sltOpenImportApplianceWizard();
            break;
        case HomeTask_Export:
            sltOpenExportApplianceWizard();
            break;
        default:
            break;
    }
}

void UIVirtualBoxManager::sltCreateMedium()
{
    /* Open Create VD Wizard: */
    sltOpenWizard(WizardType_NewVD);
}

void UIVirtualBoxManager::sltCopyMedium(const QUuid &uMediumId)
{
    /* Configure wizard variables: */
    m_uMediumId = uMediumId;

    /* Open Clone VD Wizard: */
    sltOpenWizard(WizardType_CloneVD);
}

void UIVirtualBoxManager::sltDetachToolPane(UIToolType enmToolType)
{
    AssertReturnVoid(enmToolType != UIToolType_Invalid);
    /* Add tool to detached: */
    QList<UIToolType> tools = gEDataManager->detachedTools();
    if (!tools.contains(enmToolType))
    {
        tools << enmToolType;
        gEDataManager->setDetachedTools(tools);
    }

    /* Detach Log Viewer: */
    sltOpenManagerWindow(enmToolType);
}

void UIVirtualBoxManager::sltHandleMenuPrepare(int iIndex, QMenu *pMenu)
{
    /* Update if there is update-handler: */
    if (m_menuUpdateHandlers.contains(iIndex))
        (this->*(m_menuUpdateHandlers.value(iIndex)))(pMenu);
}

void UIVirtualBoxManager::sltOpenManagerWindow(UIToolType enmType /* = UIToolType_Invalid */)
{
    /* Determine actual tool type on the basis of sender action if possible: */
    if (enmType == UIToolType_Invalid)
    {
        if (   sender()
            && sender()->inherits("UIAction"))
        {
            UIAction *pAction = qobject_cast<UIAction*>(sender());
            AssertPtrReturnVoid(pAction);
            enmType = pAction->property("UIToolType").value<UIToolType>();
        }
    }
    /* Make sure type is valid: */
    AssertReturnVoid(enmType != UIToolType_Invalid);

    /* First check if instance of widget opened the embedded way: */
    if (m_pWidget->isGlobalToolOpened(enmType))
    {
        m_pWidget->setToolsTypeGlobal(UIToolType_Home);
        m_pWidget->closeGlobalTool(enmType);
    }
    if (m_pWidget->isMachineToolOpened(enmType))
    {
        m_pWidget->setToolsTypeMachine(UIToolType_Details);
        m_pWidget->closeMachineTool(enmType);
    }

    /* Create instance if not yet created: */
    if (!m_managers.contains(enmType))
    {
        switch (enmType)
        {
            case UIToolType_Extensions: UIExtensionPackManagerFactory(m_pActionPool).prepare(m_managers[enmType], this); break;
            case UIToolType_Media: UIMediumManagerFactory(m_pActionPool).prepare(m_managers[enmType], this); break;
            case UIToolType_Network: UINetworkManagerFactory(m_pActionPool).prepare(m_managers[enmType], this); break;
            case UIToolType_Cloud: UICloudProfileManagerFactory(m_pActionPool).prepare(m_managers[enmType], this); break;
            case UIToolType_CloudConsole: UICloudConsoleManagerFactory(m_pActionPool).prepare(m_managers[enmType], this); break;
            case UIToolType_Logs:
            {
                /* Compose a list of selected machine IDs: */
                QList<QUuid> machineIDs;
                /* For each selected item: */
                foreach (UIVirtualMachineItem *pItem, currentItems())
                {
                    /* Make sure current item is local one: */
                    UIVirtualMachineItemLocal *pItemLocal = pItem->toLocal();
                    if (!pItemLocal)
                        continue;
                    /* Append machine ID: */
                    machineIDs << pItemLocal->id();
                }
                UIVMLogViewerDialogFactory(m_pActionPool, machineIDs).prepare(m_managers[enmType], this);
                break;
            }
            default: break;
        }

        connect(m_managers[enmType], &QIManagerDialog::sigEmbed,
                this, &UIVirtualBoxManager::sltEmbedManagerWindowDefault);
        connect(m_managers[enmType], &QIManagerDialog::sigClose,
                this, &UIVirtualBoxManager::sltCloseManagerWindowDefault);
    }

    /* Expose instance: */
    UIDesktopWidgetWatchdog::restoreWidget(m_managers.value(enmType));
}

void UIVirtualBoxManager::sltEmbedManagerWindow(UIToolType enmType /* = UIToolType_Invalid */)
{
    /* Determine actual tool type if possible: */
    if (enmType == UIToolType_Invalid)
    {
        if (   sender()
            && sender()->inherits("QIManagerDialog"))
        {
            QIManagerDialog *pManager = qobject_cast<QIManagerDialog*>(sender());
            AssertPtrReturnVoid(pManager);
            enmType = m_managers.key(pManager);
        }
    }

    /* Make sure type is valid: */
    AssertReturnVoid(enmType != UIToolType_Invalid);

    /* Remove tool from detached: */
    QList<UIToolType> tools = gEDataManager->detachedTools();
    if (tools.contains(UIToolType_Logs))
    {
        tools.removeAll(UIToolType_Logs);
        gEDataManager->setDetachedTools(tools);
    }

    /* Open known tool finally: */
    switch (enmType)
    {
        case UIToolType_Logs: m_pWidget->setToolsTypeMachine(enmType); break;
        default: break;
    }
}

void UIVirtualBoxManager::sltCloseManagerWindow(UIToolType enmType /* = UIToolType_Invalid */)
{
    /* Determine actual tool type if possible: */
    if (enmType == UIToolType_Invalid)
    {
        if (   sender()
            && sender()->inherits("QIManagerDialog"))
        {
            QIManagerDialog *pManager = qobject_cast<QIManagerDialog*>(sender());
            AssertPtrReturnVoid(pManager);
            enmType = m_managers.key(pManager);
        }
    }

    /* Make sure type is valid: */
    AssertReturnVoid(enmType != UIToolType_Invalid);

    /* Destroy instance if still exists: */
    if (m_managers.contains(enmType))
    {
        switch (enmType)
        {
            case UIToolType_Extensions: UIExtensionPackManagerFactory().cleanup(m_managers[enmType]); break;
            case UIToolType_Media: UIMediumManagerFactory().cleanup(m_managers[enmType]); break;
            case UIToolType_Network: UINetworkManagerFactory().cleanup(m_managers[enmType]); break;
            case UIToolType_Cloud: UICloudProfileManagerFactory().cleanup(m_managers[enmType]); break;
            case UIToolType_CloudConsole: UICloudConsoleManagerFactory().cleanup(m_managers[enmType]); break;
            case UIToolType_Logs: UIVMLogViewerDialogFactory().cleanup(m_managers[enmType]); break;
            default: break;
        }

        m_managers.remove(enmType);
    }
}

void UIVirtualBoxManager::sltOpenExportApplianceWizard()
{
    /* Check what was the action invoked us: */
    UIAction *pAction = qobject_cast<UIAction*>(sender());

    /* Export to OCI action invokes wizard directly in OCI mode: */
    m_fExportToOCI =    pAction
                     && pAction == actionPool()->action(UIActionIndexMN_M_Machine_S_ExportToOCI);
    /* Populate the list of VM names: */
    m_names.clear();
    foreach (UIVirtualMachineItem *pItem, currentItems())
        m_names << pItem->name();

    /* Open Export Appliance Wizard: */
    sltOpenWizard(WizardType_ExportAppliance);
}

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
void UIVirtualBoxManager::sltOpenExtraDataManagerWindow()
{
    gEDataManager->openWindow(this);
}
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

void UIVirtualBoxManager::sltOpenPreferencesDialog()
{
    /* Don't show the inaccessible warning
     * if the user tries to open global settings: */
    m_fFirstMediumEnumerationHandled = true;

    /* Create instance if not yet created: */
    if (!m_settings.contains(UIAdvancedSettingsDialog::Type_Global))
    {
        m_settings[UIAdvancedSettingsDialog::Type_Global] = new UIAdvancedSettingsDialogGlobal(this);
        connect(m_settings[UIAdvancedSettingsDialog::Type_Global], &UIAdvancedSettingsDialogGlobal::sigClose,
                this, &UIVirtualBoxManager::sltClosePreferencesDialog);
        const bool fSuccess = m_settings.value(UIAdvancedSettingsDialog::Type_Global)->load();
        if (!fSuccess)
        {
            delete m_settings.take(UIAdvancedSettingsDialog::Type_Global);
            return;
        }
    }

    /* Expose instance: */
    UIDesktopWidgetWatchdog::restoreWidget(m_settings.value(UIAdvancedSettingsDialog::Type_Global));
}

void UIVirtualBoxManager::sltClosePreferencesDialog()
{
    /* Remove instance if exist: */
    delete m_settings.take(UIAdvancedSettingsDialog::Type_Global);
}

void UIVirtualBoxManager::sltPerformSwitchToTool(QAction *pAction)
{
    /* Sanity checks: */
    AssertPtrReturnVoid(pAction);
    AssertPtrReturnVoid(m_pWidget);

    /* Acquire tool type: */
    const UIToolType enmType = pAction->property("UIToolType").value<UIToolType>();
    AssertReturnVoid(enmType != UIToolType_Invalid);

    /* Handle all possible classes: */
    const UIToolClass enmClass = UIToolStuff::castTypeToClass(enmType);
    switch (enmClass)
    {
        case UIToolClass_Global:
            m_pWidget->setToolsTypeGlobal(enmType);
            break;
        case UIToolClass_Machine:
            m_pWidget->setToolsTypeGlobal(UIToolType_Machines);
            m_pWidget->setToolsTypeMachine(enmType);
            break;
        default:
            AssertFailedReturnVoid();
            break;
    }
}

void UIVirtualBoxManager::sltPerformExit()
{
    close();
}

void UIVirtualBoxManager::sltOpenWizard(WizardType enmType)
{
    /* Create instance if not yet created: */
    if (!m_wizards.contains(enmType))
    {
        switch (enmType)
        {
            case WizardType_NewVM:
                m_wizards[enmType] = new UIWizardNewVM(this, actionPool(), m_pWidget->fullGroupName(), m_strISOFilePath);
                break;
            case WizardType_CloneVM:
            {
                UIVirtualMachineItem *pItem = currentItem();
                UIVirtualMachineItemLocal *pItemLocal = pItem ? pItem->toLocal() : 0;
                CMachine comMachine = pItemLocal ? pItemLocal->machine() : CMachine();
                CSnapshot comSnapshot;
                if (m_fSnapshotCloneByDefault)
                {
                    const QUuid uId = m_pWidget->currentSnapshotId();
                    if (!uId.isNull() && comMachine.isNotNull())
                    {
                        comSnapshot = comMachine.FindSnapshot(uId.toString());
                        if (comSnapshot.isNotNull())
                        {
                            const CMachine comSnapshotMachine = comSnapshot.GetMachine();
                            if (comSnapshotMachine.isNotNull())
                                comMachine = comSnapshotMachine;
                        }
                    }
                }
                m_wizards[enmType] = new UIWizardCloneVM(this,
                                                         comMachine,
                                                         pItemLocal->groups().value(0),
                                                         comSnapshot);
                break;
            }
            case WizardType_ExportAppliance:
                m_wizards[enmType] = new UIWizardExportApp(this, m_names, m_fExportToOCI);
                break;
            case WizardType_ImportAppliance:
                m_wizards[enmType] = new UIWizardImportApp(this, m_fImportFromOCI, m_strFileName);
                break;
            case WizardType_NewCloudVM:
                m_wizards[enmType] = new UIWizardNewCloudVM(this, m_pWidget->fullGroupName());
                break;
            case WizardType_AddCloudVM:
                m_wizards[enmType] = new UIWizardAddCloudVM(this, m_pWidget->fullGroupName());
                break;
            case WizardType_NewVD:
            {
                const QString strFolder = UIMediumTools::defaultFolderPathForType(UIMediumDeviceType_HardDisk);
                const QString strDiskName = uiCommon().findUniqueFileName(strFolder, "NewVirtualDisk");
                const CGuestOSType comGuestOSType = gpGlobalSession->virtualBox().GetGuestOSType("Other");
                const qulonglong uDiskSize = comGuestOSType.GetRecommendedHDD();
                m_wizards[enmType] = new UIWizardNewVD(this,
                                                       strDiskName,
                                                       strFolder,
                                                       uDiskSize);
                break;
            }
            case WizardType_CloneVD:
                m_wizards[enmType] = new UIWizardNewVD(this, m_uMediumId);
                break;
            default:
                break;
        }

        connect(m_wizards.value(enmType), &UINativeWizard::sigClose,
                this, &UIVirtualBoxManager::sltCloseWizard);
    }

    /* Expose instance: */
    m_wizards.value(enmType)->show();
    m_wizards.value(enmType)->setWindowState(m_wizards.value(enmType)->windowState() & ~Qt::WindowMinimized);
    m_wizards.value(enmType)->activateWindow();
    m_wizards.value(enmType)->raise();
}

void UIVirtualBoxManager::sltCloseWizard(WizardType enmType)
{
    /* Postprocess wizard if still exists: */
    if (m_wizards.contains(enmType))
    {
        switch (enmType)
        {
            case WizardType_NewVM:
            {
                UIWizardNewVM *pWizard = qobject_cast<UIWizardNewVM*>(m_wizards.value(enmType));
                if (pWizard->isUnattendedEnabled())
                    startUnattendedInstall(pWizard->installer(),
                                           pWizard->startHeadless(),
                                           pWizard->createdMachineId().toString());
                break;
            }
            default:
                break;
        }
    }

    /* Cleanup instance: */
    delete m_wizards.take(enmType);
}

void UIVirtualBoxManager::sltOpenNewMachineWizard()
{
    /* Get first selected item: */
    UIVirtualMachineItem *pItem = currentItem();

    /* If there is no items at all or first selected item is a local machine: */
    if (!pItem || pItem->itemType() == UIVirtualMachineItemType_Local)
        openNewMachineWizard();
    /* Otherwise we guess it's cloud related item selected: */
    else
        sltOpenWizard(WizardType_NewCloudVM);
}

void UIVirtualBoxManager::sltOpenAddMachineDialog()
{
    /* Get first selected item: */
    UIVirtualMachineItem *pItem = currentItem();

    /* If there is no items at all or first selected item is a local machine: */
    if (!pItem || pItem->itemType() == UIVirtualMachineItemType_Local)
        openAddMachineDialog();
    /* Otherwise we guess it's cloud related item selected: */
    else
        sltOpenWizard(WizardType_AddCloudVM);
}

void UIVirtualBoxManager::sltOpenGroupNameEditor()
{
    m_pWidget->openGroupNameEditor();
}

void UIVirtualBoxManager::sltDisbandGroup()
{
    m_pWidget->disbandGroup();
}

void UIVirtualBoxManager::sltOpenSettingsDialog(QString strCategory /* = QString() */,
                                                QString strControl /* = QString() */,
                                                const QUuid &uID /* = QString() */)
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));

    /* For local machine: */
    if (pItem->itemType() == UIVirtualMachineItemType_Local)
    {
        /* Process href from VM details / description: */
        if (!strCategory.isEmpty() && strCategory[0] != '#')
        {
            uiCommon().openURL(strCategory);
        }
        else
        {
            /* Check if control is coded into the URL by %%: */
            if (strControl.isEmpty())
            {
                QStringList parts = strCategory.split("%%");
                if (parts.size() == 2)
                {
                    strCategory = parts.at(0);
                    strControl = parts.at(1);
                }
            }

            /* Don't show the inaccessible warning
             * if the user tries to open VM settings: */
            m_fFirstMediumEnumerationHandled = true;

            /* Create instance if not yet created: */
            if (!m_settings.contains(UIAdvancedSettingsDialog::Type_Machine))
            {
                m_settings[UIAdvancedSettingsDialog::Type_Machine] = new UIAdvancedSettingsDialogMachine(this,
                                                                                                         uID.isNull() ? pItem->id() : uID,
                                                                                                         actionPool(),
                                                                                                         strCategory,
                                                                                                         strControl);
                connect(m_settings[UIAdvancedSettingsDialog::Type_Machine], &UIAdvancedSettingsDialogMachine::sigClose,
                        this, &UIVirtualBoxManager::sltCloseSettingsDialog);
                const bool fSuccess = m_settings.value(UIAdvancedSettingsDialog::Type_Machine)->load();
                if (!fSuccess)
                {
                    delete m_settings.take(UIAdvancedSettingsDialog::Type_Machine);
                    return;
                }
            }

            /* Expose instance: */
            UIDesktopWidgetWatchdog::restoreWidget(m_settings.value(UIAdvancedSettingsDialog::Type_Machine));
        }
    }
    /* For cloud machine: */
    else
    {
        /* Create instance if not yet created: */
        if (m_pCloudSettings.isNull())
        {
            m_pCloudSettings = new UICloudMachineSettingsDialog(this,
                                                                pItem->toCloud()->machine());
            connect(m_pCloudSettings, &UICloudMachineSettingsDialog::sigClose,
                    this, &UIVirtualBoxManager::sltCloseSettingsDialog);
        }

        /* Expose instance: */
        UIDesktopWidgetWatchdog::restoreWidget(m_pCloudSettings);
    }
}

void UIVirtualBoxManager::sltCloseSettingsDialog()
{
    /* What type of dialog should we delete? */
    enum DelType { None, Local, Cloud, All } enmType = None;
    if (qobject_cast<UIAdvancedSettingsDialog*>(sender()))
        enmType = (DelType)(enmType | Local);
    else if (qobject_cast<UICloudMachineSettingsDialog*>(sender()))
        enmType = (DelType)(enmType | Cloud);

    /* It's all if nothing: */
    if (enmType == None)
        enmType = All;

    /* Remove requested instances: */
    if (enmType & Local)
        delete m_settings.take(UIAdvancedSettingsDialog::Type_Machine);
    if (enmType & Cloud)
        delete m_pCloudSettings;
}

void UIVirtualBoxManager::sltOpenCloneMachineWizard()
{
    /* Get current items: */
    QList<UIVirtualMachineItem*> items = currentItems();

    /* We are opening Clone VM wizard for local VMs only: */
    if (isItemsLocal(items))
    {
        /* Configure wizard variables: */
        m_fSnapshotCloneByDefault = false;
        QAction *pAction = qobject_cast<QAction*>(sender());
        if (   pAction
            && pAction == actionPool()->action(UIActionIndexMN_M_Snapshot_S_Clone))
            m_fSnapshotCloneByDefault = true;

        /* Open Clone VM Wizard: */
        sltOpenWizard(WizardType_CloneVM);
    }
    /* For cloud VMs we have no such wizard for now: */
    else if (isItemsCloud(items))
    {
        /* Acquire item, propose clone name: */
        UIVirtualMachineItem *pItem = items.first();
        const QString strName = QString("%1_clone").arg(pItem->name());

        /* Create Acquire Clone VM name dialog: */
        QPointer<UIAcquireCloudMachineCloneNameDialog> pDialog = new UIAcquireCloudMachineCloneNameDialog(this, strName);
        if (pDialog->exec() == QDialog::Accepted)
        {
            /* Parse current full group name: */
            const QString strFullGroupName = m_pWidget->fullGroupName();
            const QString strProviderShortName = strFullGroupName.section('/', 1, 1);
            const QString strProfileName = strFullGroupName.section('/', 2, 2);

            /* Acquire cloud machine: */
            const CCloudMachine comMachine = pItem->toCloud()->machine();

            /* Clone VM: */
            createCloudMachineClone(strProviderShortName, strProfileName,
                                    comMachine, pDialog->value(),
                                    gpNotificationCenter);
       }
       delete pDialog;
    }
}

void UIVirtualBoxManager::sltPerformMachineMove()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));

    /* Open a file dialog for the user to select a destination folder. Start with the default machine folder: */
    const QString strBaseFolder = gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder();
    const QString strTitle = tr("Select a destination folder to move the selected virtual machine");
    const QString strDestinationFolder = QIFileDialog::getExistingDirectory(strBaseFolder, this, strTitle);
    if (!strDestinationFolder.isEmpty())
    {
        /* Move machine: */
        UINotificationProgressMachineMove *pNotification = new UINotificationProgressMachineMove(pItem->id(),
                                                                                                 strDestinationFolder,
                                                                                                 "basic");
        gpNotificationCenter->append(pNotification);
    }
}

void UIVirtualBoxManager::sltPerformMachineRemove()
{
    m_pWidget->removeMachine();
}

void UIVirtualBoxManager::sltPerformMachineMoveToNewGroup()
{
    m_pWidget->moveMachineToGroup();
}

void UIVirtualBoxManager::sltPerformMachineMoveToSpecificGroup()
{
    AssertPtrReturnVoid(sender());
    QAction *pAction = qobject_cast<QAction*>(sender());
    AssertPtrReturnVoid(pAction);
    m_pWidget->moveMachineToGroup(pAction->property("actual_group_name").toString());
}

void UIVirtualBoxManager::sltPerformStartOrShowMachine()
{
    /* Check whether we can start or show 1st VM item: */
    UIVirtualMachineItem *pItem = currentItem();
    if (pItem && pItem->isItemPoweredOff())
        sltPerformStartMachine();
    else
        sltPerformShowMachine();
}

void UIVirtualBoxManager::sltPerformStartMachine()
{
    /* Start selected VMs in corresponding mode: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));
    performStartVirtualMachines(items, UILaunchMode_Invalid);
}

void UIVirtualBoxManager::sltPerformStartMachineNormal()
{
    /* Start selected VMs in corresponding mode: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));
    performStartVirtualMachines(items, UILaunchMode_Default);
}

void UIVirtualBoxManager::sltPerformStartMachineHeadless()
{
    /* Start selected VMs in corresponding mode: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));
    performStartVirtualMachines(items, UILaunchMode_Headless);
}

void UIVirtualBoxManager::sltPerformStartMachineDetachable()
{
    /* Start selected VMs in corresponding mode: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));
    performStartVirtualMachines(items, UILaunchMode_Separate);
}

void UIVirtualBoxManager::sltPerformShowMachine()
{
    /* Show selected VMs: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));
    performShowVirtualMachines(items);
}

void UIVirtualBoxManager::sltPerformCreateConsoleConnectionForGroup()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Create input dialog to pass public key to newly created console connection: */
    QPointer<UIAcquirePublicKeyDialog> pDialog = new UIAcquirePublicKeyDialog(this);
    if (pDialog)
    {
        if (pDialog->exec() == QDialog::Accepted)
        {
            foreach (UIVirtualMachineItem *pItem, items)
            {
                /* Make sure the item exists: */
                AssertPtr(pItem);
                if (pItem)
                {
                    /* Make sure the item is of cloud type: */
                    UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
                    if (pCloudItem)
                    {
                        /* Acquire current machine: */
                        CCloudMachine comMachine = pCloudItem->machine();

                        /* Acquire machine console connection fingerprint: */
                        QString strConsoleConnectionFingerprint;
                        if (cloudMachineConsoleConnectionFingerprint(comMachine, strConsoleConnectionFingerprint))
                        {
                            /* Only if no fingerprint exist: */
                            if (strConsoleConnectionFingerprint.isEmpty())
                            {
                                /* Create cloud console connection: */
                                UINotificationProgressCloudConsoleConnectionCreate *pNotification =
                                    new UINotificationProgressCloudConsoleConnectionCreate(comMachine,
                                                                                           pDialog->publicKey());
                                gpNotificationCenter->append(pNotification);
                            }
                        }
                    }
                }
            }
        }
        delete pDialog;
    }
}

void UIVirtualBoxManager::sltPerformCreateConsoleConnectionForMachine()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));

    /* Create input dialog to pass public key to newly created console connection: */
    QPointer<UIAcquirePublicKeyDialog> pDialog = new UIAcquirePublicKeyDialog(this);
    if (pDialog)
    {
        if (pDialog->exec() == QDialog::Accepted)
        {
            /* Make sure the item is of cloud type: */
            UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
            AssertPtr(pCloudItem);
            if (pCloudItem)
            {
                /* Acquire current machine: */
                CCloudMachine comMachine = pCloudItem->machine();

                /* Acquire machine console connection fingerprint: */
                QString strConsoleConnectionFingerprint;
                if (cloudMachineConsoleConnectionFingerprint(comMachine, strConsoleConnectionFingerprint))
                {
                    /* Only if no fingerprint exist: */
                    if (strConsoleConnectionFingerprint.isEmpty())
                    {
                        /* Create cloud console connection: */
                        UINotificationProgressCloudConsoleConnectionCreate *pNotification =
                            new UINotificationProgressCloudConsoleConnectionCreate(comMachine,
                                                                                   pDialog->publicKey());
                        gpNotificationCenter->append(pNotification);
                    }
                }
            }
        }
        delete pDialog;
    }
}

void UIVirtualBoxManager::sltPerformDeleteConsoleConnectionForGroup()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Make sure the item exists: */
        AssertPtr(pItem);
        if (pItem)
        {
            /* Make sure the item is of cloud type: */
            UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
            if (pCloudItem)
            {
                /* Acquire current machine: */
                CCloudMachine comMachine = pCloudItem->machine();

                /* Acquire machine console connection fingerprint: */
                QString strConsoleConnectionFingerprint;
                if (cloudMachineConsoleConnectionFingerprint(comMachine, strConsoleConnectionFingerprint))
                {
                    /* Only if fingerprint exists: */
                    if (!strConsoleConnectionFingerprint.isEmpty())
                    {
                        /* Delete cloud console connection: */
                        UINotificationProgressCloudConsoleConnectionDelete *pNotification =
                            new UINotificationProgressCloudConsoleConnectionDelete(comMachine);
                        gpNotificationCenter->append(pNotification);
                    }
                }
            }
        }
    }
}

void UIVirtualBoxManager::sltPerformDeleteConsoleConnectionForMachine()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));

    /* Make sure the item is of cloud type: */
    UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
    AssertPtr(pCloudItem);
    if (pCloudItem)
    {
        /* Acquire current machine: */
        CCloudMachine comMachine = pCloudItem->machine();

        /* Acquire machine console connection fingerprint: */
        QString strConsoleConnectionFingerprint;
        if (cloudMachineConsoleConnectionFingerprint(comMachine, strConsoleConnectionFingerprint))
        {
            /* Only if fingerprint exists: */
            if (!strConsoleConnectionFingerprint.isEmpty())
            {
                /* Delete cloud console connection: */
                UINotificationProgressCloudConsoleConnectionDelete *pNotification =
                    new UINotificationProgressCloudConsoleConnectionDelete(comMachine);
                gpNotificationCenter->append(pNotification);
            }
        }
    }
}

void UIVirtualBoxManager::sltCopyConsoleConnectionFingerprint()
{
    QAction *pAction = qobject_cast<QAction*>(sender());
    AssertPtrReturnVoid(pAction);
    QClipboard *pClipboard = QGuiApplication::clipboard();
    AssertPtrReturnVoid(pClipboard);
    pClipboard->setText(pAction->property("fingerprint").toString());
}

void UIVirtualBoxManager::sltExecuteExternalApplication()
{
    /* Acquire passed path and argument strings: */
    QAction *pAction = qobject_cast<QAction*>(sender());
    AssertMsgReturnVoid(pAction, ("This slot should be called by action only!\n"));
    const QString strPath = pAction->property("path").toString();
    const QString strArguments = pAction->property("arguments").toString();

    /* Get current-item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));
    UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
    AssertPtrReturnVoid(pCloudItem);

    /* Get cloud machine to acquire serial command: */
    const CCloudMachine comMachine = pCloudItem->machine();

#if defined(VBOX_WS_MAC)
    /* Gather arguments: */
    QStringList arguments;
    arguments << parseShellArguments(strArguments);

    /* Make sure that isn't a request to start Open command: */
    if (strPath != "open" && strPath != "/usr/bin/open")
    {
        /* In that case just add the command we have as simple argument: */
        arguments << comMachine.GetSerialConsoleCommand();
    }
    else
    {
        /* Otherwise upload command to external file which can be opened with Open command: */
        QDir uiHomeFolder(gpGlobalSession->virtualBox().GetHomeFolder());
        const QString strAbsoluteCommandName = uiHomeFolder.absoluteFilePath("last.command");
        QFile file(strAbsoluteCommandName);
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        if (!file.open(QIODevice::WriteOnly))
            AssertFailedReturnVoid();
        file.write(comMachine.GetSerialConsoleCommand().toUtf8());
        file.close();
        arguments << strAbsoluteCommandName;
    }

    /* Execute console application finally: */
    QProcess::startDetached(strPath, arguments);
#elif defined(VBOX_WS_WIN)
    /* Gather arguments: */
    QStringList arguments;
    arguments << strArguments;
    arguments << comMachine.GetSerialConsoleCommandWindows();

    /* Execute console application finally: */
    QProcess::startDetached(QString("%1 %2").arg(strPath, arguments.join(' ')));
#elif defined(VBOX_WS_NIX)
    /* Gather arguments: */
    QStringList arguments;
    arguments << parseShellArguments(strArguments);
    arguments << comMachine.GetSerialConsoleCommand();

    /* Execute console application finally: */
    QProcess::startDetached(strPath, arguments);
#endif /* VBOX_WS_NIX */
}

void UIVirtualBoxManager::sltPerformCopyCommandSerialUnix()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));
    UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
    AssertPtrReturnVoid(pCloudItem);

    /* Acquire cloud machine: */
    CCloudMachine comMachine = pCloudItem->machine();

    /* Put copied serial command to clipboard: */
    QClipboard *pClipboard = QGuiApplication::clipboard();
    AssertPtrReturnVoid(pClipboard);
    pClipboard->setText(comMachine.GetSerialConsoleCommand());
}

void UIVirtualBoxManager::sltPerformCopyCommandSerialWindows()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));
    UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
    AssertPtrReturnVoid(pCloudItem);

    /* Acquire cloud machine: */
    CCloudMachine comMachine = pCloudItem->machine();

    /* Put copied serial command to clipboard: */
    QClipboard *pClipboard = QGuiApplication::clipboard();
    AssertPtrReturnVoid(pClipboard);
    pClipboard->setText(comMachine.GetSerialConsoleCommandWindows());
}

void UIVirtualBoxManager::sltPerformCopyCommandVNCUnix()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));
    UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
    AssertPtrReturnVoid(pCloudItem);

    /* Acquire cloud machine: */
    CCloudMachine comMachine = pCloudItem->machine();

    /* Put copied VNC command to clipboard: */
    QClipboard *pClipboard = QGuiApplication::clipboard();
    AssertPtrReturnVoid(pClipboard);
    pClipboard->setText(comMachine.GetVNCConsoleCommand());
}

void UIVirtualBoxManager::sltPerformCopyCommandVNCWindows()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));
    UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
    AssertPtrReturnVoid(pCloudItem);

    /* Acquire cloud machine: */
    CCloudMachine comMachine = pCloudItem->machine();

    /* Put copied VNC command to clipboard: */
    QClipboard *pClipboard = QGuiApplication::clipboard();
    AssertPtrReturnVoid(pClipboard);
    pClipboard->setText(comMachine.GetVNCConsoleCommandWindows());
}

void UIVirtualBoxManager::sltPerformShowLog()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));
    UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
    AssertPtrReturnVoid(pCloudItem);

    /* Acquire cloud machine: */
    CCloudMachine comMachine = pCloudItem->machine();

    /* Requesting cloud console log: */
    UINotificationProgressCloudConsoleLogAcquire *pNotification = new UINotificationProgressCloudConsoleLogAcquire(comMachine);
    connect(pNotification, &UINotificationProgressCloudConsoleLogAcquire::sigLogRead,
            this, &UIVirtualBoxManager::sltHandleConsoleLogRead);
    gpNotificationCenter->append(pNotification);
}

void UIVirtualBoxManager::sltHandleConsoleLogRead(const QString &strName, const QString &strLog)
{
    /* Prepare dialog: */
    QWidget *pWindow = new QWidget(this, Qt::Window);
    if (pWindow)
    {
        pWindow->setAttribute(Qt::WA_DeleteOnClose);
        pWindow->setWindowTitle(QString("%1 - Console Log").arg(strName));

        QVBoxLayout *pLayout = new QVBoxLayout(pWindow);
        if (pLayout)
        {
            QTextEdit *pTextEdit = new QTextEdit(pWindow);
            if (pTextEdit)
            {
                pTextEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
                pTextEdit->setReadOnly(true);
                pTextEdit->setText(strLog);
                pLayout->addWidget(pTextEdit);
            }
        }
    }

    /* Show dialog: */
    pWindow->show();
}

void UIVirtualBoxManager::sltPerformDiscardMachineState()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Prepare the list of the machines to be discarded/terminated: */
    QStringList machinesToDiscard;
    QList<UIVirtualMachineItem*> itemsToDiscard;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isActionEnabled(UIActionIndexMN_M_Group_S_Discard, QList<UIVirtualMachineItem*>() << pItem))
        {
            machinesToDiscard << pItem->name();
            itemsToDiscard << pItem;
        }
    }
    AssertMsg(!machinesToDiscard.isEmpty(), ("This action should not be allowed!"));

    /* Confirm discarding: */
    if (   machinesToDiscard.isEmpty()
        || !msgCenter().confirmDiscardSavedState(machinesToDiscard.join(", ")))
        return;

    /* For every confirmed item to discard: */
    foreach (UIVirtualMachineItem *pItem, itemsToDiscard)
    {
        /* Open a session to modify VM: */
        AssertPtrReturnVoid(pItem);
        CSession comSession = openSession(pItem->id());
        if (comSession.isNull())
            return;

        /* Get session machine: */
        CMachine comMachine = comSession.GetMachine();
        comMachine.DiscardSavedState(true);
        if (!comMachine.isOk())
            UINotificationMessage::cannotDiscardSavedState(comMachine);

        /* Unlock machine finally: */
        comSession.UnlockMachine();
    }
}

void UIVirtualBoxManager::sltPerformPauseOrResumeMachine(bool fPause)
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For every selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* But for local machine items only: */
        AssertPtrReturnVoid(pItem);
        if (pItem->itemType() != UIVirtualMachineItemType_Local)
            continue;

        /* Get local machine item state: */
        UIVirtualMachineItemLocal *pLocalItem = pItem->toLocal();
        AssertPtrReturnVoid(pLocalItem);
        const KMachineState enmState = pLocalItem->machineState();

        /* Check if current item could be paused/resumed: */
        if (!isActionEnabled(UIActionIndexMN_M_Group_T_Pause, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /* Check if current item already paused: */
        if (fPause &&
            (enmState == KMachineState_Paused ||
             enmState == KMachineState_TeleportingPausedVM))
            continue;

        /* Check if current item already resumed: */
        if (!fPause &&
            (enmState == KMachineState_Running ||
             enmState == KMachineState_Teleporting ||
             enmState == KMachineState_LiveSnapshotting))
            continue;

        /* Open a session to modify VM state: */
        CSession comSession = openExistingSession(pItem->id());
        if (comSession.isNull())
            return;

        /* Get session console: */
        CConsole comConsole = comSession.GetConsole();
        /* Pause/resume VM: */
        if (fPause)
            comConsole.Pause();
        else
            comConsole.Resume();
        if (!comConsole.isOk())
        {
            if (fPause)
                UINotificationMessage::cannotPauseMachine(comConsole);
            else
                UINotificationMessage::cannotResumeMachine(comConsole);
        }

        /* Unlock machine finally: */
        comSession.UnlockMachine();
    }
}

void UIVirtualBoxManager::sltPerformResetMachine()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Prepare the list of the machines to be reseted: */
    QStringList machineNames;
    QList<UIVirtualMachineItem*> itemsToReset;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isActionEnabled(UIActionIndexMN_M_Group_S_Reset, QList<UIVirtualMachineItem*>() << pItem))
        {
            machineNames << pItem->name();
            itemsToReset << pItem;
        }
    }
    AssertMsg(!machineNames.isEmpty(), ("This action should not be allowed!"));

    /* Confirm reseting VM: */
    if (!msgCenter().confirmResetMachine(machineNames.join(", ")))
        return;

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, itemsToReset)
    {
        switch (pItem->itemType())
        {
            case UIVirtualMachineItemType_Local:
            {
                /* Open a session to modify VM state: */
                CSession comSession = openExistingSession(pItem->id());
                if (comSession.isNull())
                    return;

                /* Get session console: */
                CConsole comConsole = comSession.GetConsole();
                /* Reset VM: */
                comConsole.Reset();

                /* Unlock machine finally: */
                comSession.UnlockMachine();
                break;
            }
            case UIVirtualMachineItemType_CloudReal:
            {
                /* Reset VM: */
                UINotificationProgressCloudMachineReset *pNotification =
                    new UINotificationProgressCloudMachineReset(pItem->toCloud()->machine());
                gpNotificationCenter->append(pNotification);
                break;
            }
            default:
                break;
        }
    }
}

void UIVirtualBoxManager::sltPerformDetachMachineUI()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Check if current item could be detached: */
        if (!isActionEnabled(UIActionIndexMN_M_Machine_S_Detach, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /// @todo Detach separate UI process..
        AssertFailed();
    }
}

void UIVirtualBoxManager::sltPerformSaveMachineState()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Sanity check: */
        AssertPtrReturnVoid(pItem);
        AssertPtrReturnVoid(pItem->toLocal());

        /* Check if current item could be saved: */
        if (!isActionEnabled(UIActionIndexMN_M_Machine_M_Stop_S_SaveState, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /* Saving VM state: */
        UINotificationProgressMachineSaveState *pNotification = new UINotificationProgressMachineSaveState(pItem->toLocal()->machine());
        gpNotificationCenter->append(pNotification);
    }
}

void UIVirtualBoxManager::sltPerformTerminateMachine()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Prepare the list of the machines to be terminated: */
    QStringList machinesToTerminate;
    QList<UIVirtualMachineItem*> itemsToTerminate;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isActionEnabled(UIActionIndexMN_M_Group_M_Stop_S_Terminate, QList<UIVirtualMachineItem*>() << pItem))
        {
            machinesToTerminate << pItem->name();
            itemsToTerminate << pItem;
        }
    }
    AssertMsg(!machinesToTerminate.isEmpty(), ("This action should not be allowed!"));

    /* Confirm terminating: */
    if (   machinesToTerminate.isEmpty()
        || !msgCenter().confirmTerminateCloudInstance(machinesToTerminate.join(", ")))
        return;

    /* For every confirmed item to terminate: */
    foreach (UIVirtualMachineItem *pItem, itemsToTerminate)
    {
        /* Sanity check: */
        AssertPtrReturnVoid(pItem);

        /* Terminating cloud VM: */
        UINotificationProgressCloudMachineTerminate *pNotification =
            new UINotificationProgressCloudMachineTerminate(pItem->toCloud()->machine());
        gpNotificationCenter->append(pNotification);
    }
}

void UIVirtualBoxManager::sltPerformShutdownMachine()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Prepare the list of the machines to be shutdowned: */
    QStringList machineNames;
    QList<UIVirtualMachineItem*> itemsToShutdown;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isActionEnabled(UIActionIndexMN_M_Machine_M_Stop_S_Shutdown, QList<UIVirtualMachineItem*>() << pItem))
        {
            machineNames << pItem->name();
            itemsToShutdown << pItem;
        }
    }
    AssertMsg(!machineNames.isEmpty(), ("This action should not be allowed!"));

    /* Confirm ACPI shutdown current VM: */
    if (!msgCenter().confirmACPIShutdownMachine(machineNames.join(", ")))
        return;

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, itemsToShutdown)
    {
        /* Sanity check: */
        AssertPtrReturnVoid(pItem);

        /* For local machine: */
        if (pItem->itemType() == UIVirtualMachineItemType_Local)
        {
            /* Open a session to modify VM state: */
            CSession comSession = openExistingSession(pItem->id());
            if (comSession.isNull())
                return;

            /* Get session console: */
            CConsole comConsole = comSession.GetConsole();
            /* ACPI Shutdown: */
            comConsole.PowerButton();
            if (!comConsole.isOk())
                UINotificationMessage::cannotACPIShutdownMachine(comConsole);

            /* Unlock machine finally: */
            comSession.UnlockMachine();
        }
        /* For real cloud machine: */
        else if (pItem->itemType() == UIVirtualMachineItemType_CloudReal)
        {
            /* Shutting cloud VM down: */
            UINotificationProgressCloudMachineShutdown *pNotification =
                new UINotificationProgressCloudMachineShutdown(pItem->toCloud()->machine());
            gpNotificationCenter->append(pNotification);
        }
    }
}

void UIVirtualBoxManager::sltPerformPowerOffMachine()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Prepare the list of the machines to be powered off: */
    QStringList machineNames;
    QList<UIVirtualMachineItem*> itemsToPowerOff;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isActionEnabled(UIActionIndexMN_M_Machine_M_Stop_S_PowerOff, QList<UIVirtualMachineItem*>() << pItem))
        {
            machineNames << pItem->name();
            itemsToPowerOff << pItem;
        }
    }
    AssertMsg(!machineNames.isEmpty(), ("This action should not be allowed!"));

    /* Confirm Power Off current VM: */
    if (!msgCenter().confirmPowerOffMachine(machineNames.join(", ")))
        return;

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, itemsToPowerOff)
    {
        /* Sanity check: */
        AssertPtrReturnVoid(pItem);

        /* For local machine: */
        if (pItem->itemType() == UIVirtualMachineItemType_Local)
        {
            /* Powering VM off: */
            UINotificationProgressMachinePowerOff *pNotification =
                new UINotificationProgressMachinePowerOff(pItem->toLocal()->machine(),
                                                          CConsole() /* dummy */,
                                                          gEDataManager->discardStateOnPowerOff(pItem->id()));
            pNotification->setProperty("machine_id", pItem->id());
            connect(pNotification, &UINotificationProgressMachinePowerOff::sigMachinePoweredOff,
                    this, &UIVirtualBoxManager::sltHandlePoweredOffMachine);
            gpNotificationCenter->append(pNotification);
        }
        /* For real cloud machine: */
        else if (pItem->itemType() == UIVirtualMachineItemType_CloudReal)
        {
            /* Powering cloud VM off: */
            UINotificationProgressCloudMachinePowerOff *pNotification =
                new UINotificationProgressCloudMachinePowerOff(pItem->toCloud()->machine());
            gpNotificationCenter->append(pNotification);
        }
    }
}

void UIVirtualBoxManager::sltHandlePoweredOffMachine(bool fSuccess, bool fIncludingDiscard)
{
    /* Was previous step successful? */
    if (fSuccess)
    {
        /* Do we have other tasks? */
        if (fIncludingDiscard)
        {
            /* Discard state if requested: */
            AssertPtrReturnVoid(sender());
            UINotificationProgressSnapshotRestore *pNotification =
                new UINotificationProgressSnapshotRestore(sender()->property("machine_id").toUuid());
            gpNotificationCenter->append(pNotification);
        }
    }
}

void UIVirtualBoxManager::sltPerformSwitchToMachineTool(QAction *pAction)
{
    /* Sanity checks: */
    AssertPtrReturnVoid(pAction);
    AssertPtrReturnVoid(m_pWidget);

    /* Acquire tool type: */
    const UIToolType enmType = pAction->property("UIToolType").value<UIToolType>();
    AssertReturnVoid(enmType != UIToolType_Invalid);

    /* Check if this tool should be opened detached way: */
    if (gEDataManager->detachedTools().contains(enmType))
        return sltOpenManagerWindow(enmType);

    /* Open the tool finally: */
    m_pWidget->setToolsTypeMachine(enmType);
}

void UIVirtualBoxManager::sltPerformRefreshMachine()
{
    m_pWidget->refreshMachine();
}

void UIVirtualBoxManager::sltShowMachineInFileManager()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Make sure current item is local one: */
        UIVirtualMachineItemLocal *pItemLocal = pItem->toLocal();
        if (!pItemLocal)
            continue;

        /* Check if that item could be shown in file-browser: */
        if (!isActionEnabled(UIActionIndexMN_M_Group_S_ShowInFileManager, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /* Show VM in filebrowser: */
        UIDesktopServices::openInFileManager(pItemLocal->machine().GetSettingsFilePath());
    }
}

void UIVirtualBoxManager::sltPerformCreateMachineShortcut()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Make sure current item is local one: */
        UIVirtualMachineItemLocal *pItemLocal = pItem->toLocal();
        if (!pItemLocal)
            continue;

        /* Check if shortcuts could be created for this item: */
        if (!isActionEnabled(UIActionIndexMN_M_Group_S_CreateShortcut, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /* Create shortcut for this VM: */
        const CMachine &comMachine = pItemLocal->machine();
        UIDesktopServices::createMachineShortcut(comMachine.GetSettingsFilePath(),
                                                 QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
                                                 comMachine.GetName(), comMachine.GetId());
    }
}

void UIVirtualBoxManager::sltPerformGroupSorting()
{
    m_pWidget->sortGroup();
}

void UIVirtualBoxManager::sltPerformMachineSearchWidgetVisibilityToggling(bool fVisible)
{
    m_pWidget->setMachineSearchWidgetVisibility(fVisible);
}

void UIVirtualBoxManager::sltPerformShowHelpBrowser()
{
    QString strHelpKeyword;
    if (m_pWidget)
        strHelpKeyword = m_pWidget->currentHelpKeyword();
    if (!strHelpKeyword.isEmpty())
        UIHelpBrowserDialog::findManualFileAndShow(strHelpKeyword);
}

void UIVirtualBoxManager::prepare()
{
#ifdef VBOX_WS_NIX
    NativeWindowSubsystem::setWMClass(this, "VirtualBox Manager", "VirtualBox Manager");
#endif

#ifdef VBOX_WS_MAC
    /* Install global event-filter, since vmstarter.app can send us FileOpen events,
     * see UIVirtualBoxManager::eventFilter for handler implementation. */
    qApp->installEventFilter(this);
#endif

    /* Cache media data early if necessary: */
    if (uiCommon().agressiveCaching())
        gpMediumEnumerator->enumerateMedia();

    /* Prepare: */
    prepareCloudMachineManager();
    prepareIcon();
    prepareMenuBar();
    prepareStatusBar();
    prepareWidgets();
    prepareConnections();

    /* Update actions initially: */
    sltHandleChooserPaneSelectionChange();

    /* Load settings: */
    loadSettings();

    /* Translate UI: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVirtualBoxManager::sltRetranslateUI);

#ifdef VBOX_WS_MAC
    /* Beta label? */
    if (UIVersionInfo::showBetaLabel())
    {
        QPixmap betaLabel = ::betaLabel(QSize(74, darwinWindowTitleHeight(this) - 1));
        ::darwinLabelWindow(this, &betaLabel);
    }
#endif /* VBOX_WS_MAC */

    /* If there are unhandled URLs we should handle them after manager is shown: */
    if (uiCommon().argumentUrlsPresent())
        QMetaObject::invokeMethod(this, "sltHandleOpenUrlCall", Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, "sltCheckUSBAccesibility", Qt::QueuedConnection);
}

void UIVirtualBoxManager::prepareCloudMachineManager()
{
    UICloudMachineManager::create();
}

void UIVirtualBoxManager::prepareIcon()
{
    /* Prepare application icon.
     * On Win host it's built-in to the executable.
     * On Mac OS X the icon referenced in info.plist is used.
     * On X11 we will provide as much icons as we can. */
#if !defined(VBOX_WS_WIN) && !defined(VBOX_WS_MAC)
    QIcon icon(":/VirtualBox.svg");
    icon.addFile(":/VirtualBox_48px.png");
    icon.addFile(":/VirtualBox_64px.png");
    setWindowIcon(icon);
#endif /* !VBOX_WS_WIN && !VBOX_WS_MAC */
}

void UIVirtualBoxManager::prepareMenuBar()
{
#ifndef VBOX_WS_MAC
    /* Create menu-bar: */
    setMenuBar(new UIMenuBar);
    if (menuBar())
    {
        /* Make sure menu-bar fills own solid background: */
        menuBar()->setAutoFillBackground(true);
    }
#endif /* !VBOX_WS_MAC */

    /* Create action-pool: */
    m_pActionPool = UIActionPool::create(UIType_ManagerUI);

    /* Prepare menu update-handlers: */
    m_menuUpdateHandlers[UIActionIndexMN_M_Group] = &UIVirtualBoxManager::updateMenuGroup;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine] = &UIVirtualBoxManager::updateMenuMachine;
    m_menuUpdateHandlers[UIActionIndexMN_M_Group_M_MoveToGroup] = &UIVirtualBoxManager::updateMenuGroupMoveToGroup;
    m_menuUpdateHandlers[UIActionIndexMN_M_Group_M_Console] = &UIVirtualBoxManager::updateMenuGroupConsole;
    m_menuUpdateHandlers[UIActionIndexMN_M_Group_M_Stop] = &UIVirtualBoxManager::updateMenuGroupClose;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine_M_MoveToGroup] = &UIVirtualBoxManager::updateMenuMachineMoveToGroup;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine_M_Console] = &UIVirtualBoxManager::updateMenuMachineConsole;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine_M_Stop] = &UIVirtualBoxManager::updateMenuMachineClose;

    /* Build menu-bar: */
    foreach (QMenu *pMenu, actionPool()->menus())
    {
#ifdef VBOX_WS_MAC
        /* Before 'Help' menu we should: */
        if (pMenu == actionPool()->action(UIActionIndex_Menu_Help)->menu())
        {
            /* Insert 'Window' menu: */
            UIWindowMenuManager::create();
            menuBar()->addMenu(gpWindowMenuManager->createMenu(this));
            gpWindowMenuManager->addWindow(this);
        }
#endif
        menuBar()->addMenu(pMenu);
    }

    /* Setup menu-bar policy: */
    menuBar()->setContextMenuPolicy(Qt::CustomContextMenu);
}

void UIVirtualBoxManager::prepareStatusBar()
{
    /* We are not using status-bar anymore: */
    statusBar()->setHidden(true);
}

void UIVirtualBoxManager::prepareWidgets()
{
    /* Prepare central-widget: */
    m_pWidget = new UIVirtualBoxWidget(this);
    if (m_pWidget)
        setCentralWidget(m_pWidget);
}

void UIVirtualBoxManager::prepareConnections()
{
#ifdef VBOX_WS_NIX
    /* Desktop event handlers: */
    connect(gpDesktop, &UIDesktopWidgetWatchdog::sigHostScreenWorkAreaResized,
            this, &UIVirtualBoxManager::sltHandleHostScreenAvailableAreaChange);
#endif

    /* UICommon connections: */
    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIVirtualBoxManager::sltHandleCommitData);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerationFinished,
            this, &UIVirtualBoxManager::sltHandleMediumEnumerationFinish);

    /* Widget connections: */
    connect(m_pWidget, &UIVirtualBoxWidget::sigChooserPaneSelectionChange,
            this, &UIVirtualBoxManager::sltHandleChooserPaneSelectionChange);
    connect(m_pWidget, &UIVirtualBoxWidget::sigStartOrShowRequest,
            this, &UIVirtualBoxManager::sltPerformStartOrShowMachine);
    connect(m_pWidget, &UIVirtualBoxWidget::sigToolTypeChangeGlobal,
            this, &UIVirtualBoxManager::sltHandleGlobalToolTypeChange);
    connect(m_pWidget, &UIVirtualBoxWidget::sigToolTypeChangeMachine,
            this, &UIVirtualBoxManager::sltHandleMachineToolTypeChange);
    connect(m_pWidget, &UIVirtualBoxWidget::sigHomeTask,
            this, &UIVirtualBoxManager::sltExecuteHomeTask);
    connect(m_pWidget, &UIVirtualBoxWidget::sigCreateMedium,
            this, &UIVirtualBoxManager::sltCreateMedium);
    connect(m_pWidget, &UIVirtualBoxWidget::sigCopyMedium,
            this, &UIVirtualBoxManager::sltCopyMedium);
    connect(m_pWidget, &UIVirtualBoxWidget::sigMachineSettingsLinkClicked,
            this, &UIVirtualBoxManager::sltOpenSettingsDialog);
    connect(m_pWidget, &UIVirtualBoxWidget::sigDetachToolPane,
            this, &UIVirtualBoxManager::sltDetachToolPane);
    connect(m_pWidget, &UIVirtualBoxWidget::sigGroupSavingStateChanged,
            this, &UIVirtualBoxManager::sltHandleUpdateActionAppearanceRequest);
    connect(m_pWidget, &UIVirtualBoxWidget::sigCloudUpdateStateChanged,
            this, &UIVirtualBoxManager::sltHandleUpdateActionAppearanceRequest);
    connect(m_pWidget, &UIVirtualBoxWidget::sigCloudMachineStateChange,
            this, &UIVirtualBoxManager::sltHandleUpdateActionAppearanceRequest);
    connect(m_pWidget, &UIVirtualBoxWidget::sigCurrentSnapshotItemChange,
            this, &UIVirtualBoxManager::sltHandleUpdateActionAppearanceRequest);

    connect(menuBar(), &QMenuBar::customContextMenuRequested,
            m_pWidget, &UIVirtualBoxWidget::sltHandleToolBarContextMenuRequest);

    /* Global VBox event handlers: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigExtensionPackInstalled,
            this, &UIVirtualBoxManager::sltHandleUpdateActionAppearanceRequest);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigExtensionPackUninstalled,
            this, &UIVirtualBoxManager::sltHandleUpdateActionAppearanceRequest);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIVirtualBoxManager::sltHandleUpdateActionAppearanceRequest);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSessionStateChange,
            this, &UIVirtualBoxManager::sltHandleUpdateActionAppearanceRequest);

    /* General action-pool connections: */
    connect(actionPool(), &UIActionPool::sigNotifyAboutMenuPrepare,
            this, &UIVirtualBoxManager::sltHandleMenuPrepare);

    /* 'File' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_File_S_ImportAppliance), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenImportApplianceWizard);
    connect(actionPool()->action(UIActionIndexMN_M_File_S_ExportAppliance), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenExportApplianceWizard);
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    connect(actionPool()->action(UIActionIndexMN_M_File_S_ShowExtraDataManager), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenExtraDataManagerWindow);
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */
    connect(actionPool()->action(UIActionIndex_M_Application_S_Preferences), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenPreferencesDialog);
    connect(actionPool()->action(UIActionIndexMN_M_File_S_Close), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformExit);

    /* 'File/Tools' menu connections: */
    connect(actionPool()->actionGroup(UIActionIndexMN_M_File_M_Tools), &QActionGroup::triggered,
            this, &UIVirtualBoxManager::sltPerformSwitchToTool);

    /* 'Home' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_Home_S_New), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenNewMachineWizard);
    connect(actionPool()->action(UIActionIndexMN_M_Home_S_Add), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenAddMachineDialog);

    /* 'Group' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_New), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenNewMachineWizard);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_Add), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenAddMachineDialog);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_Rename), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenGroupNameEditor);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_Remove), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltDisbandGroup);
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Start), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_Show), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformShowMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Group_T_Pause), &UIAction::toggled,
            this, &UIVirtualBoxManager::sltPerformPauseOrResumeMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_Reset), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformResetMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_Detach), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDetachMachineUI);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_Discard), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDiscardMachineState);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_Refresh), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformRefreshMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_ShowInFileManager), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltShowMachineInFileManager);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_CreateShortcut), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCreateMachineShortcut);
    connect(actionPool()->action(UIActionIndexMN_M_Group_S_Sort), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformGroupSorting);
    connect(actionPool()->action(UIActionIndexMN_M_Group_T_Search), &UIAction::toggled,
            this, &UIVirtualBoxManager::sltPerformMachineSearchWidgetVisibilityToggling);
    connect(m_pWidget, &UIVirtualBoxWidget::sigMachineSearchWidgetVisibilityChanged,
            actionPool()->action(UIActionIndexMN_M_Group_T_Search), &QAction::setChecked);

    /* 'Machine' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_New), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenNewMachineWizard);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Add), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenAddMachineDialog);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenSettingsDialogDefault);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Clone), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenCloneMachineWizard);
    connect(actionPool()->action(UIActionIndexMN_M_Snapshot_S_Clone), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenCloneMachineWizard);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Move), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformMachineMove);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_ExportToOCI), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenExportApplianceWizard);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Remove), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformMachineRemove);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_MoveToGroup_S_New), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformMachineMoveToNewGroup);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Start), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Show), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformShowMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_T_Pause), &UIAction::toggled,
            this, &UIVirtualBoxManager::sltPerformPauseOrResumeMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Reset), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformResetMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Detach), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDetachMachineUI);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Discard), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDiscardMachineState);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_Refresh), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformRefreshMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_ShowInFileManager), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltShowMachineInFileManager);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_CreateShortcut), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCreateMachineShortcut);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_S_SortParent), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformGroupSorting);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_T_Search), &UIAction::toggled,
            this, &UIVirtualBoxManager::sltPerformMachineSearchWidgetVisibilityToggling);
    connect(m_pWidget, &UIVirtualBoxWidget::sigMachineSearchWidgetVisibilityChanged,
            actionPool()->action(UIActionIndexMN_M_Machine_T_Search), &QAction::setChecked);

    /* 'Group/Start' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Start_S_Normal), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineNormal);
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Start_S_Headless), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineHeadless);
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Start_S_Detachable), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineDetachable);

    /* 'Machine/Start' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Start_S_Normal), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineNormal);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Start_S_Headless), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineHeadless);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Start_S_Detachable), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineDetachable);

    /* 'Group/Console' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Console_S_CreateConnection), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCreateConsoleConnectionForGroup);
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Console_S_DeleteConnection), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDeleteConsoleConnectionForGroup);
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Console_S_ConfigureApplications), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenManagerWindowDefault);

    /* 'Machine/Console' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CreateConnection), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCreateConsoleConnectionForMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_DeleteConnection), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDeleteConsoleConnectionForMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialUnix), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCopyCommandSerialUnix);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialWindows), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCopyCommandSerialWindows);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCUnix), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCopyCommandVNCUnix);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCWindows), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCopyCommandVNCWindows);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_ConfigureApplications), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenManagerWindowDefault);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_ShowLog), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformShowLog);

    /* 'Group/Stop' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_SaveState), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformSaveMachineState);
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_Terminate), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformTerminateMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_Shutdown), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformShutdownMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_PowerOff), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformPowerOffMachine);

    /* 'Machine/Stop' menu connections: */
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_SaveState), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformSaveMachineState);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_Terminate), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformTerminateMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_Shutdown), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformShutdownMachine);
    connect(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_PowerOff), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformPowerOffMachine);

    /* 'Group/Tools' menu connections: */
    connect(actionPool()->actionGroup(UIActionIndexMN_M_Group_M_Tools), &QActionGroup::triggered,
            this, &UIVirtualBoxManager::sltPerformSwitchToMachineTool);

    /* 'Machine/Tools' menu connections: */
    connect(actionPool()->actionGroup(UIActionIndexMN_M_Machine_M_Tools), &QActionGroup::triggered,
            this, &UIVirtualBoxManager::sltPerformSwitchToMachineTool);

    /* 'Help' menu contents action connection. It is done here since we need different behaviour in
     * the manager and runtime UIs: */
    connect(actionPool()->action(UIActionIndex_Simple_Contents), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformShowHelpBrowser);
}

void UIVirtualBoxManager::loadSettings()
{
    /* Load window geometry: */
    {
        const QRect geo = gEDataManager->selectorWindowGeometry(this);
        LogRel2(("GUI: UIVirtualBoxManager: Restoring geometry to: Origin=%dx%d, Size=%dx%d\n",
                 geo.x(), geo.y(), geo.width(), geo.height()));
        restoreGeometry(geo);
    }
}

void UIVirtualBoxManager::cleanupConnections()
{
    /* Honestly we should disconnect everything here,
     * but for now it's enough to disconnect the most critical. */
    m_pWidget->disconnect(this);
}

void UIVirtualBoxManager::cleanupWidgets()
{
    /* Deconfigure central-widget: */
    setCentralWidget(0);
    /* Destroy central-widget: */
    delete m_pWidget;
    m_pWidget = 0;
}

void UIVirtualBoxManager::cleanupMenuBar()
{
#ifdef VBOX_WS_MAC
    /* Cleanup 'Window' menu: */
    UIWindowMenuManager::destroy();
#endif

    /* Destroy action-pool: */
    UIActionPool::destroy(m_pActionPool);
    m_pActionPool = 0;
}

void UIVirtualBoxManager::cleanupCloudMachineManager()
{
    UICloudMachineManager::destroy();
}

void UIVirtualBoxManager::cleanup()
{
    /* Ask sub-dialogs to commit data: */
    sltHandleCommitData();

    /* Cleanup: */
    cleanupWidgets();
    cleanupMenuBar();
    cleanupCloudMachineManager();
}

UIVirtualMachineItem *UIVirtualBoxManager::currentItem() const
{
    return m_pWidget->currentItem();
}

QList<UIVirtualMachineItem*> UIVirtualBoxManager::currentItems() const
{
    return m_pWidget->currentItems();
}

bool UIVirtualBoxManager::isGroupSavingInProgress() const
{
    return m_pWidget->isGroupSavingInProgress();
}

bool UIVirtualBoxManager::isAllItemsOfOneGroupSelected() const
{
    return m_pWidget->isAllItemsOfOneGroupSelected();
}

bool UIVirtualBoxManager::isSingleGroupSelected() const
{
    return m_pWidget->isSingleGroupSelected();
}

bool UIVirtualBoxManager::isSingleLocalGroupSelected() const
{
    return m_pWidget->isSingleLocalGroupSelected();
}

bool UIVirtualBoxManager::isSingleCloudProviderGroupSelected() const
{
    return m_pWidget->isSingleCloudProviderGroupSelected();
}

bool UIVirtualBoxManager::isSingleCloudProfileGroupSelected() const
{
    return m_pWidget->isSingleCloudProfileGroupSelected();
}

bool UIVirtualBoxManager::isCloudProfileUpdateInProgress() const
{
    return m_pWidget->isCloudProfileUpdateInProgress();
}

bool UIVirtualBoxManager::checkUnattendedInstallError(const CUnattended &comUnattended) const
{
    if (!comUnattended.isOk())
    {
        UINotificationMessage::cannotRunUnattendedGuestInstall(comUnattended);
        return false;
    }
    return true;
}

void UIVirtualBoxManager::openAddMachineDialog(const QString &strFileName /* = QString() */)
{
    /* Lock the actions preventing cascade calls: */
    UIQObjectPropertySetter guardBlock(QList<QObject*>() << actionPool()->action(UIActionIndexMN_M_Home_S_Add)
                                                         << actionPool()->action(UIActionIndexMN_M_Machine_S_Add)
                                                         << actionPool()->action(UIActionIndexMN_M_Group_S_Add),
                                       "opened", true);
    connect(&guardBlock, &UIQObjectPropertySetter::sigAboutToBeDestroyed,
            this, &UIVirtualBoxManager::sltHandleUpdateActionAppearanceRequest);
    updateActionsAppearance();

    /* Initialize variables: */
#ifdef VBOX_WS_MAC
    QString strTmpFile = ::darwinResolveAlias(strFileName);
#else
    QString strTmpFile = strFileName;
#endif
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* No file specified: */
    if (strTmpFile.isEmpty())
    {
        QString strBaseFolder;
        if (currentItem() && currentItem()->toLocal())
        {
            QDir folder = QFileInfo(currentItem()->toLocal()->settingsFile()).absoluteDir();
            folder.cdUp();
            strBaseFolder = folder.absolutePath();
        }
        if (strBaseFolder.isEmpty())
            strBaseFolder = comVBox.GetSystemProperties().GetDefaultMachineFolder();
        QString strTitle = tr("Select a virtual machine file");
        QStringList extensions;
        for (int i = 0; i < VBoxFileExts.size(); ++i)
            extensions << QString("*.%1").arg(VBoxFileExts[i]);
        QString strFilter = tr("Virtual machine files (%1)").arg(extensions.join(" "));
        /* Create open file dialog: */
        QStringList fileNames = QIFileDialog::getOpenFileNames(strBaseFolder, strFilter, this, strTitle, 0, true, true);
        if (!fileNames.isEmpty())
            strTmpFile = fileNames.at(0);
    }

    /* Nothing was chosen? */
    if (strTmpFile.isEmpty())
        return;

    /* Make sure this machine can be opened: */
    CMachine comMachineNew = comVBox.OpenMachine(strTmpFile, QString());
    if (!comVBox.isOk())
    {
        UINotificationMessage::cannotOpenMachine(comVBox, strTmpFile);
        return;
    }

    /* Make sure this machine was NOT registered already: */
    CMachine comMachineOld = comVBox.FindMachine(comMachineNew.GetId().toString());
    if (!comMachineOld.isNull())
    {
        UINotificationMessage::cannotReregisterExistingMachine(comMachineOld.GetName(), strTmpFile);
        return;
    }

    /* Register that machine: */
    comVBox.RegisterMachine(comMachineNew);
}

void UIVirtualBoxManager::openNewMachineWizard(const QString &strISOFilePath /* = QString() */)
{
    /* Configure wizard variables: */
    m_strISOFilePath = strISOFilePath;

    /* Open New VM Wizard: */
    sltOpenWizard(WizardType_NewVM);
}

void UIVirtualBoxManager::openImportApplianceWizard(const QString &strFileName /* = QString() */)
{
    /* Configure wizard variables: */
    m_fImportFromOCI = false;
#ifdef VBOX_WS_MAC
    m_strFileName = ::darwinResolveAlias(strFileName);
#else
    m_strFileName = strFileName;
#endif

    /* If there is no file-name passed,
     * check if cloud stuff focused currently: */
    if (   m_strFileName.isEmpty()
        && (   m_pWidget->isSingleCloudProviderGroupSelected()
            || m_pWidget->isSingleCloudProfileGroupSelected()
            || m_pWidget->isCloudMachineItemSelected()))
    {
        m_fImportFromOCI = true;
        /* We can generate cloud hints as well: */
        m_strFileName = m_pWidget->fullGroupName();
    }

    /* Open Import Appliance Wizard: */
    sltOpenWizard(WizardType_ImportAppliance);
}

/* static */
void UIVirtualBoxManager::launchMachine(CMachine &comMachine,
                                        UILaunchMode enmLaunchMode /* = UILaunchMode_Default */)
{
    /* Switch to machine window(s) if possible: */
    if (   comMachine.GetSessionState() == KSessionState_Locked // precondition for CanShowConsoleWindow()
        && comMachine.CanShowConsoleWindow())
    {
        switchToMachine(comMachine);
        return;
    }

    /* Not for separate UI (which can connect to machine in any state): */
    if (enmLaunchMode != UILaunchMode_Separate)
    {
        /* Make sure machine-state is one of required: */
        const KMachineState enmState = comMachine.GetState(); Q_UNUSED(enmState);
        AssertMsg(   enmState == KMachineState_PoweredOff
                  || enmState == KMachineState_Saved
                  || enmState == KMachineState_Teleported
                  || enmState == KMachineState_Aborted
                  || enmState == KMachineState_AbortedSaved
                  , ("Machine must be PoweredOff/Saved/Teleported/Aborted (%d)", enmState));
    }

    /* Powering VM up: */
    UINotificationProgressMachinePowerUp *pNotification =
        new UINotificationProgressMachinePowerUp(comMachine, enmLaunchMode);
    gpNotificationCenter->append(pNotification);
}

/* static */
void UIVirtualBoxManager::launchMachine(CCloudMachine &comMachine)
{
    /* Powering cloud VM up: */
    UINotificationProgressCloudMachinePowerUp *pNotification =
        new UINotificationProgressCloudMachinePowerUp(comMachine);
    gpNotificationCenter->append(pNotification);
}

void UIVirtualBoxManager::startUnattendedInstall(const CUnattended &comUnattendedRef,
                                                 bool fStartHeadless, const QString &strMachineId)
{
    CVirtualBox comVBox = gpGlobalSession->virtualBox();
    CMachine comMachine = comVBox.FindMachine(strMachineId);
    if (comMachine.isNull())
        return;

    CUnattended comUnattended = comUnattendedRef;
    comUnattended.Prepare();
    AssertReturnVoid(checkUnattendedInstallError(comUnattended));
    comUnattended.ConstructMedia();
    AssertReturnVoid(checkUnattendedInstallError(comUnattended));
    comUnattended.ReconfigureVM();
    AssertReturnVoid(checkUnattendedInstallError(comUnattended));

    launchMachine(comMachine, fStartHeadless ? UILaunchMode_Headless : UILaunchMode_Default);
}

void UIVirtualBoxManager::performStartVirtualMachines(const QList<UIVirtualMachineItem*> &items,
                                                      UILaunchMode enmLaunchMode)
{
    /* Do nothing while group saving is in progress: */
    if (isGroupSavingInProgress())
        return;

    /* Compose the list of startable items: */
    QStringList startableMachineNames;
    QList<UIVirtualMachineItem*> startableItems;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isAtLeastOneItemCanBeStarted(QList<UIVirtualMachineItem*>() << pItem))
        {
            startableItems << pItem;
            startableMachineNames << pItem->name();
        }
    }

    /* Initially we have start auto-confirmed: */
    bool fStartConfirmed = true;
    /* But if we have more than one item to start =>
     * We should still ask user for a confirmation: */
    if (startableItems.size() > 1)
        fStartConfirmed = msgCenter().confirmStartMultipleMachines(startableMachineNames.join(", "));

    /* For every item => check if it could be launched: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (   isAtLeastOneItemCanBeStarted(QList<UIVirtualMachineItem*>() << pItem)
            && fStartConfirmed)
        {
            /* For local machine: */
            if (pItem->itemType() == UIVirtualMachineItemType_Local)
            {
                /* Fetch item launch mode: */
                UILaunchMode enmItemLaunchMode = enmLaunchMode;
                if (enmItemLaunchMode == UILaunchMode_Invalid)
                    enmItemLaunchMode = pItem->isItemRunningHeadless()
                                      ? UILaunchMode_Separate
                                      : qApp->keyboardModifiers() == Qt::ShiftModifier
                                      ? UILaunchMode_Headless
                                      : UILaunchMode_Default;
                /* Acquire local machine: */
                CMachine machine = pItem->toLocal()->machine();
                /* Launch current VM: */
                launchMachine(machine, enmItemLaunchMode);
            }
            /* For real cloud machine: */
            else if (pItem->itemType() == UIVirtualMachineItemType_CloudReal)
            {
                /* Acquire cloud machine: */
                CCloudMachine comCloudMachine = pItem->toCloud()->machine();
                /* Launch current VM: */
                launchMachine(comCloudMachine);
            }
        }
    }
}

void UIVirtualBoxManager::performShowVirtualMachines(const QList<UIVirtualMachineItem*> &items)
{
    /* Do nothing while group saving is in progress: */
    if (isGroupSavingInProgress())
        return;

    /* For every item => check if it could be shown: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isAtLeastOneItemCanBeShown(QList<UIVirtualMachineItem*>() << pItem))
        {
            /* For local machine: */
            if (pItem->itemType() == UIVirtualMachineItemType_Local)
            {
                /* Fetch item launch mode: */
                UILaunchMode enmItemLaunchMode = pItem->isItemRunningHeadless()
                                               ? UILaunchMode_Separate
                                               : qApp->keyboardModifiers() == Qt::ShiftModifier
                                               ? UILaunchMode_Headless
                                               : UILaunchMode_Default;
                /* Acquire local machine: */
                CMachine machine = pItem->toLocal()->machine();
                /* Launch current VM: */
                launchMachine(machine, enmItemLaunchMode);
            }
        }
    }
}

#ifndef VBOX_WS_WIN
QStringList UIVirtualBoxManager::parseShellArguments(const QString &strArguments)
{
    //printf("start processing arguments\n");

    /* Parse argument string: */
    QStringList arguments;
    const QRegularExpression re("(\"[^\"]+\")|('[^']+')|([^\\s\"']+)");
    int iPosition = 0;
    QRegularExpressionMatch mt = re.match(strArguments, iPosition);
    int iIndex = mt.capturedStart();
    while (iIndex != -1)
    {
        /* Get what's the sequence we have: */
        const QString strCap0 = mt.captured(0);
        /* Get what's the double-quoted sequence we have: */
        const QString strCap1 = mt.captured(1);
        /* Get what's the single-quoted sequence we have: */
        const QString strCap2 = mt.captured(2);
        /* Get what's the unquoted sequence we have: */
        const QString strCap3 = mt.captured(3);

        /* If new sequence starts where previous ended
         * we are appending new value to previous one, otherwise
         * we are appending new value to argument list itself.. */

        /* Do we have double-quoted sequence? */
        if (!strCap1.isEmpty())
        {
            //printf(" [D] double-quoted sequence starting at: %d\n", iIndex);
            /* Unquote the value and add it to the list: */
            const QString strValue = strCap1.mid(1, strCap1.size() - 2);
            if (!arguments.isEmpty() && iIndex == iPosition)
                arguments.last() += strValue;
            else
                arguments << strValue;
        }
        /* Do we have single-quoted sequence? */
        else if (!strCap2.isEmpty())
        {
            //printf(" [S] single-quoted sequence starting at: %d\n", iIndex);
            /* Unquote the value and add it to the list: */
            const QString strValue = strCap2.mid(1, strCap2.size() - 2);
            if (!arguments.isEmpty() && iIndex == iPosition)
                arguments.last() += strValue;
            else
                arguments << strValue;
        }
        /* Do we have unquoted sequence? */
        else if (!strCap3.isEmpty())
        {
            //printf(" [U] unquoted sequence starting at: %d\n", iIndex);
            /* Value wasn't unquoted, add it to the list: */
            if (!arguments.isEmpty() && iIndex == iPosition)
                arguments.last() += strCap3;
            else
                arguments << strCap3;
        }

        /* Advance position: */
        iPosition = iIndex + strCap0.size();
        /* Search for a next sequence: */
        mt = re.match(strArguments, iPosition);
        iIndex = mt.capturedStart();
    }

    //printf("arguments processed:\n");
    //foreach (const QString &strArgument, arguments)
    //    printf(" %s\n", strArgument.toUtf8().constData());

    /* Return parsed arguments: */
    return arguments;
}
#endif /* !VBOX_WS_WIN */

void UIVirtualBoxManager::updateMenuGroup(QMenu *pMenu)
{
    /* For single cloud provider/profile: */
    if (   isSingleCloudProviderGroupSelected()
        || isSingleCloudProfileGroupSelected())
    {
        /* Populate Group-menu: */
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_New));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Add));
        pMenu->addSeparator();
        if (   currentItem()
            && currentItem()->isItemPoweredOff())
            pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Start));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Reset));
        pMenu->addMenu(actionPool()->action(UIActionIndexMN_M_Group_M_Console)->menu());
        pMenu->addMenu(actionPool()->action(UIActionIndexMN_M_Group_M_Stop)->menu());
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Refresh));
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Sort));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_T_Search));
    }
    /* For other cases, like local group or no group at all: */
    else
    {
        /* Populate Group-menu: */
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_New));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Add));
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Rename));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Remove));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_MoveToGroup));
        pMenu->addSeparator();
        if (   currentItem()
            && currentItem()->isItemPoweredOff())
            pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Start));
        else
            pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Show));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_T_Pause));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Reset));
        // pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Detach));
        pMenu->addMenu(actionPool()->action(UIActionIndexMN_M_Group_M_Stop)->menu());
        pMenu->addSeparator();
        pMenu->addMenu(actionPool()->action(UIActionIndexMN_M_Group_M_Tools)->menu());
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Discard));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Refresh));
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_ShowInFileManager));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_CreateShortcut));
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Sort));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_T_Search));
    }
}

void UIVirtualBoxManager::updateMenuMachine(QMenu *pMenu)
{
    /* Get first selected item: */
    UIVirtualMachineItem *pItem = currentItem();

    /* For cloud machine(s): */
    if (   pItem
        && (   pItem->itemType() == UIVirtualMachineItemType_CloudFake
            || pItem->itemType() == UIVirtualMachineItemType_CloudReal))
    {
        /* Populate Machine-menu: */
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_New));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Add));
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
        if (gEDataManager->isSettingsInExpertMode())
            pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Clone));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Remove));
        pMenu->addSeparator();
        if (   currentItem()
            && currentItem()->isItemPoweredOff())
            pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Start));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Reset));
        pMenu->addMenu(actionPool()->action(UIActionIndexMN_M_Machine_M_Console)->menu());
        pMenu->addMenu(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop)->menu());
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Refresh));
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_SortParent));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_T_Search));
    }
    /* For other cases, like local machine(s) or no machine at all: */
    else
    {
        /* Populate Machine-menu: */
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_New));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Add));
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
        if (gEDataManager->isSettingsInExpertMode())
        {
            pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Clone));
            pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Move));
        }
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_ExportToOCI));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Remove));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_MoveToGroup));
        pMenu->addSeparator();
        if (   currentItem()
            && currentItem()->isItemPoweredOff())
            pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Start));
        else
            pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Show));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_T_Pause));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Reset));
        // pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Detach));
        pMenu->addMenu(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop)->menu());
        pMenu->addSeparator();
        pMenu->addMenu(actionPool()->action(UIActionIndexMN_M_Machine_M_Tools)->menu());
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Discard));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Refresh));
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_ShowInFileManager));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_CreateShortcut));
        pMenu->addSeparator();
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_SortParent));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_T_Search));
    }
}

void UIVirtualBoxManager::updateMenuGroupMoveToGroup(QMenu *pMenu)
{
    /* Acquire groups: */
    QStringList groups = m_pWidget->possibleGroupsForGroupToMove(m_pWidget->fullGroupName());

    /* Pre-process root group: */
    const int iRootGroupIndex = groups.indexOf("/");
    if (iRootGroupIndex >= 0)
    {
        groups.removeAt(iRootGroupIndex);
        const QString strVisibleGroupName = QApplication::translate("UIActionPool", "No Group");
        QAction *pAction = pMenu->addAction(strVisibleGroupName, this,
                                            &UIVirtualBoxManager::sltPerformMachineMoveToSpecificGroup);
        pAction->setProperty("actual_group_name", "/");
    }

    /* Add separator there will be other actions: */
    if (!groups.isEmpty())
        pMenu->addSeparator();

    /* Enumerate remaining groups: */
    foreach (const QString &strGroupName, groups)
    {
        QString strVisibleGroupName = strGroupName;
        if (strVisibleGroupName.startsWith('/'))
            strVisibleGroupName.remove(0, 1);
        QAction *pAction = pMenu->addAction(strVisibleGroupName, this,
                                            &UIVirtualBoxManager::sltPerformMachineMoveToSpecificGroup);
        pAction->setProperty("actual_group_name", strGroupName);
    }
}

void UIVirtualBoxManager::updateMenuGroupConsole(QMenu *pMenu)
{
    /* Populate 'Group' / 'Console' menu: */
    pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Console_S_CreateConnection));
    pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Console_S_DeleteConnection));
    pMenu->addSeparator();
    pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Console_S_ConfigureApplications));
}

void UIVirtualBoxManager::updateMenuGroupClose(QMenu *pMenu)
{
    /* Get first selected item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertPtrReturnVoid(pItem);
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For local machine: */
    if (pItem->itemType() == UIVirtualMachineItemType_Local)
    {
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_SaveState));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_Shutdown));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_PowerOff));
    }
    else
    {
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_Terminate));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_Shutdown));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_PowerOff));
    }

    /* Configure 'Group' / 'Stop' menu: */
    actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_Shutdown)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Stop_S_Shutdown, items));
}

void UIVirtualBoxManager::updateMenuMachineMoveToGroup(QMenu *pMenu)
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));

    /* Acquire groups: */
    QStringList groups = m_pWidget->possibleGroupsForMachineToMove(pItem->id());

    /* Pre-process root group: */
    const int iRootGroupIndex = groups.indexOf("/");
    if (iRootGroupIndex >= 0)
    {
        groups.removeAt(iRootGroupIndex);
        const QString strVisibleGroupName = QApplication::translate("UIActionPool", "No Group");
        QAction *pAction = pMenu->addAction(strVisibleGroupName, this,
                                            &UIVirtualBoxManager::sltPerformMachineMoveToSpecificGroup);
        pAction->setProperty("actual_group_name", "/");
    }

    /* Add separator there will be other actions: */
    if (!groups.isEmpty())
        pMenu->addSeparator();

    /* Enumerate remaining groups: */
    foreach (const QString &strGroupName, groups)
    {
        QString strVisibleGroupName = strGroupName;
        if (strVisibleGroupName.startsWith('/'))
            strVisibleGroupName.remove(0, 1);
        QAction *pAction = pMenu->addAction(strVisibleGroupName, this,
                                            &UIVirtualBoxManager::sltPerformMachineMoveToSpecificGroup);
        pAction->setProperty("actual_group_name", strGroupName);
    }
}

void UIVirtualBoxManager::updateMenuMachineConsole(QMenu *pMenu)
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));
    UIVirtualMachineItemCloud *pCloudItem = pItem->toCloud();
    AssertPtrReturnVoid(pCloudItem);

    /* Acquire current cloud machine: */
    CCloudMachine comMachine = pCloudItem->machine();
    const QString strFingerprint = comMachine.GetConsoleConnectionFingerprint();

    /* Populate 'Group' / 'Console' menu: */
    if (strFingerprint.isEmpty())
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CreateConnection));
    else
    {
        /* Copy fingerprint to clipboard action: */
        const QString strFingerprintCompressed = strFingerprint.size() <= 12
                                               ? strFingerprint
                                               : QString("%1...%2").arg(strFingerprint.left(6), strFingerprint.right(6));
        QAction *pAction = pMenu->addAction(UIIconPool::iconSet(":/cloud_machine_console_copy_connection_fingerprint_16px.png",
                                                                ":/cloud_machine_console_copy_connection_fingerprint_disabled_16px.png"),
                                            QApplication::translate("UIActionPool", "Copy Key Fingerprint (%1)").arg(strFingerprintCompressed),
                                            this, &UIVirtualBoxManager::sltCopyConsoleConnectionFingerprint);
        pAction->setProperty("fingerprint", strFingerprint);

        /* Copy command to clipboard actions: */
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialUnix));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialWindows));
//        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCUnix));
//        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCWindows));
        pMenu->addSeparator();

        /* Default Connect action: */
        QAction *pDefaultAction = pMenu->addAction(QApplication::translate("UIActionPool", "Connect", "to cloud VM"),
                                                   this, &UIVirtualBoxManager::sltExecuteExternalApplication);
#if defined(VBOX_WS_MAC)
        pDefaultAction->setProperty("path", "open");
#elif defined(VBOX_WS_WIN)
        pDefaultAction->setProperty("path", "powershell");
#elif defined(VBOX_WS_NIX)
        const QPair<QString, QString> terminalData = defaultTerminalData();
        pDefaultAction->setProperty("path", terminalData.first);
        pDefaultAction->setProperty("arguments", QString("%1 sh -c").arg(terminalData.second));
#endif

        /* Terminal application/profile action list: */
        const QStringList restrictions = gEDataManager->cloudConsoleManagerRestrictions();
        foreach (const QString strApplicationId, gEDataManager->cloudConsoleManagerApplications())
        {
            const QString strApplicationDefinition = QString("/%1").arg(strApplicationId);
            if (restrictions.contains(strApplicationDefinition))
                continue;
            const QString strApplicationOptions = gEDataManager->cloudConsoleManagerApplication(strApplicationId);
            const QStringList applicationValues = strApplicationOptions.split(',');
            bool fAtLeastOneProfileListed = false;
            foreach (const QString strProfileId, gEDataManager->cloudConsoleManagerProfiles(strApplicationId))
            {
                const QString strProfileDefinition = QString("/%1/%2").arg(strApplicationId, strProfileId);
                if (restrictions.contains(strProfileDefinition))
                    continue;
                const QString strProfileOptions = gEDataManager->cloudConsoleManagerProfile(strApplicationId, strProfileId);
                const QStringList profileValues = strProfileOptions.split(',');
                QAction *pAction = pMenu->addAction(QApplication::translate("UIActionPool",
                                                                            "Connect with %1 (%2)",
                                                                            "with terminal application (profile)")
                                                        .arg(applicationValues.value(0), profileValues.value(0)),
                                                    this, &UIVirtualBoxManager::sltExecuteExternalApplication);
                pAction->setProperty("path", applicationValues.value(1));
                pAction->setProperty("arguments", profileValues.value(1));
                fAtLeastOneProfileListed = true;
            }
            if (!fAtLeastOneProfileListed)
            {
                QAction *pAction = pMenu->addAction(QApplication::translate("UIActionPool",
                                                                            "Connect with %1",
                                                                            "with terminal application")
                                                        .arg(applicationValues.value(0)),
                                                    this, &UIVirtualBoxManager::sltExecuteExternalApplication);
                pAction->setProperty("path", applicationValues.value(1));
                pAction->setProperty("arguments", applicationValues.value(2));
            }
        }
        /* Terminal application configuration tool: */
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_ConfigureApplications));
        pMenu->addSeparator();

        /* Delete connection action finally: */
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_DeleteConnection));
    }

    /* Show console log action: */
    pMenu->addSeparator();
    pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_ShowLog));
}

void UIVirtualBoxManager::updateMenuMachineClose(QMenu *pMenu)
{
    /* Get first selected item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertPtrReturnVoid(pItem);
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For local machine: */
    if (pItem->itemType() == UIVirtualMachineItemType_Local)
    {
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_SaveState));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_Shutdown));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_PowerOff));
    }
    else
    {
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_Terminate));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_Shutdown));
        pMenu->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_PowerOff));
    }

    /* Configure 'Machine' / 'Stop' menu: */
    actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_Shutdown)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Stop_S_Shutdown, items));
}

void UIVirtualBoxManager::updateActionsVisibility()
{
    /* Determine whether Machine or Group menu should be shown at all: */
    const bool fMachinesToolShown  = m_pWidget->currentGlobalTool() == UIToolType_Machines;
    const bool fGlobalToolShown    = !fMachinesToolShown;
    const bool fGroupMenuShown   = fMachinesToolShown && m_pWidget->isGroupItemSelected() && isSingleGroupSelected();
    const bool fMachineMenuShown = fMachinesToolShown && m_pWidget->isMachineItemSelected() && !isSingleGroupSelected();
    actionPool()->action(UIActionIndexMN_M_Home)->setVisible(fGlobalToolShown);
    actionPool()->action(UIActionIndexMN_M_Group)->setVisible(fGroupMenuShown);
    actionPool()->action(UIActionIndexMN_M_Machine)->setVisible(fMachineMenuShown);

    /* Determine whether Extensions menu should be visible: */
    const bool fExtensionsMenuShown = fGlobalToolShown && m_pWidget->currentGlobalTool() == UIToolType_Extensions;
    actionPool()->action(UIActionIndexMN_M_Extension)->setVisible(fExtensionsMenuShown);
    /* Determine whether Media menu should be visible: */
    const bool fMediumMenuShown = fGlobalToolShown && m_pWidget->currentGlobalTool() == UIToolType_Media;
    actionPool()->action(UIActionIndexMN_M_Medium)->setVisible(fMediumMenuShown);
    /* Determine whether Network menu should be visible: */
    const bool fNetworkMenuShown = fGlobalToolShown && m_pWidget->currentGlobalTool() == UIToolType_Network;
    actionPool()->action(UIActionIndexMN_M_Network)->setVisible(fNetworkMenuShown);
    /* Determine whether Cloud menu should be visible: */
    const bool fCloudMenuShown = fGlobalToolShown && m_pWidget->currentGlobalTool() == UIToolType_Cloud;
    actionPool()->action(UIActionIndexMN_M_Cloud)->setVisible(fCloudMenuShown);
    /* Determine whether Activities menu should be visible: */
    const bool fResourcesMenuShown = fGlobalToolShown && m_pWidget->currentGlobalTool() == UIToolType_Resources;
    actionPool()->action(UIActionIndexMN_M_VMActivityOverview)->setVisible(fResourcesMenuShown);

    /* Determine whether Snapshots menu should be visible: */
    const bool fSnapshotMenuShown = (fMachineMenuShown || fGroupMenuShown) &&
                                    m_pWidget->currentMachineTool() == UIToolType_Snapshots;
    actionPool()->action(UIActionIndexMN_M_Snapshot)->setVisible(fSnapshotMenuShown);
    /* Determine whether Logs menu should be visible: */
    const bool fLogViewerMenuShown = (fMachineMenuShown || fGroupMenuShown) &&
                                     m_pWidget->currentMachineTool() == UIToolType_Logs;
    actionPool()->action(UIActionIndex_M_Log)->setVisible(fLogViewerMenuShown);
    /* Determine whether Performance menu should be visible: */
    const bool fPerformanceMenuShown = (fMachineMenuShown || fGroupMenuShown) &&
                                       m_pWidget->currentMachineTool() == UIToolType_VMActivity;
    actionPool()->action(UIActionIndex_M_Activity)->setVisible(fPerformanceMenuShown);
    /* Determine whether File Manager menu item should be visible: */
    const bool fFileManagerMenuShown = (fMachineMenuShown || fGroupMenuShown) &&
                                       m_pWidget->currentMachineTool() == UIToolType_FileManager;
    actionPool()->action(UIActionIndex_M_FileManager)->setVisible(fFileManagerMenuShown);

    /* Hide action shortcuts: */
    if (!fGlobalToolShown)
        actionPool()->setShortcutsVisible(UIActionIndexMN_M_Home, false);
    if (!fGroupMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexMN_M_Group, false);
    if (!fMachineMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexMN_M_Machine, false);

    /* Show action shortcuts: */
    if (fGlobalToolShown)
        actionPool()->setShortcutsVisible(UIActionIndexMN_M_Home, true);
    if (fGroupMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexMN_M_Group, true);
    if (fMachineMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexMN_M_Machine, true);
}

void UIVirtualBoxManager::updateActionsAppearance()
{
    /* Get current items: */
    QList<UIVirtualMachineItem*> items = currentItems();

    /* Enable/disable File/Application actions: */
    actionPool()->action(UIActionIndex_M_Application_S_Preferences)->setEnabled(isActionEnabled(UIActionIndex_M_Application_S_Preferences, items));
    actionPool()->action(UIActionIndexMN_M_File_S_ExportAppliance)->setEnabled(isActionEnabled(UIActionIndexMN_M_File_S_ExportAppliance, items));
    actionPool()->action(UIActionIndexMN_M_File_S_ImportAppliance)->setEnabled(isActionEnabled(UIActionIndexMN_M_File_S_ImportAppliance, items));

    /* Enable/disable welcome actions: */
    actionPool()->action(UIActionIndexMN_M_Home_S_New)->setEnabled(isActionEnabled(UIActionIndexMN_M_Home_S_New, items));
    actionPool()->action(UIActionIndexMN_M_Home_S_Add)->setEnabled(isActionEnabled(UIActionIndexMN_M_Home_S_Add, items));

    /* Enable/disable group actions: */
    actionPool()->action(UIActionIndexMN_M_Group_S_New)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_New, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_Add)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_Add, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_Rename)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_Rename, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_Remove)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_Remove, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_MoveToGroup)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_MoveToGroup, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_Show)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_Show, items));
    actionPool()->action(UIActionIndexMN_M_Group_T_Pause)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_T_Pause, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_Reset)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_Reset, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_Detach)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_Detach, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_Discard)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_Discard, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_Refresh)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_Refresh, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_ShowInFileManager)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_ShowInFileManager, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_CreateShortcut)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_CreateShortcut, items));
    actionPool()->action(UIActionIndexMN_M_Group_S_Sort)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_S_Sort, items));

    /* Enable/disable machine actions: */
    actionPool()->action(UIActionIndexMN_M_Machine_S_New)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_New, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Add)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Add, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Settings)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Settings, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Clone)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Clone, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Move)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Move, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_ExportToOCI)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_ExportToOCI, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Remove)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Remove, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_MoveToGroup)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_MoveToGroup, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_MoveToGroup_S_New)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_MoveToGroup_S_New, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Show)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Show, items));
    actionPool()->action(UIActionIndexMN_M_Machine_T_Pause)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_T_Pause, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Reset)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Reset, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Detach)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Detach, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Discard)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Discard, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_Refresh)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_Refresh, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_ShowInFileManager)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_ShowInFileManager, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_CreateShortcut)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_CreateShortcut, items));
    actionPool()->action(UIActionIndexMN_M_Machine_S_SortParent)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_S_SortParent, items));

    /* Enable/disable group-start-or-show actions: */
    actionPool()->action(UIActionIndexMN_M_Group_M_Start)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Start, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Start_S_Normal)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Start_S_Normal, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Start_S_Headless)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Start_S_Headless, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Start_S_Detachable)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Start_S_Detachable, items));

    /* Enable/disable machine-start-or-show actions: */
    actionPool()->action(UIActionIndexMN_M_Machine_M_Start)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Start, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Start_S_Normal)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Start_S_Normal, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Start_S_Headless)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Start_S_Headless, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Start_S_Detachable)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Start_S_Detachable, items));

    /* Enable/disable group-console actions: */
    actionPool()->action(UIActionIndexMN_M_Group_M_Console)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Console, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Console_S_CreateConnection)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Console_S_CreateConnection, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Console_S_DeleteConnection)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Console_S_DeleteConnection, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Console_S_ConfigureApplications)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Console_S_ConfigureApplications, items));

    /* Enable/disable machine-console actions: */
    actionPool()->action(UIActionIndexMN_M_Machine_M_Console)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Console, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CreateConnection)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Console_S_CreateConnection, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_DeleteConnection)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Console_S_DeleteConnection, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialUnix)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialUnix, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialWindows)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialWindows, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCUnix)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCUnix, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCWindows)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCWindows, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_ConfigureApplications)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Console_S_ConfigureApplications, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Console_S_ShowLog)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Console_S_ShowLog, items));

    /* Enable/disable group-stop actions: */
    actionPool()->action(UIActionIndexMN_M_Group_M_Stop)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Stop, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_SaveState)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Stop_S_SaveState, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_Terminate)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Stop_S_Terminate, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_Shutdown)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Stop_S_Shutdown, items));
    actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_PowerOff)->setEnabled(isActionEnabled(UIActionIndexMN_M_Group_M_Stop_S_PowerOff, items));

    /* Enable/disable machine-stop actions: */
    actionPool()->action(UIActionIndexMN_M_Machine_M_Stop)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Stop, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_SaveState)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Stop_S_SaveState, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_Terminate)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Stop_S_Terminate, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_Shutdown)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Stop_S_Shutdown, items));
    actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_PowerOff)->setEnabled(isActionEnabled(UIActionIndexMN_M_Machine_M_Stop_S_PowerOff, items));

    /* Pause/Resume action is deremined by 1st started item: */
    UIVirtualMachineItem *pFirstStartedAction = 0;
    foreach (UIVirtualMachineItem *pSelectedItem, items)
    {
        if (pSelectedItem->isItemStarted())
        {
            pFirstStartedAction = pSelectedItem;
            break;
        }
    }
    /* Update the group Pause/Resume action appearance: */
    actionPool()->action(UIActionIndexMN_M_Group_T_Pause)->blockSignals(true);
    actionPool()->action(UIActionIndexMN_M_Group_T_Pause)->setChecked(pFirstStartedAction && pFirstStartedAction->isItemPaused());
    actionPool()->action(UIActionIndexMN_M_Group_T_Pause)->retranslateUi();
    actionPool()->action(UIActionIndexMN_M_Group_T_Pause)->blockSignals(false);
    /* Update the machine Pause/Resume action appearance: */
    actionPool()->action(UIActionIndexMN_M_Machine_T_Pause)->blockSignals(true);
    actionPool()->action(UIActionIndexMN_M_Machine_T_Pause)->setChecked(pFirstStartedAction && pFirstStartedAction->isItemPaused());
    actionPool()->action(UIActionIndexMN_M_Machine_T_Pause)->retranslateUi();
    actionPool()->action(UIActionIndexMN_M_Machine_T_Pause)->blockSignals(false);

    /* Update action toggle states: */
    if (m_pWidget)
    {
        switch (m_pWidget->currentGlobalTool())
        {
            case UIToolType_Home:
            {
                actionPool()->action(UIActionIndexMN_M_File_M_Tools_T_HomeScreen)->setChecked(true);
                break;
            }
            case UIToolType_Machines:
            {
                actionPool()->action(UIActionIndexMN_M_File_M_Tools_T_MachineManager)->setChecked(true);
                break;
            }
            case UIToolType_Extensions:
            {
                actionPool()->action(UIActionIndexMN_M_File_M_Tools_T_ExtensionPackManager)->setChecked(true);
                break;
            }
            case UIToolType_Media:
            {
                actionPool()->action(UIActionIndexMN_M_File_M_Tools_T_VirtualMediaManager)->setChecked(true);
                break;
            }
            case UIToolType_Network:
            {
                actionPool()->action(UIActionIndexMN_M_File_M_Tools_T_NetworkManager)->setChecked(true);
                break;
            }
            case UIToolType_Cloud:
            {
                actionPool()->action(UIActionIndexMN_M_File_M_Tools_T_CloudProfileManager)->setChecked(true);
                break;
            }
            case UIToolType_Resources:
            {
                actionPool()->action(UIActionIndexMN_M_File_M_Tools_T_VMActivityOverview)->setChecked(true);
                break;
            }
            default:
                break;
        }

        switch (m_pWidget->currentMachineTool())
        {
            case UIToolType_Details:
            {
                actionPool()->action(UIActionIndexMN_M_Group_M_Tools_T_Details)->setChecked(true);
                actionPool()->action(UIActionIndexMN_M_Machine_M_Tools_T_Details)->setChecked(true);
                break;
            }
            case UIToolType_Snapshots:
            {
                actionPool()->action(UIActionIndexMN_M_Group_M_Tools_T_Snapshots)->setChecked(true);
                actionPool()->action(UIActionIndexMN_M_Machine_M_Tools_T_Snapshots)->setChecked(true);
                break;
            }
            case UIToolType_Logs:
            {
                actionPool()->action(UIActionIndexMN_M_Group_M_Tools_T_Logs)->setChecked(true);
                actionPool()->action(UIActionIndexMN_M_Machine_M_Tools_T_Logs)->setChecked(true);
                break;
            }
            case UIToolType_VMActivity:
            {
                actionPool()->action(UIActionIndexMN_M_Group_M_Tools_T_Activity)->setChecked(true);
                actionPool()->action(UIActionIndexMN_M_Machine_M_Tools_T_Activity)->setChecked(true);
                break;
            }
            case UIToolType_FileManager:
            {
                actionPool()->action(UIActionIndexMN_M_Group_M_Tools_T_FileManager)->setChecked(true);
                actionPool()->action(UIActionIndexMN_M_Machine_M_Tools_T_FileManager)->setChecked(true);
                break;
            }
            default:
                break;
        }
    }
}

bool UIVirtualBoxManager::isActionEnabled(int iActionIndex, const QList<UIVirtualMachineItem*> &items)
{
    /* Make sure action pool exists: */
    AssertPtrReturn(actionPool(), false);

    /* Any "opened" action is by definition disabled: */
    if (   actionPool()->action(iActionIndex)
        && actionPool()->action(iActionIndex)->property("opened").toBool())
        return false;

    /* Some actions available even if no VM items present yet: */
    switch (iActionIndex)
    {
        /* For known *global* action types: */
        case UIActionIndex_M_Application_S_Preferences:
        case UIActionIndexMN_M_File_S_ExportAppliance:
        case UIActionIndexMN_M_File_S_ImportAppliance:
        case UIActionIndexMN_M_Home_S_New:
        case UIActionIndexMN_M_Home_S_Add:
            return true;
        /* For known *machine* action types: */
        case UIActionIndexMN_M_Group_S_New:
        case UIActionIndexMN_M_Group_S_Add:
        case UIActionIndexMN_M_Machine_S_New:
        case UIActionIndexMN_M_Machine_S_Add:
            return !isGroupSavingInProgress();
        default:
            break;
    }

    /* No other *machine* actions enabled for empty item list: */
    if (items.isEmpty())
        return false;

    /* Get first item: */
    UIVirtualMachineItem *pItem = items.first();

    /* For known *machine* action types: */
    switch (iActionIndex)
    {
        case UIActionIndexMN_M_Group_S_Sort:
        {
            return !isGroupSavingInProgress() &&
                   isSingleGroupSelected() &&
                   isItemsLocal(items);
        }
        case UIActionIndexMN_M_Group_S_Rename:
        case UIActionIndexMN_M_Group_S_Remove:
        {
            return !isGroupSavingInProgress() &&
                   isSingleGroupSelected() &&
                   isItemsLocal(items) &&
                   isItemsPoweredOff(items);
        }
        case UIActionIndexMN_M_Machine_S_Settings:
        {
            return !isGroupSavingInProgress() &&
                   items.size() == 1 &&
                   pItem->configurationAccessLevel() != ConfigurationAccessLevel_Null &&
                   (m_pWidget->currentMachineTool() != UIToolType_Snapshots ||
                    m_pWidget->isCurrentStateItemSelected());
        }
        case UIActionIndexMN_M_Machine_S_Clone:
        case UIActionIndexMN_M_Machine_S_Move:
        {
            return !isGroupSavingInProgress() &&
                   items.size() == 1 &&
                   pItem->isItemEditable();
        }
        case UIActionIndexMN_M_Machine_S_ExportToOCI:
        {
            return items.size() == 1 &&
                   UIExtension::isExtentionPackInstalled() &&
                   pItem->toLocal();
        }
        case UIActionIndexMN_M_Machine_S_Remove:
        {
            return !isGroupSavingInProgress() &&
                   (isItemsLocal(items) || !isCloudProfileUpdateInProgress()) &&
                   isAtLeastOneItemRemovable(items);
        }
        case UIActionIndexMN_M_Group_M_MoveToGroup:
        case UIActionIndexMN_M_Machine_M_MoveToGroup:
        case UIActionIndexMN_M_Machine_M_MoveToGroup_S_New:
        {
            return !isGroupSavingInProgress() &&
                   isItemsLocal(items) &&
                   isItemsPoweredOff(items);
        }
        case UIActionIndexMN_M_Group_M_Start:
        case UIActionIndexMN_M_Group_M_Start_S_Normal:
        case UIActionIndexMN_M_Machine_M_Start:
        case UIActionIndexMN_M_Machine_M_Start_S_Normal:
        {
            return !isGroupSavingInProgress() &&
                   isAtLeastOneItemCanBeStarted(items) &&
                    (m_pWidget->currentMachineTool() != UIToolType_Snapshots ||
                     m_pWidget->isCurrentStateItemSelected());
        }
        case UIActionIndexMN_M_Group_M_Start_S_Headless:
        case UIActionIndexMN_M_Group_M_Start_S_Detachable:
        case UIActionIndexMN_M_Machine_M_Start_S_Headless:
        case UIActionIndexMN_M_Machine_M_Start_S_Detachable:
        {
            return !isGroupSavingInProgress() &&
                   isItemsLocal(items) &&
                   isAtLeastOneItemCanBeStarted(items) &&
                    (m_pWidget->currentMachineTool() != UIToolType_Snapshots ||
                     m_pWidget->isCurrentStateItemSelected());
        }
        case UIActionIndexMN_M_Group_S_Show:
        case UIActionIndexMN_M_Machine_S_Show:
        {
            return !isGroupSavingInProgress() &&
                   isAtLeastOneItemCanBeShown(items) &&
                    (m_pWidget->currentMachineTool() != UIToolType_Snapshots ||
                     m_pWidget->isCurrentStateItemSelected());
        }
        case UIActionIndexMN_M_Group_S_Discard:
        case UIActionIndexMN_M_Machine_S_Discard:
        {
            return !isGroupSavingInProgress() &&
                   isItemsLocal(items) &&
                   isAtLeastOneItemDiscardable(items) &&
                    (m_pWidget->currentMachineTool() != UIToolType_Snapshots ||
                     m_pWidget->isCurrentStateItemSelected());
        }
        case UIActionIndexMN_M_Group_T_Pause:
        case UIActionIndexMN_M_Machine_T_Pause:
        {
            return isItemsLocal(items) &&
                   isAtLeastOneItemStarted(items);
        }
        case UIActionIndexMN_M_Group_S_Reset:
        case UIActionIndexMN_M_Machine_S_Reset:
        {
            return isAtLeastOneItemRunning(items);
        }
        case UIActionIndexMN_M_Group_S_Detach:
        case UIActionIndexMN_M_Machine_S_Detach:
        {
            return isItemsLocal(items) &&
                   isAtLeastOneItemRunning(items) &&
                   isAtLeastOneItemDetachable(items);
        }
        case UIActionIndexMN_M_Group_S_Refresh:
        case UIActionIndexMN_M_Machine_S_Refresh:
        {
            return isAtLeastOneItemInaccessible(items);
        }
        case UIActionIndexMN_M_Group_S_ShowInFileManager:
        case UIActionIndexMN_M_Machine_S_ShowInFileManager:
        {
            return isItemsLocal(items) &&
                   isAtLeastOneItemAccessible(items);
        }
        case UIActionIndexMN_M_Machine_S_SortParent:
        {
            return !isGroupSavingInProgress() &&
                   isItemsLocal(items);
        }
        case UIActionIndexMN_M_Group_S_CreateShortcut:
        case UIActionIndexMN_M_Machine_S_CreateShortcut:
        {
            return isAtLeastOneItemSupportsShortcuts(items);
        }
        case UIActionIndexMN_M_Group_M_Console:
        case UIActionIndexMN_M_Group_M_Console_S_CreateConnection:
        case UIActionIndexMN_M_Group_M_Console_S_DeleteConnection:
        case UIActionIndexMN_M_Group_M_Console_S_ConfigureApplications:
        case UIActionIndexMN_M_Machine_M_Console:
        case UIActionIndexMN_M_Machine_M_Console_S_CreateConnection:
        case UIActionIndexMN_M_Machine_M_Console_S_DeleteConnection:
        case UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialUnix:
        case UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialWindows:
        case UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCUnix:
        case UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCWindows:
        case UIActionIndexMN_M_Machine_M_Console_S_ConfigureApplications:
        case UIActionIndexMN_M_Machine_M_Console_S_ShowLog:
        {
            return isAtLeastOneItemStarted(items);
        }
        case UIActionIndexMN_M_Group_M_Stop:
        case UIActionIndexMN_M_Machine_M_Stop:
        {
            return    (isItemsLocal(items) && isAtLeastOneItemStarted(items))
                   || (isItemsCloud(items) && isAtLeastOneItemDiscardable(items));
        }
        case UIActionIndexMN_M_Group_M_Stop_S_SaveState:
        case UIActionIndexMN_M_Machine_M_Stop_S_SaveState:
        {
            return    isActionEnabled(UIActionIndexMN_M_Machine_M_Stop, items)
                   && isItemsLocal(items);
        }
        case UIActionIndexMN_M_Group_M_Stop_S_Terminate:
        case UIActionIndexMN_M_Machine_M_Stop_S_Terminate:
        {
            return    isActionEnabled(UIActionIndexMN_M_Machine_M_Stop, items)
                   && isAtLeastOneItemDiscardable(items);
        }
        case UIActionIndexMN_M_Group_M_Stop_S_Shutdown:
        case UIActionIndexMN_M_Machine_M_Stop_S_Shutdown:
        {
            return    isActionEnabled(UIActionIndexMN_M_Machine_M_Stop, items)
                   && isAtLeastOneItemAbleToShutdown(items);
        }
        case UIActionIndexMN_M_Group_M_Stop_S_PowerOff:
        case UIActionIndexMN_M_Machine_M_Stop_S_PowerOff:
        {
            return    isActionEnabled(UIActionIndexMN_M_Machine_M_Stop, items)
                   && isAtLeastOneItemStarted(items);
        }
        default:
            break;
    }

    /* Unknown actions are disabled: */
    return false;
}

/* static */
bool UIVirtualBoxManager::isItemsLocal(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (!pItem->toLocal())
            return false;
    return true;
}

/* static */
bool UIVirtualBoxManager::isItemsCloud(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (!pItem->toCloud())
            return false;
    return true;
}

/* static */
bool UIVirtualBoxManager::isItemsPoweredOff(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (!pItem->isItemPoweredOff())
            return false;
    return true;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemAbleToShutdown(const QList<UIVirtualMachineItem*> &items)
{
    /* Enumerate all the passed items: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Skip non-running machines: */
        if (!pItem->isItemRunning())
            continue;

        /* For local machine: */
        if (pItem->itemType() == UIVirtualMachineItemType_Local)
        {
            /* Skip session failures: */
            CSession session = openExistingSession(pItem->id());
            if (session.isNull())
                continue;
            /* Skip console failures: */
            CConsole console = session.GetConsole();
            if (console.isNull())
            {
                /* Do not forget to release machine: */
                session.UnlockMachine();
                continue;
            }
            /* Is the guest entered ACPI mode? */
            bool fGuestEnteredACPIMode = console.GetGuestEnteredACPIMode();
            /* Do not forget to release machine: */
            session.UnlockMachine();
            /* True if the guest entered ACPI mode: */
            if (fGuestEnteredACPIMode)
                return true;
        }
        /* For real cloud machine: */
        else if (pItem->itemType() == UIVirtualMachineItemType_CloudReal)
        {
            /* Running cloud VM has it by definition: */
            return true;
        }
    }
    /* False by default: */
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemSupportsShortcuts(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (   pItem->accessible()
            && pItem->toLocal()
#ifdef VBOX_WS_MAC
            /* On Mac OS X this are real alias files, which don't work with the old legacy xml files. */
            && pItem->toLocal()->settingsFile().endsWith(".vbox", Qt::CaseInsensitive)
#endif
            )
            return true;
    }
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemAccessible(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (pItem->accessible())
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemInaccessible(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (!pItem->accessible())
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemRemovable(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (pItem->isItemRemovable())
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemCanBeStarted(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (pItem->isItemPoweredOff() && pItem->isItemEditable())
            return true;
    }
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemCanBeShown(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (   pItem->isItemStarted()
            && pItem->isItemCanBeSwitchedTo())
            return true;
    }
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemCanBeStartedOrShown(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (   (   pItem->isItemPoweredOff()
                && pItem->isItemEditable())
            || (   pItem->isItemStarted()
                && pItem->isItemCanBeSwitchedTo()))
            return true;
    }
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemDiscardable(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (   pItem->isItemSaved()
            && pItem->isItemEditable())
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemStarted(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (pItem->isItemStarted())
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemRunning(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (pItem->isItemRunning())
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemDetachable(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (pItem->isItemRunningHeadless())
            return true;
    return false;
}

#ifdef VBOX_WS_NIX
/* static */
QPair<QString, QString> UIVirtualBoxManager::defaultTerminalData()
{
    /* List known terminals: */
    QStringList knownTerminalNames;
    knownTerminalNames << "gnome-terminal"
                       << "terminator"
                       << "konsole"
                       << "xfce4-terminal"
                       << "mate-terminal"
                       << "lxterminal"
                       << "tilda"
                       << "xterm"
                       << "aterm"
                       << "rxvt-unicode"
                       << "rxvt";

    /* Fill map of known terminal --execute argument exceptions,
     * keep in mind, terminals doesn't mentioned here will be
     * used with default `-e` argument: */
    QMap<QString, QString> knownTerminalArguments;
    knownTerminalArguments["gnome-terminal"] = "--";
    knownTerminalArguments["terminator"] = "-x";
    knownTerminalArguments["xfce4-terminal"] = "-x";
    knownTerminalArguments["mate-terminal"] = "-x";
    knownTerminalArguments["tilda"] = "-c";

    /* Search for a first one suitable through shell command -v test: */
    foreach (const QString &strTerminalName, knownTerminalNames)
    {
        const QString strPath = "sh";
        const QStringList arguments = QStringList() << "-c" << QString("command -v '%1'").arg(strTerminalName);
        QProcess process;
        process.start(strPath, arguments, QIODevice::ReadOnly);
        process.waitForFinished(3000);
        if (process.exitCode() == 0)
        {
            const QString strResult = process.readAllStandardOutput();
            if (strResult.startsWith('/'))
                return qMakePair(strResult.trimmed(), knownTerminalArguments.value(strTerminalName, "-e"));
        }
    }
    return QPair<QString, QString>();
}
#endif


#include "UIVirtualBoxManager.moc"
