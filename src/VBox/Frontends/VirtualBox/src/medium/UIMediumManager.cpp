/* $Id: UIMediumManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumManager class implementation.
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
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIFileDialog.h"
#include "QILabel.h"
#include "QIMessageBox.h"
#include "QITabWidget.h"
#include "QIToolBar.h"
#include "UIActionPoolManager.h"
#include "UICommon.h"
#include "UIIconPool.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UILoggingDefs.h"
#include "UIMedium.h"
#include "UIMediumDetailsWidget.h"
#include "UIMediumItem.h"
#include "UIMediumEnumerator.h"
#include "UIMediumManager.h"
#include "UIMediumSearchWidget.h"
#include "UIMediumTools.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UIShortcutPool.h"
#include "UITranslationEventListener.h"
#include "UIVirtualBoxEventHandler.h"

/* COM includes: */
#include "CMachine.h"
#include "CMediumAttachment.h"
#include "CMediumFormat.h"
#include "CStorageController.h"
#include "CSystemProperties.h"

#ifdef VBOX_WS_MAC
# include "UIWindowMenuManager.h"
#endif /* VBOX_WS_MAC */



/** Functor allowing to check if passed UIMediumItem is suitable by @a uID. */
class CheckIfSuitableByID : public CheckIfSuitableBy
{
public:
    /** Constructor accepting @a uID to compare with. */
    CheckIfSuitableByID(const QUuid &uID) : m_uID(uID) {}

private:
    /** Determines whether passed UIMediumItem is suitable by @a uID. */
    bool isItSuitable(UIMediumItem *pItem) const  RT_OVERRIDE RT_FINAL { return pItem->id() == m_uID; }
    /** Holds the @a uID to compare to. */
    QUuid m_uID;
};

/** Functor allowing to check if passed UIMediumItem is suitable by @a state. */
class CheckIfSuitableByState : public CheckIfSuitableBy
{
public:
    /** Constructor accepting @a state to compare with. */
    CheckIfSuitableByState(KMediumState state) : m_state(state) {}

private:
    /** Determines whether passed UIMediumItem is suitable by @a state. */
    bool isItSuitable(UIMediumItem *pItem) const  RT_OVERRIDE RT_FINAL { return pItem->state() == m_state; }
    /** Holds the @a state to compare to. */
    KMediumState m_state;
};


/*********************************************************************************************************************************
*   Class UIEnumerationProgressBar implementation.                                                                               *
*********************************************************************************************************************************/

UIEnumerationProgressBar::UIEnumerationProgressBar(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
{
    /* Prepare: */
    prepare();
}

void UIEnumerationProgressBar::setText(const QString &strText)
{
    m_pLabel->setText(strText);
}

int UIEnumerationProgressBar::value() const
{
    return m_pProgressBar->value();
}

void UIEnumerationProgressBar::setValue(int iValue)
{
    m_pProgressBar->setValue(iValue);
}

void UIEnumerationProgressBar::setMaximum(int iValue)
{
    m_pProgressBar->setMaximum(iValue);
}

void UIEnumerationProgressBar::prepare()
{
    /* Create layout: */
    QHBoxLayout *pLayout = new QHBoxLayout(this);
    {
        /* Configure layout: */
        pLayout->setContentsMargins(0, 0, 0, 0);
        /* Create label: */
        m_pLabel = new QLabel;
        /* Create progress-bar: */
        m_pProgressBar = new QProgressBar;
        {
            /* Configure progress-bar: */
            m_pProgressBar->setTextVisible(false);
        }
        /* Add widgets into layout: */
        pLayout->addWidget(m_pLabel);
        pLayout->addWidget(m_pProgressBar);
    }
}


/*********************************************************************************************************************************
*   Class UIMediumManagerWidget implementation.                                                                                  *
*********************************************************************************************************************************/

UIMediumManagerWidget::UIMediumManagerWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                                             bool fShowToolbar /* = true */, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_pActionPool(pActionPool)
    , m_fShowToolbar(fShowToolbar)
    , m_fPreventChangeCurrentItem(false)
    , m_pTabWidget(0)
    , m_iTabCount(3)
    , m_fInaccessibleHD(false)
    , m_fInaccessibleCD(false)
    , m_fInaccessibleFD(false)
    , m_iconHD(UIIconPool::iconSet(":/hd_16px.png", ":/hd_disabled_16px.png"))
    , m_iconCD(UIIconPool::iconSet(":/cd_16px.png", ":/cd_disabled_16px.png"))
    , m_iconFD(UIIconPool::iconSet(":/fd_16px.png", ":/fd_disabled_16px.png"))
    , m_pDetailsWidget(0)
    , m_pToolBar(0)
    , m_pProgressBar(0)
    , m_pSearchWidget(0)
{
    /* Prepare: */
    prepare();
}

QMenu *UIMediumManagerWidget::menu() const
{
    return m_pActionPool->action(UIActionIndexMN_M_MediumWindow)->menu();
}

void UIMediumManagerWidget::setProgressBar(UIEnumerationProgressBar *pProgressBar)
{
    /* Cache progress-bar reference:*/
    m_pProgressBar = pProgressBar;

    /* Update translation: */
    sltRetranslateUI();
}

void UIMediumManagerWidget::sltRetranslateUI()
{
    /* Translate tab-widget: */
    if (m_pTabWidget)
    {
        m_pTabWidget->setTabText(tabIndex(UIMediumDeviceType_HardDisk), UIMediumManager::tr("&Hard disks"));
        m_pTabWidget->setTabText(tabIndex(UIMediumDeviceType_DVD), UIMediumManager::tr("&Optical disks"));
        m_pTabWidget->setTabText(tabIndex(UIMediumDeviceType_Floppy), UIMediumManager::tr("&Floppy disks"));
    }

    /* Translate HD tree-widget: */
    QITreeWidget *pTreeWidgetHD = treeWidget(UIMediumDeviceType_HardDisk);
    if (pTreeWidgetHD)
    {
        pTreeWidgetHD->setWhatsThis(UIMediumManager::tr("Registered hard drives"));
        pTreeWidgetHD->headerItem()->setText(0, UIMediumManager::tr("Name"));
        pTreeWidgetHD->headerItem()->setText(1, UIMediumManager::tr("Virtual Size"));
        pTreeWidgetHD->headerItem()->setText(2, UIMediumManager::tr("Actual Size"));
    }

    /* Translate CD tree-widget: */
    QITreeWidget *pTreeWidgetCD = treeWidget(UIMediumDeviceType_DVD);
    if (pTreeWidgetCD)
    {
        pTreeWidgetCD->setWhatsThis(UIMediumManager::tr("Registered optical disks"));
        pTreeWidgetCD->headerItem()->setText(0, UIMediumManager::tr("Name"));
        pTreeWidgetCD->headerItem()->setText(1, UIMediumManager::tr("Size"));
    }

    /* Translate FD tree-widget: */
    QITreeWidget *pTreeWidgetFD = treeWidget(UIMediumDeviceType_Floppy);
    if (pTreeWidgetFD)
    {
        pTreeWidgetFD->setWhatsThis(UIMediumManager::tr("Registered floppy disks"));
        pTreeWidgetFD->headerItem()->setText(0, UIMediumManager::tr("Name"));
        pTreeWidgetFD->headerItem()->setText(1, UIMediumManager::tr("Size"));
    }

    /* Translate progress-bar: */
    if (m_pProgressBar)
    {
        m_pProgressBar->setText(UIMediumManager::tr("Checking accessibility"));
#ifdef VBOX_WS_MAC
        /* Make sure that the widgets aren't jumping around
         * while the progress-bar get visible. */
        m_pProgressBar->adjustSize();
        //int h = m_pProgressBar->height();
        //if (m_pButtonBox)
        //    m_pButtonBox->setMinimumHeight(h + 12);
#endif
    }

    /* Full refresh if there is at least one item present: */
    if (   (pTreeWidgetHD && pTreeWidgetHD->topLevelItemCount())
        || (pTreeWidgetCD && pTreeWidgetCD->topLevelItemCount())
        || (pTreeWidgetFD && pTreeWidgetFD->topLevelItemCount()))
        sltRefreshAll();
}

void UIMediumManagerWidget::sltResetMediumDetailsChanges()
{
    /* Push the current item data into details-widget: */
    sltHandleCurrentTabChanged();
}

void UIMediumManagerWidget::sltApplyMediumDetailsChanges()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Get item data: */
    UIDataMedium oldData = *pMediumItem;
    UIDataMedium newData = m_pDetailsWidget->data();

    /* Search for corresponding medium: */
    CMedium comMedium = gpMediumEnumerator->medium(pMediumItem->id()).medium();

    /* Try to assign new medium type: */
    if (   comMedium.isOk()
        && newData.m_options.m_enmMediumType != oldData.m_options.m_enmMediumType)
        pMediumItem->changeMediumType(newData.m_options.m_enmMediumType);

    /* Try to assign new medium description: */
    if (   comMedium.isOk()
        && newData.m_options.m_strDescription != oldData.m_options.m_strDescription)
    {
        comMedium.SetDescription(newData.m_options.m_strDescription);

        /* Show error message if necessary: */
        if (!comMedium.isOk())
            UINotificationMessage::cannotChangeMediumParameter(comMedium);
    }

    /* Try to assign new medium location: */
    if (   comMedium.isOk()
        && newData.m_options.m_strLocation != oldData.m_options.m_strLocation)
    {
        /* Assign new medium location: */
        UINotificationProgressMediumMove *pNotification = new UINotificationProgressMediumMove(comMedium,
                                                                                               newData.m_options.m_strLocation);
        connect(pNotification, &UINotificationProgressMediumMove::sigProgressFinished,
                this, &UIMediumManagerWidget::sltHandleMoveProgressFinished);
        gpNotificationCenter->append(pNotification);
    }

    /* Try to assign new medium size: */
    if (   comMedium.isOk()
        && newData.m_options.m_uLogicalSize != oldData.m_options.m_uLogicalSize)
    {
        /* Assign new medium size: */
        UINotificationProgressMediumResize *pNotification = new UINotificationProgressMediumResize(comMedium,
                                                                                                   newData.m_options.m_uLogicalSize);
        connect(pNotification, &UINotificationProgressMediumResize::sigProgressFinished,
                this, &UIMediumManagerWidget::sltHandleResizeProgressFinished);
        gpNotificationCenter->append(pNotification);
    }

    /* Recache current item: */
    pMediumItem->refreshAll();

    /* Push the current item data into details-widget: */
    sltHandleCurrentTabChanged();
}

