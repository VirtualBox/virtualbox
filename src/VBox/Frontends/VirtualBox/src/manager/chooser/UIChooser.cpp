/* $Id: UIChooser.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooser class implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <QVBoxLayout>

/* GUI includes: */
#include "UIChooser.h"
#include "UIChooserModel.h"
#include "UIChooserView.h"
#include "UIVirtualMachineItem.h"


UIChooser::UIChooser(QWidget *pParent, UIActionPool *pActionPool)
    : QWidget(pParent)
    , m_pActionPool(pActionPool)
    , m_pChooserModel(0)
    , m_pChooserView(0)
{
    prepare();
}

UIChooser::~UIChooser()
{
    cleanup();
}

bool UIChooser::isGroupSavingInProgress() const
{
    AssertPtrReturn(model(), false);
    return model()->isGroupSavingInProgress();
}

void UIChooser::setKeepCloudNodesUpdated(bool fUpdate)
{
    AssertPtrReturnVoid(model());
    return model()->setKeepCloudNodesUpdated(fUpdate);
}

bool UIChooser::isCloudProfileUpdateInProgress() const
{
    AssertPtrReturn(model(), false);
    return model()->isCloudProfileUpdateInProgress();
}

QList<UIVirtualMachineItemCloud*> UIChooser::cloudMachineItems() const
{
    AssertPtrReturn(model(), QList<UIVirtualMachineItemCloud*>());
    return model()->cloudMachineItems();
}

UIVirtualMachineItem *UIChooser::currentItem() const
{
    AssertPtrReturn(model(), 0);
    return model()->firstSelectedMachineItem();
}

QList<UIVirtualMachineItem*> UIChooser::currentItems() const
{
    AssertPtrReturn(model(), QList<UIVirtualMachineItem*>());
    return model()->selectedMachineItems();
}

bool UIChooser::isGroupItemSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isGroupItemSelected();
}

bool UIChooser::isGlobalItemSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isGlobalItemSelected();
}

bool UIChooser::isMachineItemSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isMachineItemSelected();
}

bool UIChooser::isLocalMachineItemSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isLocalMachineItemSelected();
}

bool UIChooser::isCloudMachineItemSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isCloudMachineItemSelected();
}

bool UIChooser::isSingleGroupSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isSingleGroupSelected();
}

bool UIChooser::isSingleLocalGroupSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isSingleLocalGroupSelected();
}

bool UIChooser::isSingleCloudProviderGroupSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isSingleCloudProviderGroupSelected();
}

bool UIChooser::isSingleCloudProfileGroupSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isSingleCloudProfileGroupSelected();
}

bool UIChooser::isAllItemsOfOneGroupSelected() const
{
    AssertPtrReturn(model(), false);
    return model()->isAllItemsOfOneGroupSelected();
}

QString UIChooser::fullGroupName() const
{
    AssertPtrReturn(model(), QString());
    return model()->fullGroupName();
}

void UIChooser::openGroupNameEditor()
{
    AssertPtrReturnVoid(model());
    model()->startEditingSelectedGroupItemName();
}

void UIChooser::disbandGroup()
{
    AssertPtrReturnVoid(model());
    model()->disbandSelectedGroupItem();
}

void UIChooser::removeMachine()
{
    AssertPtrReturnVoid(model());
    model()->removeSelectedMachineItems();
}

void UIChooser::moveMachineToGroup(const QString &strName)
{
    AssertPtrReturnVoid(model());
    model()->moveSelectedMachineItemsToGroupItem(strName);
}

QStringList UIChooser::possibleGroupsForMachineToMove(const QUuid &uId)
{
    AssertPtrReturn(model(), QStringList());
    return model()->possibleGroupNodeNamesForMachineNodeToMove(uId);
}

QStringList UIChooser::possibleGroupsForGroupToMove(const QString &strFullName)
{
    AssertPtrReturn(model(), QStringList());
    return model()->possibleGroupNodeNamesForGroupNodeToMove(strFullName);
}

void UIChooser::refreshMachine()
{
    AssertPtrReturnVoid(model());
    model()->refreshSelectedMachineItems();
}

void UIChooser::sortGroup()
{
    AssertPtrReturnVoid(model());
    model()->sortSelectedGroupItem();
}

void UIChooser::setMachineSearchWidgetVisibility(bool fVisible)
{
    AssertPtrReturnVoid(view());
    view()->setSearchWidgetVisible(fVisible);
}

void UIChooser::setCurrentMachine(const QUuid &uId)
{
    AssertPtrReturnVoid(model());
    model()->setCurrentMachineItem(uId);
}

void UIChooser::setCurrentGlobal()
{
    AssertPtrReturnVoid(model());
    model()->setCurrentGlobalItem();
}

void UIChooser::setGlobalItemHeightHint(int iHeight)
{
    AssertPtrReturnVoid(model());
    model()->setGlobalItemHeightHint(iHeight);
}

void UIChooser::sltToolMenuRequested(const QPoint &position, UIVirtualMachineItem *pItem)
{
    /* Translate scene coordinates to global one: */
    AssertPtrReturnVoid(view());
    emit sigToolMenuRequested(mapToGlobal(view()->mapFromScene(position)), pItem);
}

