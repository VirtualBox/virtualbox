/* $Id: UIFileManagerPaneContainer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMLogViewer class implementation.
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
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QProgressBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTextEdit>
#include <QTime>

/* GUI includes: */
#include "QILabel.h"
#include "QIToolButton.h"
#include "UIErrorString.h"
#include "UIFileManagerPaneContainer.h"
#include "UIFileManager.h"
#include "UIProgressEventHandler.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include <iprt/assert.h>


/*********************************************************************************************************************************
*   UIFileOperationProgressWidget definition.                                                                                    *
*********************************************************************************************************************************/

class UIFileOperationProgressWidget : public QFrame
{

    Q_OBJECT;

signals:

    void sigProgressComplete(QUuid progressId);
    void sigProgressFail(QString strErrorString, QString strSourceTableName, FileManagerLogType eLogType);
    void sigFocusIn(QWidget *pWidget);
    void sigFocusOut(QWidget *pWidget);

public:

    UIFileOperationProgressWidget(const CProgress &comProgress, const QString &strSourceTableName, QWidget *pParent = 0);
    ~UIFileOperationProgressWidget();
    bool isCompleted() const;
    bool isCanceled() const;

protected:

    virtual void focusInEvent(QFocusEvent *pEvent) RT_OVERRIDE RT_FINAL;
    virtual void focusOutEvent(QFocusEvent *pEvent) RT_OVERRIDE RT_FINAL;

private slots:

    void sltHandleProgressPercentageChange(const QUuid &uProgressId, const int iPercent);
    void sltHandleProgressComplete(const QUuid &uProgressId);
    void sltCancelProgress();
    void sltRetranslateUI();

private:
    enum OperationStatus
    {
        OperationStatus_NotStarted,
        OperationStatus_Working,
        OperationStatus_Paused,
        OperationStatus_Canceled,
        OperationStatus_Succeded,
        OperationStatus_Failed,
        OperationStatus_Invalid,
        OperationStatus_Max
    };

    void prepare();
    void prepareWidgets();
    void prepareEventHandler();
    void cleanupEventHandler();

    OperationStatus         m_eStatus;
    CProgress               m_comProgress;
    UIProgressEventHandler *m_pEventHandler;
    QGridLayout            *m_pMainLayout;
    QProgressBar           *m_pProgressBar;
    QIToolButton           *m_pCancelButton;
    QILabel                *m_pStatusLabel;
    QILabel                *m_pOperationDescriptionLabel;
    /** Name of the table from which this operation has originated. */
    QString                 m_strSourceTableName;
};


/*********************************************************************************************************************************
*   UIFileOperationProgressWidget implementation.                                                                                *
*********************************************************************************************************************************/

UIFileOperationProgressWidget::UIFileOperationProgressWidget(const CProgress &comProgress, const QString &strSourceTableName, QWidget *pParent /* = 0 */)
    : QFrame(pParent)
    , m_eStatus(OperationStatus_NotStarted)
    , m_comProgress(comProgress)
    , m_pEventHandler(0)
    , m_pMainLayout(0)
    , m_pProgressBar(0)
    , m_pCancelButton(0)
    , m_pStatusLabel(0)
    , m_pOperationDescriptionLabel(0)
    , m_strSourceTableName(strSourceTableName)
{
    prepare();
    setFocusPolicy(Qt::ClickFocus);
    setStyleSheet("QFrame:focus {  border-width: 1px; border-style: dashed; border-color: black; }");
}

UIFileOperationProgressWidget::~UIFileOperationProgressWidget()
{
    cleanupEventHandler();
}

bool UIFileOperationProgressWidget::isCompleted() const
{
    if (m_comProgress.isNull())
        return true;
    return m_comProgress.GetCompleted();
}

bool UIFileOperationProgressWidget::isCanceled() const
{
    if (m_comProgress.isNull())
        return true;
    return m_comProgress.GetCanceled();
}

