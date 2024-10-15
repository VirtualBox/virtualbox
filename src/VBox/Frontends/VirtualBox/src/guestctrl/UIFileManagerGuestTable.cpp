/* $Id: UIFileManagerGuestTable.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileManagerGuestTable class implementation.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPushButton>
#include <QUuid>

/* GUI includes: */
#include "QILabel.h"
#include "UIActionPool.h"
#include "UIConverter.h"
#include "UICommon.h"
#include "UIErrorString.h"
#include "UIFileSystemModel.h"
#include "UIFileManager.h"
#include "UIFileManagerHostTable.h"
#include "UIFileManagerGuestTable.h"
#include "UIFileTableNavigationWidget.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UILocalMachineStuff.h"
#include "UIMessageCenter.h"
#include "UIPathOperations.h"
#include "UIUserNamePasswordEditor.h"
#include "UIVirtualBoxEventHandler.h"
#include "QILineEdit.h"
#include "QIToolBar.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CConsole.h"
#include "CFsObjInfo.h"
#include "CGuestFsObjInfo.h"
#include "CGuestDirectory.h"
#include "CProgress.h"
#include "CGuestSessionStateChangedEvent.h"

/* Other VBox includes: */
#include <iprt/err.h>

/*********************************************************************************************************************************
*   UIGuestSessionWidget definition.                                                                                   *
*********************************************************************************************************************************/
/** A QWidget extension containing text entry fields for password and username and buttons to
  *  start/stop a guest session. */
class UIGuestSessionWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigOpenSession(QString strUserName, QString strPassword);
    void sigCloseSession();

public:

    UIGuestSessionWidget(QWidget *pParent = 0);
    /** Disables certain widget after a guest session has been opened. */
    void switchSessionOpenMode();
    /** Makes sure certain widgets are enabled so that a guest session can be opened. */
    void switchSessionCloseMode();
    void markForError(bool fMarkForError);
    void setStatusLabelIconAndToolTip(const QIcon &icon, const QString &strToolTip);
    void setLoginWidgetsEnabled(bool fEnabled);

protected:

    void keyPressEvent(QKeyEvent * pEvent) RT_OVERRIDE;
    void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

private slots:

    void sltRetranslateUI();
    void sltButtonClick();
    void sltHandleTextChanged(const QString &strText);

private:

    enum ButtonMode
    {
        ButtonMode_Open,
        ButtonMode_Close
    };

    void          prepareWidgets();
    void          updateButton();

    ButtonMode    m_enmButtonMode;
    QILineEdit   *m_pUserNameEdit;
    UIPasswordLineEdit   *m_pPasswordEdit;
    QPushButton  *m_pButton;
    QHBoxLayout  *m_pMainLayout;
    QColor        m_defaultBaseColor;
    QColor        m_errorBaseColor;
    bool          m_fMarkedForError;
    QLabel       *m_pStatusIconLabel;
};


/*********************************************************************************************************************************
*   UIGuestSessionWidget implementation.                                                                                   *
*********************************************************************************************************************************/

UIGuestSessionWidget::UIGuestSessionWidget(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmButtonMode(ButtonMode_Open)
    , m_pUserNameEdit(0)
    , m_pPasswordEdit(0)
    , m_pButton(0)
    , m_pMainLayout(0)
    , m_fMarkedForError(0)
    , m_pStatusIconLabel(0)
{
    prepareWidgets();
}

void UIGuestSessionWidget::prepareWidgets()
{
    m_pMainLayout = new QHBoxLayout(this);
    if (!m_pMainLayout)
        return;

    m_pMainLayout->setContentsMargins(0, 0, 0, 0);

    m_pUserNameEdit = new QILineEdit;
    if (m_pUserNameEdit)
    {
        m_pMainLayout->addWidget(m_pUserNameEdit, 2);
        m_pUserNameEdit->setPlaceholderText(QApplication::translate("UIFileManager", "User Name"));
        m_defaultBaseColor = m_pUserNameEdit->palette().color(QPalette::Base);
        m_errorBaseColor = QColor(m_defaultBaseColor.red(),
                                  0.5 * m_defaultBaseColor.green(),
                                  0.5 * m_defaultBaseColor.blue());
        connect(m_pUserNameEdit, &QILineEdit::textChanged,
                this, &UIGuestSessionWidget::sltHandleTextChanged);
    }

    m_pPasswordEdit = new UIPasswordLineEdit;
    if (m_pPasswordEdit)
    {
        m_pMainLayout->addWidget(m_pPasswordEdit, 2);
        m_pPasswordEdit->setPlaceholderText(QApplication::translate("UIFileManager", "Password"));
        m_pPasswordEdit->setEchoMode(QLineEdit::Password);
        connect(m_pPasswordEdit, &UIPasswordLineEdit::textChanged,
                this, &UIGuestSessionWidget::sltHandleTextChanged);
    }

    m_pButton = new QPushButton;
    if (m_pButton)
    {
        m_pMainLayout->addWidget(m_pButton);
        connect(m_pButton, &QPushButton::clicked, this, &UIGuestSessionWidget::sltButtonClick);
    }
    m_pStatusIconLabel = new QLabel(this);
    if (m_pStatusIconLabel)
    {
        m_pMainLayout->addWidget(m_pStatusIconLabel);
        m_pStatusIconLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    }

    m_pMainLayout->insertStretch(-1, 1);
    switchSessionOpenMode();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIGuestSessionWidget::sltRetranslateUI);
}

void UIGuestSessionWidget::sltButtonClick()
{
    if (m_enmButtonMode == ButtonMode_Open && m_pUserNameEdit && m_pPasswordEdit)
        emit sigOpenSession(m_pUserNameEdit->text(), m_pPasswordEdit->text());
    else if (m_enmButtonMode == ButtonMode_Close)
        emit sigCloseSession();
}

void UIGuestSessionWidget::sltHandleTextChanged(const QString &strText)
{
    Q_UNUSED(strText);
    markForError(false);
}

void UIGuestSessionWidget::sltRetranslateUI()
{
    if (m_pUserNameEdit)
    {
        m_pUserNameEdit->setToolTip(QApplication::translate("UIFileManager", "User name to authenticate session creation"));
        m_pUserNameEdit->setPlaceholderText(QApplication::translate("UIFileManager", "User Name"));

    }
    if (m_pPasswordEdit)
    {
        m_pPasswordEdit->setToolTip(QApplication::translate("UIFileManager", "Password to authenticate session creation"));
        m_pPasswordEdit->setPlaceholderText(QApplication::translate("UIFileManager", "Password"));
    }

    if (m_pButton)
    {
        if (m_enmButtonMode == ButtonMode_Open)
        {
            m_pButton->setText(QApplication::translate("UIFileManager", "Open Session"));
            m_pButton->setToolTip(QApplication::translate("UIFileManager", "Open Session"));
        }
        else
        {
            m_pButton->setText(QApplication::translate("UIFileManager", "Close Session"));
            m_pButton->setToolTip(QApplication::translate("UIFileManager", "Close Session"));
        }
    }
}

void UIGuestSessionWidget::keyPressEvent(QKeyEvent * pEvent)
{
    /* Emit sigOpenSession upon enter press: */
    if (pEvent->key() == Qt::Key_Enter || pEvent->key() == Qt::Key_Return)
    {
        if ((m_pUserNameEdit && m_pUserNameEdit->hasFocus()) ||
            (m_pPasswordEdit && m_pPasswordEdit->hasFocus()))
            sigOpenSession(m_pUserNameEdit->text(), m_pPasswordEdit->text());
    }
    QWidget::keyPressEvent(pEvent);
}

