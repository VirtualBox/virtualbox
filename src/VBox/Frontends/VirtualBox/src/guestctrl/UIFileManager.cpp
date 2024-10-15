/* $Id: UIFileManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileManager class implementation.
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
#include <QHBoxLayout>
#include <QPushButton>
#include <QSplitter>

/* GUI includes: */
#include "QITabWidget.h"
#include "QITreeWidget.h"
#include "QIToolBar.h"
#include "UIActionPool.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIErrorString.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIFileManager.h"
#include "UIFileManagerPaneContainer.h"
#include "UIFileManagerGuestTable.h"
#include "UIFileManagerHostTable.h"
#include "UIGuestControlInterface.h"
#include "UIVirtualMachineItem.h"

/* COM includes: */
#include "CConsole.h"
#include "CFsObjInfo.h"
#include "CGuestDirectory.h"
#include "CGuestFsObjInfo.h"
#include "CGuestSession.h"


/*********************************************************************************************************************************
*   UIFileOperationsList definition.                                                                                   *
*********************************************************************************************************************************/

class UIFileOperationsList : public QITreeWidget
{
    Q_OBJECT;
public:

    UIFileOperationsList(QWidget *pParent = 0);
};


/*********************************************************************************************************************************
*   UIFileManagerOptions implementation.                                                                             *
*********************************************************************************************************************************/

UIFileManagerOptions *UIFileManagerOptions::m_pInstance = 0;

UIFileManagerOptions* UIFileManagerOptions::instance()
{
    if (!m_pInstance)
    m_pInstance = new UIFileManagerOptions;
    return m_pInstance;
}

void UIFileManagerOptions::create()
{
    if (m_pInstance)
        return;
    m_pInstance = new UIFileManagerOptions;
}

void UIFileManagerOptions::destroy()
{
    delete m_pInstance;
    m_pInstance = 0;
}

 UIFileManagerOptions::~UIFileManagerOptions()
{
}

UIFileManagerOptions::UIFileManagerOptions()
    : fListDirectoriesOnTop(true)
    , fAskDeleteConfirmation(false)
    , fShowHumanReadableSizes(true)
    , fShowHiddenObjects(true)
{
}

/*********************************************************************************************************************************
*   UIFileOperationsList implementation.                                                                                   *
*********************************************************************************************************************************/

UIFileOperationsList::UIFileOperationsList(QWidget *pParent)
    :QITreeWidget(pParent)
{}


/*********************************************************************************************************************************
*   UIFileManager implementation.                                                                                    *
*********************************************************************************************************************************/

UIFileManager::UIFileManager(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                             const CMachine &comMachine, QWidget *pParent, bool fShowToolbar)
    : QWidget(pParent)
    , m_pMainLayout(0)
    , m_pVerticalSplitter(0)
    , m_pFileTableSplitter(0)
    , m_pToolBar(0)
    , m_pVerticalToolBar(0)
    , m_pHostFileTable(0)
    , m_pGuestTablesContainer(0)
    , m_enmEmbedding(enmEmbedding)
    , m_pActionPool(pActionPool)
    , m_fShowToolbar(fShowToolbar)
    , m_pLogPanel(0)
    , m_pPanel(0)
    , m_fCommitDataSignalReceived(false)
{
    loadOptions();
    prepareObjects();
    prepareConnections();
    restorePanelVisibility();
    UIFileManagerOptions::create();
    uiCommon().setHelpKeyword(this, "guestadd-gc-file-manager");

    if (!comMachine.isNull())
        setMachines( QVector<QUuid>() << comMachine.GetId());
}

UIFileManager::~UIFileManager()
{
    UIFileManagerOptions::destroy();
    if (m_pGuestTablesContainer)
    {
        for (int i = 0; i < m_pGuestTablesContainer->count(); ++i)
        {
            UIFileManagerGuestTable *pTable = qobject_cast<UIFileManagerGuestTable*>(m_pGuestTablesContainer->widget(i));
            if (pTable)
                pTable->disconnect();
        }
    }
}

