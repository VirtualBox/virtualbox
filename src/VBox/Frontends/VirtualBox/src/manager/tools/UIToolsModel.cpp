/* $Id: UIToolsModel.cpp 168992 2025-05-26 13:30:39Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolsModel class implementation.
 */

/*
 * Copyright (C) 2012-2025 Oracle and/or its affiliates.
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
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QTransform>

/* GUI includes: */
#include "UICommon.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UILoggingDefs.h"
#include "UIToolsItem.h"
#include "UIToolsModel.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UIToolsModel::UIToolsModel(QObject *pParent, UIToolClass enmClass)
    : QObject(pParent)
    , m_enmClass(enmClass)
    , m_enmAlignment(m_enmClass == UIToolClass_Machine ? Qt::Horizontal : Qt::Vertical)
    , m_pView(0)
    , m_pScene(0)
    , m_fItemsEnabled(true)
    , m_fShowItemNames(gEDataManager->isToolTextVisible())
{
    prepare();
}

UIToolsModel::~UIToolsModel()
{
    cleanup();
}

void UIToolsModel::init()
{
    /* Update linked values: */
    updateLayout();
    sltItemMinimumWidthHintChanged();
    sltItemMinimumHeightHintChanged();

    /* Load current items: */
    loadCurrentItems();
}

QGraphicsScene *UIToolsModel::scene() const
{
    return m_pScene;
}

QPaintDevice *UIToolsModel::paintDevice() const
{
    if (scene() && !scene()->views().isEmpty())
        return scene()->views().first();
    return 0;
}

QGraphicsItem *UIToolsModel::itemAt(const QPointF &position) const
{
    return scene() ? scene()->itemAt(position, QTransform()) : 0;
}

UIToolsView *UIToolsModel::view() const
{
    return m_pView;
}

void UIToolsModel::setView(UIToolsView *pView)
{
    m_pView = pView;
}

void UIToolsModel::setToolsType(UIToolType enmType)
{
    const UIToolClass enmClass = UIToolStuff::castTypeToClass(enmType);
    if (!currentItem(enmClass) || currentItem(enmClass)->itemType() != enmType)
        setCurrentItem(item(enmType));
}

UIToolType UIToolsModel::toolsType(UIToolClass enmClass) const
{
    return currentItem(enmClass) ? currentItem(enmClass)->itemType() : UIToolType_Invalid;
}

void UIToolsModel::setItemsEnabled(bool fEnabled)
{
    if (m_fItemsEnabled != fEnabled)
    {
        m_fItemsEnabled = fEnabled;
        foreach (UIToolsItem *pItem, items())
            pItem->setEnabled(m_fItemsEnabled);
    }
}

bool UIToolsModel::isItemsEnabled() const
{
    return m_fItemsEnabled;
}

void UIToolsModel::setRestrictedToolTypes(UIToolClass enmClass, const QList<UIToolType> &types)
{
    if (m_mapRestrictedToolTypes.value(enmClass) != types)
    {
        m_mapRestrictedToolTypes[enmClass] = types;
        foreach (UIToolsItem *pItem, items())
        {
            if (pItem->itemClass() != enmClass)
                continue;
            const bool fRestricted = m_mapRestrictedToolTypes.value(enmClass).contains(pItem->itemType());
            pItem->setHiddenByReason(fRestricted, UIToolsItem::HidingReason_Restricted);
        }

        /* Update linked values: */
        updateLayout();
        sltItemMinimumWidthHintChanged();
        sltItemMinimumHeightHintChanged();
    }
}

QList<UIToolType> UIToolsModel::restrictedToolTypes(UIToolClass enmClass) const
{
    return m_mapRestrictedToolTypes.value(enmClass);
}

QVariant UIToolsModel::data(int iKey) const
{
    /* Provide other members with required data: */
    switch (iKey)
    {
        /* Layout hints: */
        case ToolsModelData_Margin: return 0;
        case ToolsModelData_Spacing: return 1;

        /* Default: */
        default: break;
    }
    return QVariant();
}