void UIGuestSessionWidget::showEvent(QShowEvent *pEvent)
{
    QWidget::showEvent(pEvent);
    if (m_pUserNameEdit)
        m_pUserNameEdit->setFocus();
}

void UIGuestSessionWidget::switchSessionOpenMode()
{
    if (m_pUserNameEdit)
        m_pUserNameEdit->setEnabled(true);
    if (m_pPasswordEdit)
        m_pPasswordEdit->setEnabled(true);
    m_enmButtonMode = ButtonMode_Open;
    sltRetranslateUI();
}

void UIGuestSessionWidget::switchSessionCloseMode()
{
    if (m_pUserNameEdit)
        m_pUserNameEdit->setEnabled(false);
    if (m_pPasswordEdit)
        m_pPasswordEdit->setEnabled(false);
    m_enmButtonMode = ButtonMode_Close;
    sltRetranslateUI();
}

void UIGuestSessionWidget::markForError(bool fMarkForError)
{
    if (m_fMarkedForError == fMarkForError)
        return;
    m_fMarkedForError = fMarkForError;

    if (m_pUserNameEdit)
    {
        QPalette mPalette = m_pUserNameEdit->palette();
        if (m_fMarkedForError)
            mPalette.setColor(QPalette::Base, m_errorBaseColor);
        else
            mPalette.setColor(QPalette::Base, m_defaultBaseColor);
        m_pUserNameEdit->setPalette(mPalette);
    }
    if (m_pPasswordEdit)
    {
        QPalette mPalette = m_pPasswordEdit->palette();
        if (m_fMarkedForError)
            mPalette.setColor(QPalette::Base, m_errorBaseColor);
        else
            mPalette.setColor(QPalette::Base, m_defaultBaseColor);
        m_pPasswordEdit->setPalette(mPalette);
    }
}

void UIGuestSessionWidget::setStatusLabelIconAndToolTip(const QIcon &icon, const QString &strToolTip)
{
    if (!m_pStatusIconLabel)
        return;
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_ButtonIconSize);
    m_pStatusIconLabel->setPixmap(icon.pixmap(QSize(iIconMetric, iIconMetric)));
    m_pStatusIconLabel->setToolTip(strToolTip);
}

void UIGuestSessionWidget::setLoginWidgetsEnabled(bool fEnabled)
{
    if (m_pUserNameEdit)
        m_pUserNameEdit->setEnabled(fEnabled);
    if (m_pPasswordEdit)
        m_pPasswordEdit->setEnabled(fEnabled);
    if (m_pButton)
        m_pButton->setEnabled(fEnabled);
}


/*********************************************************************************************************************************
*   UIGuestDirectoryDiskUsageComputer definition.                                                                                *
*********************************************************************************************************************************/

/** Open directories recursively and sum the disk usage. Don't block the GUI thread while doing this */
class UIGuestDirectoryDiskUsageComputer : public UIDirectoryDiskUsageComputer
{
    Q_OBJECT;

public:

    UIGuestDirectoryDiskUsageComputer(QObject *parent, QStringList strStartPath, const CGuestSession &session);

protected:

    virtual void run() RT_OVERRIDE;
    virtual void directoryStatisticsRecursive(const QString &path, UIDirectoryStatistics &statistics) RT_OVERRIDE;

private:

    CGuestSession m_comGuestSession;
};


/*********************************************************************************************************************************
*   UIGuestDirectoryDiskUsageComputer implementation.                                                                            *
*********************************************************************************************************************************/

UIGuestDirectoryDiskUsageComputer::UIGuestDirectoryDiskUsageComputer(QObject *parent, QStringList pathList, const CGuestSession &session)
    :UIDirectoryDiskUsageComputer(parent, pathList)
    , m_comGuestSession(session)
{
}

void UIGuestDirectoryDiskUsageComputer::run()
{
    /* Initialize COM: */
    COMBase::InitializeCOM(false);
    UIDirectoryDiskUsageComputer::run();
    /* Cleanup COM: */
    COMBase::CleanupCOM();
}

void UIGuestDirectoryDiskUsageComputer::directoryStatisticsRecursive(const QString &path, UIDirectoryStatistics &statistics)
{
    if (m_comGuestSession.isNull())
        return;
    /* Prevent modification of the continue flag while reading: */
    m_mutex.lock();
    /* Check if m_fOkToContinue is set to false. if so just end recursion: */
    if (!isOkToContinue())
    {
        m_mutex.unlock();
        return;
    }
    m_mutex.unlock();

    CGuestFsObjInfo fileInfo = m_comGuestSession.FsObjQueryInfo(path, true);

    if (!m_comGuestSession.isOk())
        return;
    /* if the object is a file or a symlink then read the size and return: */
    if (fileInfo.GetType() == KFsObjType_File)
    {
        statistics.m_totalSize += fileInfo.GetObjectSize();
        ++statistics.m_uFileCount;
        sigResultUpdated(statistics);
        return;
    }
    else if (fileInfo.GetType() == KFsObjType_Symlink)
    {
        statistics.m_totalSize += fileInfo.GetObjectSize();
        ++statistics.m_uSymlinkCount;
        sigResultUpdated(statistics);
        return;
    }

    if (fileInfo.GetType() != KFsObjType_Directory)
        return;
    /* Open the directory to start reading its content: */
    QVector<KDirectoryOpenFlag> flag(1, KDirectoryOpenFlag_None);
    CGuestDirectory directory = m_comGuestSession.DirectoryOpen(path, /*aFilter*/ "", flag);
    if (!m_comGuestSession.isOk())
        return;

    if (directory.isOk())
    {
        CFsObjInfo fsInfo = directory.Read();
        while (fsInfo.isOk())
        {
            if (fsInfo.GetType() == KFsObjType_File)
                statistics.m_uFileCount++;
            else if (fsInfo.GetType() == KFsObjType_Symlink)
                statistics.m_uSymlinkCount++;
            else if(fsInfo.GetType() == KFsObjType_Directory)
            {
                QString dirPath = UIPathOperations::mergePaths(path, fsInfo.GetName());
                directoryStatisticsRecursive(dirPath, statistics);
            }
        }
    }
    sigResultUpdated(statistics);
}

UIFileManagerGuestTable::UIFileManagerGuestTable(UIActionPool *pActionPool, const CMachine &comMachine, QWidget *pParent /*= 0*/)
    :UIFileManagerTable(pActionPool, pParent)
    , m_comMachine(comMachine)
    , m_pGuestSessionWidget(0)
    , m_fIsCurrent(false)
    , pszMinimumGuestAdditionVersion("6.1")
{
    setModelFileSystem(isWindowsFileSystem());
    if (!m_comMachine.isNull())
        m_strTableName = m_comMachine.GetName();
    prepareToolbar();
    prepareGuestSessionPanel();
    prepareActionConnections();
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIFileManagerGuestTable::sltMachineStateChange);
    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIFileManagerGuestTable::sltCommitDataSignalReceived);

    if (m_pActionPool && m_pActionPool->action(UIActionIndex_M_FileManager_T_GuestSession))
        m_pActionPool->action(UIActionIndex_M_FileManager_T_GuestSession)->setChecked(true);

    if (!m_comMachine.isNull() && m_comMachine.GetState() == KMachineState_Running)
        openMachineSession();
    setStateAndEnableWidgets();

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIFileManagerGuestTable::sltRetranslateUI);
}