void UIMediumManagerWidget::sltHandleMediumCreated(const QUuid &uMediumID)
{
    /* Search for corresponding medium: */
    UIMedium medium = gpMediumEnumerator->medium(uMediumID);

    /* Ignore non-interesting media: */
    if (medium.isNull() || medium.isHostDrive())
        return;

    /* Ignore media (and their children) which are
     * marked as hidden or attached to hidden machines only: */
    if (UIMedium::isMediumAttachedToHiddenMachinesOnly(medium))
        return;

    /* Create medium-item for corresponding medium: */
    UIMediumItem *pMediumItem = createMediumItem(medium);

    /* Make sure medium-item was created: */
    if (!pMediumItem)
        return;

    /* If medium-item change allowed and
     * 1. medium-enumeration is not currently in progress or
     * 2. if there is no currently medium-item selected
     * we have to choose newly added medium-item as current one: */
    if (   !m_fPreventChangeCurrentItem
        && (   !gpMediumEnumerator->isMediumEnumerationInProgress()
            || !mediumItem(medium.type())))
        setCurrentItem(treeWidget(medium.type()), pMediumItem);
}

void UIMediumManagerWidget::sltHandleMediumDeleted(const QUuid &uMediumID)
{
    /* Make sure corresponding medium-item deleted: */
    deleteMediumItem(uMediumID);
}

void UIMediumManagerWidget::sltHandleMediumEnumerationStart()
{
    /* Disable 'refresh' action: */
    if (m_pActionPool->action(UIActionIndexMN_M_Medium_S_Refresh))
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Refresh)->setEnabled(false);

    /* Disable details-widget: */
    if (m_pDetailsWidget)
        m_pDetailsWidget->setOptionsEnabled(false);

    /* Reset and show progress-bar: */
    if (m_pProgressBar)
    {
        m_pProgressBar->setMaximum(gpMediumEnumerator->mediumIDs().size());
        m_pProgressBar->setValue(0);
        m_pProgressBar->show();
    }

    /* Reset inaccessibility flags: */
    m_fInaccessibleHD =
        m_fInaccessibleCD =
            m_fInaccessibleFD = false;

    /* Reset tab-widget icons: */
    if (m_pTabWidget)
    {
        m_pTabWidget->setTabIcon(tabIndex(UIMediumDeviceType_HardDisk), m_iconHD);
        m_pTabWidget->setTabIcon(tabIndex(UIMediumDeviceType_DVD), m_iconCD);
        m_pTabWidget->setTabIcon(tabIndex(UIMediumDeviceType_Floppy), m_iconFD);
    }

    /* Repopulate tree-widgets content: */
    repopulateTreeWidgets();

    /* Re-fetch all current medium-items: */
    refetchCurrentMediumItems();
    refetchCurrentChosenMediumItem();
}

void UIMediumManagerWidget::sltHandleMediumEnumerated(const QUuid &uMediumID)
{
    /* Search for corresponding medium: */
    UIMedium medium = gpMediumEnumerator->medium(uMediumID);

    /* Ignore non-interesting media: */
    if (medium.isNull() || medium.isHostDrive())
        return;

    /* Ignore media (and their children) which are
     * marked as hidden or attached to hidden machines only: */
    if (UIMedium::isMediumAttachedToHiddenMachinesOnly(medium))
        return;

    /* Update medium-item for corresponding medium: */
    updateMediumItem(medium);

    /* Advance progress-bar: */
    if (m_pProgressBar)
        m_pProgressBar->setValue(m_pProgressBar->value() + 1);
}

void UIMediumManagerWidget::sltHandleMediumEnumerationFinish()
{
    /* Hide progress-bar: */
    if (m_pProgressBar)
        m_pProgressBar->hide();

    /* Enable details-widget: */
    if (m_pDetailsWidget)
        m_pDetailsWidget->setOptionsEnabled(true);

    /* Enable 'refresh' action: */
    if (m_pActionPool->action(UIActionIndexMN_M_Medium_S_Refresh))
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Refresh)->setEnabled(true);

    /* Re-fetch all current medium-items: */
    refetchCurrentMediumItems();
    refetchCurrentChosenMediumItem();
}

void UIMediumManagerWidget::sltHandleMachineStateChange(const QUuid &uId, const KMachineState state)
{
    UIMediumItem *pCurrentItem = currentMediumItem();
    if (!pCurrentItem)
        return;
    /* If this machine is not using the current medium then we don't care about its state: */
    if (!pCurrentItem->isMediumAttachedTo(uId))
        return;
    bool fMediumIsModifiable = true;
    if (state != KMachineState_Aborted && state != KMachineState_PoweredOff && state != KMachineState_AbortedSaved)
        fMediumIsModifiable = false;
    m_pDetailsWidget->enableDisableMediumModificationWidgets(fMediumIsModifiable);
}

void UIMediumManagerWidget::sltAddMedium()
{
    QString strDefaultMachineFolder = gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder();
    UIMediumTools::openMediumWithFileOpenDialog(currentMediumType(), this,
                                                strDefaultMachineFolder, true /* use most recent medium folder */);
}

void UIMediumManagerWidget::sltCreateMedium()
{
    /* What's with current medium type? */
    const UIMediumDeviceType enmMediumType = currentMediumType();

    /* Ask listener to start hard-disk creation: */
    if (enmMediumType == UIMediumDeviceType_HardDisk)
        emit sigCreateMedium();
    else
        UIMediumTools::openMediumCreatorDialog(m_pActionPool, this, currentMediumType());
}

void UIMediumManagerWidget::sltCopyMedium()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Ask listener to start medium copying: */
    emit sigCopyMedium(pMediumItem->medium().id());
}

