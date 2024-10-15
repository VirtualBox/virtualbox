/* $Id: UIMediumSelector.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumSelector class implementation.
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
#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QMenuBar>
#include <QVBoxLayout>
#include <QPushButton>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIFileDialog.h"
#include "QIMessageBox.h"
#include "QITabWidget.h"
#include "QIToolBar.h"
#include "QIToolButton.h"
#include "UIActionPool.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UILoggingDefs.h"
#include "UIMediumEnumerator.h"
#include "UIMediumSearchWidget.h"
#include "UIMediumSelector.h"
#include "UIMediumTools.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UIIconPool.h"
#include "UIMedium.h"
#include "UIMediumItem.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CMachine.h"
#include "CMediumAttachment.h"
#include "CMediumFormat.h"
#include "CStorageController.h"
#include "CSystemProperties.h"

#ifdef VBOX_WS_MAC
# include "UIWindowMenuManager.h"
#endif /* VBOX_WS_MAC */


UIMediumSelector::UIMediumSelector(const QUuid &uCurrentMediumId, UIMediumDeviceType enmMediumType, const QString &machineName,
                                   const QString &machineSettingsFilePath, const QString &strMachineGuestOSTypeId,
                                   const QUuid &uMachineID, QWidget *pParent, UIActionPool *pActionPool)
    : QIWithRestorableGeometry<QIMainDialog>(pParent)
    , m_pCentralWidget(0)
    , m_pMainLayout(0)
    , m_pTreeWidget(0)
    , m_enmMediumType(enmMediumType)
    , m_pButtonBox(0)
    , m_pCancelButton(0)
    , m_pChooseButton(0)
    , m_pLeaveEmptyButton(0)
    , m_pMainMenu(0)
    , m_pToolBar(0)
    , m_pActionAdd(0)
    , m_pActionCreate(0)
    , m_pActionRefresh(0)
    , m_pAttachedSubTreeRoot(0)
    , m_pNotAttachedSubTreeRoot(0)
    , m_pParent(pParent)
    , m_pSearchWidget(0)
    , m_iCurrentShownIndex(0)
    , m_strMachineFolder(machineSettingsFilePath)
    , m_strMachineName(machineName)
    , m_strMachineGuestOSTypeId(strMachineGuestOSTypeId)
    , m_uMachineID(uMachineID)
    , m_pActionPool(pActionPool)
    , m_iGeometrySaveTimerId(-1)
{
    /* Start full medium-enumeration (if necessary): */
    if (!gpMediumEnumerator->isFullMediumEnumerationRequested())
        gpMediumEnumerator->enumerateMedia();
    configure();
    finalize();
    selectMedium(uCurrentMediumId);
    loadSettings();
}

void UIMediumSelector::setEnableCreateAction(bool fEnable)
{
    if (!m_pActionCreate)
        return;
    m_pActionCreate->setEnabled(fEnable);
    m_pActionCreate->setVisible(fEnable);
}

QList<QUuid> UIMediumSelector::selectedMediumIds() const
{
    QList<QUuid> selectedIds;
    if (!m_pTreeWidget)
        return selectedIds;
    QList<QTreeWidgetItem*> selectedItems = m_pTreeWidget->selectedItems();
    for (int i = 0; i < selectedItems.size(); ++i)
    {
        UIMediumItem *item = dynamic_cast<UIMediumItem*>(selectedItems.at(i));
        if (item)
            selectedIds.push_back(item->medium().id());
    }
    return selectedIds;
}