UIFileManagerGuestTable::~UIFileManagerGuestTable()
{
    cleanAll();
}

void UIFileManagerGuestTable::initFileTable()
{
    if (!m_comGuestSession.isOk() || m_comGuestSession.GetStatus() != KGuestSessionStatus_Started)
        return;
    /* To determine the path separator we need to have a valid guest session: */
    determinePathSeparator();
    initializeFileTree();
}

void UIFileManagerGuestTable::sltRetranslateUI()
{
    if (m_pLocationLabel)
        m_pLocationLabel->setText(UIFileManager::tr("Guest File System:"));

    if (m_pGuestSessionWidget)
    {
        QIcon icon;
        QString strWarningText;
        switch (m_enmState)
        {
            case State_InvalidMachineReference:
                strWarningText = UIFileManager::tr("Machine reference is invalid.");
                icon = UIIconPool::iconSet(":/status_error_16px.png");
                break;
            case State_MachineNotRunning:
                strWarningText = UIFileManager::tr("File manager cannot work since the selected guest is not currently running.");
                icon = UIIconPool::iconSet(":/status_error_16px.png");
                break;
            case State_MachinePaused:
                strWarningText = UIFileManager::tr("File manager cannot work since the guest is paused.");
                icon = UIIconPool::iconSet(":/session_info_16px.png");
                break;
            case State_NoGuestAdditions:
                strWarningText = UIFileManager::tr("File manager cannot work since no guest additions were detected.");
                icon = UIIconPool::iconSet(":/status_error_16px.png");
                break;
            case State_GuestAdditionsTooOld:
                strWarningText = UIFileManager::tr("File manager cannot work. The guest additions need to be updated.");
                icon = UIIconPool::iconSet(":/status_error_16px.png");
                break;
            case State_SessionPossible:
                strWarningText = UIFileManager::tr("Enter a valid user name and password to initiate the file manager.");
                icon = UIIconPool::iconSet(":/session_info_16px.png");
                break;
            case State_SessionRunning:
                strWarningText = UIFileManager::tr("Guest control session is running.");
                icon = UIIconPool::iconSet(":/status_check_16px.png");
                break;
            case State_SessionError:
                strWarningText = UIFileManager::tr("Some error has occurred. Please check the log panel.");
                icon = UIIconPool::iconSet(":/status_error_16px.png");
                break;
            default:
                break;
        }
        m_pGuestSessionWidget->setStatusLabelIconAndToolTip(icon, strWarningText);
    }

    UIFileManagerTable::sltRetranslateUI();
}

bool UIFileManagerGuestTable::readDirectory(const QString& strPath,
                                     UIFileSystemItem *parent, bool isStartDir /*= false*/)
{
    if (!parent)
        return false;

    CGuestDirectory directory;
    QVector<KDirectoryOpenFlag> flag;
    flag.push_back(KDirectoryOpenFlag_None);

    directory = m_comGuestSession.DirectoryOpen(UIPathOperations::sanitize(strPath), /*aFilter*/ "", flag);
    if (!m_comGuestSession.isOk())
    {
        emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
        return false;
    }

    parent->setIsOpened(true);
    if (directory.isOk())
    {
        int const cMaxEntries = _4K;   /* Maximum number of entries to read at once per List() call. */
        bool      fUseRead    = false; /* Whether to use the Read() API or the newer (faster) List() call. */

        QVector<CFsObjInfo> vecFsInfo = directory.List(cMaxEntries);
        /* IGuestDirectory::List() will return VBOX_E_NOT_SUPPORTED if older Guest Additions are installed. */
        if (directory.rc() == VBOX_E_NOT_SUPPORTED)
        {
            CFsObjInfo fsInfo = directory.Read();
            if (directory.isOk())
            {
                vecFsInfo.push_back(fsInfo);
                fUseRead = true;
            }
        }

        QMap<QString, UIFileSystemItem*> fileObjects;

        while (directory.isOk())
        {
            for (int i = 0; i < vecFsInfo.size(); i++)
            {
                CFsObjInfo const &fsInfo = vecFsInfo[i];

                if (fsInfo.GetName() != "." && fsInfo.GetName() != "..")
                {
                    QVector<QVariant> data;
                    QDateTime changeTime = QDateTime::fromMSecsSinceEpoch(fsInfo.GetChangeTime()/RT_NS_1MS);
                    KFsObjType fsObjectType = fileType(fsInfo);
                    UIFileSystemItem *item = new UIFileSystemItem(fsInfo.GetName(), parent, fsObjectType);
                    if (!item)
                        continue;
                    item->setData(static_cast<qulonglong>(fsInfo.GetObjectSize()), UIFileSystemModelData_Size);
                    item->setData(changeTime, UIFileSystemModelData_ChangeTime);
                    item->setData(fsInfo.GetUserName(), UIFileSystemModelData_Owner);
                    item->setData(permissionString(fsInfo), UIFileSystemModelData_Permissions);
                    item->setIsOpened(false);
                    item->setIsHidden(isFileObjectHidden(fsInfo));
                    fileObjects.insert(fsInfo.GetName(), item);
                    /* @todo. We will need to wait a fully implemented SymlinkRead function
                     * to be able to handle sym links properly: */
                    // QString path = UIPathOperations::mergePaths(strPath, fsInfo.GetName());
                    // QVector<KSymlinkReadFlag> aFlags;
                    // printf("%s %s %s\n", qPrintable(fsInfo.GetName()), qPrintable(path),
                    //        qPrintable(m_comGuestSession.SymlinkRead(path, aFlags)));
                }
            }

            if (fUseRead)
            {
                CFsObjInfo fsInfo = directory.Read();
                if (directory.isOk())
                {
                    vecFsInfo.clear();
                    vecFsInfo.push_back(fsInfo);
                }
            }
            else
                vecFsInfo = directory.List(cMaxEntries);
        }

        checkDotDot(fileObjects, parent, isStartDir);
    }
    else
    {
        directory.Close();
        return false;
    }
    directory.Close();
    return true;
}

void UIFileManagerGuestTable::deleteByItem(UIFileSystemItem *item)
{
    if (!item)
        return;
    if (item->isUpDirectory())
        return;

    if (item->isDirectory())
    {
        QVector<KDirectoryRemoveRecFlag> aFlags(1, KDirectoryRemoveRecFlag_ContentAndDir);
        m_comGuestSession.DirectoryRemoveRecursive(UIPathOperations::removeTrailingDelimiters(item->path()), aFlags);
    }
    else
        m_comGuestSession.FsObjRemove(UIPathOperations::removeTrailingDelimiters(item->path()));
    if (!m_comGuestSession.isOk())
    {
        emit sigLogOutput(QString(item->path()).append(" could not be deleted"), m_strTableName, FileManagerLogType_Error);
        emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
    }
}

void UIFileManagerGuestTable::goToHomeDirectory()
{
    if (m_comGuestSession.isNull())
        return;
    if (!rootItem() || rootItem()->childCount() <= 0)
        return;
    UIFileSystemItem *startDirItem = rootItem()->child(0);
    if (!startDirItem)
        return;

    const QString strUserHome = UIPathOperations::sanitize(m_comGuestSession.GetUserHome());
    if (!m_comGuestSession.isOk())
    {
        emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
        return;
    }
    goIntoDirectory(UIPathOperations::pathTrail(strUserHome));
}