void UIMediumManagerWidget::sltMoveMedium()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Copy current medium-item: */
    pMediumItem->move();

    /* Push the current item data into details-widget: */
    sltHandleCurrentTabChanged();
}

void UIMediumManagerWidget::sltRemoveMedium()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Remove current medium-item: */
    pMediumItem->remove(true /* show message box */);
}

void UIMediumManagerWidget::sltReleaseMedium()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Remove current medium-item: */
    bool fResult = pMediumItem->release(true /* show message box */, false /* induced */);

    /* Refetch currently chosen medium-item: */
    if (fResult)
        refetchCurrentChosenMediumItem();
}

void UIMediumManagerWidget::sltClear()
{
    /* Currently we clear only DVD medium type items: */
    if (currentMediumType() != UIMediumDeviceType_DVD && currentMediumType() != UIMediumDeviceType_Floppy)
        return;
    QITreeWidget* pTreeWidget = currentTreeWidget();
    AssertReturnVoid(pTreeWidget);
    /* Iterate over the tree items assuming medium items are immediate children of the root and they dont have children
    *  themselves which currently holds for DVD and floppy  medium types: */
    QList<UIMediumItem*> mediumsToRemove;
    QStringList nameList;
    for (int i = 0; i < pTreeWidget->childCount(); ++i)
    {
        UIMediumItem *pMediumItem = qobject_cast<UIMediumItem*>(pTreeWidget->childItem(i));
        if (!pMediumItem)
            continue;
        if (pMediumItem->state() == KMediumState_Inaccessible)
        {
            mediumsToRemove << pMediumItem;
            nameList << pMediumItem->name();
        }
    }
    if (!msgCenter().confirmInaccesibleMediaClear(nameList, currentMediumType(), this))
        return;

    foreach (UIMediumItem *pMediumItem, mediumsToRemove)
    {
        pMediumItem->release(false /* no messag box */, false /* induced */);
        pMediumItem->remove(false /* show no message box */);
    }
}

void UIMediumManagerWidget::sltToggleMediumDetailsVisibility(bool fVisible)
{
    /* Save the setting: */
    gEDataManager->setVirtualMediaManagerDetailsExpanded(fVisible);
    /* Toggle medium details visibility: */
    if (m_pDetailsWidget)
        m_pDetailsWidget->setVisible(fVisible);
    /* Notify external lsiteners: */
    emit sigMediumDetailsVisibilityChanged(fVisible);
}

void UIMediumManagerWidget::sltToggleMediumSearchVisibility(bool fVisible)
{
    /* Save the setting: */
    gEDataManager->setVirtualMediaManagerSearchWidgetExpanded(fVisible);
    /* Toggle medium details visibility: */
    if (m_pSearchWidget)
        m_pSearchWidget->setVisible(fVisible);
}

void UIMediumManagerWidget::sltRefreshAll()
{
    /* Restart full medium-enumeration: */
    gpMediumEnumerator->enumerateMedia();
}

void UIMediumManagerWidget::sltHandleMoveProgressFinished()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Recache current item: */
    pMediumItem->refreshAll();

    /* Push the current item data into details-widget: */
    sltHandleCurrentTabChanged();
}

void UIMediumManagerWidget::sltHandleResizeProgressFinished()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();
    AssertMsgReturnVoid(pMediumItem, ("Current item must not be null"));
    AssertReturnVoid(!pMediumItem->id().isNull());

    /* Recache current item: */
    pMediumItem->refreshAll();

    /* Push the current item data into details-widget: */
    sltHandleCurrentTabChanged();
}

void UIMediumManagerWidget::sltHandleCurrentTabChanged()
{
    /* Get current tree-widget: */
    QITreeWidget *pTreeWidget = currentTreeWidget();
    if (pTreeWidget)
    {
        /* If another tree-widget was focused before,
         * move focus to current tree-widget: */
        if (qobject_cast<QITreeWidget*>(focusWidget()))
            pTreeWidget->setFocus();
    }

    /* Update action icons: */
    updateActionIcons();

    /* Raise the required information-container: */
    if (m_pDetailsWidget)
        m_pDetailsWidget->setCurrentType(currentMediumType());

    enableClearAction();

    /* Re-fetch currently chosen medium-item: */
    refetchCurrentChosenMediumItem();
    sltHandlePerformSearch();
}

void UIMediumManagerWidget::sltHandleCurrentItemChanged()
{
    /* Get sender() tree-widget: */
    QITreeWidget *pTreeWidget = qobject_cast<QITreeWidget*>(sender());
    AssertMsgReturnVoid(pTreeWidget, ("This slot should be called by tree-widget only!\n"));

    /* Re-fetch current medium-item of required type: */
    refetchCurrentMediumItem(mediumType(pTreeWidget));
}

void UIMediumManagerWidget::sltHandleContextMenuRequest(const QPoint &position)
{
    /* Get current tree-widget: */
    QITreeWidget *pTreeWidget = currentTreeWidget();
    AssertPtrReturnVoid(pTreeWidget);

    /* If underlaying item was found => make sure that item is current one: */
    QTreeWidgetItem *pItem = pTreeWidget->itemAt(position);
    if (pItem)
        setCurrentItem(pTreeWidget, pItem);

    /* Compose temporary context-menu: */
    QMenu menu;
    if (pTreeWidget->itemAt(position))
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Copy));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Move));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Remove));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Release));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Search));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Details));
    }
    else
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Add));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Create));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Search));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Refresh));
    }
    /* And show it: */
    menu.exec(pTreeWidget->viewport()->mapToGlobal(position));
}

void UIMediumManagerWidget::sltPerformTablesAdjustment()
{
    /* Get all the tree-widgets: */
    const QList<QITreeWidget*> trees = m_trees.values();

    /* Calculate deduction for every header: */
    QList<int> deductions;
    foreach (QITreeWidget *pTreeWidget, trees)
    {
        int iDeduction = 0;
        for (int iHeaderIndex = 1; iHeaderIndex < pTreeWidget->header()->count(); ++iHeaderIndex)
            iDeduction += pTreeWidget->header()->sectionSize(iHeaderIndex);
        deductions << iDeduction;
    }

    /* Adjust the table's first column: */
    for (int iTreeIndex = 0; iTreeIndex < trees.size(); ++iTreeIndex)
    {
        QITreeWidget *pTreeWidget = trees[iTreeIndex];
        int iSize0 = pTreeWidget->viewport()->width() - deductions[iTreeIndex];
        if (pTreeWidget->header()->sectionSize(0) != iSize0)
            pTreeWidget->header()->resizeSection(0, iSize0);
    }
}

void UIMediumManagerWidget::sltHandlePerformSearch()
{
    performSearch(true);
}

void UIMediumManagerWidget::sltDetachCOM()
{
    /* Clear tree-widgets: */
    QITreeWidget *pTreeWidgetHD = treeWidget(UIMediumDeviceType_HardDisk);
    if (pTreeWidgetHD)
        pTreeWidgetHD->clear();
    QITreeWidget *pTreeWidgetCD = treeWidget(UIMediumDeviceType_DVD);
    if (pTreeWidgetCD)
        pTreeWidgetCD->clear();
    QITreeWidget *pTreeWidgetFD = treeWidget(UIMediumDeviceType_Floppy);
    if (pTreeWidgetFD)
        pTreeWidgetFD->clear();
}

void UIMediumManagerWidget::prepare()
{
    /* Prepare connections: */
    prepareConnections();
    /* Prepare actions: */
    prepareActions();
    /* Prepare widgets: */
    prepareWidgets();

    /* Load settings: */
    loadSettings();

    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMediumManagerWidget::sltRetranslateUI);

    /* Start full medium-enumeration (if necessary): */
    if (!gpMediumEnumerator->isFullMediumEnumerationRequested())
        gpMediumEnumerator->enumerateMedia();
    /* Emulate medium-enumeration otherwise: */
    else
    {
        /* Emulate medium-enumeration start: */
        sltHandleMediumEnumerationStart();

        /* Emulate medium-enumeration finish (if necessary): */
        if (!gpMediumEnumerator->isMediumEnumerationInProgress())
            sltHandleMediumEnumerationFinish();
    }
    uiCommon().setHelpKeyword(this,"virtual-media-manager");
}