void UIFileOperationProgressWidget::sltRetranslateUI()
{
    if (m_pCancelButton)
        m_pCancelButton->setToolTip(UIFileManager::tr("Cancel"));

    switch (m_eStatus)
    {
        case OperationStatus_NotStarted:
            m_pStatusLabel->setText(UIFileManager::tr("Not yet started"));
            break;
        case OperationStatus_Working:
            m_pStatusLabel->setText(UIFileManager::tr("Working"));
            break;
        case OperationStatus_Paused:
            m_pStatusLabel->setText(UIFileManager::tr("Paused"));
            break;
        case OperationStatus_Canceled:
            m_pStatusLabel->setText(UIFileManager::tr("Canceled"));
            break;
        case OperationStatus_Succeded:
            m_pStatusLabel->setText(UIFileManager::tr("Succeded"));
            break;
        case OperationStatus_Failed:
            m_pStatusLabel->setText(UIFileManager::tr("Failed"));
            break;
        case OperationStatus_Invalid:
        case OperationStatus_Max:
        default:
            m_pStatusLabel->setText(UIFileManager::tr("Invalid"));
            break;
    }
}

void UIFileOperationProgressWidget::focusInEvent(QFocusEvent *pEvent)
{
    QFrame::focusInEvent(pEvent);
    emit sigFocusIn(this);
}

void UIFileOperationProgressWidget::focusOutEvent(QFocusEvent *pEvent)
{
    QFrame::focusOutEvent(pEvent);
    emit sigFocusOut(this);
}

void UIFileOperationProgressWidget::prepare()
{
    prepareWidgets();
    prepareEventHandler();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIFileOperationProgressWidget::sltRetranslateUI);
}

void UIFileOperationProgressWidget::prepareWidgets()
{
    m_pMainLayout = new QGridLayout;
    if (!m_pMainLayout)
        return;
    //m_pMainLayout->setSpacing(0);

    m_pOperationDescriptionLabel = new QILabel;
    if (m_pOperationDescriptionLabel)
    {
        m_pOperationDescriptionLabel->setContextMenuPolicy(Qt::NoContextMenu);
        m_pMainLayout->addWidget(m_pOperationDescriptionLabel, 0, 0, 1, 3);
        if (!m_comProgress.isNull())
            m_pOperationDescriptionLabel->setText(m_comProgress.GetDescription());
    }

    m_pProgressBar = new QProgressBar;
    if (m_pProgressBar)
    {
        m_pProgressBar->setMinimum(0);
        m_pProgressBar->setMaximum(100);
        m_pProgressBar->setTextVisible(true);
        m_pMainLayout->addWidget(m_pProgressBar, 1, 0, 1, 2);
    }

    m_pCancelButton = new QIToolButton;
    if (m_pCancelButton)
    {
        m_pCancelButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DockWidgetCloseButton));
        connect(m_pCancelButton, &QIToolButton::clicked, this, &UIFileOperationProgressWidget::sltCancelProgress);
        if (!m_comProgress.isNull() && !m_comProgress.GetCancelable())
            m_pCancelButton->setEnabled(false);
        m_pMainLayout->addWidget(m_pCancelButton, 1, 2, 1, 1);
    }

    m_pStatusLabel = new QILabel;
    if (m_pStatusLabel)
    {
        m_pStatusLabel->setContextMenuPolicy(Qt::NoContextMenu);
        m_pMainLayout->addWidget(m_pStatusLabel, 1, 3, 1, 1);
    }

    setLayout(m_pMainLayout);
    sltRetranslateUI();
}

void UIFileOperationProgressWidget::prepareEventHandler()
{
    if (m_comProgress.isNull())
        return;
    m_pEventHandler = new UIProgressEventHandler(this, m_comProgress);
    connect(m_pEventHandler, &UIProgressEventHandler::sigProgressPercentageChange,
            this, &UIFileOperationProgressWidget::sltHandleProgressPercentageChange);
    connect(m_pEventHandler, &UIProgressEventHandler::sigProgressTaskComplete,
            this, &UIFileOperationProgressWidget::sltHandleProgressComplete);
    m_eStatus = OperationStatus_Working;
    sltRetranslateUI();
}

void UIFileOperationProgressWidget::cleanupEventHandler()
{
    delete m_pEventHandler;
    m_pEventHandler = 0;
}

void UIFileOperationProgressWidget::sltHandleProgressPercentageChange(const QUuid &uProgressId, const int iPercent)
{
    Q_UNUSED(uProgressId);
    m_pProgressBar->setValue(iPercent);
}