bool UIFileManagerGuestTable::renameItem(UIFileSystemItem *item, const QString &strOldPath)
{
    if (!item || item->isUpDirectory())
        return false;
    //QString newPath = UIPathOperations::removeTrailingDelimiters(UIPathOperations::constructNewItemPath(item->path(), newBaseName));
    QVector<KFsObjRenameFlag> aFlags(1, KFsObjRenameFlag_Replace);

    m_comGuestSession.FsObjRename(strOldPath, item->path(), aFlags);

    if (!m_comGuestSession.isOk())
    {
        emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
        return false;
    }

    return true;
}

bool UIFileManagerGuestTable::createDirectory(const QString &path, const QString &directoryName)
{
    QString newDirectoryPath = UIPathOperations::mergePaths(path, directoryName);
    QVector<KDirectoryCreateFlag> flags(1, KDirectoryCreateFlag_None);

    m_comGuestSession.DirectoryCreate(newDirectoryPath, 0/*aMode*/, flags);

    if (!m_comGuestSession.isOk())
    {
        emit sigLogOutput(newDirectoryPath.append(" could not be created"), m_strTableName, FileManagerLogType_Error);
        emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
        return false;
    }
    emit sigLogOutput(newDirectoryPath.append(" has been created"), m_strTableName, FileManagerLogType_Info);
    return true;
}

void UIFileManagerGuestTable::copyHostToGuest(const QStringList &hostSourcePathList,
                                              const QString &strDestination /* = QString() */)
{
    if (!checkGuestSession())
        return;
    QVector<QString> sourcePaths = hostSourcePathList.toVector();
    /* Remove empty source paths. Typically happens when up directory is selected: */
    sourcePaths.removeAll(QString());

    QVector<QString> aFilters;
    QVector<QString> aFlags;
    QString strDestinationPath = UIPathOperations::addTrailingDelimiters(strDestination);
    if (strDestinationPath.isEmpty())
        strDestinationPath = UIPathOperations::addTrailingDelimiters(currentDirectoryPath());

    if (strDestinationPath.isEmpty())
    {
        emit sigLogOutput("No destination for copy operation", m_strTableName, FileManagerLogType_Error);
        return;
    }
    if (sourcePaths.empty())
    {
        emit sigLogOutput("No source for copy operation", m_strTableName, FileManagerLogType_Error);
        return;
    }
    QString strDirectoryFlags("CopyIntoExisting,Recursive,FollowLinks");
    QString strFileFlags("FollowLinks");

    for (int i = 0; i < sourcePaths.size(); ++i)
    {
        sourcePaths[i] = UIPathOperations::removeTrailingDelimiters(sourcePaths[i]);
        KFsObjType enmFileType = UIFileManagerHostTable::fileType(sourcePaths[i]);
        if (enmFileType == KFsObjType_Unknown)
            emit sigLogOutput(QString("Querying information for host item %1 failed.").arg(sourcePaths[i]), m_strTableName, FileManagerLogType_Error);
        /* If the source is an directory, make sure to add the appropriate flag to make copying work
         * into existing directories on the guest. This otherwise would fail (default): */
        else if (enmFileType == KFsObjType_Directory)
            aFlags << strDirectoryFlags;
        else
            aFlags << strFileFlags;
    }

    CProgress progress = m_comGuestSession.CopyToGuest(sourcePaths, aFilters, aFlags, strDestinationPath);
    if (!checkGuestSession())
        return;
    emit sigNewFileOperation(progress, m_strTableName);
}

QUuid UIFileManagerGuestTable::machineId()
{
    if (m_comMachine.isNull())
        return QUuid();
    return m_comMachine.GetId();
}

bool UIFileManagerGuestTable::isGuestSessionRunning() const
{
    return m_enmState == State_SessionRunning;
}

void UIFileManagerGuestTable::setIsCurrent(bool fIsCurrent)
{
    if (m_fIsCurrent == fIsCurrent)
        return;
    m_fIsCurrent = fIsCurrent;
    prepareActionConnections();
}

void UIFileManagerGuestTable::copyGuestToHost(const QString& hostDestinationPath)
{
    if (!checkGuestSession())
        return;
    QVector<QString> sourcePaths = selectedItemPathList().toVector();
    QVector<QString> aFilters;
    QVector<QString> aFlags;

    /* Remove empty source paths. Typically happens when up directory is selected: */
    sourcePaths.removeAll(QString());

    if (hostDestinationPath.isEmpty())
    {
        emit sigLogOutput("No destination for copy operation", m_strTableName, FileManagerLogType_Error);
        return;
    }
    if (sourcePaths.empty())
    {
        emit sigLogOutput("No source for copy operation", m_strTableName, FileManagerLogType_Error);
        return;
    }

    QString strDestinationPath = UIPathOperations::addTrailingDelimiters(hostDestinationPath);
    QString strDirectoryFlags("CopyIntoExisting,Recursive,FollowLinks");
    QString strFileFlags;
    //foreach (const QString &strSource, sourcePaths)
    for (int i = 0; i < sourcePaths.size(); ++i)
    {
        sourcePaths[i] = UIPathOperations::removeTrailingDelimiters(sourcePaths[i]);
        /** @todo Cache this info and use the item directly, which has this info already? */

        /* If the source is an directory, make sure to add the appropriate flag to make copying work
         * into existing directories on the guest. This otherwise would fail (default). */
        CGuestFsObjInfo fileInfo = m_comGuestSession.FsObjQueryInfo(sourcePaths[i], true);
        if (!m_comGuestSession.isOk())
        {
            emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
            return;
        }

        if (fileType(fileInfo) == KFsObjType_Directory)
            aFlags << strDirectoryFlags;
        else
            aFlags << strFileFlags;

    }

    CProgress progress = m_comGuestSession.CopyFromGuest(sourcePaths, aFilters, aFlags, strDestinationPath);
    if (!checkGuestSession())
        return;
    emit sigNewFileOperation(progress, m_strTableName);
}

KFsObjType UIFileManagerGuestTable::fileType(const CFsObjInfo &fsInfo)
{
    if (fsInfo.isNull() || !fsInfo.isOk())
        return KFsObjType_Unknown;
    if (fsInfo.GetType() == KFsObjType_Directory)
         return KFsObjType_Directory;
    else if (fsInfo.GetType() == KFsObjType_File)
        return KFsObjType_File;
    else if (fsInfo.GetType() == KFsObjType_Symlink)
        return KFsObjType_Symlink;

    return KFsObjType_Unknown;
}

KFsObjType UIFileManagerGuestTable::fileType(const CGuestFsObjInfo &fsInfo)
{
    if (fsInfo.isNull() || !fsInfo.isOk())
        return KFsObjType_Unknown;
    if (fsInfo.GetType() == KFsObjType_Directory)
         return KFsObjType_Directory;
    else if (fsInfo.GetType() == KFsObjType_File)
        return KFsObjType_File;
    else if (fsInfo.GetType() == KFsObjType_Symlink)
        return KFsObjType_Symlink;

    return KFsObjType_Unknown;
}