void UIChooser::prepare()
{
    /* Prepare everything: */
    prepareModel();
    prepareWidgets();
    prepareConnections();

    /* Init model: */
    initModel();
}

void UIChooser::prepareModel()
{
    /* Prepare Chooser-model: */
    m_pChooserModel = new UIChooserModel(this, actionPool());
}

void UIChooser::prepareWidgets()
{
    /* Prepare main-layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        pMainLayout->setContentsMargins(0, 0, 0, 0);
        pMainLayout->setSpacing(0);

        /* Prepare Chooser-view: */
        m_pChooserView = new UIChooserView(this);
        if (m_pChooserView)
        {
            AssertPtrReturnVoid(model());
            m_pChooserView->setModel(model());
            m_pChooserView->setScene(model()->scene());
            m_pChooserView->show();
            setFocusProxy(m_pChooserView);

            /* Add into layout: */
            pMainLayout->addWidget(m_pChooserView);
        }
    }
}

void UIChooser::prepareConnections()
{
    AssertPtrReturnVoid(model());
    AssertPtrReturnVoid(view());

    /* Abstract Chooser-model connections: */
    connect(model(), &UIChooserModel::sigGroupSavingStateChanged,
            this, &UIChooser::sigGroupSavingStateChanged);
    connect(model(), &UIChooserModel::sigCloudProfileStateChange,
            this, &UIChooser::sigCloudProfileStateChange);
    connect(model(), &UIChooserModel::sigCloudMachineStateChange,
            this, &UIChooser::sigCloudMachineStateChange);
    connect(model(), &UIChooserModel::sigCloudUpdateStateChanged,
            this, &UIChooser::sigCloudUpdateStateChanged);

    /* Chooser-model connections: */
    connect(model(), &UIChooserModel::sigToolMenuRequested,
            this, &UIChooser::sltToolMenuRequested);
    connect(model(), &UIChooserModel::sigSelectionChanged,
            this, &UIChooser::sigSelectionChanged);
    connect(model(), &UIChooserModel::sigSelectionInvalidated,
            this, &UIChooser::sigSelectionInvalidated);
    connect(model(), &UIChooserModel::sigToggleStarted,
            this, &UIChooser::sigToggleStarted);
    connect(model(), &UIChooserModel::sigToggleFinished,
            this, &UIChooser::sigToggleFinished);
    connect(model(), &UIChooserModel::sigRootItemMinimumWidthHintChanged,
            view(), &UIChooserView::sltMinimumWidthHintChanged);
    connect(model(), &UIChooserModel::sigStartOrShowRequest,
            this, &UIChooser::sigStartOrShowRequest);

    /* Chooser-view connections: */
    connect(view(), &UIChooserView::sigResized,
            model(), &UIChooserModel::sltHandleViewResized);
    connect(view(), &UIChooserView::sigSearchWidgetVisibilityChanged,
            this, &UIChooser::sigMachineSearchWidgetVisibilityChanged);
}

void UIChooser::initModel()
{
    AssertPtrReturnVoid(model());
    model()->init();
}

void UIChooser::deinitModel()
{
    AssertPtrReturnVoid(model());
    model()->deinit();
}

void UIChooser::cleanupConnections()
{
    AssertPtrReturnVoid(model());
    AssertPtrReturnVoid(view());

    /* Abstract Chooser-model connections: */
    disconnect(model(), &UIChooserModel::sigGroupSavingStateChanged,
               this, &UIChooser::sigGroupSavingStateChanged);
    disconnect(model(), &UIChooserModel::sigCloudProfileStateChange,
               this, &UIChooser::sigCloudProfileStateChange);
    disconnect(model(), &UIChooserModel::sigCloudMachineStateChange,
               this, &UIChooser::sigCloudMachineStateChange);
    disconnect(model(), &UIChooserModel::sigCloudUpdateStateChanged,
               this, &UIChooser::sigCloudUpdateStateChanged);

    /* Chooser-model connections: */
    disconnect(model(), &UIChooserModel::sigToolMenuRequested,
               this, &UIChooser::sltToolMenuRequested);
    disconnect(model(), &UIChooserModel::sigSelectionChanged,
               this, &UIChooser::sigSelectionChanged);
    disconnect(model(), &UIChooserModel::sigSelectionInvalidated,
               this, &UIChooser::sigSelectionInvalidated);
    disconnect(model(), &UIChooserModel::sigToggleStarted,
               this, &UIChooser::sigToggleStarted);
    disconnect(model(), &UIChooserModel::sigToggleFinished,
               this, &UIChooser::sigToggleFinished);
    disconnect(model(), &UIChooserModel::sigRootItemMinimumWidthHintChanged,
               view(), &UIChooserView::sltMinimumWidthHintChanged);
    disconnect(model(), &UIChooserModel::sigStartOrShowRequest,
               this, &UIChooser::sigStartOrShowRequest);

    /* Chooser-view connections: */
    disconnect(view(), &UIChooserView::sigResized,
               model(), &UIChooserModel::sltHandleViewResized);
    disconnect(view(), &UIChooserView::sigSearchWidgetVisibilityChanged,
               this, &UIChooser::sigMachineSearchWidgetVisibilityChanged);
}

void UIChooser::cleanup()
{
    /* Deinit model: */
    deinitModel();

    /* Cleanup everything: */
    cleanupConnections();
}