QMenu *UIFileManager::menu() const
{
    if (!m_pActionPool)
        return 0;
    return m_pActionPool->action(UIActionIndex_M_FileManager)->menu();
}

void UIFileManager::prepareObjects()
{
    /* m_pMainLayout is the outer most layout containing the main toolbar and splitter widget: */
    m_pMainLayout = new QVBoxLayout(this);
    if (!m_pMainLayout)
        return;

    /* Configure layout: */
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
    m_pMainLayout->setSpacing(10);
#else
    m_pMainLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif

    if (m_fShowToolbar)
        prepareToolBar();

    QWidget *pTopWidget = new QWidget;
    QVBoxLayout *pTopLayout = new QVBoxLayout;
    pTopLayout->setSpacing(0);
    pTopLayout->setContentsMargins(0, 0, 0, 0);
    pTopWidget->setLayout(pTopLayout);

    m_pFileTableSplitter = new QSplitter;

    if (m_pFileTableSplitter)
    {
        m_pFileTableSplitter->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        m_pFileTableSplitter->setContentsMargins(0, 0, 0, 0);

        /* This widget hosts host file table and vertical toolbar. */
        QWidget *pHostTableAndVerticalToolbarWidget = new QWidget;
        QHBoxLayout *pHostTableAndVerticalToolbarLayout = new QHBoxLayout(pHostTableAndVerticalToolbarWidget);
        pHostTableAndVerticalToolbarLayout->setSpacing(0);
        pHostTableAndVerticalToolbarLayout->setContentsMargins(0, 0, 0, 0);

        m_pHostFileTable = new UIFileManagerHostTable(m_pActionPool);
        if (m_pHostFileTable)
            pHostTableAndVerticalToolbarLayout->addWidget(m_pHostFileTable);

        m_pFileTableSplitter->addWidget(pHostTableAndVerticalToolbarWidget);
        prepareVerticalToolBar(pHostTableAndVerticalToolbarLayout);

        m_pGuestTablesContainer = new QITabWidget;
        if (m_pGuestTablesContainer)
        {
            m_pGuestTablesContainer->setTabPosition(QTabWidget::East);
            m_pGuestTablesContainer->setTabBarAutoHide(true);
            m_pFileTableSplitter->addWidget(m_pGuestTablesContainer);
        }
        m_pFileTableSplitter->setStretchFactor(0, 1);
        m_pFileTableSplitter->setStretchFactor(1, 1);
    }

    pTopLayout->addWidget(m_pFileTableSplitter);
    for (int i = 0; i < m_pFileTableSplitter->count(); ++i)
        m_pFileTableSplitter->setCollapsible(i, false);

    m_pVerticalSplitter = new QSplitter;
    if (m_pVerticalSplitter)
    {
        m_pMainLayout->addWidget(m_pVerticalSplitter);
        m_pVerticalSplitter->setOrientation(Qt::Vertical);
        m_pVerticalSplitter->setHandleWidth(4);

        m_pVerticalSplitter->addWidget(pTopWidget);

        for (int i = 0; i < m_pVerticalSplitter->count(); ++i)
            m_pVerticalSplitter->setCollapsible(i, false);
        m_pVerticalSplitter->setStretchFactor(0, 3);
        m_pVerticalSplitter->setStretchFactor(1, 1);
        m_pVerticalSplitter->setStretchFactor(2, 1);
    }

    m_pPanel = new UIFileManagerPaneContainer(this, UIFileManagerOptions::instance());
    AssertReturnVoid(m_pPanel);

    m_panelActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_T_Preferences));
    m_panelActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_T_Log));
    m_panelActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_T_Operations));

    m_pActionPool->action(UIActionIndex_M_FileManager_T_Preferences)->setData(static_cast<int>(UIFileManagerPaneContainer::Page_Preferences));
    m_pActionPool->action(UIActionIndex_M_FileManager_T_Log)->setData(static_cast<int>(UIFileManagerPaneContainer::Page_Log));
    m_pActionPool->action(UIActionIndex_M_FileManager_T_Operations)->setData(static_cast<int>(UIFileManagerPaneContainer::Page_Operations));

    m_pVerticalSplitter->addWidget(m_pPanel);
    m_pPanel->hide();
}