QString UIFileManagerGuestTable::fsObjectPropertyString()
{
    QStringList selectedObjects = selectedItemPathList();
    if (selectedObjects.isEmpty())
        return QString();
    if (selectedObjects.size() == 1)
    {
        if (selectedObjects.at(0).isNull())
            return QString();

        CGuestFsObjInfo fileInfo = m_comGuestSession.FsObjQueryInfo(selectedObjects.at(0), false /*aFollowSymlinks*/);
        if (!m_comGuestSession.isOk())
        {
            emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
            return QString();
        }

        QStringList propertyStringList;

        /* Name: */
        propertyStringList << UIFileManager::tr("<b>Name:</b> %1<br/>").arg(UIPathOperations::getObjectName(fileInfo.GetName()));

        /* Size: */
        LONG64 size = fileInfo.GetObjectSize();
        propertyStringList << UIFileManager::tr("<b>Size:</b> %1 bytes").arg(QString::number(size));
        if (size >= UIFileManagerTable::m_iKiloByte)
            propertyStringList << QString(" (%1)<br/>").arg(humanReadableSize(size));
        else
            propertyStringList << QString("<br/>");

        /* Allocated size: */
        size = fileInfo.GetAllocatedSize();
        propertyStringList << UIFileManager::tr("<b>Allocated:</b> %1 bytes").arg(QString::number(size));
        if (size >= UIFileManagerTable::m_iKiloByte)
            propertyStringList << QString(" (%1)<br/>").arg(humanReadableSize(size));
        else
            propertyStringList << QString("<br/>");

        /* Type: */
        QString str;
        KFsObjType const enmType = fileInfo.GetType();
        switch (enmType)
        {
            case KFsObjType_Directory:  str = UIFileManager::tr("directory"); break;
            case KFsObjType_File:       str = UIFileManager::tr("file"); break;
            case KFsObjType_Symlink:    str = UIFileManager::tr("symbolic link"); break;
            case KFsObjType_DevChar:    str = UIFileManager::tr("character device"); break;
            case KFsObjType_DevBlock:   str = UIFileManager::tr("block device"); break;
            case KFsObjType_Fifo:       str = UIFileManager::tr("fifo"); break;
            case KFsObjType_Socket:     str = UIFileManager::tr("socket"); break;
            case KFsObjType_WhiteOut:   str = UIFileManager::tr("whiteout"); break;
            case KFsObjType_Unknown:    str = UIFileManager::tr("unknown"); break;
            default:                    str = UIFileManager::tr("illegal-value"); break;
        }
        propertyStringList <<  UIFileManager::tr("<b>Type:</b> %1<br/>").arg(str);

        /* INode number, device, link count: */
        propertyStringList << UIFileManager::tr("<b>INode:</b> %1<br/>").arg(fileInfo.GetNodeId());
        propertyStringList << UIFileManager::tr("<b>Device:</b> %1<br/>").arg(fileInfo.GetNodeIdDevice());  /** @todo hex */
        propertyStringList << UIFileManager::tr("<b>Hardlinks:</b> %1<br/>").arg(fileInfo.GetHardLinks());

        /* Attributes: */
        str = fileInfo.GetFileAttributes();
        if (!str.isEmpty())
        {
            int offSpace = str.indexOf(' ');
            if (offSpace < 0)
                offSpace = str.length();
            propertyStringList << UIFileManager::tr("<b>Mode:</b> %1<br/>").arg(str.left(offSpace));
            propertyStringList << UIFileManager::tr("<b>Attributes:</b> %1<br/>").arg(str.mid(offSpace + 1).trimmed());
        }

        /* Character/block device ID: */
        ULONG uDeviceNo = fileInfo.GetDeviceNumber();
        if (uDeviceNo != 0 || enmType == KFsObjType_DevChar || enmType == KFsObjType_DevBlock)
            propertyStringList << UIFileManager::tr("<b>Device ID:</b> %1<br/>").arg(uDeviceNo); /** @todo hex */

        /* Owner: */
        propertyStringList << UIFileManager::tr("<b>Owner:</b> %1 (%2)<br/>").
            arg(fileInfo.GetUserName()).arg(fileInfo.GetUID());
        propertyStringList << UIFileManager::tr("<b>Group:</b> %1 (%2)<br/>").
            arg(fileInfo.GetGroupName()).arg(fileInfo.GetGID());

        /* Timestamps: */
        propertyStringList << UIFileManager::tr("<b>Birth:</b> %1<br/>").
            arg(QDateTime::fromMSecsSinceEpoch(fileInfo.GetBirthTime() / RT_NS_1MS).toString());
        propertyStringList << UIFileManager::tr("<b>Change:</b> %1<br/>").
            arg(QDateTime::fromMSecsSinceEpoch(fileInfo.GetChangeTime() / RT_NS_1MS).toString());
        propertyStringList << UIFileManager::tr("<b>Modified:</b> %1<br/>").
            arg(QDateTime::fromMSecsSinceEpoch(fileInfo.GetModificationTime() / RT_NS_1MS).toString());
        propertyStringList << UIFileManager::tr("<b>Access:</b> %1<br/>").
            arg(QDateTime::fromMSecsSinceEpoch(fileInfo.GetAccessTime() / RT_NS_1MS).toString());

        /* Join the list elements into a single string seperated by empty string: */
        return propertyStringList.join(QString());
    }

    int fileCount = 0;
    int directoryCount = 0;
    ULONG64 totalSize = 0;

    for(int i = 0; i < selectedObjects.size(); ++i)
    {
        CGuestFsObjInfo fileInfo = m_comGuestSession.FsObjQueryInfo(selectedObjects.at(0), true);
        if (!m_comGuestSession.isOk())
        {
            emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
            continue;
        }

        KFsObjType type = fileType(fileInfo);

        if (type == KFsObjType_File)
            ++fileCount;
        if (type == KFsObjType_Directory)
            ++directoryCount;
        totalSize += fileInfo.GetObjectSize();
    }
    QStringList propertyStringList;
    propertyStringList << UIFileManager::tr("<b>Selected:</b> %1 files and %2 directories<br/>").
        arg(QString::number(fileCount)).arg(QString::number(directoryCount));
    propertyStringList << UIFileManager::tr("<b>Size (non-recursive):</b> %1 bytes").arg(QString::number(totalSize));
    if (totalSize >= m_iKiloByte)
        propertyStringList << QString(" (%1)").arg(humanReadableSize(totalSize));

    return propertyStringList.join(QString());;
}

void UIFileManagerGuestTable::showProperties()
{
    if (m_comGuestSession.isNull())
        return;
    QString fsPropertyString = fsObjectPropertyString();
    if (fsPropertyString.isEmpty())
        return;

    m_pPropertiesDialog = new UIPropertiesDialog(this);
    if (!m_pPropertiesDialog)
        return;

    QStringList selectedObjects = selectedItemPathList();
    if (selectedObjects.size() == 0)
        return;

    m_pPropertiesDialog->setWindowTitle(UIFileManager::tr("Properties"));
    m_pPropertiesDialog->setPropertyText(fsPropertyString);
    m_pPropertiesDialog->execute();

    delete m_pPropertiesDialog;
    m_pPropertiesDialog = 0;
}