void UIMediumManagerWidget::prepareConnections()
{
    /* Listen to vm state changed event so that we can disable/enable widgets related to the current medium if neds be: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIMediumManagerWidget::sltHandleMachineStateChange);

    /* Configure medium-processing connections: */
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumCreated,
            this, &UIMediumManagerWidget::sltHandleMediumCreated);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumDeleted,
            this, &UIMediumManagerWidget::sltHandleMediumDeleted);

    /* Configure medium-enumeration connections: */
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerationStarted,
            this, &UIMediumManagerWidget::sltHandleMediumEnumerationStart);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerated,
            this, &UIMediumManagerWidget::sltHandleMediumEnumerated);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerationFinished,
            this, &UIMediumManagerWidget::sltHandleMediumEnumerationFinish);

    /* Configure COM related connections: */
    connect(&uiCommon(), &UICommon::sigAskToDetachCOM,
            this, &UIMediumManagerWidget::sltDetachCOM);
}

void UIMediumManagerWidget::prepareActions()
{
    /* First of all, add actions which has smaller shortcut scope: */
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Add));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Create));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Copy));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Move));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Remove));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Release));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Clear));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Search));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Details));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Refresh));

    /* Connect actions: */
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Add), &QAction::triggered,
            this, &UIMediumManagerWidget::sltAddMedium);
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Create), &QAction::triggered,
            this, &UIMediumManagerWidget::sltCreateMedium);
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Copy), &QAction::triggered,
            this, &UIMediumManagerWidget::sltCopyMedium);
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Move), &QAction::triggered,
            this, &UIMediumManagerWidget::sltMoveMedium);
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Remove), &QAction::triggered,
            this, &UIMediumManagerWidget::sltRemoveMedium);
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Release), &QAction::triggered,
            this, &UIMediumManagerWidget::sltReleaseMedium);
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Clear), &QAction::triggered,
            this, &UIMediumManagerWidget::sltClear);
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Details), &QAction::toggled,
            this, &UIMediumManagerWidget::sltToggleMediumDetailsVisibility);
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Search), &QAction::toggled,
            this, &UIMediumManagerWidget::sltToggleMediumSearchVisibility);
    connect(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Refresh), &QAction::triggered,
            this, &UIMediumManagerWidget::sltRefreshAll);

    /* Update action icons: */
    updateActionIcons();
}

void UIMediumManagerWidget::prepareWidgets()
{
    /* Create main-layout: */
    new QVBoxLayout(this);
    AssertPtrReturnVoid(layout());
    {
        /* Configure layout: */
        layout()->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
        layout()->setSpacing(10);
#else
        layout()->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif

        /* Prepare toolbar, if requested: */
        if (m_fShowToolbar)
            prepareToolBar();
        /* Prepare tab-widget: */
        prepareTabWidget();
        /* Prepare search-widget: */
        prepareSearchWidget();
        /* Prepare details-widget: */
        prepareDetailsWidget();
    }
}

void UIMediumManagerWidget::prepareToolBar()
{
    /* Create toolbar: */
    m_pToolBar = new QIToolBar(parentWidget());
    AssertPtrReturnVoid(m_pToolBar);
    {
        /* Configure toolbar: */
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        /* Add toolbar actions: */
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Add));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Create));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Copy));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Move));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Remove));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Release));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Clear));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Search));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Details));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Medium_S_Refresh));

#ifdef VBOX_WS_MAC
        /* Check whether we are embedded into a stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Add into layout: */
            layout()->addWidget(m_pToolBar);
        }
#else
        /* Add into layout: */
        layout()->addWidget(m_pToolBar);
#endif
    }
}

void UIMediumManagerWidget::prepareTabWidget()
{
    /* Create tab-widget: */
    m_pTabWidget = new QITabWidget;
    AssertPtrReturnVoid(m_pTabWidget);
    {
        /* Create tabs: */
        for (int i = 0; i < m_iTabCount; ++i)
            prepareTab((UIMediumDeviceType)i);
        /* Configure tab-widget: */
        m_pTabWidget->setFocusPolicy(Qt::TabFocus);
        m_pTabWidget->setTabIcon(tabIndex(UIMediumDeviceType_HardDisk), m_iconHD);
        m_pTabWidget->setTabIcon(tabIndex(UIMediumDeviceType_DVD), m_iconCD);
        m_pTabWidget->setTabIcon(tabIndex(UIMediumDeviceType_Floppy), m_iconFD);
        connect(m_pTabWidget, &QITabWidget::currentChanged, this, &UIMediumManagerWidget::sltHandleCurrentTabChanged);

        /* Add tab-widget into central layout: */
        layout()->addWidget(m_pTabWidget);

        /* Update other widgets according chosen tab: */
        sltHandleCurrentTabChanged();
    }
}

void UIMediumManagerWidget::prepareTab(UIMediumDeviceType type)
{
    /* Create tab: */
    m_pTabWidget->addTab(new QWidget, QString());
    QWidget *pTab = tab(type);
    AssertPtrReturnVoid(pTab);
    {
        /* Create tab layout: */
        QVBoxLayout *pLayout = new QVBoxLayout(pTab);
        AssertPtrReturnVoid(pLayout);
        {
#ifdef VBOX_WS_MAC
            /* Configure layout: */
            pLayout->setContentsMargins(10, 10, 10, 10);
#endif

            /* Prepare tree-widget: */
            prepareTreeWidget(type, type == UIMediumDeviceType_HardDisk ? 3 : 2);
        }
    }
}

void UIMediumManagerWidget::prepareTreeWidget(UIMediumDeviceType type, int iColumns)
{
    /* Create tree-widget: */
    m_trees.insert(tabIndex(type), new QITreeWidget);
    QITreeWidget *pTreeWidget = treeWidget(type);
    AssertPtrReturnVoid(pTreeWidget);
    {
        /* Configure tree-widget: */
        pTreeWidget->setExpandsOnDoubleClick(false);
        pTreeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        pTreeWidget->setAlternatingRowColors(true);
        pTreeWidget->setAllColumnsShowFocus(true);
        pTreeWidget->setAcceptDrops(true);
        pTreeWidget->setColumnCount(iColumns);
        pTreeWidget->sortItems(0, Qt::AscendingOrder);
        if (iColumns > 0)
            pTreeWidget->header()->setSectionResizeMode(0, QHeaderView::Fixed);
        if (iColumns > 1)
            pTreeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        if (iColumns > 2)
            pTreeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        pTreeWidget->header()->setStretchLastSection(false);
        pTreeWidget->setSortingEnabled(true);
        connect(pTreeWidget, &QITreeWidget::currentItemChanged,
                this, &UIMediumManagerWidget::sltHandleCurrentItemChanged);
        connect(pTreeWidget, &QITreeWidget::itemDoubleClicked,
                m_pActionPool->action(UIActionIndexMN_M_Medium_T_Details), &QAction::setChecked);
        connect(pTreeWidget, &QITreeWidget::customContextMenuRequested,
                this, &UIMediumManagerWidget::sltHandleContextMenuRequest);
        connect(pTreeWidget, &QITreeWidget::resized,
                this, &UIMediumManagerWidget::sltPerformTablesAdjustment, Qt::QueuedConnection);
        connect(pTreeWidget->header(), &QHeaderView::sectionResized,
                this, &UIMediumManagerWidget::sltPerformTablesAdjustment, Qt::QueuedConnection);
        /* Add tree-widget into tab layout: */
        tab(type)->layout()->addWidget(pTreeWidget);
    }
}

void UIMediumManagerWidget::prepareDetailsWidget()
{
    /* Create details-widget: */
    m_pDetailsWidget = new UIMediumDetailsWidget(this, m_enmEmbedding);
    AssertPtrReturnVoid(m_pDetailsWidget);
    {
        /* Configure details-widget: */
        m_pDetailsWidget->setVisible(false);
        m_pDetailsWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        connect(m_pDetailsWidget, &UIMediumDetailsWidget::sigAcceptAllowed,
                this, &UIMediumManagerWidget::sigAcceptAllowed);
        connect(m_pDetailsWidget, &UIMediumDetailsWidget::sigRejectAllowed,
                this, &UIMediumManagerWidget::sigRejectAllowed);
        connect(m_pDetailsWidget, &UIMediumDetailsWidget::sigDataChangeRejected,
                this, &UIMediumManagerWidget::sltResetMediumDetailsChanges);
        connect(m_pDetailsWidget, &UIMediumDetailsWidget::sigDataChangeAccepted,
                this, &UIMediumManagerWidget::sltApplyMediumDetailsChanges);

        /* Add into layout: */
        layout()->addWidget(m_pDetailsWidget);
    }
}