void UIFileManager::prepareVerticalToolBar(QHBoxLayout *layout)
{
    m_pVerticalToolBar = new QIToolBar;
    if (!m_pVerticalToolBar && !m_pActionPool)
        return;

    m_pVerticalToolBar->setOrientation(Qt::Vertical);

    /* Add to dummy QWidget to toolbar to center the action icons vertically: */
    QWidget *topSpacerWidget = new QWidget(this);
    topSpacerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    topSpacerWidget->setVisible(true);
    QWidget *bottomSpacerWidget = new QWidget(this);
    bottomSpacerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    bottomSpacerWidget->setVisible(true);

    m_pVerticalToolBar->addWidget(topSpacerWidget);
    if (m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost))
    {
        m_pVerticalToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost));
        m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost)->setEnabled(false);
    }
    if (m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest))
    {
        m_pVerticalToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest));
        m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest)->setEnabled(false);
    }

    m_pVerticalToolBar->addWidget(bottomSpacerWidget);

    layout ->addWidget(m_pVerticalToolBar);
}

void UIFileManager::prepareConnections()
{
    if (m_pActionPool)
    {
        if (m_pActionPool->action(UIActionIndex_M_FileManager_T_Preferences))
            connect(m_pActionPool->action(UIActionIndex_M_FileManager_T_Preferences), &QAction::toggled,
                    this, &UIFileManager::sltPanelActionToggled);
        if (m_pActionPool->action(UIActionIndex_M_FileManager_T_Log))
            connect(m_pActionPool->action(UIActionIndex_M_FileManager_T_Log), &QAction::toggled,
                    this, &UIFileManager::sltPanelActionToggled);
        if (m_pActionPool->action(UIActionIndex_M_FileManager_T_Operations))
            connect(m_pActionPool->action(UIActionIndex_M_FileManager_T_Operations), &QAction::toggled,
                    this, &UIFileManager::sltPanelActionToggled);
        if (m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost))
            connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost), &QAction::triggered,
                    this, &UIFileManager::sltCopyGuestToHost);
        if (m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest))
            connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest), &QAction::triggered,
                    this, &UIFileManager::sltCopyHostToGuest);
    }

    if (m_pPanel)
    {
        connect(m_pPanel, &UIFileManagerPaneContainer::sigOptionsChanged,
                this, &UIFileManager::sltHandleOptionsUpdated);
        connect(m_pPanel, &UIFileManagerPaneContainer::sigFileOperationComplete,
                this, &UIFileManager::sltFileOperationComplete);
        connect(m_pPanel, &UIFileManagerPaneContainer::sigFileOperationFail,
                this, &UIFileManager::sltReceieveLogOutput);
        connect(m_pPanel, &UIFileManagerPaneContainer::sigCurrentTabChanged,
                this, &UIFileManager::sltPanelCurrentTabChanged);
        connect(m_pPanel, &UIFileManagerPaneContainer::sigHidden,
                this, &UIFileManager::sltPanelContainerHidden);
    }

    if (m_pHostFileTable)
    {
        connect(m_pHostFileTable, &UIFileManagerHostTable::sigLogOutput,
                this, &UIFileManager::sltReceieveLogOutput);
        connect(m_pHostFileTable, &UIFileManagerHostTable::sigDeleteConfirmationOptionChanged,
                this, &UIFileManager::sltHandleOptionsUpdated);
        connect(m_pHostFileTable, &UIFileManagerGuestTable::sigSelectionChanged,
                this, &UIFileManager::sltFileTableSelectionChanged);
    }
    if (m_pGuestTablesContainer)
        connect(m_pGuestTablesContainer, &QITabWidget::currentChanged, this,
                &UIFileManager::sltCurrentTabChanged);

    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIFileManager::sltCommitDataSignalReceived);
}