void UIFileOperationProgressWidget::sltHandleProgressComplete(const QUuid &uProgressId)
{
    Q_UNUSED(uProgressId);
    if (m_pCancelButton)
        m_pCancelButton->setEnabled(false);

    if (!m_comProgress.isOk() || m_comProgress.GetResultCode() != 0)
    {
        emit sigProgressFail(UIErrorString::formatErrorInfo(m_comProgress), m_strSourceTableName, FileManagerLogType_Error);
        m_eStatus = OperationStatus_Failed;
    }
    else
    {
        emit sigProgressComplete(m_comProgress.GetId());
        m_eStatus = OperationStatus_Succeded;
    }
    if (m_pProgressBar)
        m_pProgressBar->setValue(100);

    cleanupEventHandler();
    sltRetranslateUI();
}

void UIFileOperationProgressWidget::sltCancelProgress()
{
    m_comProgress.Cancel();
    /* Since we dont have a "progress canceled" event we have to do this here: */
    if (m_pCancelButton)
        m_pCancelButton->setEnabled(false);
    if (m_pProgressBar)
        m_pProgressBar->setEnabled(false);
    m_eStatus = OperationStatus_Canceled;
    cleanupEventHandler();
    sltRetranslateUI();
}

/*********************************************************************************************************************************
*   UIFileManagerLogViewer definition.                                                                                   *
*********************************************************************************************************************************/

class UIFileManagerLogViewer : public QTextEdit
{

    Q_OBJECT;

public:

    UIFileManagerLogViewer(QWidget *pParent = 0);

protected:

    virtual void contextMenuEvent(QContextMenuEvent * event) final override;

private slots:

    void sltClear();
};

/*********************************************************************************************************************************
*   UIFileManagerLogViewer implementation.                                                                                   *
*********************************************************************************************************************************/

UIFileManagerLogViewer::UIFileManagerLogViewer(QWidget *pParent /* = 0 */)
    :QTextEdit(pParent)
{
    setUndoRedoEnabled(false);
    setReadOnly(true);
}

void UIFileManagerLogViewer::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();

    QAction *pClearAction = menu->addAction(UIFileManager::tr("Clear"));
    connect(pClearAction, &QAction::triggered, this, &UIFileManagerLogViewer::sltClear);
    menu->exec(event->globalPos());
    delete menu;
}

void UIFileManagerLogViewer::sltClear()
{
    clear();
}


/*********************************************************************************************************************************
*   UIFileManagerPaneContainer implementation.                                                                                   *
*********************************************************************************************************************************/

UIFileManagerPaneContainer::UIFileManagerPaneContainer(QWidget *pParent, UIFileManagerOptions *pFileManagerOptions)
    : UIPaneContainer(pParent)
    , m_pListDirectoriesOnTopCheckBox(0)
    , m_pDeleteConfirmationCheckBox(0)
    , m_pHumanReabableSizesCheckBox(0)
    , m_pShowHiddenObjectsCheckBox(0)
    , m_pFileManagerOptions(pFileManagerOptions)
    , m_pLogTextEdit(0)
    , m_pScrollArea(0)
    , m_pOperationsTabLayout(0)
    , m_pContainerSpaceItem(0)
    , m_pWidgetInFocus(0)
{
    prepare();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIFileManagerPaneContainer::sltRetranslateUI);
}


void UIFileManagerPaneContainer::prepare()
{
    preparePreferencesTab();
    prepareLogTab();
    prepareOperationsTab();
}