/* static */
int UIMediumSelector::openMediumSelectorDialog(QWidget *pParent, UIMediumDeviceType  enmMediumType, const QUuid &uCurrentMediumId,
                                               QUuid &uSelectedMediumUuid, const QString &strMachineFolder, const QString &strMachineName,
                                               const QString &strMachineGuestOSTypeId, bool fEnableCreate, const QUuid &uMachineID,
                                               UIActionPool *pActionPool)
{
    QUuid uMachineOrGlobalId = uMachineID == QUuid() ? gEDataManager->GlobalID : uMachineID;

    QWidget *pDialogParent = windowManager().realParentWindow(pParent);
    QPointer<UIMediumSelector> pSelector = new UIMediumSelector(uCurrentMediumId, enmMediumType, strMachineName,
                                                                strMachineFolder, strMachineGuestOSTypeId,
                                                                uMachineOrGlobalId, pDialogParent, pActionPool);

    if (!pSelector)
        return static_cast<int>(UIMediumSelector::ReturnCode_Rejected);
    pSelector->setEnableCreateAction(fEnableCreate);
    windowManager().registerNewParent(pSelector, pDialogParent);

    int iResult = pSelector->exec(false);
    UIMediumSelector::ReturnCode returnCode;

    if (iResult >= static_cast<int>(UIMediumSelector::ReturnCode_Max) || iResult < 0)
        returnCode = UIMediumSelector::ReturnCode_Rejected;
    else
        returnCode = static_cast<UIMediumSelector::ReturnCode>(iResult);

    if (returnCode == UIMediumSelector::ReturnCode_Accepted)
    {
        QList<QUuid> selectedMediumIds = pSelector->selectedMediumIds();

        /* Currently we only care about the 0th since we support single selection by intention: */
        if (selectedMediumIds.isEmpty())
            returnCode = UIMediumSelector::ReturnCode_Rejected;
        else
        {
            uSelectedMediumUuid = selectedMediumIds[0];
            gpMediumEnumerator->updateRecentlyUsedMediumListAndFolder(enmMediumType,
                                                                      gpMediumEnumerator->medium(uSelectedMediumUuid).location());
        }
    }
    delete pSelector;
    return static_cast<int>(returnCode);
}

void UIMediumSelector::sltRetranslateUI()
{
    if (m_pCancelButton)
    {
        m_pCancelButton->setText(tr("&Cancel"));
        m_pCancelButton->setToolTip(tr("Cancel"));
    }
    if (m_pLeaveEmptyButton)
    {
        m_pLeaveEmptyButton->setText(tr("Leave &Empty"));
        m_pLeaveEmptyButton->setToolTip(tr("Leave the drive empty"));
    }

    if (m_pChooseButton)
    {
        m_pChooseButton->setText(tr("C&hoose"));
        m_pChooseButton->setToolTip(tr("Attach the selected medium to the drive"));
    }

    if (m_pTreeWidget)
    {
        m_pTreeWidget->setWhatsThis(tr("Shows a list of all registered media"));
        m_pTreeWidget->headerItem()->setText(0, tr("Name"));
        m_pTreeWidget->headerItem()->setText(1, tr("Virtual Size"));
        m_pTreeWidget->headerItem()->setText(2, tr("Actual Size"));
    }
}

bool UIMediumSelector::event(QEvent *pEvent)
{
    if (pEvent->type() == QEvent::Resize || pEvent->type() == QEvent::Move)
    {
        if (m_iGeometrySaveTimerId != -1)
            killTimer(m_iGeometrySaveTimerId);
        m_iGeometrySaveTimerId = startTimer(300);
    }
    else if (pEvent->type() == QEvent::Timer)
    {
        QTimerEvent *pTimerEvent = static_cast<QTimerEvent*>(pEvent);
        if (pTimerEvent->timerId() == m_iGeometrySaveTimerId)
        {
            killTimer(m_iGeometrySaveTimerId);
            m_iGeometrySaveTimerId = -1;
            saveDialogGeometry();
        }
    }
    return QIWithRestorableGeometry<QIMainDialog>::event(pEvent);
}

void UIMediumSelector::configure()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/media_manager_32px.png", ":/media_manager_16px.png"));
#endif

    setTitle();
    prepareWidgets();
    prepareActions();
    prepareMenuAndToolBar();
    prepareConnections();
}

void UIMediumSelector::prepareActions()
{
    if (!m_pActionPool)
        return;

    switch (m_enmMediumType)
    {
        case UIMediumDeviceType_DVD:
            m_pActionAdd = m_pActionPool->action(UIActionIndex_M_MediumSelector_AddCD);
            m_pActionCreate = m_pActionPool->action(UIActionIndex_M_MediumSelector_CreateCD);
            break;
        case UIMediumDeviceType_Floppy:
            m_pActionAdd = m_pActionPool->action(UIActionIndex_M_MediumSelector_AddFD);
            m_pActionCreate = m_pActionPool->action(UIActionIndex_M_MediumSelector_CreateFD);
            break;
        case UIMediumDeviceType_HardDisk:
        case UIMediumDeviceType_All:
        case UIMediumDeviceType_Invalid:
        default:
            m_pActionAdd = m_pActionPool->action(UIActionIndex_M_MediumSelector_AddHD);
            m_pActionCreate = m_pActionPool->action(UIActionIndex_M_MediumSelector_CreateHD);
            break;
    }

    m_pActionRefresh = m_pActionPool->action(UIActionIndex_M_MediumSelector_Refresh);
}