void UIFileManagerGuestTable::determineDriveLetters()
{
    if (m_comGuestSession.isNull())
        return;

    KPathStyle pathStyle = m_comGuestSession.GetPathStyle();
    if (pathStyle != KPathStyle_DOS)
        return;

    m_driveLetterList.clear();

    QVector<QString> mountPoints = m_comGuestSession.GetMountPoints();
    if (m_comGuestSession.isOk())
    {
        foreach (const QString &strPoint, mountPoints)
            m_driveLetterList.push_back(UIPathOperations::replaceDosDelimeter(strPoint));
    }
    else
    {
        for (int i = 'A'; i <= 'Z'; ++i)
        {
            QString path((char)i);
            path += ":/";
            bool exists = m_comGuestSession.DirectoryExists(path, false /* aFollowSymlinks */);
            if (exists)
                m_driveLetterList.push_back(path);
        }
    }
}

void UIFileManagerGuestTable::determinePathSeparator()
{
    if (m_comGuestSession.isNull())
        return;
    KPathStyle pathStyle = m_comGuestSession.GetPathStyle();
    if (pathStyle == KPathStyle_DOS)
        setPathSeparator(UIPathOperations::dosDelimiter);
}

void UIFileManagerGuestTable::prepareToolbar()
{
    if (m_pToolBar && m_pActionPool)
    {
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoBackward));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoForward));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoUp));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoHome));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Refresh));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Delete));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Rename));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_CreateNewDirectory));

        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Copy));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Cut));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Paste));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_SelectAll));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_InvertSelection));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_ShowProperties));
        m_selectionDependentActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Delete));
        m_selectionDependentActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Rename));
        m_selectionDependentActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Copy));
        m_selectionDependentActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Cut));
        m_selectionDependentActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_ShowProperties));

        /* Hide these actions for now until we have a suitable guest-to-guest copy function: */
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Copy)->setVisible(false);
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Cut)->setVisible(false);
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Paste)->setVisible(false);

        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_T_GuestSession));
    }

    setSelectionDependentActionsEnabled(false);
    setPasteActionEnabled(false);
}

void UIFileManagerGuestTable::createFileViewContextMenu(const QWidget *pWidget, const QPoint &point)
{
    if (!pWidget)
        return;

    QMenu menu;
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoBackward));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoForward));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoUp));

    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoHome));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Refresh));
    menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Delete));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Rename));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_CreateNewDirectory));
    menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Copy));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Cut));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Paste));
    menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_SelectAll));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_InvertSelection));
    menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_ShowProperties));
    menu.exec(pWidget->mapToGlobal(point));
}

void UIFileManagerGuestTable::setPasteActionEnabled(bool fEnabled)
{
    m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Paste)->setEnabled(fEnabled);
}

void UIFileManagerGuestTable::pasteCutCopiedObjects()
{
}

void UIFileManagerGuestTable::manageConnection(bool fConnect, QAction *pAction, void (UIFileManagerGuestTable::*fptr)(void))
{
    if (!pAction || !fptr)
        return;
    if (fConnect)
        connect(pAction, &QAction::triggered, this, fptr);
    else
        disconnect(pAction, 0, this, 0);
}

void UIFileManagerGuestTable::prepareActionConnections()
{
    if (m_pActionPool->action(UIActionIndex_M_FileManager_T_GuestSession))
    {
        if (m_fIsCurrent)
            connect(m_pActionPool->action(UIActionIndex_M_FileManager_T_GuestSession), &QAction::toggled,
                    this, &UIFileManagerGuestTable::sltGuestSessionPanelToggled);
        else
            disconnect(m_pActionPool->action(UIActionIndex_M_FileManager_T_GuestSession), 0, this, 0);
    }

    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoUp), &UIFileManagerTable::sltGoUp);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoHome), &UIFileManagerTable::sltGoHome);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoBackward), &UIFileManagerTable::sltGoBackward);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoForward), &UIFileManagerTable::sltGoForward);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Refresh), &UIFileManagerTable::sltRefresh);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Delete), &UIFileManagerTable::sltDelete);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Rename), &UIFileManagerTable::sltRename);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Copy), &UIFileManagerTable::sltCopy);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Cut), &UIFileManagerTable::sltCut);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_Paste), &UIFileManagerTable::sltPaste);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_SelectAll), &UIFileManagerTable::sltSelectAll);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_InvertSelection), &UIFileManagerTable::sltInvertSelection);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_ShowProperties), &UIFileManagerTable::sltShowProperties);
    manageConnection(m_fIsCurrent, m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_CreateNewDirectory), &UIFileManagerTable::sltCreateNewDirectory);

    /* Also disable/enable go forward/backward actions: */
    toggleForwardBackwardActions();
}

void UIFileManagerGuestTable::prepareGuestSessionPanel()
{
    if (m_pMainLayout)
    {
        m_pGuestSessionWidget = new UIGuestSessionWidget;
        if (m_pGuestSessionWidget)
        {
            m_pMainLayout->addWidget(m_pGuestSessionWidget, m_pMainLayout->rowCount(), 0, 1, m_pMainLayout->columnCount());
            m_pGuestSessionWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
            connect(m_pGuestSessionWidget, &UIGuestSessionWidget::sigOpenSession,
                    this, &UIFileManagerGuestTable::sltOpenGuestSession);
            connect(m_pGuestSessionWidget, &UIGuestSessionWidget::sigCloseSession,
                    this, &UIFileManagerGuestTable::sltHandleCloseSessionRequest);
        }
    }
}

bool UIFileManagerGuestTable::checkGuestSession()
{
    if (!m_comGuestSession.isOk())
    {
        emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
        return false;
    }
    return true;
}

QString UIFileManagerGuestTable::permissionString(const CFsObjInfo &fsInfo)
{
    /* Attributes: */
    QString strAttributes = fsInfo.GetFileAttributes();

    if (strAttributes.isEmpty())
        return strAttributes;

    int offSpace = strAttributes.indexOf(' ');
    if (offSpace < 0)
        offSpace = strAttributes.length();
    return strAttributes.left(offSpace);
}

bool UIFileManagerGuestTable::isFileObjectHidden(const CFsObjInfo &fsInfo)
{
    QString strAttributes = fsInfo.GetFileAttributes();

    if (strAttributes.isEmpty())
        return false;

    int offSpace = strAttributes.indexOf(' ');
    if (offSpace < 0)
        offSpace = strAttributes.length();
    QString strRight(strAttributes.mid(offSpace + 1).trimmed());

    if (strRight.indexOf('H', Qt::CaseSensitive) == -1)
        return false;
    return true;
}

void UIFileManagerGuestTable::sltGuestSessionPanelToggled(bool fChecked)
{
    if (m_pGuestSessionWidget)
        m_pGuestSessionWidget->setVisible(fChecked);
}

void UIFileManagerGuestTable::sltMachineStateChange(const QUuid &uMachineId, const KMachineState enmMachineState)
{
    if (uMachineId.isNull() || m_comMachine.isNull() || uMachineId != m_comMachine.GetId())
        return;

    if (enmMachineState == KMachineState_Running)
        openMachineSession();
    else if (enmMachineState != KMachineState_Paused)
        cleanAll();
    setStateAndEnableWidgets();
}

bool UIFileManagerGuestTable::closeMachineSession()
{
    if (!m_comGuest.isNull())
        m_comGuest.detach();

    if (!m_comConsole.isNull())
        m_comConsole.detach();

    if (!m_comSession.isNull())
    {
        m_comSession.UnlockMachine();
        m_comSession.detach();
    }
    return true;
}