void UIFileManager::prepareToolBar()
{
    /* Create toolbar: */
    m_pToolBar = new QIToolBar(parentWidget());
    if (m_pToolBar)
    {
        /* Configure toolbar: */
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_T_Preferences));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_T_Operations));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_T_Log));

#ifdef VBOX_WS_MAC
        /* Check whether we are embedded into a stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Add into layout: */
            m_pMainLayout->addWidget(m_pToolBar);
        }
#else
        /* Add into layout: */
        m_pMainLayout->addWidget(m_pToolBar);
#endif
    }
}

void UIFileManager::sltReceieveLogOutput(QString strOutput, const QString &strMachineName, FileManagerLogType eLogType)
{
    appendLog(strOutput, strMachineName, eLogType);
}

void UIFileManager::sltCopyGuestToHost()
{
    copyToHost();
}

void UIFileManager::sltCopyHostToGuest()
{
    copyToGuest();
}

void UIFileManager::sltPanelActionToggled(bool fChecked)
{
    QAction *pSenderAction = qobject_cast<QAction*>(sender());
    if (!pSenderAction)
        return;

    m_pPanel->setVisible(fChecked);

    if (fChecked)
    {
        /* Make sure only pSenderAction is toggled on. */
        for(QSet<QAction*>::iterator iter = m_panelActions.begin(); iter != m_panelActions.end(); ++iter)
        {
            QAction *pAction = *iter;
            if (!pAction || pAction == pSenderAction)
                continue;
            pAction->blockSignals(true);
            pAction->setChecked(false);
            pAction->blockSignals(false);
        }
        m_pPanel->blockSignals(true);
        m_pPanel->setCurrentIndex(pSenderAction->data().toInt());
        m_pPanel->blockSignals(false);
    }

    // UIDialogPanel* pPanel = 0;
    // /* Look for the sender() within the m_panelActionMap's values: */
    // for (QMap<UIDialogPanel*, QAction*>::const_iterator iterator = m_panelActionMap.begin();
    //     iterator != m_panelActionMap.end(); ++iterator)
    // {
    //     if (iterator.value() == pSenderAction)
    //         pPanel = iterator.key();
    // }
    // if (!pPanel)
    //     return;
    // if (fChecked)
    //     showPanel(pPanel);
    // else
    //     hidePanel(pPanel);
}

void UIFileManager::sltReceieveNewFileOperation(const CProgress &comProgress, const QString &strTableName)
{
    if (m_pPanel)
        m_pPanel->addNewProgress(comProgress, strTableName);
}

void UIFileManager::sltFileOperationComplete(QUuid progressId)
{
    Q_UNUSED(progressId);
    if (m_pHostFileTable)
        m_pHostFileTable->refresh();
    /// @todo we need to refresh only the table from which the completed file operation has originated
    for (int i = 0; i < m_pGuestTablesContainer->count(); ++i)
    {
        UIFileManagerGuestTable *pTable = qobject_cast<UIFileManagerGuestTable*>(m_pGuestTablesContainer->widget(i));
        if (pTable)
            pTable->refresh();
    }
}

void UIFileManager::sltHandleHidePanel(UIDialogPanel *pPanel)
{
    hidePanel(pPanel);
}

void UIFileManager::sltHandleShowPanel(UIDialogPanel *pPanel)
{
    showPanel(pPanel);
}

void UIFileManager::sltCommitDataSignalReceived()
{
    m_fCommitDataSignalReceived = true;
}