void UIMediumSelector::prepareMenuAndToolBar()
{
    if (!m_pMainMenu || !m_pToolBar)
        return;

    m_pMainMenu->addAction(m_pActionAdd);
    m_pMainMenu->addAction(m_pActionCreate);
    m_pMainMenu->addSeparator();
    m_pMainMenu->addAction(m_pActionRefresh);

    m_pToolBar->addAction(m_pActionAdd);
    if (!(gEDataManager->restrictedDialogTypes(m_uMachineID) & UIExtraDataMetaDefs::DialogType_VISOCreator))
        m_pToolBar->addAction(m_pActionCreate);
    m_pToolBar->addSeparator();
    m_pToolBar->addAction(m_pActionRefresh);
}

void UIMediumSelector::prepareConnections()
{
    /* Configure medium-enumeration connections: */
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumCreated,
            this, &UIMediumSelector::sltHandleMediumCreated);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerationStarted,
            this, &UIMediumSelector::sltHandleMediumEnumerationStart);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerated,
            this, &UIMediumSelector::sltHandleMediumEnumerated);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerationFinished,
            this, &UIMediumSelector::sltHandleMediumEnumerationFinish);
    if (m_pActionAdd)
        connect(m_pActionAdd, &QAction::triggered, this, &UIMediumSelector::sltAddMedium);
    if (m_pActionCreate)
        connect(m_pActionCreate, &QAction::triggered, this, &UIMediumSelector::sltCreateMedium);
    if (m_pActionRefresh)
        connect(m_pActionRefresh, &QAction::triggered, this, &UIMediumSelector::sltHandleRefresh);

    if (m_pTreeWidget)
    {
        connect(m_pTreeWidget, &QITreeWidget::itemSelectionChanged, this, &UIMediumSelector::sltHandleItemSelectionChanged);
        connect(m_pTreeWidget, &QITreeWidget::itemDoubleClicked, this, &UIMediumSelector::sltHandleTreeWidgetDoubleClick);
        connect(m_pTreeWidget, &QITreeWidget::customContextMenuRequested, this, &UIMediumSelector::sltHandleTreeContextMenuRequest);
    }

    if (m_pCancelButton)
        connect(m_pCancelButton, &QPushButton::clicked, this, &UIMediumSelector::sltButtonCancel);
    if (m_pChooseButton)
        connect(m_pChooseButton, &QPushButton::clicked, this, &UIMediumSelector::sltButtonChoose);
    if (m_pLeaveEmptyButton)
        connect(m_pLeaveEmptyButton, &QPushButton::clicked, this, &UIMediumSelector::sltButtonLeaveEmpty);

    if (m_pSearchWidget)
    {
        connect(m_pSearchWidget, &UIMediumSearchWidget::sigPerformSearch,
                this, &UIMediumSelector::sltHandlePerformSearch);
    }
}

UIMediumItem* UIMediumSelector::addTreeItem(const UIMedium &medium, QITreeWidgetItem *pParent)
{
    if (!pParent)
        return 0;
    switch (m_enmMediumType)
    {
        case UIMediumDeviceType_DVD:
            return new UIMediumItemCD(medium, pParent);
            break;
        case UIMediumDeviceType_Floppy:
            return new UIMediumItemFD(medium, pParent);
            break;
        case UIMediumDeviceType_HardDisk:
        case UIMediumDeviceType_All:
        case UIMediumDeviceType_Invalid:
        default:
            return createHardDiskItem(medium, pParent);
            break;
    }
}