void UIFileManagerPaneContainer::preparePreferencesTab()
{
    QWidget *pPreferencesTab = new QWidget;
    m_pListDirectoriesOnTopCheckBox = new QCheckBox;
    m_pDeleteConfirmationCheckBox = new QCheckBox;
    m_pHumanReabableSizesCheckBox = new QCheckBox;
    m_pShowHiddenObjectsCheckBox = new QCheckBox;

    AssertReturnVoid(pPreferencesTab);
    AssertReturnVoid(m_pListDirectoriesOnTopCheckBox);
    AssertReturnVoid(m_pDeleteConfirmationCheckBox);
    AssertReturnVoid(m_pHumanReabableSizesCheckBox);
    AssertReturnVoid(m_pShowHiddenObjectsCheckBox);

    if (m_pFileManagerOptions)
    {
        if (m_pListDirectoriesOnTopCheckBox)
            m_pListDirectoriesOnTopCheckBox->setChecked(m_pFileManagerOptions->fListDirectoriesOnTop);
        if (m_pDeleteConfirmationCheckBox)
            m_pDeleteConfirmationCheckBox->setChecked(m_pFileManagerOptions->fAskDeleteConfirmation);
        if (m_pHumanReabableSizesCheckBox)
            m_pHumanReabableSizesCheckBox->setChecked(m_pFileManagerOptions->fShowHumanReadableSizes);
        if (m_pShowHiddenObjectsCheckBox)
            m_pShowHiddenObjectsCheckBox->setChecked(m_pFileManagerOptions->fShowHiddenObjects);
    }

    connect(m_pListDirectoriesOnTopCheckBox, &QCheckBox::toggled,
            this, &UIFileManagerPaneContainer::sltListDirectoryCheckBoxToogled);
    connect(m_pDeleteConfirmationCheckBox, &QCheckBox::toggled,
            this, &UIFileManagerPaneContainer::sltDeleteConfirmationCheckBoxToogled);
    connect(m_pHumanReabableSizesCheckBox, &QCheckBox::toggled,
            this, &UIFileManagerPaneContainer::sltHumanReabableSizesCheckBoxToogled);
    connect(m_pShowHiddenObjectsCheckBox, &QCheckBox::toggled,
            this, &UIFileManagerPaneContainer::sltShowHiddenObjectsCheckBoxToggled);

    QGridLayout *pPreferencesLayout = new QGridLayout(pPreferencesTab);
    AssertReturnVoid(pPreferencesLayout);
    //pPreferencesLayout->setContentsMargins(0, 0, 0, 0);

    pPreferencesLayout->addWidget(m_pListDirectoriesOnTopCheckBox, 0, 0, 1, 1);
    pPreferencesLayout->addWidget(m_pDeleteConfirmationCheckBox,   1, 0, 1, 1);
    pPreferencesLayout->addWidget(m_pHumanReabableSizesCheckBox,   0, 1, 1, 1);
    pPreferencesLayout->addWidget(m_pShowHiddenObjectsCheckBox,    1, 1, 1, 1);
    pPreferencesLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding), 2, 0, 1, 2);

    insertTab(Page_Preferences, pPreferencesTab);
}

void UIFileManagerPaneContainer::prepareLogTab()
{
    QWidget *pLogTab = new QWidget;
    AssertReturnVoid(pLogTab);
    QHBoxLayout *pLogLayout = new QHBoxLayout(pLogTab);
    AssertReturnVoid(pLogLayout);
    pLogLayout->setContentsMargins(0, 0, 0, 0);
    m_pLogTextEdit = new UIFileManagerLogViewer;
    if (m_pLogTextEdit)
        pLogLayout->addWidget(m_pLogTextEdit);
    insertTab(Page_Log, pLogTab);
}

void UIFileManagerPaneContainer::prepareOperationsTab()
{
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Active, QPalette::Window, pal.color(QPalette::Active, QPalette::Base));
    setPalette(pal);

    m_pScrollArea = new QScrollArea;
    AssertReturnVoid(m_pScrollArea);
    QWidget *pOperationsTab = new QWidget;
    AssertReturnVoid(pOperationsTab);
    m_pOperationsTabLayout = new QVBoxLayout;
    AssertReturnVoid(m_pOperationsTabLayout);

    QScrollBar *pVerticalScrollBar = m_pScrollArea->verticalScrollBar();
    if (pVerticalScrollBar)
        QObject::connect(pVerticalScrollBar, &QScrollBar::rangeChanged, this, &UIFileManagerPaneContainer::sltScrollToBottom);

    m_pScrollArea->setBackgroundRole(QPalette::Window);
    m_pScrollArea->setWidgetResizable(true);

    //mainLayout()->addWidget(m_pScrollArea);

    m_pScrollArea->setWidget(pOperationsTab);
    pOperationsTab->setLayout(m_pOperationsTabLayout);
    m_pOperationsTabLayout->addStretch(4);
    insertTab(Page_Operations, m_pScrollArea);
}

void UIFileManagerPaneContainer::sltListDirectoryCheckBoxToogled(bool bChecked)
{
    if (!m_pFileManagerOptions)
        return;
    m_pFileManagerOptions->fListDirectoriesOnTop = bChecked;
    emit sigOptionsChanged();
}

void UIFileManagerPaneContainer::sltDeleteConfirmationCheckBoxToogled(bool bChecked)
{
    if (!m_pFileManagerOptions)
        return;
    m_pFileManagerOptions->fAskDeleteConfirmation = bChecked;
    emit sigOptionsChanged();
}