void UIMediumManagerWidget::prepareSearchWidget()
{
    m_pSearchWidget = new UIMediumSearchWidget(this);
    AssertPtrReturnVoid(m_pSearchWidget);
    {
        m_pSearchWidget->setVisible(false);
        m_pSearchWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        connect(m_pSearchWidget, &UIMediumSearchWidget::sigPerformSearch,
                this, &UIMediumManagerWidget::sltHandlePerformSearch);

        /* Add into layout: */
        layout()->addWidget(m_pSearchWidget);
    }

}

void UIMediumManagerWidget::loadSettings()
{
    /* Details action/widget: */
    m_pActionPool->action(UIActionIndexMN_M_Medium_T_Details)->setChecked(gEDataManager->virtualMediaManagerDetailsExpanded());
    sltToggleMediumDetailsVisibility(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Details)->isChecked());

    /* Search action/widget: */
    m_pActionPool->action(UIActionIndexMN_M_Medium_T_Search)->setChecked(gEDataManager->virtualMediaManagerSearchWidgetExpanded());
    sltToggleMediumSearchVisibility(m_pActionPool->action(UIActionIndexMN_M_Medium_T_Search)->isChecked());
}

void UIMediumManagerWidget::repopulateTreeWidgets()
{
    /* Remember current medium-items: */
    if (UIMediumItem *pMediumItem = mediumItem(UIMediumDeviceType_HardDisk))
        m_uCurrentIdHD = pMediumItem->id();
    if (UIMediumItem *pMediumItem = mediumItem(UIMediumDeviceType_DVD))
        m_uCurrentIdCD = pMediumItem->id();
    if (UIMediumItem *pMediumItem = mediumItem(UIMediumDeviceType_Floppy))
        m_uCurrentIdFD = pMediumItem->id();

    /* Clear tree-widgets: */
    QITreeWidget *pTreeWidgetHD = treeWidget(UIMediumDeviceType_HardDisk);
    if (pTreeWidgetHD)
    {
        setCurrentItem(pTreeWidgetHD, 0);
        pTreeWidgetHD->clear();
    }
    QITreeWidget *pTreeWidgetCD = treeWidget(UIMediumDeviceType_DVD);
    if (pTreeWidgetCD)
    {
        setCurrentItem(pTreeWidgetCD, 0);
        pTreeWidgetCD->clear();
    }
    QITreeWidget *pTreeWidgetFD = treeWidget(UIMediumDeviceType_Floppy);
    if (pTreeWidgetFD)
    {
        setCurrentItem(pTreeWidgetFD, 0);
        pTreeWidgetFD->clear();
    }

    /* Create medium-items (do not change current one): */
    m_fPreventChangeCurrentItem = true;
    foreach (const QUuid &uMediumID, gpMediumEnumerator->mediumIDs())
        sltHandleMediumCreated(uMediumID);
    m_fPreventChangeCurrentItem = false;

    /* Select first item as current one if nothing selected: */
    if (pTreeWidgetHD && !mediumItem(UIMediumDeviceType_HardDisk))
        if (QTreeWidgetItem *pItem = pTreeWidgetHD->topLevelItem(0))
            setCurrentItem(pTreeWidgetHD, pItem);
    if (pTreeWidgetCD && !mediumItem(UIMediumDeviceType_DVD))
        if (QTreeWidgetItem *pItem = pTreeWidgetCD->topLevelItem(0))
            setCurrentItem(pTreeWidgetCD, pItem);
    if (pTreeWidgetFD && !mediumItem(UIMediumDeviceType_Floppy))
        if (QTreeWidgetItem *pItem = pTreeWidgetFD->topLevelItem(0))
            setCurrentItem(pTreeWidgetFD, pItem);

    sltHandlePerformSearch();
}

void UIMediumManagerWidget::refetchCurrentMediumItem(UIMediumDeviceType type)
{
    /* Get corresponding medium-item: */
    UIMediumItem *pMediumItem = mediumItem(type);

#ifdef VBOX_WS_MAC
    /* Set the file for the proxy icon: */
    if (pMediumItem == currentMediumItem())
        setWindowFilePath(pMediumItem ? pMediumItem->location() : QString());
#endif /* VBOX_WS_MAC */

    /* Make sure current medium-item visible: */
    if (pMediumItem)
        treeWidget(type)->scrollToItem(pMediumItem, QAbstractItemView::EnsureVisible);

    /* Update actions: */
    updateActions();

    /* Update details-widget: */
    if (m_pDetailsWidget)
    {
        m_pDetailsWidget->setData(pMediumItem ? *pMediumItem : UIDataMedium(type));
        if (pMediumItem && currentMediumItem())
            m_pDetailsWidget->enableDisableMediumModificationWidgets(currentMediumItem()->isMediumModifiable());
    }
}

void UIMediumManagerWidget::refetchCurrentChosenMediumItem()
{
    refetchCurrentMediumItem(currentMediumType());
}

void UIMediumManagerWidget::refetchCurrentMediumItems()
{
    refetchCurrentMediumItem(UIMediumDeviceType_HardDisk);
    refetchCurrentMediumItem(UIMediumDeviceType_DVD);
    refetchCurrentMediumItem(UIMediumDeviceType_Floppy);
}

void UIMediumManagerWidget::updateActions()
{
    /* Get current medium-item: */
    UIMediumItem *pMediumItem = currentMediumItem();

    /* Calculate actions accessibility: */
    const bool fNotInEnumeration = !UIMediumEnumerator::exists() || !gpMediumEnumerator->isMediumEnumerationInProgress();

    /* Apply actions accessibility: */
    const bool fActionEnabledCopy = fNotInEnumeration && pMediumItem && checkMediumFor(pMediumItem, Action_Copy);
    m_pActionPool->action(UIActionIndexMN_M_Medium_S_Copy)->setEnabled(fActionEnabledCopy);
    const bool fActionEnabledMove = fNotInEnumeration && pMediumItem && checkMediumFor(pMediumItem, Action_Edit);
    m_pActionPool->action(UIActionIndexMN_M_Medium_S_Move)->setEnabled(fActionEnabledMove);
    const bool fActionEnabledRemove = fNotInEnumeration && pMediumItem && checkMediumFor(pMediumItem, Action_Remove);
    m_pActionPool->action(UIActionIndexMN_M_Medium_S_Remove)->setEnabled(fActionEnabledRemove);
    const bool fActionEnabledRelease = fNotInEnumeration && pMediumItem && checkMediumFor(pMediumItem, Action_Release);
    m_pActionPool->action(UIActionIndexMN_M_Medium_S_Release)->setEnabled(fActionEnabledRelease);
    const bool fActionEnabledDetails = true;
    m_pActionPool->action(UIActionIndexMN_M_Medium_T_Details)->setEnabled(fActionEnabledDetails);
}

void UIMediumManagerWidget::updateActionIcons()
{
    const UIMediumDeviceType enmCurrentMediumType = currentMediumType();
    if (enmCurrentMediumType != UIMediumDeviceType_Invalid)
    {
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Add)->setState((int)enmCurrentMediumType);
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Create)->setState((int)enmCurrentMediumType);
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Copy)->setState((int)enmCurrentMediumType);
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Move)->setState((int)enmCurrentMediumType);
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Remove)->setState((int)enmCurrentMediumType);
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Release)->setState((int)enmCurrentMediumType);
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Clear)->setState((int)enmCurrentMediumType);
        m_pActionPool->action(UIActionIndexMN_M_Medium_T_Search)->setState((int)enmCurrentMediumType);
        m_pActionPool->action(UIActionIndexMN_M_Medium_T_Details)->setState((int)enmCurrentMediumType);
    }
}