void UIFileManager::sltFileTableSelectionChanged(bool fHasSelection)
{
    /* If we dont have a guest session running that actions should stay disabled: */
    if (!currentGuestTable() || !currentGuestTable()->isGuestSessionRunning())
    {
        m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest)->setEnabled(false);
        m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost)->setEnabled(false);
        return;
    }

    /* Enable/disable vertical toolbar actions: */
    UIFileManagerGuestTable *pGuestTable = qobject_cast<UIFileManagerGuestTable*>(sender());

    /* If the signal is coming from a guest table which is not the current one just dont do anything: */
    if (pGuestTable && pGuestTable != currentGuestTable())
        return;

    if (pGuestTable)
    {
        if (m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost))
            m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost)->setEnabled(fHasSelection);
        return;
    }

    if (sender() == m_pHostFileTable && m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest))
        m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest)->setEnabled(fHasSelection);
}

void UIFileManager::sltCurrentTabChanged(int iIndex)
{
    Q_UNUSED(iIndex);
    setVerticalToolBarActionsEnabled();

    /* Mark the current guest table: */
    UIFileManagerGuestTable *pCurrentGuestTable = currentGuestTable();
    if (!pCurrentGuestTable)
        return;
    for (int i = 0; i < m_pGuestTablesContainer->count(); ++i)
    {
        UIFileManagerGuestTable *pTable = qobject_cast<UIFileManagerGuestTable*>(m_pGuestTablesContainer->widget(i));
        if (!pTable)
            continue;
        pTable->setIsCurrent(pTable == pCurrentGuestTable);
    }
    /* Disable host file table if guest session is not running: */
    if (m_pHostFileTable)
        m_pHostFileTable->setEnabled(pCurrentGuestTable->isGuestSessionRunning());
    /* Disable/enable file table submenus of the menu: */
    UIMenu *pGuestSubmenu = m_pActionPool->action(UIActionIndex_M_FileManager_M_GuestSubmenu)->menu();
    if (pGuestSubmenu)
        pGuestSubmenu->setEnabled(pCurrentGuestTable->isGuestSessionRunning());
    UIMenu *pHostSubmenu = m_pActionPool->action(UIActionIndex_M_FileManager_M_HostSubmenu)->menu();
    if (pHostSubmenu)
        pHostSubmenu->setEnabled(pCurrentGuestTable->isGuestSessionRunning());
}

void UIFileManager::sltGuestFileTableStateChanged(bool fIsRunning)
{
    if (m_pHostFileTable)
        m_pHostFileTable->setEnabled(fIsRunning);
}

void UIFileManager::sltHandleOptionsUpdated()
{
    if (m_pPanel)
        m_pPanel->updatePreferences();

    for (int i = 0; i < m_pGuestTablesContainer->count(); ++i)
    {
        UIFileManagerGuestTable *pTable = qobject_cast<UIFileManagerGuestTable*>(m_pGuestTablesContainer->widget(i));
        if (pTable)
            pTable->optionsUpdated();
    }
    if (m_pHostFileTable)
        m_pHostFileTable->optionsUpdated();
    saveOptions();
}

void UIFileManager::sltPanelCurrentTabChanged(int iIndex)
{
    if (!m_pPanel || !m_pPanel->isVisible())
        return;

    for(QSet<QAction*>::iterator iter = m_panelActions.begin(); iter != m_panelActions.end(); ++iter)
    {
        QAction *pAction = *iter;

        pAction->blockSignals(true);
        pAction->setChecked(false);
        pAction->blockSignals(false);
    }

    switch (static_cast<UIFileManagerPaneContainer::Page>(iIndex))
    {
        case UIFileManagerPaneContainer::Page_Preferences:
            m_pActionPool->action(UIActionIndex_M_FileManager_T_Preferences)->setChecked(true);
            break;
        case UIFileManagerPaneContainer::Page_Operations:
            m_pActionPool->action(UIActionIndex_M_FileManager_T_Operations)->setChecked(true);
            break;
        case UIFileManagerPaneContainer::Page_Log:
            m_pActionPool->action(UIActionIndex_M_FileManager_T_Log)->setChecked(true);
            break;
        case UIFileManagerPaneContainer::Page_Max:
        default:
            break;
    }
}