void UIFileManagerPaneContainer::sltHumanReabableSizesCheckBoxToogled(bool bChecked)
{
    if (!m_pFileManagerOptions)
        return;
    m_pFileManagerOptions->fShowHumanReadableSizes = bChecked;
    emit sigOptionsChanged();
}

void UIFileManagerPaneContainer::sltShowHiddenObjectsCheckBoxToggled(bool bChecked)
{
    if (!m_pFileManagerOptions)
        return;
    m_pFileManagerOptions->fShowHiddenObjects = bChecked;
    emit sigOptionsChanged();
}

void UIFileManagerPaneContainer::sltRetranslateUI()
{
    if (m_pListDirectoriesOnTopCheckBox)
    {
        m_pListDirectoriesOnTopCheckBox->setText(QApplication::translate("UIFileManager", "List directories on top"));
        m_pListDirectoriesOnTopCheckBox->setToolTip(QApplication::translate("UIFileManager", "List directories before files"));
    }

    if (m_pDeleteConfirmationCheckBox)
    {
        m_pDeleteConfirmationCheckBox->setText(QApplication::translate("UIFileManager", "Ask before delete"));
        m_pDeleteConfirmationCheckBox->setToolTip(QApplication::translate("UIFileManager", "Show a confirmation dialog "
                                                                                "before deleting files and directories"));
    }

    if (m_pHumanReabableSizesCheckBox)
    {
        m_pHumanReabableSizesCheckBox->setText(QApplication::translate("UIFileManager", "Human readable sizes"));
        m_pHumanReabableSizesCheckBox->setToolTip(QApplication::translate("UIFileManager", "Show file/directory sizes in human "
                                                                                "readable format rather than in bytes"));
    }

    if (m_pShowHiddenObjectsCheckBox)
    {
        m_pShowHiddenObjectsCheckBox->setText(QApplication::translate("UIFileManager", "Show hidden objects"));
        m_pShowHiddenObjectsCheckBox->setToolTip(QApplication::translate("UIFileManager", "Show hidden files/directories"));
    }

    setTabText(Page_Preferences, QApplication::translate("UIFileManager", "Preferences"));
    setTabText(Page_Log, QApplication::translate("UIFileManager", "Log"));
    setTabText(Page_Operations, QApplication::translate("UIFileManager", "Operations"));
}

void UIFileManagerPaneContainer::updatePreferences()
{
    if (!m_pFileManagerOptions)
        return;

    if (m_pListDirectoriesOnTopCheckBox)
    {
        m_pListDirectoriesOnTopCheckBox->blockSignals(true);
        m_pListDirectoriesOnTopCheckBox->setChecked(m_pFileManagerOptions->fListDirectoriesOnTop);
        m_pListDirectoriesOnTopCheckBox->blockSignals(false);
    }

    if (m_pDeleteConfirmationCheckBox)
    {
        m_pDeleteConfirmationCheckBox->blockSignals(true);
        m_pDeleteConfirmationCheckBox->setChecked(m_pFileManagerOptions->fAskDeleteConfirmation);
        m_pDeleteConfirmationCheckBox->blockSignals(false);
    }

    if (m_pHumanReabableSizesCheckBox)
    {
        m_pHumanReabableSizesCheckBox->blockSignals(true);
        m_pHumanReabableSizesCheckBox->setChecked(m_pFileManagerOptions->fShowHumanReadableSizes);
        m_pHumanReabableSizesCheckBox->blockSignals(false);
    }

    if (m_pShowHiddenObjectsCheckBox)
    {
        m_pShowHiddenObjectsCheckBox->blockSignals(true);
        m_pShowHiddenObjectsCheckBox->setChecked(m_pFileManagerOptions->fShowHiddenObjects);
        m_pShowHiddenObjectsCheckBox->blockSignals(false);
    }
}

void UIFileManagerPaneContainer::appendLog(const QString &strLog, const QString &strMachineName, FileManagerLogType eLogType)
{
    if (!m_pLogTextEdit)
        return;
    QString strStartTag("<font color=\"Black\">");
    QString strEndTag("</font>");
    if (eLogType == FileManagerLogType_Error)
    {
        strStartTag = "<b><font color=\"Red\">";
        strEndTag = "</font></b>";
    }
    QString strColoredLog = QString("%1 %2: %3 %4 %5").arg(strStartTag).arg(QTime::currentTime().toString("hh:mm:ss:z")).arg(strMachineName).arg(strLog).arg(strEndTag);
    m_pLogTextEdit->append(strColoredLog);
    m_pLogTextEdit->moveCursor(QTextCursor::End);
    m_pLogTextEdit->ensureCursorVisible();
}