void UIMediumManagerWidget::updateTabIcons(UIMediumItem *pMediumItem, Action action)
{
    /* Make sure medium-item is valid: */
    AssertReturnVoid(pMediumItem);

    /* Prepare data for tab: */
    const QIcon *pIcon = 0;
    bool *pfInaccessible = 0;
    const UIMediumDeviceType mediumType = pMediumItem->mediumType();
    switch (mediumType)
    {
        case UIMediumDeviceType_HardDisk:
            pIcon = &m_iconHD;
            pfInaccessible = &m_fInaccessibleHD;
            break;
        case UIMediumDeviceType_DVD:
            pIcon = &m_iconCD;
            pfInaccessible = &m_fInaccessibleCD;
            break;
        case UIMediumDeviceType_Floppy:
            pIcon = &m_iconFD;
            pfInaccessible = &m_fInaccessibleFD;
            break;
        default:
            AssertFailed();
    }
    AssertReturnVoid(pIcon && pfInaccessible);

    switch (action)
    {
        case Action_Add:
        {
            /* Does it change the overall state? */
            if (*pfInaccessible || pMediumItem->state() != KMediumState_Inaccessible)
                break; /* no */

            *pfInaccessible = true;

            if (m_pTabWidget)
                m_pTabWidget->setTabIcon(tabIndex(mediumType), generalIconPool().warningIcon());

            break;
        }
        case Action_Edit:
        case Action_Remove:
        {
            bool fCheckRest = false;

            if (action == Action_Edit)
            {
                /* Does it change the overall state? */
                if ((*pfInaccessible && pMediumItem->state() == KMediumState_Inaccessible) ||
                    (!*pfInaccessible && pMediumItem->state() != KMediumState_Inaccessible))
                    break; /* no */

                /* Is the given item in charge? */
                if (!*pfInaccessible && pMediumItem->state() == KMediumState_Inaccessible)
                    *pfInaccessible = true; /* yes */
                else
                    fCheckRest = true; /* no */
            }
            else
                fCheckRest = true;

            if (fCheckRest)
            {
                /* Find the first KMediumState_Inaccessible item to be in charge: */
                CheckIfSuitableByState lookForState(KMediumState_Inaccessible);
                CheckIfSuitableByID ignoreID(pMediumItem->id());
                UIMediumItem *pInaccessibleMediumItem = searchItem(pMediumItem->parentTree(), lookForState, &ignoreID);
                *pfInaccessible = !!pInaccessibleMediumItem;
            }

            if (m_pTabWidget)
            {
                if (*pfInaccessible)
                    m_pTabWidget->setTabIcon(tabIndex(mediumType), generalIconPool().warningIcon());
                else
                    m_pTabWidget->setTabIcon(tabIndex(mediumType), *pIcon);
            }

            break;
        }

        default:
            break;
    }
}

UIMediumItem* UIMediumManagerWidget::createMediumItem(const UIMedium &medium)
{
    /* Get medium type: */
    UIMediumDeviceType type = medium.type();

    /* Create medium-item: */
    UIMediumItem *pMediumItem = 0;
    switch (type)
    {
        /* Of hard-drive type: */
        case UIMediumDeviceType_HardDisk:
        {
            /* Make sure corresponding tree-widget exists: */
            QITreeWidget *pTreeWidget = treeWidget(UIMediumDeviceType_HardDisk);
            if (pTreeWidget)
            {
                /* Recursively create hard-drive item: */
                pMediumItem = createHardDiskItem(medium);
                /* Make sure item was created: */
                if (!pMediumItem)
                    break;
                if (pMediumItem->id() == m_uCurrentIdHD)
                {
                    setCurrentItem(pTreeWidget, pMediumItem);
                    m_uCurrentIdHD = QUuid();
                }
            }
            break;
        }
        /* Of optical-image type: */
        case UIMediumDeviceType_DVD:
        {
            /* Make sure corresponding tree-widget exists: */
            QITreeWidget *pTreeWidget = treeWidget(UIMediumDeviceType_DVD);
            if (pTreeWidget)
            {
                /* Create optical-disk item: */
                pMediumItem = new UIMediumItemCD(medium, pTreeWidget);
                /* Make sure item was created: */
                if (!pMediumItem)
                    break;
                LogRel2(("UIMediumManager: Optical medium-item with ID={%s} created.\n", medium.id().toString().toUtf8().constData()));
                if (pMediumItem->id() == m_uCurrentIdCD)
                {
                    setCurrentItem(pTreeWidget, pMediumItem);
                    m_uCurrentIdCD = QUuid();
                }
            }
            break;
        }
        /* Of floppy-image type: */
        case UIMediumDeviceType_Floppy:
        {
            /* Make sure corresponding tree-widget exists: */
            QITreeWidget *pTreeWidget = treeWidget(UIMediumDeviceType_Floppy);
            if (pTreeWidget)
            {
                /* Create floppy-disk item: */
                pMediumItem = new UIMediumItemFD(medium, pTreeWidget);
                /* Make sure item was created: */
                if (!pMediumItem)
                    break;
                LogRel2(("UIMediumManager: Floppy medium-item with ID={%s} created.\n", medium.id().toString().toUtf8().constData()));
                if (pMediumItem->id() == m_uCurrentIdFD)
                {
                    setCurrentItem(pTreeWidget, pMediumItem);
                    m_uCurrentIdFD = QUuid();
                }
            }
            break;
        }
        default: AssertMsgFailed(("Medium-type unknown: %d\n", type)); break;
    }

    /* Make sure item was created: */
    if (!pMediumItem)
        return 0;

    /* Update tab-icons: */
    updateTabIcons(pMediumItem, Action_Add);

    /* Toogle enable/disable of clear action: */
    enableClearAction();

    /* Reperform the medium search (don't jump to the found element): */
    performSearch(false);

    /* Re-fetch medium-item if it is current one created: */
    if (pMediumItem == mediumItem(type))
        refetchCurrentMediumItem(type);

    /* Return created medium-item: */
    return pMediumItem;
}

UIMediumItem* UIMediumManagerWidget::createHardDiskItem(const UIMedium &medium)
{
    /* Make sure passed medium is valid: */
    AssertReturn(!medium.medium().isNull(), 0);

    /* Make sure corresponding tree-widget exists: */
    QITreeWidget *pTreeWidget = treeWidget(UIMediumDeviceType_HardDisk);
    if (pTreeWidget)
    {
        /* Search for existing medium-item: */
        UIMediumItem *pMediumItem = searchItem(pTreeWidget, CheckIfSuitableByID(medium.id()));

        /* If medium-item do not exists: */
        if (!pMediumItem)
        {
            /* If medium have a parent: */
            if (medium.parentID() != UIMedium::nullID())
            {
                /* Try to find parent medium-item: */
                UIMediumItem *pParentMediumItem = searchItem(pTreeWidget, CheckIfSuitableByID(medium.parentID()));
                /* If parent medium-item was not found: */
                if (!pParentMediumItem)
                {
                    /* Make sure corresponding parent medium is already cached! */
                    UIMedium parentMedium = gpMediumEnumerator->medium(medium.parentID());
                    if (parentMedium.isNull())
                        AssertMsgFailed(("Parent medium with ID={%s} was not found!\n", medium.parentID().toString().toUtf8().constData()));
                    /* Try to create parent medium-item: */
                    else
                        pParentMediumItem = createHardDiskItem(parentMedium);
                }
                /* If parent medium-item was found: */
                if (pParentMediumItem)
                {
                    pMediumItem = new UIMediumItemHD(medium, pParentMediumItem);
                    LogRel2(("UIMediumManager: Child hard-disk medium-item with ID={%s} created.\n", medium.id().toString().toUtf8().constData()));
                }
            }
            /* Else just create item as top-level one: */
            if (!pMediumItem)
            {
                pMediumItem = new UIMediumItemHD(medium, pTreeWidget);
                LogRel2(("UIMediumManager: Root hard-disk medium-item with ID={%s} created.\n", medium.id().toString().toUtf8().constData()));
            }
        }

        /* Return created medium-item: */
        return pMediumItem;
    }

    /* Return null by default: */
    return 0;
}