void UIToolsModel::setCurrentItem(UIToolsItem *pItem)
{
    /* Valid item passed? */
    if (pItem)
    {
        /* What's the item class? */
        const UIToolClass enmClass = pItem->itemClass();

        /* Is there something changed? */
        if (m_mapCurrentItems.value(enmClass) == pItem)
            return;

        /* Remember old current-item: */
        UIToolsItem *pOldCurrentItem = m_mapCurrentItems.value(enmClass);
        /* Set new item as current: */
        m_mapCurrentItems[enmClass] = pItem;

        /* Rebuild whole layout if names hidden for machine tools: */
        if (   !showItemNames()
            && m_enmClass == UIToolClass_Machine)
            updateLayout();

        /* Update old item (if any): */
        if (pOldCurrentItem)
            pOldCurrentItem->update();
        /* Update new item: */
        m_mapCurrentItems.value(enmClass)->update();

        /* Notify about selection change: */
        emit sigSelectionChanged(toolsType(enmClass));
    }
    /* Null item passed? */
    else
    {
        /* Is there something changed? */
        if (   !m_mapCurrentItems.value(UIToolClass_Global)
            && !m_mapCurrentItems.value(UIToolClass_Machine))
            return;

        /* Clear all current items: */
        m_mapCurrentItems[UIToolClass_Global] = 0;
        m_mapCurrentItems[UIToolClass_Machine] = 0;

        /* Notify about selection change: */
        emit sigSelectionChanged(UIToolType_Invalid);
    }
}

UIToolsItem *UIToolsModel::currentItem(UIToolClass enmClass) const
{
    return m_mapCurrentItems.value(enmClass);
}

QList<UIToolsItem*> UIToolsModel::items() const
{
    return m_items;
}

UIToolsItem *UIToolsModel::item(UIToolType enmType) const
{
    foreach (UIToolsItem *pItem, items())
        if (pItem->itemType() == enmType)
            return pItem;
    return 0;
}

bool UIToolsModel::showItemNames() const
{
    return m_fShowItemNames;
}

void UIToolsModel::updateLayout()
{
    /* Prepare variables: */
    const int iMargin = data(ToolsModelData_Margin).toInt();
    const int iSpacing = data(ToolsModelData_Spacing).toInt();
    const QSize viewportSize = scene()->views()[0]->viewport()->size();
    const int iViewportWidth = viewportSize.width();
    const int iViewportHeight = viewportSize.height();

    /* Depending on tool class: */
    switch (m_enmClass)
    {
        case UIToolClass_Global:
        {
            /* Start from above: */
            int iVerticalIndent = iMargin;

            /* Layout Global children: */
            foreach (UIToolsItem *pItem, items())
            {
                /* Skip everything besides Global children: */
                const UIToolClass enmClass = pItem->itemClass();
                if (enmClass != UIToolClass_Global)
                    continue;

                /* Make sure item visible: */
                if (!pItem->isVisible())
                    continue;

                /* Acquire item properties: */
                const int iItemHeight = pItem->minimumHeightHint();

                /* Set item position: */
                pItem->setPos(iMargin, iVerticalIndent);
                /* Set root-item size: */
                pItem->resize(iViewportWidth, iItemHeight);
                /* Make sure item is shown: */
                pItem->show();
                /* Advance vertical indent: */
                iVerticalIndent += (iItemHeight + iSpacing);
            }

            /* Start from bottom: */
            int iVerticalIndentAux = iViewportHeight - iMargin;

            /* Layout aux children: */
            foreach (UIToolsItem *pItem, items())
            {
                /* Skip everything besides Aux children: */
                if (pItem->itemClass() != UIToolClass_Aux)
                    continue;

                /* Set item position: */
                pItem->setPos(iMargin, iVerticalIndentAux - pItem->minimumHeightHint());
                /* Set root-item size: */
                pItem->resize(iViewportWidth, pItem->minimumHeightHint());
                /* Make sure item is shown: */
                pItem->show();
                /* Decrease vertical indent: */
                iVerticalIndentAux -= (pItem->minimumHeightHint() + iSpacing);
            }

            break;
        }

        case UIToolClass_Machine:
        {
            /* Start from left: */
            int iHorizontalIndent = iMargin;

            /* Layout Machine children: */
            foreach (UIToolsItem *pItem, items())
            {
                /* Skip everything besides Machine children: */
                const UIToolClass enmClass = pItem->itemClass();
                if (enmClass != UIToolClass_Machine)
                    continue;

                /* Make sure item visible: */
                if (!pItem->isVisible())
                    continue;

                /* Acquire item properties: */
                const int iItemWidth = pItem->minimumWidthHint();

                /* Set item position: */
                pItem->setPos(iHorizontalIndent, iMargin);
                /* Set root-item size: */
                pItem->resize(iItemWidth, iViewportHeight);
                /* Make sure item is shown: */
                pItem->show();
                /* Advance vertical indent: */
                iHorizontalIndent += (iItemWidth + iSpacing);
            }

            break;
        }

        default:
            AssertFailedReturnVoid();
            break;
    }
}