void UIFileManager::sltPanelContainerHidden()
{
    foreach (QAction *pPanelAction, m_panelActions)
    {
        if (!pPanelAction)
            continue;
        pPanelAction->blockSignals(true);
        pPanelAction->setChecked(false);
        pPanelAction->blockSignals(false);
    }
}

void UIFileManager::setVerticalToolBarActionsEnabled()
{
    if (!m_pGuestTablesContainer)
        return;
    UIFileManagerGuestTable *pTable = currentGuestTable();
    if (!pTable)
        return;

    bool fRunning = pTable->isGuestSessionRunning();
    if (m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost))
        m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToHost)->setEnabled(fRunning && pTable->hasSelection());

    if (m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest))
    {
        bool fHostHasSelection = m_pHostFileTable ? m_pHostFileTable->hasSelection() : false;
        m_pActionPool->action(UIActionIndex_M_FileManager_S_CopyToGuest)->setEnabled(fRunning && fHostHasSelection);
    }
}

void UIFileManager::copyToHost()
{
    if (m_pGuestTablesContainer && m_pHostFileTable)
    {
        UIFileManagerGuestTable *pGuestFileTable = currentGuestTable();
        if (pGuestFileTable)
            pGuestFileTable->copyGuestToHost(m_pHostFileTable->currentDirectoryPath());
    }
}

void UIFileManager::copyToGuest()
{
    if (m_pGuestTablesContainer && m_pHostFileTable)
    {
        UIFileManagerGuestTable *pGuestFileTable = currentGuestTable();
        if (pGuestFileTable)
            pGuestFileTable->copyHostToGuest(m_pHostFileTable->selectedItemPathList());
    }
}

template<typename T>
QStringList UIFileManager::getFsObjInfoStringList(const T &fsObjectInfo) const
{
    QStringList objectInfo;
    if (!fsObjectInfo.isOk())
        return objectInfo;
    objectInfo << fsObjectInfo.GetName();
    return objectInfo;
}

void UIFileManager::saveOptions()
{
    if (m_fCommitDataSignalReceived)
        return;
    /* Save the options: */
    UIFileManagerOptions *pOptions = UIFileManagerOptions::instance();
    if (pOptions)
    {
        gEDataManager->setFileManagerOptions(pOptions->fListDirectoriesOnTop,
                                             pOptions->fAskDeleteConfirmation,
                                             pOptions->fShowHumanReadableSizes,
                                             pOptions->fShowHiddenObjects);
    }
}

void UIFileManager::restorePanelVisibility()
{
    /** Make sure the actions are set to not-checked. this prevents an unlikely
     *  bug when the extrakey for the visible panels are manually modified: */
    // foreach(QAction* pAction, m_panelActionMap.values())
    // {
    //     pAction->blockSignals(true);
    //     pAction->setChecked(false);
    //     pAction->blockSignals(false);
    // }
    /* Load the visible panel list and show them: */
    // QStringList strNameList = gEDataManager->fileManagerVisiblePanels();
    // foreach(const QString strName, strNameList)
    // {
    //     foreach(UIDialogPanel* pPanel, m_panelActionMap.keys())
    //     {
    //         if (strName == pPanel->panelName())
    //         {
    //             showPanel(pPanel);
    //             break;
    //         }
    //     }
    // }
}

void UIFileManager::loadOptions()
{
    /* Load options: */
    UIFileManagerOptions *pOptions = UIFileManagerOptions::instance();
    if (pOptions)
    {
        pOptions->fListDirectoriesOnTop = gEDataManager->fileManagerListDirectoriesFirst();
        pOptions->fAskDeleteConfirmation = gEDataManager->fileManagerShowDeleteConfirmation();
        pOptions->fShowHumanReadableSizes = gEDataManager->fileManagerShowHumanReadableSizes();
        pOptions->fShowHiddenObjects = gEDataManager->fileManagerShowHiddenObjects();
    }
}