UIMediumItem* UIMediumSelector::createHardDiskItem(const UIMedium &medium, QITreeWidgetItem *pParent)
{
    if (medium.medium().isNull())
        return 0;
    if (!m_pTreeWidget)
        return 0;
    /* Search the tree to see if we already have the item: */
    UIMediumItem *pMediumItem = searchItem(0, medium.id());
    if (pMediumItem)
        return pMediumItem;
    /* Check if the corresponding medium has a parent */
    if (medium.parentID() != UIMedium::nullID())
    {
        UIMediumItem *pParentMediumItem = searchItem(0, medium.parentID());
        /* If parent medium-item was not found we create it: */
        if (!pParentMediumItem)
        {
            /* Make sure corresponding parent medium is already cached! */
            UIMedium parentMedium = gpMediumEnumerator->medium(medium.parentID());
            if (parentMedium.isNull())
                AssertMsgFailed(("Parent medium with ID={%s} was not found!\n", medium.parentID().toString().toUtf8().constData()));
            /* Try to create parent medium-item: */
            else
                pParentMediumItem = createHardDiskItem(parentMedium, pParent);
        }
        if (pParentMediumItem)
        {
            pMediumItem = new UIMediumItemHD(medium, pParentMediumItem);
            LogRel2(("UIMediumManager: Child hard-disk medium-item with ID={%s} created.\n", medium.id().toString().toUtf8().constData()));
        }
        else
            AssertMsgFailed(("Parent medium with ID={%s} could not be created!\n", medium.parentID().toString().toUtf8().constData()));
    }

    /* No parents, thus just create item as top-level one: */
    else
    {
        pMediumItem = new UIMediumItemHD(medium, pParent);
        LogRel2(("UIMediumManager: Root hard-disk medium-item with ID={%s} created.\n", medium.id().toString().toUtf8().constData()));
    }
    return pMediumItem;
}

void UIMediumSelector::restoreSelection(const QList<QUuid> &selectedMediums, QVector<UIMediumItem*> &mediumList)
{
    if (!m_pTreeWidget)
        return;
    if (selectedMediums.isEmpty())
    {
        m_pTreeWidget->setCurrentItem(0);
        return;
    }
    bool selected = false;
    for (int i = 0; i < mediumList.size(); ++i)
    {
        if (!mediumList[i])
            continue;
        if (selectedMediums.contains(mediumList[i]->medium().id()))
        {
            mediumList[i]->setSelected(true);
            selected = true;
        }
    }

    if (!selected)
        m_pTreeWidget->setCurrentItem(0);
}

void UIMediumSelector::prepareWidgets()
{
    m_pCentralWidget = new QWidget;
    if (!m_pCentralWidget)
        return;
    setCentralWidget(m_pCentralWidget);

    m_pMainLayout = new QVBoxLayout;
    m_pCentralWidget->setLayout(m_pMainLayout);

    if (!m_pMainLayout || !menuBar())
        return;

    if (m_pActionPool && m_pActionPool->action(UIActionIndex_M_MediumSelector))
    {
        m_pMainMenu = m_pActionPool->action(UIActionIndex_M_MediumSelector)->menu();
        if (m_pMainMenu)
            menuBar()->addMenu(m_pMainMenu);
    }

    m_pToolBar = new QIToolBar;
    if (m_pToolBar)
    {
        /* Configure toolbar: */
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        m_pMainLayout->addWidget(m_pToolBar);
    }

    m_pTreeWidget = new QITreeWidget;
    if (m_pTreeWidget)
    {
        m_pTreeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        m_pMainLayout->addWidget(m_pTreeWidget);
        m_pTreeWidget->setAlternatingRowColors(true);
        int iColumnCount = (m_enmMediumType == UIMediumDeviceType_HardDisk) ? 3 : 2;
        m_pTreeWidget->setColumnCount(iColumnCount);
        m_pTreeWidget->setSortingEnabled(true);
        m_pTreeWidget->sortItems(0, Qt::AscendingOrder);
        m_pTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    }

    m_pSearchWidget = new UIMediumSearchWidget;
    if (m_pSearchWidget)
    {
        m_pMainLayout->addWidget(m_pSearchWidget);
    }

    m_pButtonBox = new QIDialogButtonBox;
    if (m_pButtonBox)
    {
        /* Configure button-box: */
        m_pCancelButton = m_pButtonBox->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);

        /* Only DVDs and Floppies can be left empty: */
        if (m_enmMediumType == UIMediumDeviceType_DVD || m_enmMediumType == UIMediumDeviceType_Floppy)
            m_pLeaveEmptyButton = m_pButtonBox->addButton(tr("Leave Empty"), QDialogButtonBox::ActionRole);

        m_pChooseButton = m_pButtonBox->addButton(tr("Choose"), QDialogButtonBox::AcceptRole);
        m_pCancelButton->setShortcut(Qt::Key_Escape);

        /* Add button-box into main layout: */
        m_pMainLayout->addWidget(m_pButtonBox);
    }

    repopulateTreeWidget();
}