void UIToolsModel::sltItemMinimumWidthHintChanged()
{
    /* Prepare variables: */
    const int iMargin = data(ToolsModelData_Margin).toInt();
    const int iSpacing = data(ToolsModelData_Spacing).toInt();

    /* Calculate maximum horizontal width: */
    int iMinimumWidthHint = 0;
    iMinimumWidthHint += 2 * iMargin;

    switch (m_enmAlignment)
    {
        case Qt::Vertical:
            foreach (UIToolsItem *pItem, items())
                iMinimumWidthHint = qMax(iMinimumWidthHint, pItem->minimumWidthHint());
            break;
        case Qt::Horizontal:
            foreach (UIToolsItem *pItem, items())
                if (pItem->isVisible())
                    iMinimumWidthHint += (pItem->minimumWidthHint() + iSpacing);
            iMinimumWidthHint -= iSpacing;
            break;
    }

    /* Notify listeners: */
    emit sigItemMinimumWidthHintChanged(iMinimumWidthHint);
}

void UIToolsModel::sltItemMinimumHeightHintChanged()
{
    /* Prepare variables: */
    const int iMargin = data(ToolsModelData_Margin).toInt();
    const int iSpacing = data(ToolsModelData_Spacing).toInt();

    /* Calculate summary vertical height: */
    int iMinimumHeightHint = 0;
    iMinimumHeightHint += 2 * iMargin;

    switch (m_enmAlignment)
    {
        case Qt::Vertical:
            foreach (UIToolsItem *pItem, items())
                if (pItem->isVisible())
                    iMinimumHeightHint += (pItem->minimumHeightHint() + iSpacing);
            iMinimumHeightHint -= iSpacing;
            break;
        case Qt::Horizontal:
            foreach (UIToolsItem *pItem, items())
                iMinimumHeightHint = qMax(iMinimumHeightHint, pItem->minimumHeightHint());
            break;
    }

    /* Notify listeners: */
    emit sigItemMinimumHeightHintChanged(iMinimumHeightHint);
}