void UIFileManager::hidePanel(UIDialogPanel* panel)
{
    Q_UNUSED(panel);
    // if (!m_pActionPool)
    //     return;
    // if (panel && panel->isVisible())
    //     panel->setVisible(false);
    // QMap<UIDialogPanel*, QAction*>::iterator iterator = m_panelActionMap.find(panel);
    // if (iterator != m_panelActionMap.end())
    // {
    //     if (iterator.value() && iterator.value()->isChecked())
    //         iterator.value()->setChecked(false);
    // }
    // m_visiblePanelsList.removeAll(panel);
    // manageEscapeShortCut();
    // savePanelVisibility();
}

void UIFileManager::showPanel(UIDialogPanel* panel)
{
    Q_UNUSED(panel);
    // if (panel && panel->isHidden())
    //     panel->setVisible(true);
    // QMap<UIDialogPanel*, QAction*>::iterator iterator = m_panelActionMap.find(panel);
    // if (iterator != m_panelActionMap.end())
    // {
    //     if (!iterator.value()->isChecked())
    //         iterator.value()->setChecked(true);
    // }
    // if (!m_visiblePanelsList.contains(panel))
    //     m_visiblePanelsList.push_back(panel);
    // manageEscapeShortCut();
    // savePanelVisibility();
}

void UIFileManager::manageEscapeShortCut()
{
    /* if there is no visible panels give the escape shortcut to parent dialog: */
    // if (m_visiblePanelsList.isEmpty())
    // {
    //     emit sigSetCloseButtonShortCut(QKeySequence(Qt::Key_Escape));
    //     return;
    // }
    // /* Take the escape shortcut from the dialog: */
    // emit sigSetCloseButtonShortCut(QKeySequence());
    // /* Just loop thru the visible panel list and set the esc key to the
    //    panel which made visible latest */
    // for (int i = 0; i < m_visiblePanelsList.size() - 1; ++i)
    //     m_visiblePanelsList[i]->setCloseButtonShortCut(QKeySequence());

    // m_visiblePanelsList.back()->setCloseButtonShortCut(QKeySequence(Qt::Key_Escape));
}

void UIFileManager::appendLog(const QString &strLog, const QString &strMachineName, FileManagerLogType eLogType)
{
    if (m_pPanel)
        m_pPanel->appendLog(strLog, strMachineName, eLogType);
}

void UIFileManager::savePanelVisibility()
{
    if (m_fCommitDataSignalReceived)
        return;
    // /* Save a list of currently visible panels: */
    // QStringList strNameList;
    // foreach(UIDialogPanel* pPanel, m_visiblePanelsList)
    //     strNameList.append(pPanel->panelName());
    // gEDataManager->setFileManagerVisiblePanels(strNameList);
}

void UIFileManager::setSelectedVMListItems(const QList<UIVirtualMachineItem*> &items)
{
    AssertReturnVoid(m_pGuestTablesContainer);
    QVector<QUuid> selectedMachines;

    foreach (const UIVirtualMachineItem *item, items)
    {
        if (!item)
            continue;
        selectedMachines << item->id();
    }
    QUuid lastSelection = selectedMachines.isEmpty() ? QUuid() : selectedMachines.last();
    /** Iterate through the current tabs and add any machine id for which we have a running guest session to the
      * list of machine ids we want to have a tab for: */
    for (int i = 0; i < m_pGuestTablesContainer->count(); ++i)
    {
        UIFileManagerGuestTable *pTable = qobject_cast<UIFileManagerGuestTable*>(m_pGuestTablesContainer->widget(i));
        if (!pTable || !pTable->isGuestSessionRunning())
            continue;
        if (!selectedMachines.contains(pTable->machineId()))
            selectedMachines << pTable->machineId();
    }

    setMachines(selectedMachines, lastSelection);
}