void UIMediumSelector::sltButtonChoose()
{
    done(static_cast<int>(ReturnCode_Accepted));
}

void UIMediumSelector::sltButtonCancel()
{
    done(static_cast<int>(ReturnCode_Rejected));
}

void UIMediumSelector::sltButtonLeaveEmpty()
{
    done(static_cast<int>(ReturnCode_LeftEmpty));
}

void UIMediumSelector::sltAddMedium()
{
    QUuid uMediumID = UIMediumTools::openMediumWithFileOpenDialog(m_enmMediumType, this, m_strMachineFolder,
                                                                  true /* fUseLastFolder */);
    if (uMediumID.isNull())
        return;
    repopulateTreeWidget();
    selectMedium(uMediumID);
}

void UIMediumSelector::sltCreateMedium()
{
    QUuid uMediumId = UIMediumTools::openMediumCreatorDialog(m_pActionPool, this, m_enmMediumType, m_strMachineFolder,
                                                             m_strMachineName, m_strMachineGuestOSTypeId);
    /* Make sure that the data structure is updated and newly created medium is selected and visible: */
    sltHandleMediumCreated(uMediumId);
}

void UIMediumSelector::sltHandleItemSelectionChanged()
{
    updateChooseButton();
}

void UIMediumSelector::sltHandleTreeWidgetDoubleClick(QTreeWidgetItem * item, int column)
{
    Q_UNUSED(column);
    if (!dynamic_cast<UIMediumItem*>(item))
        return;
    accept();
}

void UIMediumSelector::sltHandleMediumCreated(const QUuid &uMediumId)
{
    if (uMediumId.isNull())
        return;
    /* Update the tree widget making sure we show the new item: */
    repopulateTreeWidget();
    /* Select the new item: */
    selectMedium(uMediumId);
    /* Update the search: */
    m_pSearchWidget->search(m_pTreeWidget);
}

void UIMediumSelector::sltHandleMediumEnumerationStart()
{
    /* Disable controls. Left Alone button box 'Ok' button. it is handle by tree population: */
    if (m_pActionRefresh)
        m_pActionRefresh->setEnabled(false);
}

void UIMediumSelector::sltHandleMediumEnumerated()
{
}

void UIMediumSelector::sltHandleMediumEnumerationFinish()
{
    repopulateTreeWidget();
    if (m_pActionRefresh)
        m_pActionRefresh->setEnabled(true);
}

void UIMediumSelector::sltHandleRefresh()
{
    /* Restart full medium-enumeration: */
    gpMediumEnumerator->enumerateMedia();
    /* Update the search: */
    m_pSearchWidget->search(m_pTreeWidget);
}

void UIMediumSelector::sltHandlePerformSearch()
{
    if (!m_pSearchWidget)
        return;
    m_pSearchWidget->search(m_pTreeWidget);
}

void UIMediumSelector::sltHandleTreeContextMenuRequest(const QPoint &point)
{
    QWidget *pSender = qobject_cast<QWidget*>(sender());
    if (!pSender)
        return;

    QMenu menu;
    QAction *pExpandAll = menu.addAction(tr("Expand All"));
    QAction *pCollapseAll = menu.addAction(tr("Collapse All"));
    if (!pExpandAll || !pCollapseAll)
        return;

    pExpandAll->setIcon(UIIconPool::iconSet(":/expand_all_16px.png"));
    pCollapseAll->setIcon(UIIconPool::iconSet(":/collapse_all_16px.png"));

    connect(pExpandAll, &QAction::triggered, this, &UIMediumSelector::sltHandleTreeExpandAllSignal);
    connect(pCollapseAll, &QAction::triggered, this, &UIMediumSelector::sltHandleTreeCollapseAllSignal);

    menu.exec(pSender->mapToGlobal(point));
}

void UIMediumSelector::sltHandleTreeExpandAllSignal()
{
    if (m_pTreeWidget)
        m_pTreeWidget->expandAll();
}