bool UIFileManagerGuestTable::openMachineSession()
{
    if (m_comMachine.isNull())
    {
        emit sigLogOutput("Invalid machine reference", m_strTableName, FileManagerLogType_Error);
        return false;
    }
    m_comSession = openSession(m_comMachine.GetId(), KLockType_Shared);
    if (m_comSession.isNull())
    {
        emit sigLogOutput("Could not open machine session", m_strTableName, FileManagerLogType_Error);
        return false;
    }

    m_comConsole = m_comSession.GetConsole();
    if (m_comConsole.isNull())
    {
        emit sigLogOutput("Machine console is invalid", m_strTableName, FileManagerLogType_Error);
        return false;
    }

    m_comGuest = m_comConsole.GetGuest();
    if (m_comGuest.isNull())
    {
        emit sigLogOutput("Guest reference is invalid", m_strTableName, FileManagerLogType_Error);
        return false;
    }

    /* Prepare guest listener for guest session related events: */
    {
        QVector<KVBoxEventType> eventTypes;
        eventTypes << KVBoxEventType_OnGuestSessionRegistered;
        prepareListener(m_pQtGuestListener, m_comGuestListener, m_comGuest.GetEventSource(), eventTypes);
        connect(m_pQtGuestListener->getWrapped(), &UIMainEventListener::sigGuestSessionUnregistered,
                this, &UIFileManagerGuestTable::sltGuestSessionUnregistered);
        connect(m_pQtGuestListener->getWrapped(), &UIMainEventListener::sigGuestSessionRegistered,
                this, &UIFileManagerGuestTable::sltGuestSessionRegistered);
    }

    /* Prepare console listener for guest additions state change events: */
    {
        QVector<KVBoxEventType> eventTypes;
        eventTypes << KVBoxEventType_OnAdditionsStateChanged;
        prepareListener(m_pQtConsoleListener, m_comConsoleListener, m_comConsole.GetEventSource(), eventTypes);
        connect(m_pQtConsoleListener->getWrapped(), &UIMainEventListener::sigAdditionsChange,
                this, &UIFileManagerGuestTable::sltAdditionsStateChange);
    }
    emit sigLogOutput("Shared machine session opened", m_strTableName, FileManagerLogType_Info);
    return true;
}

int UIFileManagerGuestTable::isGuestAdditionsAvailable(const char* pszMinimumVersion)
{
    if (m_comGuest.isNull() || !pszMinimumVersion)
        return 0;

    /* Guest control stuff is in userland: */
    if (!m_comGuest.GetAdditionsStatus(KAdditionsRunLevelType_Userland))
        return 0;

    if (!m_comGuest.isOk())
        return 0;

    /* Check the related GA facility: */
    LONG64 iLastUpdatedIgnored;
    if (m_comGuest.GetFacilityStatus(KAdditionsFacilityType_VBoxService, iLastUpdatedIgnored) != KAdditionsFacilityStatus_Active)
        return 0;

    if (!m_comGuest.isOk())
        return 0;

    /* Check if GA is new enough to have the goodies: */
    QString strGAVersion = m_comGuest.GetAdditionsVersion();
    int iCode = RTStrVersionCompare(strGAVersion.toUtf8().constData(), pszMinimumVersion);
    if (iCode >= 0)
        return 1;
    else
        return -1;

    return 0;
}

void UIFileManagerGuestTable::cleanupGuestListener()
{
    if (!m_pQtGuestListener.isNull())
    {
        m_pQtGuestListener->getWrapped()->disconnect();
        if (!m_comGuest.isNull())
            cleanupListener(m_pQtGuestListener, m_comGuestListener, m_comGuest.GetEventSource());
    }
}

void UIFileManagerGuestTable::cleanupGuestSessionListener()
{
    if (!m_pQtSessionListener.isNull())
    {
        m_pQtSessionListener->getWrapped()->disconnect();
        if (!m_comGuestSession.isNull())
            cleanupListener(m_pQtSessionListener, m_comSessionListener, m_comGuestSession.GetEventSource());
    }
}

void UIFileManagerGuestTable::cleanupConsoleListener()
{
    if (!m_pQtConsoleListener.isNull())
    {
        m_pQtConsoleListener->getWrapped()->disconnect();
        if (!m_comConsole.isNull())
            cleanupListener(m_pQtConsoleListener, m_comConsoleListener, m_comConsole.GetEventSource());
    }
}

void UIFileManagerGuestTable::prepareListener(ComObjPtr<UIMainEventListenerImpl> &QtListener,
                                              CEventListener &comEventListener,
                                              CEventSource comEventSource, QVector<KVBoxEventType>& eventTypes)
{
    if (!comEventSource.isOk())
        return;
    /* Create event listener instance: */
    QtListener.createObject();
    QtListener->init(new UIMainEventListener, this);
    comEventListener = CEventListener(QtListener);

    /* Register event listener for CProgress event source: */
    comEventSource.RegisterListener(comEventListener, eventTypes, FALSE /* active? */);

    /* Register event sources in their listeners as well: */
    QtListener->getWrapped()->registerSource(comEventSource, comEventListener);
}

void UIFileManagerGuestTable::cleanupListener(ComObjPtr<UIMainEventListenerImpl> &QtListener,
                                              CEventListener &comEventListener,
                                              CEventSource comEventSource)
{
    if (!comEventSource.isOk())
        return;
    /* Unregister everything: */
    QtListener->getWrapped()->unregisterSources();
    QtListener.setNull();
    /* Make sure VBoxSVC is available: */
    if (!gpGlobalSession->isVBoxSVCAvailable())
        return;

    /* Unregister event listener for CProgress event source: */
    comEventSource.UnregisterListener(comEventListener);
}

void UIFileManagerGuestTable::sltGuestSessionUnregistered(CGuestSession guestSession)
{
    if (guestSession.isNull())
        return;
    if (guestSession == m_comGuestSession && !m_comGuestSession.isNull())
    {
        m_comGuestSession.detach();
        emit sigLogOutput("Guest session unregistered", m_strTableName, FileManagerLogType_Info);
    }
}

void UIFileManagerGuestTable::sltGuestSessionRegistered(CGuestSession guestSession)
{
    if (guestSession == m_comGuestSession && !m_comGuestSession.isNull())
        emit sigLogOutput("Guest session registered", m_strTableName, FileManagerLogType_Info);
}

void UIFileManagerGuestTable::sltGuestSessionStateChanged(const CGuestSessionStateChangedEvent &cEvent)
{
    if (cEvent.isOk())
    {
        CVirtualBoxErrorInfo cErrorInfo = cEvent.GetError();
        if (cErrorInfo.GetResultDetail() < VINF_SUCCESS)
            emit sigLogOutput(cErrorInfo.GetText(), m_strTableName, FileManagerLogType_Error);

        if (m_pGuestSessionWidget)
            m_pGuestSessionWidget->markForError(cErrorInfo.GetResultDetail() == VERR_AUTHENTICATION_FAILURE);
    }

    setStateAndEnableWidgets();

    if (m_comGuestSession.isNull())
        emit sigLogOutput("Guest session is invalid!", m_strTableName, FileManagerLogType_Error);
    else
    {
        if (m_comGuestSession.isOk())
        {
            emit sigLogOutput(QString("%1: %2").arg("Guest session status has changed").arg(gpConverter->toString(m_comGuestSession.GetStatus())),
                              m_strTableName, FileManagerLogType_Info);

            switch (m_comGuestSession.GetStatus())
            {
                case KGuestSessionStatus_Started:
                    {
                        initFileTable();
                        break;
                    }
                case KGuestSessionStatus_Terminating:
                case KGuestSessionStatus_Terminated:
                case KGuestSessionStatus_TimedOutKilled:
                case KGuestSessionStatus_TimedOutAbnormally:
                case KGuestSessionStatus_Down:
                case KGuestSessionStatus_Error:
                    {
                        cleanupGuestSessionListener();
                        closeGuestSession();
                        break;
                    }
                case KGuestSessionStatus_Undefined:
                case KGuestSessionStatus_Starting:
                case KGuestSessionStatus_Max:
                default:
                    break;
            }
        }
        else
            emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
    }
}