void UIFileManager::setMachines(const QVector<QUuid> &machineIds, const QUuid &lastSelectedMachineId /* = QUuid() */)
{
    AssertReturnVoid(m_pGuestTablesContainer);

    /* List of machines that are newly added to selected machine list: */
    QVector<QUuid> newSelections;
    QVector<QUuid> unselectedMachines(m_machineIds);

    foreach (const QUuid &id, machineIds)
    {
        unselectedMachines.removeAll(id);
        if (!m_machineIds.contains(id))
            newSelections << id;
    }
    m_machineIds = machineIds;

    addTabs(newSelections);
    removeTabs(unselectedMachines);
    if (!lastSelectedMachineId.isNull())
    {
        int iIndexToSelect = -1;
        for (int i = 0; i < m_pGuestTablesContainer->count() && iIndexToSelect == -1; ++i)
        {
            UIFileManagerGuestTable *pTable = qobject_cast<UIFileManagerGuestTable*>(m_pGuestTablesContainer->widget(i));
            if (!pTable)
                continue;
            if (lastSelectedMachineId == pTable->machineId())
                iIndexToSelect = i;
        }
        if (iIndexToSelect != -1)
            m_pGuestTablesContainer->setCurrentIndex(iIndexToSelect);
    }
}

void UIFileManager::removeTabs(const QVector<QUuid> &machineIdsToRemove)
{
    if (!m_pGuestTablesContainer)
        return;
    QVector<UIFileManagerGuestTable*> removeList;

    for (int i = m_pGuestTablesContainer->count() - 1; i >= 0; --i)
    {
        UIFileManagerGuestTable *pTable = qobject_cast<UIFileManagerGuestTable*>(m_pGuestTablesContainer->widget(i));
        if (!pTable)
            continue;
        if (machineIdsToRemove.contains(pTable->machineId()))
        {
            removeList << pTable;
            m_pGuestTablesContainer->removeTab(i);
        }
    }
    qDeleteAll(removeList.begin(), removeList.end());
}

void UIFileManager::addTabs(const QVector<QUuid> &machineIdsToAdd)
{
    if (!m_pGuestTablesContainer)
        return;

    foreach (const QUuid &id, machineIdsToAdd)
    {
        CMachine comMachine = gpGlobalSession->virtualBox().FindMachine(id.toString());
        if (comMachine.isNull())
            continue;
        UIFileManagerGuestTable *pGuestFileTable = new UIFileManagerGuestTable(m_pActionPool, comMachine, m_pGuestTablesContainer);
        m_pGuestTablesContainer->addTab(pGuestFileTable, comMachine.GetName());
        if (pGuestFileTable)
        {
            connect(pGuestFileTable, &UIFileManagerGuestTable::sigLogOutput,
                    this, &UIFileManager::sltReceieveLogOutput);
            connect(pGuestFileTable, &UIFileManagerGuestTable::sigSelectionChanged,
                    this, &UIFileManager::sltFileTableSelectionChanged);
            connect(pGuestFileTable, &UIFileManagerGuestTable::sigNewFileOperation,
                    this, &UIFileManager::sltReceieveNewFileOperation);
            connect(pGuestFileTable, &UIFileManagerGuestTable::sigStateChanged,
                    this, &UIFileManager::sltGuestFileTableStateChanged);
            connect(pGuestFileTable, &UIFileManagerGuestTable::sigDeleteConfirmationOptionChanged,
                    this, &UIFileManager::sltHandleOptionsUpdated);
        }
    }
}

UIFileManagerGuestTable *UIFileManager::currentGuestTable()
{
    if (!m_pGuestTablesContainer)
        return 0;
    return qobject_cast<UIFileManagerGuestTable*>(m_pGuestTablesContainer->currentWidget());
}
#include "UIFileManager.moc"