void UIMediumSelector::sltHandleTreeCollapseAllSignal()
{
    if (m_pTreeWidget)
        m_pTreeWidget->collapseAll();

    if (m_pAttachedSubTreeRoot)
        m_pTreeWidget->setExpanded(m_pTreeWidget->itemIndex(m_pAttachedSubTreeRoot), true);
    if (m_pNotAttachedSubTreeRoot)
        m_pTreeWidget->setExpanded(m_pTreeWidget->itemIndex(m_pNotAttachedSubTreeRoot), true);
}

void UIMediumSelector::selectMedium(const QUuid &uMediumID)
{
    if (!m_pTreeWidget || uMediumID.isNull())
        return;
    UIMediumItem *pMediumItem = searchItem(0, uMediumID);
    if (pMediumItem)
    {
        m_pTreeWidget->setCurrentItem(pMediumItem);
        QModelIndex itemIndex = m_pTreeWidget->itemIndex(pMediumItem);
        if (itemIndex.isValid())
            m_pTreeWidget->scrollTo(itemIndex, QAbstractItemView::PositionAtCenter);
    }
}

void UIMediumSelector::updateChooseButton()
{
    if (!m_pTreeWidget || !m_pChooseButton)
        return;
    QList<QTreeWidgetItem*> selectedItems = m_pTreeWidget->selectedItems();
    if (selectedItems.isEmpty())
    {
        m_pChooseButton->setEnabled(false);
        return;
    }

    /* check if at least one of the selected items is a UIMediumItem */
    bool mediumItemSelected = false;
    for (int i = 0; i < selectedItems.size() && !mediumItemSelected; ++i)
    {
        if (dynamic_cast<UIMediumItem*>(selectedItems.at(i)))
            mediumItemSelected = true;
    }
    if (mediumItemSelected)
        m_pChooseButton->setEnabled(true);
    else
        m_pChooseButton->setEnabled(false);
}

void UIMediumSelector::finalize()
{
    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMediumSelector::sltRetranslateUI);
}

void UIMediumSelector::showEvent(QShowEvent *pEvent)
{
    Q_UNUSED(pEvent);

    if (m_pTreeWidget)
        m_pTreeWidget->setFocus();
}

void UIMediumSelector::repopulateTreeWidget()
{
    if (!m_pTreeWidget)
        return;
    /* Cache the currently selected items: */
    QList<QTreeWidgetItem*> selectedItems = m_pTreeWidget->selectedItems();
    QList<QUuid> selectedMedia = selectedMediumIds();
    /* uuid list of selected items: */
    /* Reset the related data structure: */
    m_mediumItemList.clear();
    m_pTreeWidget->clear();
    m_pAttachedSubTreeRoot = 0;
    m_pNotAttachedSubTreeRoot = 0;
    QVector<UIMediumItem*> menuItemVector;
    foreach (const QUuid &uMediumID, gpMediumEnumerator->mediumIDs())
    {
        UIMedium medium = gpMediumEnumerator->medium(uMediumID);
        if (medium.type() == m_enmMediumType)
        {
            bool isMediumAttached = !(medium.medium().GetMachineIds().isEmpty());
            QITreeWidgetItem *pParent = 0;
            if (isMediumAttached)
            {
                if (!m_pAttachedSubTreeRoot)
                {
                    QStringList strList;
                    strList << "Attached";
                    m_pAttachedSubTreeRoot = new QITreeWidgetItem(m_pTreeWidget, strList);
                }
                pParent = m_pAttachedSubTreeRoot;

            }
            else
            {
                if (!m_pNotAttachedSubTreeRoot)
                {
                    QStringList strList;
                    strList << "Not Attached";
                    m_pNotAttachedSubTreeRoot = new QITreeWidgetItem(m_pTreeWidget, strList);
                }
                pParent = m_pNotAttachedSubTreeRoot;
            }
            UIMediumItem *treeItem = addTreeItem(medium, pParent);
            m_mediumItemList.append(treeItem);
            menuItemVector.push_back(treeItem);
        }
    }
    restoreSelection(selectedMedia, menuItemVector);
    saveDefaultForeground();
    updateChooseButton();
    if (m_pAttachedSubTreeRoot)
        m_pTreeWidget->expandItem(m_pAttachedSubTreeRoot);
    if (m_pNotAttachedSubTreeRoot)
        m_pTreeWidget->expandItem(m_pNotAttachedSubTreeRoot);
    m_pTreeWidget->resizeColumnToContents(0);
}