void UIMediumManagerWidget::updateMediumItem(const UIMedium &medium)
{
    /* Get medium type: */
    UIMediumDeviceType type = medium.type();

    /* Search for existing medium-item: */
    UIMediumItem *pMediumItem = searchItem(treeWidget(type), CheckIfSuitableByID(medium.id()));

    /* Create item if doesn't exists: */
    if (!pMediumItem)
        pMediumItem = createMediumItem(medium);

    /* Make sure item was created: */
    if (!pMediumItem)
        return;

    /* Update medium-item: */
    pMediumItem->setMedium(medium);
    LogRel2(("UIMediumManager: Medium-item with ID={%s} updated.\n", medium.id().toString().toUtf8().constData()));

    /* Update tab-icons: */
    updateTabIcons(pMediumItem, Action_Edit);

    /* Toogle enable/disable of clear action: */
    enableClearAction();

    /* Re-fetch medium-item if it is current one updated: */
    if (pMediumItem == mediumItem(type))
        refetchCurrentMediumItem(type);

    /* Update all the children recursively as well: */
    foreach(const QUuid &uMediumId, gpMediumEnumerator->mediumIDs())
    {
        UIMedium guiMedium = gpMediumEnumerator->medium(uMediumId);
        if (   !guiMedium.isNull()
            && guiMedium.parentID() == medium.id())
            updateMediumItem(guiMedium);
    }
}

void UIMediumManagerWidget::deleteMediumItem(const QUuid &uMediumID)
{
    /* Search for corresponding tree-widget: */
    QList<UIMediumDeviceType> types;
    types << UIMediumDeviceType_HardDisk << UIMediumDeviceType_DVD << UIMediumDeviceType_Floppy;
    QITreeWidget *pTreeWidget = 0;
    UIMediumItem *pMediumItem = 0;
    foreach (UIMediumDeviceType type, types)
    {
        /* Get iterated tree-widget: */
        pTreeWidget = treeWidget(type);
        /* Search for existing medium-item: */
        pMediumItem = searchItem(pTreeWidget, CheckIfSuitableByID(uMediumID));
        if (pMediumItem)
            break;
    }

    /* Make sure item was found: */
    if (!pMediumItem)
        return;

    /* Update tab-icons: */
    updateTabIcons(pMediumItem, Action_Remove);

    /* Toogle enable/disable of clear action: */
    enableClearAction();

    /* Delete medium-item: */
    delete pMediumItem;
    LogRel2(("UIMediumManager: Medium-item with ID={%s} deleted.\n", uMediumID.toString().toUtf8().constData()));

    /* Reperform the medium search (don't jump to the found element): */
    performSearch(false);

    /* If there is no current medium-item now selected
     * we have to choose first-available medium-item as current one: */
    if (!pTreeWidget->currentItem())
        setCurrentItem(pTreeWidget, pTreeWidget->topLevelItem(0));
}

QWidget* UIMediumManagerWidget::tab(UIMediumDeviceType type) const
{
    /* Determine tab index for passed medium type: */
    int iIndex = tabIndex(type);

    /* Return tab for known tab index: */
    if (iIndex >= 0 && iIndex < m_iTabCount)
        return iIndex < m_pTabWidget->count() ? m_pTabWidget->widget(iIndex) : 0;

    /* Null by default: */
    return 0;
}

QITreeWidget* UIMediumManagerWidget::treeWidget(UIMediumDeviceType type) const
{
    /* Determine tab index for passed medium type: */
    int iIndex = tabIndex(type);

    /* Return tree-widget for known tab index: */
    if (iIndex >= 0 && iIndex < m_iTabCount)
        return m_trees.value(iIndex, 0);

    /* Null by default: */
    return 0;
}

UIMediumItem* UIMediumManagerWidget::mediumItem(UIMediumDeviceType type) const
{
    /* Get corresponding tree-widget: */
    QITreeWidget *pTreeWidget = treeWidget(type);
    /* Return corresponding medium-item: */
    return pTreeWidget ? toMediumItem(pTreeWidget->currentItem()) : 0;
}

UIMediumDeviceType UIMediumManagerWidget::mediumType(QITreeWidget *pTreeWidget) const
{
    /* Determine tab index of passed tree-widget: */
    int iIndex = m_trees.key(pTreeWidget, -1);

    /* Return medium type for known tab index: */
    if (iIndex >= 0 && iIndex < m_iTabCount)
        return (UIMediumDeviceType)iIndex;

    /* Invalid by default: */
    AssertFailedReturn(UIMediumDeviceType_Invalid);
}

UIMediumDeviceType UIMediumManagerWidget::currentMediumType() const
{
    /* Invalid if tab-widget doesn't exists: */
    if (!m_pTabWidget)
        return UIMediumDeviceType_Invalid;

    /* Return current medium type: */
    return (UIMediumDeviceType)m_pTabWidget->currentIndex();
}

QITreeWidget* UIMediumManagerWidget::currentTreeWidget() const
{
    /* Return current tree-widget: */
    return treeWidget(currentMediumType());
}

UIMediumItem* UIMediumManagerWidget::currentMediumItem() const
{
    /* Return current medium-item: */
    return mediumItem(currentMediumType());
}

void UIMediumManagerWidget::setCurrentItem(QITreeWidget *pTreeWidget, QTreeWidgetItem *pItem)
{
    /* Make sure passed tree-widget is valid: */
    AssertPtrReturnVoid(pTreeWidget);

    /* Make passed item current for passed tree-widget: */
    pTreeWidget->setCurrentItem(pItem);

    /* If non NULL item was passed: */
    if (pItem)
    {
        /* Make sure it's also selected, and visible: */
        pItem->setSelected(true);
        pTreeWidget->scrollToItem(pItem, QAbstractItemView::EnsureVisible);
    }

    /* Re-fetch currently chosen medium-item: */
    refetchCurrentChosenMediumItem();
}

void UIMediumManagerWidget::enableClearAction()
{
    if (!m_pActionPool || !m_pActionPool->action(UIActionIndexMN_M_Medium_S_Clear))
        return;

    if (currentMediumType() == UIMediumDeviceType_HardDisk)
    {
        m_pActionPool->action(UIActionIndexMN_M_Medium_S_Clear)->setVisible(false);
        return;
    }
    m_pActionPool->action(UIActionIndexMN_M_Medium_S_Clear)->setVisible(true);
    bool fEnable = ((currentMediumType() == UIMediumDeviceType_DVD) && m_fInaccessibleCD) ||
        ((currentMediumType() == UIMediumDeviceType_Floppy) && m_fInaccessibleFD);
    m_pActionPool->action(UIActionIndexMN_M_Medium_S_Clear)->setEnabled(fEnable);
}

void UIMediumManagerWidget::performSearch(bool fSelectNext)
{
    if (!m_pSearchWidget || !m_pTabWidget)
        return;

    QITreeWidget *pTreeWidget = treeWidget(static_cast<UIMediumDeviceType>(m_pTabWidget->currentIndex()));
    if (!pTreeWidget)
        return;
    m_pSearchWidget->search(pTreeWidget, fSelectNext);
}

/* static */
int UIMediumManagerWidget::tabIndex(UIMediumDeviceType type)
{
    /* Return tab index corresponding to known medium type: */
    switch (type)
    {
        case UIMediumDeviceType_HardDisk: return 0;
        case UIMediumDeviceType_DVD:      return 1;
        case UIMediumDeviceType_Floppy:   return 2;
        default: break;
    }

    /* -1 by default: */
    return -1;
}

/* static */
UIMediumItem* UIMediumManagerWidget::searchItem(QITreeWidget *pTreeWidget, const CheckIfSuitableBy &condition, CheckIfSuitableBy *pException)
{
    /* Make sure argument is valid: */
    if (!pTreeWidget)
        return 0;

    /* Return wrapper: */
    return searchItem(pTreeWidget->invisibleRootItem(), condition, pException);
}