void UIFileManagerGuestTable::sltOpenGuestSession(QString strUserName, QString strPassword)
{
    if (strUserName.isEmpty())
    {
        emit sigLogOutput("No user name is given", m_strTableName, FileManagerLogType_Error);
        if (m_pGuestSessionWidget)
            m_pGuestSessionWidget->markForError(true);
        return;
    }
    openGuestSession(strUserName, strPassword);
}

void UIFileManagerGuestTable::toggleForwardBackwardActions()
{
    if (!m_pNavigationWidget)
        return;
    if (m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoForward))
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoForward)->setEnabled(m_pNavigationWidget->canGoForward());
    if (m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoBackward))
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Guest_GoBackward)->setEnabled(m_pNavigationWidget->canGoBackward());
}

void UIFileManagerGuestTable::setState()
{
    if (m_comMachine.isNull())
    {
        m_enmState = State_InvalidMachineReference;
        return;
    }
    if (m_comMachine.GetState() == KMachineState_Paused)
    {
        m_enmState = State_MachinePaused;
        return;
    }
    if (m_comMachine.GetState() != KMachineState_Running)
    {
        m_enmState = State_MachineNotRunning;
        return;
    }

    int iGADetectCode = isGuestAdditionsAvailable(pszMinimumGuestAdditionVersion);
    if (iGADetectCode == 0)
    {
        m_enmState = State_NoGuestAdditions;
        return;
    }
    else if (iGADetectCode == -1)
    {
        m_enmState = State_GuestAdditionsTooOld;
        return;
    }

    if (!m_comGuestSession.isNull() && m_comGuestSession.GetStatus() == KGuestSessionStatus_Started)
    {
        m_enmState = State_SessionRunning;
        return;
    }
    if (!m_comGuestSession.isNull() && m_comGuestSession.GetStatus() == KGuestSessionStatus_Error)
    {
        m_enmState = State_SessionError;
        return;
    }
    m_enmState = State_SessionPossible;
}

void UIFileManagerGuestTable::setStateAndEnableWidgets()
{
    setState();
    setSessionDependentWidgetsEnabled();
    sltRetranslateUI();
}

void UIFileManagerGuestTable::sltHandleCloseSessionRequest()
{
    cleanupGuestSessionListener();
    closeGuestSession();
    setStateAndEnableWidgets();
}

void UIFileManagerGuestTable::sltCommitDataSignalReceived()
{
    cleanAll();
    if (!m_comMachine.isNull())
        m_comMachine.detach();
}

void UIFileManagerGuestTable::sltAdditionsStateChange()
{
    setStateAndEnableWidgets();
}

void UIFileManagerGuestTable::setSessionDependentWidgetsEnabled()
{
    /* Disable menu actions if guest session is not running: */
    UIMenu *pGuestSubmenu = m_pActionPool->action(UIActionIndex_M_FileManager_M_GuestSubmenu)->menu();
    if (pGuestSubmenu)
        pGuestSubmenu->setEnabled(m_enmState == State_SessionRunning);
    UIMenu *pHostSubmenu = m_pActionPool->action(UIActionIndex_M_FileManager_M_HostSubmenu)->menu();
    if (pHostSubmenu)
        pHostSubmenu->setEnabled(m_enmState == State_SessionRunning);

    /*Manage the guest session (login) widget: */
    if (m_pGuestSessionWidget)
    {
        m_pGuestSessionWidget->setLoginWidgetsEnabled(m_enmState == State_SessionPossible ||
                                                      m_enmState == State_SessionRunning ||
                                                      m_enmState == State_SessionError);
        if (m_enmState == State_SessionPossible)
            m_pGuestSessionWidget->switchSessionOpenMode();
        else if (m_enmState == State_SessionRunning)
            m_pGuestSessionWidget->switchSessionCloseMode();
    }
    /* Call to parent: */
    setSessionWidgetsEnabled(m_enmState == State_SessionRunning);

    emit sigStateChanged(m_enmState == State_SessionRunning);
}

bool UIFileManagerGuestTable::isWindowsFileSystem() const
{
    if (!m_comMachine.isOk())
        return false;
    return m_comMachine.GetOSTypeId().contains("windows", Qt::CaseInsensitive);
}

bool UIFileManagerGuestTable::openGuestSession(const QString &strUserName, const QString &strPassword)
{
    if (m_comGuest.isNull())
    {
        emit sigLogOutput("Guest reference is invalid", m_strTableName, FileManagerLogType_Error);
        return false;
    }

    int iGADetectCode = isGuestAdditionsAvailable(pszMinimumGuestAdditionVersion);
    if (iGADetectCode == 0)
    {
        emit sigLogOutput("Could not find Guest Additions",
                          m_strTableName, FileManagerLogType_Error);
        if (m_pGuestSessionWidget)
            m_pGuestSessionWidget->markForError(true);
        return false;
    }
    else if (iGADetectCode == -1)
    {
        emit sigLogOutput(QString("%1 %2").arg("The Guest Additions are older than ").arg(pszMinimumGuestAdditionVersion),
                          m_strTableName, FileManagerLogType_Error);
        if (m_pGuestSessionWidget)
            m_pGuestSessionWidget->markForError(true);
        return false;
    }

    m_comGuestSession = m_comGuest.CreateSession(strUserName, strPassword,
                                                 QString() /* Domain */, "File Manager Session");
    if (m_comGuestSession.isNull())
    {
        emit sigLogOutput("Could not create guest session", m_strTableName, FileManagerLogType_Error);
        return false;
    }

    if (!m_comGuestSession.isOk())
    {
        emit sigLogOutput(UIErrorString::formatErrorInfo(m_comGuestSession), m_strTableName, FileManagerLogType_Error);
        return false;
    }

    QVector<KVBoxEventType> eventTypes(QVector<KVBoxEventType>() << KVBoxEventType_OnGuestSessionStateChanged);
    prepareListener(m_pQtSessionListener, m_comSessionListener, m_comGuestSession.GetEventSource(), eventTypes);
    qRegisterMetaType<CGuestSessionStateChangedEvent>();
    connect(m_pQtSessionListener->getWrapped(), &UIMainEventListener::sigGuestSessionStatedChanged,
            this, &UIFileManagerGuestTable::sltGuestSessionStateChanged);

    return true;
}

void UIFileManagerGuestTable::closeGuestSession()
{
    if (!m_comGuestSession.isNull())
    {
        m_comGuestSession.Close();
        m_comGuestSession.detach();
        emit sigLogOutput("Guest session is closed", m_strTableName, FileManagerLogType_Info);
    }
    reset();
}

void UIFileManagerGuestTable::cleanAll()
{
    cleanupConsoleListener();
    cleanupGuestListener();
    cleanupGuestSessionListener();

    closeGuestSession();
    closeMachineSession();
}

#include "UIFileManagerGuestTable.moc"