void UIMediumSelector::saveDefaultForeground()
{
    if (!m_pTreeWidget)
        return;
    if (m_defaultItemForeground == QBrush() && m_pTreeWidget->topLevelItemCount() >= 1)
    {
        QTreeWidgetItem *item = m_pTreeWidget->topLevelItem(0);
        if (item)
        {
            QVariant data = item->data(0, Qt::ForegroundRole);
            if (data.canConvert<QBrush>())
            {
                m_defaultItemForeground = data.value<QBrush>();
            }
        }
    }
}

UIMediumItem* UIMediumSelector::searchItem(const QTreeWidgetItem *pParent, const QUuid &mediumId)
{
    if (!m_pTreeWidget)
        return 0;
    if (!pParent)
         pParent = m_pTreeWidget->invisibleRootItem();
    if (!pParent)
        return 0;

    for (int i = 0; i < pParent->childCount(); ++i)
    {
        QTreeWidgetItem *pChild = pParent->child(i);
        if (!pChild)
            continue;
        UIMediumItem *mediumItem = dynamic_cast<UIMediumItem*>(pChild);
        if (mediumItem)
        {
            if (mediumItem->id() == mediumId)
                return mediumItem;
        }
        UIMediumItem *pResult = searchItem(pChild, mediumId);
        if (pResult)
            return pResult;
    }
    return 0;
}

void UIMediumSelector::setTitle()
{
    switch (m_enmMediumType)
    {
        case UIMediumDeviceType_DVD:
            if (!m_strMachineName.isEmpty())
                setWindowTitle(QString("%1 - %2").arg(m_strMachineName).arg(tr("Optical Disk Selector")));
            else
                setWindowTitle(QString("%1").arg(tr("Optical Disk Selector")));
            break;
        case UIMediumDeviceType_Floppy:
            if (!m_strMachineName.isEmpty())
                setWindowTitle(QString("%1 - %2").arg(m_strMachineName).arg(tr("Floppy Disk Selector")));
            else
                setWindowTitle(QString("%1").arg(tr("Floppy Disk Selector")));
            break;
        case UIMediumDeviceType_HardDisk:
            if (!m_strMachineName.isEmpty())
                setWindowTitle(QString("%1 - %2").arg(m_strMachineName).arg(tr("Hard Disk Selector")));
            else
                setWindowTitle(QString("%1").arg(tr("Hard Disk Selector")));
            break;
        case UIMediumDeviceType_All:
        case UIMediumDeviceType_Invalid:
        default:
            if (!m_strMachineName.isEmpty())
                setWindowTitle(QString("%1 - %2").arg(m_strMachineName).arg(tr("Virtual Medium Selector")));
            else
                setWindowTitle(QString("%1").arg(tr("Virtual Medium Selector")));
            break;
    }
}

void UIMediumSelector::saveDialogGeometry()
{
    const QRect geo = currentGeometry();
    LogRel2(("GUI: UIMediumSelector: Saving geometry as: Origin=%dx%d, Size=%dx%d\n",
             geo.x(), geo.y(), geo.width(), geo.height()));
    gEDataManager->setMediumSelectorDialogGeometry(geo, isCurrentlyMaximized());
}

void UIMediumSelector::loadSettings()
{
    const QRect availableGeo = gpDesktop->availableGeometry(this);
    int iDefaultWidth = availableGeo.width() / 2;
    int iDefaultHeight = availableGeo.height() * 3 / 4;
    QRect defaultGeo(0, 0, iDefaultWidth, iDefaultHeight);

    QWidget *pParent = windowManager().realParentWindow(m_pParent ? m_pParent : windowManager().mainWindowShown());
    /* Load geometry from extradata: */
    const QRect geo = gEDataManager->mediumSelectorDialogGeometry(this, pParent, defaultGeo);
    LogRel2(("GUI: UISoftKeyboard: Restoring geometry to: Origin=%dx%d, Size=%dx%d\n",
             geo.x(), geo.y(), geo.width(), geo.height()));

    restoreGeometry(geo);
}