void UIFileManagerPaneContainer::addNewProgress(const CProgress &comProgress, const QString &strSourceTableName)
{
    if (!m_pOperationsTabLayout)
        return;

    UIFileOperationProgressWidget *pOperationsWidget = new UIFileOperationProgressWidget(comProgress, strSourceTableName);
    if (!pOperationsWidget)
        return;
    m_widgetSet.insert(pOperationsWidget);
    m_pOperationsTabLayout->insertWidget(m_pOperationsTabLayout->count() - 1, pOperationsWidget);

    connect(pOperationsWidget, &UIFileOperationProgressWidget::sigProgressComplete,
            this, &UIFileManagerPaneContainer::sigFileOperationComplete);
    connect(pOperationsWidget, &UIFileOperationProgressWidget::sigProgressFail,
            this, &UIFileManagerPaneContainer::sigFileOperationFail);

    connect(pOperationsWidget, &UIFileOperationProgressWidget::sigFocusIn,
            this, &UIFileManagerPaneContainer::sltHandleWidgetFocusIn);
    connect(pOperationsWidget, &UIFileOperationProgressWidget::sigFocusOut,
            this, &UIFileManagerPaneContainer::sltHandleWidgetFocusOut);
}

void UIFileManagerPaneContainer::contextMenuEvent(QContextMenuEvent *pEvent)
{
    QMenu *menu = new QMenu(this);

    if (m_pWidgetInFocus)
    {
        QAction *pRemoveSelected = menu->addAction(UIFileManager::tr("Remove Selected"));
        connect(pRemoveSelected, &QAction::triggered,
                this, &UIFileManagerPaneContainer::sltRemoveSelected);
    }

    QAction *pRemoveFinished = menu->addAction(UIFileManager::tr("Remove Finished"));
    QAction *pRemoveAll = menu->addAction(UIFileManager::tr("Remove All"));

    connect(pRemoveFinished, &QAction::triggered,
            this, &UIFileManagerPaneContainer::sltRemoveFinished);
    connect(pRemoveAll, &QAction::triggered,
            this, &UIFileManagerPaneContainer::sltRemoveAll);

    menu->exec(pEvent->globalPos());
    delete menu;
}

void UIFileManagerPaneContainer::sltRemoveFinished()
{
    QList<UIFileOperationProgressWidget*> widgetsToRemove;
    foreach (QWidget *pWidget, m_widgetSet)
    {
        UIFileOperationProgressWidget *pProgressWidget = qobject_cast<UIFileOperationProgressWidget*>(pWidget);
        if (pProgressWidget && pProgressWidget->isCompleted())
        {
            delete pProgressWidget;
            widgetsToRemove << pProgressWidget;
        }
    }
    foreach (UIFileOperationProgressWidget *pWidget, widgetsToRemove)
        m_widgetSet.remove(pWidget);
}

void UIFileManagerPaneContainer::sltRemoveAll()
{
    foreach (QWidget *pWidget, m_widgetSet)
    {
        if (pWidget)
        {
            delete pWidget;
        }
    }
    m_widgetSet.clear();
}

void UIFileManagerPaneContainer::sltRemoveSelected()
{
    if (!m_pWidgetInFocus)
        return;
    delete m_pWidgetInFocus;
    m_widgetSet.remove(m_pWidgetInFocus);
}

void UIFileManagerPaneContainer::sltHandleWidgetFocusIn(QWidget *pWidget)
{
    if (!pWidget)
        return;
    m_pWidgetInFocus = pWidget;
}

void UIFileManagerPaneContainer::sltHandleWidgetFocusOut(QWidget *pWidget)
{
    if (!pWidget)
        return;
    m_pWidgetInFocus = 0;
}

void UIFileManagerPaneContainer::sltScrollToBottom(int iMin, int iMax)
{
    Q_UNUSED(iMin);
    if (m_pScrollArea)
    m_pScrollArea->verticalScrollBar()->setValue(iMax);
}


#include "UIFileManagerPaneContainer.moc"