/* static */
UIMediumItem* UIMediumManagerWidget::searchItem(QTreeWidgetItem *pParentItem, const CheckIfSuitableBy &condition, CheckIfSuitableBy *pException)
{
    /* Make sure argument is valid: */
    if (!pParentItem)
        return 0;

    /* Verify passed item if it is of 'medium' type too: */
    if (UIMediumItem *pMediumParentItem = toMediumItem(pParentItem))
        if (   condition.isItSuitable(pMediumParentItem)
            && (!pException || !pException->isItSuitable(pMediumParentItem)))
            return pMediumParentItem;

    /* Iterate other all the children: */
    for (int iChildIndex = 0; iChildIndex < pParentItem->childCount(); ++iChildIndex)
        if (UIMediumItem *pMediumChildItem = toMediumItem(pParentItem->child(iChildIndex)))
            if (UIMediumItem *pRequiredMediumChildItem = searchItem(pMediumChildItem, condition, pException))
                return pRequiredMediumChildItem;

    /* Null by default: */
    return 0;
}

/* static */
bool UIMediumManagerWidget::checkMediumFor(UIMediumItem *pItem, Action action)
{
    /* Make sure passed ID is valid: */
    AssertReturn(pItem, false);

    switch (action)
    {
        case Action_Edit:
        {
            /* Edit means changing the description and alike; any media that is
             * not being read to or written from can be altered in these terms. */
            switch (pItem->state())
            {
                case KMediumState_NotCreated:
                case KMediumState_Inaccessible:
                case KMediumState_LockedRead:
                case KMediumState_LockedWrite:
                    return false;
                default:
                    break;
            }
            return true;
        }
        case Action_Copy:
        {
            return true;
        }
        case Action_Remove:
        {
            /* Removable if not attached to anything: */
            return !pItem->isUsed();
        }
        case Action_Release:
        {
            /* Releasable if attached but not in snapshots: */
            return pItem->isUsed() && !pItem->isUsedInSnapshots();
        }

        default:
            break;
    }

    AssertFailedReturn(false);
}

/* static */
UIMediumItem* UIMediumManagerWidget::toMediumItem(QTreeWidgetItem *pItem)
{
    /* Cast passed QTreeWidgetItem to UIMediumItem if possible: */
    return pItem && pItem->type() == QITreeWidgetItem::ItemType ? static_cast<UIMediumItem*>(pItem) : 0;
}


/*********************************************************************************************************************************
*   Class UIMediumManagerFactory implementation.                                                                                 *
*********************************************************************************************************************************/

UIMediumManagerFactory::UIMediumManagerFactory(UIActionPool *pActionPool /* = 0 */)
    : m_pActionPool(pActionPool)
{
}

void UIMediumManagerFactory::create(QIManagerDialog *&pDialog, QWidget *pCenterWidget)
{
    pDialog = new UIMediumManager(pCenterWidget, m_pActionPool);
}


/*********************************************************************************************************************************
*   Class UIMediumManager implementation.                                                                                        *
*********************************************************************************************************************************/

UIMediumManager::UIMediumManager(QWidget *pCenterWidget, UIActionPool *pActionPool)
    : QIManagerDialog(pCenterWidget)
    , m_pActionPool(pActionPool)
    , m_pProgressBar(0)
{
}

void UIMediumManager::sltHandleButtonBoxClick(QAbstractButton *pButton)
{
    /* Disable buttons first of all: */
    button(ButtonType_Reset)->setEnabled(false);
    button(ButtonType_Apply)->setEnabled(false);

    /* Compare with known buttons: */
    if (pButton == button(ButtonType_Reset))
        emit sigDataChangeRejected();
    else
    if (pButton == button(ButtonType_Apply))
        emit sigDataChangeAccepted();
}

void UIMediumManager::sltRetranslateUI()
{
    /* Translate window title: */
    setWindowTitle(tr("Virtual Media Manager"));

    /* Translate buttons: */
    button(ButtonType_Reset)->setText(tr("Reset"));
    button(ButtonType_Apply)->setText(tr("Apply"));
    button(ButtonType_Close)->setText(tr("Close"));
    button(ButtonType_Help)->setText(tr("Help"));
    button(ButtonType_Reset)->setStatusTip(tr("Reset changes in current medium details"));
    button(ButtonType_Apply)->setStatusTip(tr("Apply changes in current medium details"));
    button(ButtonType_Close)->setStatusTip(tr("Close dialog without saving"));
    button(ButtonType_Help)->setStatusTip(tr("Show dialog help"));
    button(ButtonType_Reset)->setShortcut(QString("Ctrl+Backspace"));
    button(ButtonType_Apply)->setShortcut(QString("Ctrl+Return"));
    button(ButtonType_Close)->setShortcut(Qt::Key_Escape);
    button(ButtonType_Help)->setShortcut(UIShortcutPool::standardSequence(QKeySequence::HelpContents));
    button(ButtonType_Reset)->setToolTip(tr("Reset Changes (%1)").arg(button(ButtonType_Reset)->shortcut().toString()));
    button(ButtonType_Apply)->setToolTip(tr("Apply Changes (%1)").arg(button(ButtonType_Apply)->shortcut().toString()));
    button(ButtonType_Close)->setToolTip(tr("Close Window (%1)").arg(button(ButtonType_Close)->shortcut().toString()));
    button(ButtonType_Help)->setToolTip(tr("Show Help (%1)").arg(button(ButtonType_Help)->shortcut().toString()));
}

void UIMediumManager::configure()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/media_manager_32px.png", ":/media_manager_16px.png"));
#endif
}

void UIMediumManager::configureCentralWidget()
{
    /* Create widget: */
    UIMediumManagerWidget *pWidget = new UIMediumManagerWidget(EmbedTo_Dialog, m_pActionPool, true, this);
    AssertPtrReturnVoid(pWidget);
    {
        /* Configure widget: */
        setWidget(pWidget);
        setWidgetMenu(pWidget->menu());
#ifdef VBOX_WS_MAC
        setWidgetToolbar(pWidget->toolbar());
#endif
        connect(this, &UIMediumManager::sigDataChangeRejected,
                pWidget, &UIMediumManagerWidget::sltResetMediumDetailsChanges);
        connect(this, &UIMediumManager::sigDataChangeAccepted,
                pWidget, &UIMediumManagerWidget::sltApplyMediumDetailsChanges);

        /* Add into layout: */
        centralWidget()->layout()->addWidget(pWidget);
    }
}

void UIMediumManager::configureButtonBox()
{
    /* Configure button-box: */
    connect(widget(), &UIMediumManagerWidget::sigMediumDetailsVisibilityChanged,
            button(ButtonType_Apply), &QPushButton::setVisible);
    connect(widget(), &UIMediumManagerWidget::sigMediumDetailsVisibilityChanged,
            button(ButtonType_Reset), &QPushButton::setVisible);
    connect(widget(), &UIMediumManagerWidget::sigAcceptAllowed,
            button(ButtonType_Apply), &QPushButton::setEnabled);
    connect(widget(), &UIMediumManagerWidget::sigRejectAllowed,
            button(ButtonType_Reset), &QPushButton::setEnabled);
    connect(buttonBox(), &QIDialogButtonBox::clicked,
            this, &UIMediumManager::sltHandleButtonBoxClick);
    // WORKAROUND:
    // Since we connected signals later than extra-data loaded
    // for signals above, we should handle that stuff here again:
    button(ButtonType_Apply)->setVisible(gEDataManager->virtualMediaManagerDetailsExpanded());
    button(ButtonType_Reset)->setVisible(gEDataManager->virtualMediaManagerDetailsExpanded());

    /* Create progress-bar: */
    m_pProgressBar = new UIEnumerationProgressBar;
    AssertPtrReturnVoid(m_pProgressBar);
    {
        /* Configure progress-bar: */
        m_pProgressBar->hide();
        /* Add progress-bar into button-box layout: */
        buttonBox()->addExtraWidget(m_pProgressBar);
        /* Notify widget it has progress-bar: */
        widget()->setProgressBar(m_pProgressBar);
    }
}

void UIMediumManager::finalize()
{
    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMediumManager::sltRetranslateUI);
}

UIMediumManagerWidget *UIMediumManager::widget()
{
    return qobject_cast<UIMediumManagerWidget*>(QIManagerDialog::widget());
}