bool UIToolsModel::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Process only scene events: */
    if (pWatched != scene())
        return QObject::eventFilter(pWatched, pEvent);

    /* Checking event-type: */
    switch (pEvent->type())
    {
        /* Mouse handler: */
        case QEvent::GraphicsSceneMouseRelease:
        {
            /* Acquire event: */
            QGraphicsSceneMouseEvent *pMouseEvent = static_cast<QGraphicsSceneMouseEvent*>(pEvent);
            /* Get item under mouse cursor: */
            QPointF scenePos = pMouseEvent->scenePos();
            if (QGraphicsItem *pItemUnderMouse = itemAt(scenePos))
            {
                /* Which item we just clicked? Is it enabled? */
                UIToolsItem *pClickedItem = qgraphicsitem_cast<UIToolsItem*>(pItemUnderMouse);
                if (pClickedItem && pClickedItem->isEnabled())
                {
                    /* Handle known item classes: */
                    switch (pClickedItem->itemClass())
                    {
                        case UIToolClass_Aux:
                        {
                            /* Handle known item types: */
                            switch (pClickedItem->itemType())
                            {
                                case UIToolType_Toggle:
                                {
                                    /* Save the change: */
                                    gEDataManager->setToolTextVisible(!m_fShowItemNames);
                                    return true;
                                }
                                default:
                                    break;
                            }
                            break;
                        }
                        case UIToolClass_Global:
                        case UIToolClass_Machine:
                        {
                            /* Make clicked item the current one: */
                            if (pClickedItem->isEnabled())
                            {
                                setCurrentItem(pClickedItem);
                                return true;
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QObject::eventFilter(pWatched, pEvent);
}

void UIToolsModel::sltHandleCommitData()
{
    /* Save current items first of all: */
    saveCurrentItems();
}

void UIToolsModel::sltRetranslateUI()
{
    foreach (UIToolsItem *pItem, m_items)
    {
        switch (pItem->itemType())
        {
            // Aux
            case UIToolType_Toggle:      pItem->setName(tr("Show text")); break;
            // Global
            case UIToolType_Home:        pItem->setName(tr("Home")); break;
            case UIToolType_Machines:    pItem->setName(tr("Machines")); break;
            case UIToolType_Extensions:  pItem->setName(tr("Extensions")); break;
            case UIToolType_Media:       pItem->setName(tr("Media")); break;
            case UIToolType_Network:     pItem->setName(tr("Network")); break;
            case UIToolType_Cloud:       pItem->setName(tr("Cloud")); break;
            case UIToolType_Resources:   pItem->setName(tr("Resources")); break;
            // Machine
            case UIToolType_Details:     pItem->setName(tr("Details")); break;
            case UIToolType_Snapshots:   pItem->setName(tr("Snapshots")); break;
            case UIToolType_Logs:        pItem->setName(tr("Logs")); break;
            case UIToolType_VMActivity:  pItem->setName(tr("Resource Use")); break;
            case UIToolType_FileManager: pItem->setName(tr("File Manager")); break;
            default: break;
        }
    }
}

void UIToolsModel::sltHandleToolLabelsVisibilityChange(bool fVisible)
{
    /* Toggle the button: */
    m_fShowItemNames = fVisible;
    /* Update geometry for all the items: */
    foreach (UIToolsItem *pItem, m_items)
        pItem->updateGeometry();
    /* Recalculate layout: */
    updateLayout();
}

void UIToolsModel::prepare()
{
    /* Prepare everything: */
    prepareScene();
    prepareItems();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIToolsModel::prepareScene()
{
    m_pScene = new QGraphicsScene(this);
    if (m_pScene)
        m_pScene->installEventFilter(this);
}

void UIToolsModel::prepareItems()
{
    /* Depending on tool class: */
    switch (m_enmClass)
    {
        case UIToolClass_Global:
        {
            /* Home: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/welcome_screen_24px.png",
                                                                    ":/welcome_screen_24px.png"),
                                       UIToolType_Home);

            /* Machines: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/machine_details_manager_24px.png",
                                                                    ":/machine_details_manager_disabled_24px.png"),
                                       UIToolType_Machines);

            /* Extensions: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/extension_pack_manager_24px.png",
                                                                    ":/extension_pack_manager_disabled_24px.png"),
                                       UIToolType_Extensions);

            /* Media: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/media_manager_24px.png",
                                                                    ":/media_manager_disabled_24px.png"),
                                       UIToolType_Media);

            /* Network: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/host_iface_manager_24px.png",
                                                                    ":/host_iface_manager_disabled_24px.png"),
                                       UIToolType_Network);

            /* Cloud: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/cloud_profile_manager_24px.png",
                                                                    ":/cloud_profile_manager_disabled_24px.png"),
                                       UIToolType_Cloud);

            /* Resources: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/resources_monitor_24px.png",
                                                                    ":/resources_monitor_disabled_24px.png"),
                                       UIToolType_Resources);

            /* Toggle: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/tools_menu_24px.png",
                                                                    ":/tools_menu_24px.png"),
                                       UIToolType_Toggle);

            break;
        }
        case UIToolClass_Machine:
        {
            /* Details: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/machine_details_manager_24px.png",
                                                                    ":/machine_details_manager_disabled_24px.png"),
                                       UIToolType_Details);

            /* Snapshots: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/snapshot_manager_24px.png",
                                                                    ":/snapshot_manager_disabled_24px.png"),
                                       UIToolType_Snapshots);

            /* Logs: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/vm_show_logs_24px.png",
                                                                    ":/vm_show_logs_disabled_24px.png"),
                                       UIToolType_Logs);

            /* Activity: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/performance_monitor_24px.png",
                                                                    ":/performance_monitor_disabled_24px.png"),
                                       UIToolType_VMActivity);

            /* File Manager: */
            m_items << new UIToolsItem(scene(), UIIconPool::iconSet(":/file_manager_24px.png",
                                                                    ":/file_manager_disabled_24px.png"),
                                       UIToolType_FileManager);

            break;
        }
        default:
            AssertFailedReturnVoid();
            break;
    }
}

void UIToolsModel::prepareConnections()
{
    /* UICommon connections: */
    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIToolsModel::sltHandleCommitData);

    /* Translation stuff: */
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIToolsModel::sltRetranslateUI);

    /* Extra-data stuff: */
    connect(gEDataManager, &UIExtraDataManager::sigToolLabelsVisibilityChange,
            this, &UIToolsModel::sltHandleToolLabelsVisibilityChange);
}

void UIToolsModel::loadCurrentItems()
{
    /* Load last tool types: */
    UIToolType enmTypeGlobal, enmTypeMachine;
    gEDataManager->toolsPaneLastItemsChosen(enmTypeGlobal, enmTypeMachine);
    LogRel2(("GUI: UIToolsModel: Restoring tool items as: Global=%d, Machine=%d\n",
             (int)enmTypeGlobal, (int)enmTypeMachine));
    UIToolsItem *pItem = 0;

    /* Depending on tool class: */
    switch (m_enmClass)
    {
        case UIToolClass_Global:
        {
            pItem = item(enmTypeGlobal);
            if (!pItem)
                pItem = item(UIToolType_Home);
            setCurrentItem(pItem);
            break;
        }
        case UIToolClass_Machine:
        {
            pItem = item(enmTypeMachine);
            if (!pItem)
                pItem = item(UIToolType_Details);
            setCurrentItem(pItem);
            break;
        }
        default:
            AssertFailedReturnVoid();
            break;
    }
}

void UIToolsModel::saveCurrentItems()
{
    /* Load last tool types: */
    UIToolType enmTypeGlobal, enmTypeMachine;
    gEDataManager->toolsPaneLastItemsChosen(enmTypeGlobal, enmTypeMachine);

    /* Depending on tool class: */
    switch (m_enmClass)
    {
        case UIToolClass_Global:
        {
            if (UIToolsItem *pItem = currentItem(UIToolClass_Global))
                enmTypeGlobal = pItem->itemType();
            break;
        }
        case UIToolClass_Machine:
        {
            if (UIToolsItem *pItem = currentItem(UIToolClass_Machine))
                enmTypeMachine = pItem->itemType();
            break;
        }
        default:
            AssertFailedReturnVoid();
            break;
    }

    /* Save selected items data: */
    LogRel2(("GUI: UIToolsModel: Saving tool items as: Global=%d, Machine=%d\n",
             (int)enmTypeGlobal, (int)enmTypeMachine));
    gEDataManager->setToolsPaneLastItemsChosen(enmTypeGlobal, enmTypeMachine);
}

void UIToolsModel::cleanupItems()
{
    foreach (UIToolsItem *pItem, m_items)
        delete pItem;
    m_items.clear();
}

void UIToolsModel::cleanupScene()
{
    delete m_pScene;
    m_pScene = 0;
}

void UIToolsModel::cleanup()
{
    /* Cleanup everything: */
    cleanupItems();
    cleanupScene();
}
