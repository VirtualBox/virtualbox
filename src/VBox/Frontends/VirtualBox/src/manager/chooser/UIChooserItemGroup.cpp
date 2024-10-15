/* $Id: UIChooserItemGroup.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserItemGroup class implementation.
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
#include <QApplication>
#include <QGraphicsLinearLayout>
#include <QGraphicsScene>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QRegularExpression>
#include <QStyleOptionGraphicsItem>
#include <QWindow>

/* GUI includes: */
#include "UIChooserItemGlobal.h"
#include "UIChooserItemGroup.h"
#include "UIChooserItemMachine.h"
#include "UIChooserModel.h"
#include "UIChooserNodeGroup.h"
#include "UIChooserNodeMachine.h"
#include "UIGraphicsRotatorButton.h"
#include "UIGraphicsScrollArea.h"
#include "UIIconPool.h"
#include "UIImageTools.h"
#include "UITranslationEventListener.h"
#include "UIVirtualBoxManager.h"
#include "UIVirtualMachineItem.h"


/*********************************************************************************************************************************
*   Class UIChooserItemGroup implementation.                                                                                     *
*********************************************************************************************************************************/

UIChooserItemGroup::UIChooserItemGroup(QGraphicsScene *pScene, UIChooserNodeGroup *pNode)
    : UIChooserItem(0, pNode)
    , m_pScene(pScene)
    , m_iRootBackgroundDarknessStart(0)
    , m_iRootBackgroundDarknessFinal(0)
    , m_iItemBackgroundDarknessStart(0)
    , m_iItemBackgroundDarknessFinal(0)
    , m_iHighlightLightness(0)
    , m_iAdditionalHeight(0)
    , m_pToggleButton(0)
    , m_pNameEditorWidget(0)
    , m_pContainerFavorite(0)
    , m_pLayoutFavorite(0)
    , m_pScrollArea(0)
    , m_pContainer(0)
    , m_pLayout(0)
    , m_pLayoutGlobal(0)
    , m_pLayoutGroup(0)
    , m_pLayoutMachine(0)
    , m_iPreviousMinimumWidthHint(0)
{
    prepare();
}

UIChooserItemGroup::UIChooserItemGroup(UIChooserItem *pParent, UIChooserNodeGroup *pNode)
    : UIChooserItem(pParent, pNode)
    , m_pScene(0)
    , m_iRootBackgroundDarknessStart(0)
    , m_iRootBackgroundDarknessFinal(0)
    , m_iItemBackgroundDarknessStart(0)
    , m_iItemBackgroundDarknessFinal(0)
    , m_iHighlightLightness(0)
    , m_iAdditionalHeight(0)
    , m_pToggleButton(0)
    , m_pNameEditorWidget(0)
    , m_pContainerFavorite(0)
    , m_pLayoutFavorite(0)
    , m_pScrollArea(0)
    , m_pContainer(0)
    , m_pLayout(0)
    , m_pLayoutGlobal(0)
    , m_pLayoutGroup(0)
    , m_pLayoutMachine(0)
    , m_iPreviousMinimumWidthHint(0)
{
    prepare();
}

UIChooserItemGroup::~UIChooserItemGroup()
{
    cleanup();
}

UIChooserNodeGroup *UIChooserItemGroup::nodeToGroupType() const
{
    return node() ? node()->toGroupNode() : 0;
}

QUuid UIChooserItemGroup::id() const
{
    return nodeToGroupType() ? nodeToGroupType()->id() : QUuid();
}

UIChooserNodeGroupType UIChooserItemGroup::groupType() const
{
    return nodeToGroupType() ? nodeToGroupType()->groupType() : UIChooserNodeGroupType_Invalid;
}

bool UIChooserItemGroup::isClosed() const
{
    return nodeToGroupType()->isClosed() && !isRoot();
}

void UIChooserItemGroup::close(bool fAnimated /* = true */)
{
    AssertMsg(!isRoot(), ("Can't close root-item!"));
    m_pToggleButton->setToggled(false, fAnimated);
}

bool UIChooserItemGroup::isOpened() const
{
    return nodeToGroupType()->isOpened() || isRoot();
}

void UIChooserItemGroup::open(bool fAnimated /* = true */)
{
    AssertMsg(!isRoot(), ("Can't open root-item!"));
    m_pToggleButton->setToggled(true, fAnimated);
}

void UIChooserItemGroup::updateFavorites()
{
    /* Global items only for now, move items to corresponding layout: */
    foreach (UIChooserItem *pItem, items(UIChooserNodeType_Global))
        if (pItem->isFavorite())
        {
            for (int iIndex = 0; iIndex < m_pLayoutGlobal->count(); ++iIndex)
                if (m_pLayoutGlobal->itemAt(iIndex) == pItem)
                    m_pLayoutFavorite->addItem(pItem);
        }
        else
        {
            for (int iIndex = 0; iIndex < m_pLayoutFavorite->count(); ++iIndex)
                if (m_pLayoutFavorite->itemAt(iIndex) == pItem)
                    m_pLayoutGlobal->addItem(pItem);
        }

    /* Update/activate children layout: */
    m_pLayout->updateGeometry();
    m_pLayout->activate();

    /* Relayout model: */
    model()->updateLayout();
}

int UIChooserItemGroup::scrollingValue() const
{
    return m_pScrollArea->scrollingValue();
}

void UIChooserItemGroup::setScrollingValue(int iValue)
{
    m_pScrollArea->setScrollingValue(iValue);
}

void UIChooserItemGroup::scrollBy(int iDelta)
{
    m_pScrollArea->scrollBy(iDelta);
}

void UIChooserItemGroup::makeSureItemIsVisible(UIChooserItem *pItem)
{
    /* Make sure item exists: */
    AssertPtrReturnVoid(pItem);

    /* Convert child rectangle to local coordinates for this group. This also
     * works for a child at any sub-level, doesn't necessary of this group. */
    const QPointF positionInScene = pItem->mapToScene(QPointF(0, 0));
    const QPointF positionInGroup = mapFromScene(positionInScene);
    const QRectF itemRectInGroup = QRectF(positionInGroup, pItem->size());
    m_pScrollArea->makeSureRectIsVisible(itemRectInGroup);
}

/* static */
QString UIChooserItemGroup::className()
{
    return "UIChooserItemGroup";
}

void UIChooserItemGroup::sltRetranslateUI()
{
    updateToggleButtonToolTip();
}

void UIChooserItemGroup::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    UIChooserItem::showEvent(pEvent);

    /* Update pixmaps: */
    updatePixmaps();
}

void UIChooserItemGroup::resizeEvent(QGraphicsSceneResizeEvent *pEvent)
{
    /* Call to base-class: */
    UIChooserItem::resizeEvent(pEvent);

    /* What is the new geometry? */
    const QRectF newGeometry = geometry();

    /* Should we update visible name? */
    if (previousGeometry().width() != newGeometry.width())
        updateVisibleName();

    /* Remember the new geometry: */
    setPreviousGeometry(newGeometry);
}

void UIChooserItemGroup::hoverMoveEvent(QGraphicsSceneHoverEvent *pEvent)
{
    /* Skip if hovered: */
    if (isHovered())
        return;

    /* Prepare variables: */
    const QPoint pos = pEvent->pos().toPoint();
    const int iMarginV = data(GroupItemData_MarginV).toInt();
    const int iHeaderHeight = m_minimumHeaderSize.height();
    const int iFullHeaderHeight = 2 * iMarginV + iHeaderHeight;
    /* Skip if hovered part out of the header: */
    if (pos.y() >= iFullHeaderHeight)
        return;

    /* Call to base-class: */
    UIChooserItem::hoverMoveEvent(pEvent);

    /* Update linked values: */
    updateVisibleName();
}

void UIChooserItemGroup::hoverLeaveEvent(QGraphicsSceneHoverEvent *pEvent)
{
    /* Skip if not hovered: */
    if (!isHovered())
        return;

    /* Call to base-class: */
    UIChooserItem::hoverLeaveEvent(pEvent);

    /* Update linked values: */
    updateVisibleName();
}

void UIChooserItemGroup::paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget* /* pWidget = 0 */)
{
    /* Acquire rectangle: */
    const QRect rectangle = pOptions->rect;

    /* Paint background: */
    paintBackground(pPainter, rectangle);
    /* Paint frame: */
    paintFrame(pPainter, rectangle);
    /* Paint header: */
    paintHeader(pPainter, rectangle);
}

void UIChooserItemGroup::startEditing()
{
    /* Not for root: */
    if (isRoot())
        return;

    /* Not while saving groups: */
    if (model()->isGroupSavingInProgress())
        return;

    /* Make sure item visible: */
    if (model()->root())
        model()->root()->toGroupItem() ->makeSureItemIsVisible(this);

    /* Assign name-editor text: */
    m_pNameEditorWidget->setText(name());

    /* Layout name-editor: */
    const int iMarginV = data(GroupItemData_MarginV).toInt();
    const int iHeaderHeight = 2 * iMarginV + m_minimumHeaderSize.height();
    const QSize headerSize = QSize(geometry().width(), iHeaderHeight);
    const QGraphicsView *pView = model()->scene()->views().first();
    const QPointF viewPoint = pView->mapFromScene(mapToScene(QPointF(0, 0)));
    const QPoint globalPoint = pView->parentWidget()->mapToGlobal(viewPoint.toPoint());
    m_pNameEditorWidget->move(globalPoint);
    m_pNameEditorWidget->resize(headerSize);

    /* Show name-editor: */
    m_pNameEditorWidget->show();
    m_pNameEditorWidget->setFocus();
}

void UIChooserItemGroup::updateItem()
{
    /* Update this group-item: */
    updateVisibleName();
    updateMinimumHeaderSize();
    updateToolTip();
    update();

    /* Update parent group-item: */
    if (parentItem())
    {
        parentItem()->updateToolTip();
        parentItem()->update();
    }
}

void UIChooserItemGroup::updateToolTip()
{
    /* Not for root item: */
    if (isRoot())
        return;

    /* Prepare variables: */
    QStringList toolTipInfo;

    /* Should we add name? */
    if (!name().isEmpty())
    {
        /* Template: */
        QString strTemplateForName = tr("<b>%1</b>", "Group item tool-tip / Group name");

        /* Append value: */
        toolTipInfo << strTemplateForName.arg(name());
    }

    /* Should we add group info? */
    if (!items(UIChooserNodeType_Group).isEmpty())
    {
        /* Template: */
        QString strGroupCount = tr("%n group(s)", "Group item tool-tip / Group info", items(UIChooserNodeType_Group).size());

        /* Append value: */
        QString strValue = tr("<nobr>%1</nobr>", "Group item tool-tip / Group info wrapper").arg(strGroupCount);
        toolTipInfo << strValue;
    }

    /* Should we add machine info? */
    if (!items(UIChooserNodeType_Machine).isEmpty())
    {
        /* Check if 'this' group contains started VMs: */
        int iCountOfStartedMachineItems = 0;
        foreach (UIChooserItem *pItem, items(UIChooserNodeType_Machine))
        {
            AssertPtrReturnVoid(pItem);
            UIChooserItemMachine *pMachineItem = pItem->toMachineItem();
            AssertPtrReturnVoid(pMachineItem);
            AssertPtrReturnVoid(pMachineItem->cache());
            if (pMachineItem->cache()->isItemStarted())
                ++iCountOfStartedMachineItems;
        }
        /* Template: */
        QString strMachineCount = tr("%n machine(s)", "Group item tool-tip / Machine info", items(UIChooserNodeType_Machine).size());
        QString strStartedMachineCount = tr("(%n running)", "Group item tool-tip / Running machine info", iCountOfStartedMachineItems);

        /* Append value: */
        QString strValue = !iCountOfStartedMachineItems ?
                           tr("<nobr>%1</nobr>", "Group item tool-tip / Machine info wrapper").arg(strMachineCount) :
                           tr("<nobr>%1 %2</nobr>", "Group item tool-tip / Machine info wrapper, including running").arg(strMachineCount).arg(strStartedMachineCount);
        toolTipInfo << strValue;
    }

    /* Set tool-tip: */
    setToolTip(toolTipInfo.join("<br>"));
}

void UIChooserItemGroup::installEventFilterHelper(QObject *pSource)
{
    /* The only object which need's that filter for now is scroll-area: */
    pSource->installEventFilter(m_pScrollArea);
}

QList<UIChooserItem*> UIChooserItemGroup::items(UIChooserNodeType type /* = UIChooserNodeType_Any */) const
{
    switch (type)
    {
        case UIChooserNodeType_Any: return items(UIChooserNodeType_Global) + items(UIChooserNodeType_Group) + items(UIChooserNodeType_Machine);
        case UIChooserNodeType_Global: return m_globalItems;
        case UIChooserNodeType_Group: return m_groupItems;
        case UIChooserNodeType_Machine: return m_machineItems;
        default: break;
    }
    return QList<UIChooserItem*>();
}

void UIChooserItemGroup::addItem(UIChooserItem *pItem, bool fFavorite, int iPosition)
{
    /* Check item type: */
    switch (pItem->type())
    {
        case UIChooserNodeType_Global:
        {
            AssertMsg(!m_globalItems.contains(pItem), ("Global-item already added!"));
            if (iPosition < 0 || iPosition >= m_globalItems.size())
            {
                if (fFavorite)
                    m_pLayoutFavorite->addItem(pItem);
                else
                    m_pLayoutGlobal->addItem(pItem);
                m_globalItems.append(pItem);
            }
            else
            {
                if (fFavorite)
                    m_pLayoutFavorite->insertItem(iPosition, pItem);
                else
                    m_pLayoutGlobal->insertItem(iPosition, pItem);
                m_globalItems.insert(iPosition, pItem);
            }
            break;
        }
        case UIChooserNodeType_Group:
        {
            AssertMsg(!m_groupItems.contains(pItem), ("Group-item already added!"));
            if (iPosition < 0 || iPosition >= m_groupItems.size())
            {
                m_pLayoutGroup->addItem(pItem);
                m_groupItems.append(pItem);
            }
            else
            {
                m_pLayoutGroup->insertItem(iPosition, pItem);
                m_groupItems.insert(iPosition, pItem);
            }
            break;
        }
        case UIChooserNodeType_Machine:
        {
            AssertMsg(!m_machineItems.contains(pItem), ("Machine-item already added!"));
            if (iPosition < 0 || iPosition >= m_machineItems.size())
            {
                m_pLayoutMachine->addItem(pItem);
                m_machineItems.append(pItem);
            }
            else
            {
                m_pLayoutMachine->insertItem(iPosition, pItem);
                m_machineItems.insert(iPosition, pItem);
            }
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid item type!"));
            break;
        }
    }

    /* Update linked values: */
    updateLayoutSpacings();
    updateItemCountInfo();
    updateToolTip();
    updateGeometry();
}

void UIChooserItemGroup::removeItem(UIChooserItem *pItem)
{
    /* Check item type: */
    switch (pItem->type())
    {
        case UIChooserNodeType_Global:
        {
            AssertMsg(m_globalItems.contains(pItem), ("Global-item was not found!"));
            m_globalItems.removeAt(m_globalItems.indexOf(pItem));
            if (pItem->isFavorite())
                m_pLayoutFavorite->removeItem(pItem);
            else
                m_pLayoutGlobal->removeItem(pItem);
            break;
        }
        case UIChooserNodeType_Group:
        {
            AssertMsg(m_groupItems.contains(pItem), ("Group-item was not found!"));
            m_groupItems.removeAt(m_groupItems.indexOf(pItem));
            if (pItem->isFavorite())
                m_pLayoutFavorite->removeItem(pItem);
            else
                m_pLayoutGroup->removeItem(pItem);
            break;
        }
        case UIChooserNodeType_Machine:
        {
            AssertMsg(m_machineItems.contains(pItem), ("Machine-item was not found!"));
            m_machineItems.removeAt(m_machineItems.indexOf(pItem));
            if (pItem->isFavorite())
                m_pLayoutFavorite->removeItem(pItem);
            else
                m_pLayoutMachine->removeItem(pItem);
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid item type!"));
            break;
        }
    }

    /* Update linked values: */
    updateLayoutSpacings();
    updateItemCountInfo();
    updateToolTip();
    updateGeometry();
}

UIChooserItem* UIChooserItemGroup::searchForItem(const QString &strSearchTag, int iSearchFlags)
{
    /* Are we searching among group-items? */
    if (   (   iSearchFlags & UIChooserItemSearchFlag_LocalGroup
            && groupType() == UIChooserNodeGroupType_Local)
        || (   iSearchFlags & UIChooserItemSearchFlag_CloudProvider
            && groupType() == UIChooserNodeGroupType_Provider)
        || (   iSearchFlags & UIChooserItemSearchFlag_CloudProfile
            && groupType() == UIChooserNodeGroupType_Profile))
    {
        /* Are we searching by the exact ID? */
        if (iSearchFlags & UIChooserItemSearchFlag_ExactId)
        {
            /* Exact ID matches? */
            if (id().toString() == strSearchTag)
                return this;
        }
        /* Are we searching by the exact name? */
        else if (iSearchFlags & UIChooserItemSearchFlag_ExactName)
        {
            /* Exact name matches? */
            if (name() == strSearchTag)
                return this;
        }
        /* Are we searching by the full name? */
        else if (iSearchFlags & UIChooserItemSearchFlag_FullName)
        {
            /* Full name matches? */
            if (fullName() == strSearchTag)
                return this;
        }
        /* Are we searching by the few first symbols? */
        else
        {
            /* Name starts with passed symbols? */
            if (name().startsWith(strSearchTag, Qt::CaseInsensitive))
                return this;
        }
    }

    /* Search among all the children, but machines first: */
    foreach (UIChooserItem *pItem, items(UIChooserNodeType_Machine))
        if (UIChooserItem *pFoundItem = pItem->searchForItem(strSearchTag, iSearchFlags))
            return pFoundItem;
    foreach (UIChooserItem *pItem, items(UIChooserNodeType_Global))
        if (UIChooserItem *pFoundItem = pItem->searchForItem(strSearchTag, iSearchFlags))
            return pFoundItem;
    foreach (UIChooserItem *pItem, items(UIChooserNodeType_Group))
        if (UIChooserItem *pFoundItem = pItem->searchForItem(strSearchTag, iSearchFlags))
            return pFoundItem;

    /* Found nothing? */
    return 0;
}

UIChooserItem *UIChooserItemGroup::firstMachineItem()
{
    /* If this group-item have at least one machine-item: */
    if (node()->hasNodes(UIChooserNodeType_Machine))
        /* Return the first machine-item: */
        return items(UIChooserNodeType_Machine).first()->firstMachineItem();
    /* If this group-item have at least one group-item: */
    else if (node()->hasNodes(UIChooserNodeType_Group))
        /* Return the first machine-item of the first group-item: */
        return items(UIChooserNodeType_Group).first()->firstMachineItem();
    /* Found nothing? */
    return 0;
}

void UIChooserItemGroup::updateGeometry()
{
    /* Update/activate children layout: */
    m_pLayout->updateGeometry();
    m_pLayout->activate();

    /* Call to base-class: */
    UIChooserItem::updateGeometry();

    /* Special handling for root-groups: */
    if (isRoot())
    {
        /* Root-group should notify Chooser-view if minimum-width-hint was changed: */
        const int iMinimumWidthHint = minimumWidthHint();
        if (m_iPreviousMinimumWidthHint != iMinimumWidthHint)
        {
            /* Save new minimum-width-hint, notify listener: */
            m_iPreviousMinimumWidthHint = iMinimumWidthHint;
            emit sigMinimumWidthHintChanged(m_iPreviousMinimumWidthHint);
        }
    }
}

void UIChooserItemGroup::updateLayout()
{
    /* Prepare variables: */
    const int iMarginHL = data(GroupItemData_MarginHL).toInt();
    const int iMarginV = data(GroupItemData_MarginV).toInt();
    const int iParentIndent = data(GroupItemData_ParentIndent).toInt();
    const int iFullHeaderHeight = m_minimumHeaderSize.height();
    int iPreviousVerticalIndent = 0;

    /* Header (root-item): */
    if (isRoot())
    {
        /* Acquire view: */
        const QGraphicsView *pView = model()->scene()->views().first();

        /* Adjust scroll-view geometry: */
        QSize viewSize = pView->size();
        viewSize.setHeight(viewSize.height() - iPreviousVerticalIndent);
        /* Adjust favorite children container: */
        m_pContainerFavorite->resize(viewSize.width(), m_pContainerFavorite->minimumSizeHint().height());
        m_pContainerFavorite->setPos(0, iPreviousVerticalIndent);
        iPreviousVerticalIndent += m_pContainerFavorite->minimumSizeHint().height();
        /* Adjust other children scroll-area: */
        m_pScrollArea->resize(viewSize.width(), viewSize.height() - m_pContainerFavorite->minimumSizeHint().height());
        m_pScrollArea->setPos(0, iPreviousVerticalIndent);
    }
    /* Header (non-root-item): */
    else
    {
        /* Toggle-button: */
        if (m_pToggleButton)
        {
            /* Prepare variables: */
            int iToggleButtonHeight = m_toggleButtonSize.height();
            /* Layout toggle-button: */
            int iToggleButtonX = iMarginHL;
            int iToggleButtonY = iToggleButtonHeight == iFullHeaderHeight ? iMarginV :
                                 iMarginV + (iFullHeaderHeight - iToggleButtonHeight) / 2;
            m_pToggleButton->setPos(iToggleButtonX, iToggleButtonY);
            /* Show toggle-button: */
            m_pToggleButton->show();
        }

        /* Prepare body indent: */
        iPreviousVerticalIndent = 2 * iMarginV + iFullHeaderHeight;

        /* Adjust scroll-view geometry: */
        QSize itemSize = size().toSize();
        itemSize.setHeight(itemSize.height() - iPreviousVerticalIndent);
        /* Adjust favorite children container: */
        m_pContainerFavorite->resize(itemSize.width() - iParentIndent, m_pContainerFavorite->minimumSizeHint().height());
        m_pContainerFavorite->setPos(iParentIndent, iPreviousVerticalIndent);
        iPreviousVerticalIndent += m_pContainerFavorite->minimumSizeHint().height();
        /* Adjust other children scroll-area: */
        m_pScrollArea->resize(itemSize.width() - iParentIndent, itemSize.height() - m_pContainerFavorite->minimumSizeHint().height());
        m_pScrollArea->setPos(iParentIndent, iPreviousVerticalIndent);
    }

    /* No body for closed group: */
    if (isClosed())
    {
        m_pContainerFavorite->hide();
        m_pScrollArea->hide();
    }
    /* Body for opened group: */
    else
    {
        m_pContainerFavorite->show();
        m_pScrollArea->show();
        foreach (UIChooserItem *pItem, items())
            pItem->updateLayout();
    }
}

int UIChooserItemGroup::minimumWidthHint() const
{
    return minimumWidthHintForGroup(isOpened());
}

int UIChooserItemGroup::minimumHeightHint() const
{
    return minimumHeightHintForGroup(isOpened());
}

QSizeF UIChooserItemGroup::sizeHint(Qt::SizeHint enmWhich, const QSizeF &constraint /* = QSizeF() */) const
{
    /* If Qt::MinimumSize requested: */
    if (enmWhich == Qt::MinimumSize)
        return minimumSizeHintForGroup(isOpened());
    /* Else call to base-class: */
    return UIChooserItem::sizeHint(enmWhich, constraint);
}

QPixmap UIChooserItemGroup::toPixmap()
{
    /* Ask item to paint itself into pixmap: */
    qreal dDpr = gpManager->windowHandle()->devicePixelRatio();
    QSize actualSize = size().toSize();
    QPixmap pixmap(actualSize * dDpr);
    pixmap.setDevicePixelRatio(dDpr);
    QPainter painter(&pixmap);
    QStyleOptionGraphicsItem options;
    options.rect = QRect(QPoint(0, 0), actualSize);
    paint(&painter, &options);
    return pixmap;
}

bool UIChooserItemGroup::isDropAllowed(QGraphicsSceneDragDropEvent *pEvent, UIChooserItemDragToken where) const
{
    /* No drops while saving groups: */
    if (model()->isGroupSavingInProgress())
        return false;
    /* If drag token is shown, its up to parent to decide: */
    if (where != UIChooserItemDragToken_Off)
        return parentItem()->isDropAllowed(pEvent);

    /* Else we should check mime format: */
    const QMimeData *pMimeData = pEvent->mimeData();
    if (pMimeData->hasFormat(UIChooserItemGroup::className()))
    {
        /* Get passed group-item: */
        const UIChooserItemMimeData *pCastedMimeData = qobject_cast<const UIChooserItemMimeData*>(pMimeData);
        AssertPtrReturn(pCastedMimeData, false);
        UIChooserItem *pItem = pCastedMimeData->item();
        AssertPtrReturn(pItem, false);
        UIChooserItemGroup *pGroupItem = pItem->toGroupItem();
        AssertPtrReturn(pGroupItem, false);

        /* For local items: */
        if (   groupType() == UIChooserNodeGroupType_Local
            && pGroupItem->groupType() == UIChooserNodeGroupType_Local)
        {
            /* Make sure passed machine isn't immutable within own group: */
            if (   pGroupItem->isContainsLockedMachine()
                && !m_groupItems.contains(pItem))
                return false;
            /* Make sure passed group is not 'this': */
            if (pItem == this)
                return false;
            /* Make sure passed group is not among our parents: */
            const UIChooserItem *pTestedItem = this;
            while (UIChooserItem *pParentOfTestedWidget = pTestedItem->parentItem())
            {
                if (pItem == pParentOfTestedWidget)
                    return false;
                pTestedItem = pParentOfTestedWidget;
            }

            /* Allow finally: */
            return true;
        }
        /* For profiles inside provider and providers inside root group: */
        else
        if (   (   groupType() == UIChooserNodeGroupType_Provider
                && pGroupItem->groupType() == UIChooserNodeGroupType_Profile)
            || (   groupType() == UIChooserNodeGroupType_Local
                   && pGroupItem->groupType() == UIChooserNodeGroupType_Provider))
        {
            /* Make sure passed item is ours: */
            return m_groupItems.contains(pItem);
        }
    }
    else if (pMimeData->hasFormat(UIChooserItemMachine::className()))
    {
        /* Get passed machine-item: */
        const UIChooserItemMimeData *pCastedMimeData = qobject_cast<const UIChooserItemMimeData*>(pMimeData);
        AssertPtrReturn(pCastedMimeData, false);
        UIChooserItem *pItem = pCastedMimeData->item();
        AssertPtrReturn(pItem, false);
        UIChooserItemMachine *pMachineItem = pItem->toMachineItem();
        AssertPtrReturn(pMachineItem, false);

        /* For local items: */
        if (   groupType() == UIChooserNodeGroupType_Local
            && pMachineItem->cacheType() == UIVirtualMachineItemType_Local)
        {
            /* Make sure passed machine isn't immutable within own group: */
            if (   pMachineItem->isLockedMachine()
                && !m_machineItems.contains(pItem))
                return false;
            switch (pEvent->proposedAction())
            {
                case Qt::MoveAction:
                {
                    /* Make sure passed item is ours or there is no other item with such id: */
                    return m_machineItems.contains(pItem) || !isContainsMachine(pMachineItem->id());
                }
                case Qt::CopyAction:
                {
                    /* Make sure there is no other item with such id: */
                    return !isContainsMachine(pMachineItem->id());
                }
                default:
                    break;
            }
        }
        /* For cloud items: */
        else
        if (   groupType() == UIChooserNodeGroupType_Profile
            && pMachineItem->cacheType() == UIVirtualMachineItemType_CloudReal)
        {
            /* Make sure passed item is ours: */
            return m_machineItems.contains(pItem);
        }
    }
    /* That was invalid mime: */
    return false;
}

void UIChooserItemGroup::processDrop(QGraphicsSceneDragDropEvent *pEvent, UIChooserItem *pFromWho, UIChooserItemDragToken where)
{
    /* Get mime: */
    const QMimeData *pMime = pEvent->mimeData();
    /* Check mime format: */
    if (pMime->hasFormat(UIChooserItemGroup::className()))
    {
        switch (pEvent->proposedAction())
        {
            case Qt::MoveAction:
            case Qt::CopyAction:
            {
                /* Remember scene: */
                UIChooserModel *pModel = model();

                /* Get passed group-item: */
                const UIChooserItemMimeData *pCastedMime = qobject_cast<const UIChooserItemMimeData*>(pMime);
                AssertMsg(pCastedMime, ("Can't cast passed mime-data to UIChooserItemMimeData!"));
                UIChooserNode *pNode = pCastedMime->item()->node();

                /* Check if we have position information: */
                int iPosition = m_groupItems.size();
                if (pFromWho && where != UIChooserItemDragToken_Off)
                {
                    /* Make sure sender item if our child: */
                    AssertMsg(m_groupItems.contains(pFromWho), ("Sender item is NOT our child!"));
                    if (m_groupItems.contains(pFromWho))
                    {
                        iPosition = m_groupItems.indexOf(pFromWho);
                        if (where == UIChooserItemDragToken_Down)
                            ++iPosition;
                    }
                }

                /* Copy passed group-item into this group: */
                UIChooserNodeGroup *pNewGroupNode = new UIChooserNodeGroup(node(), iPosition, pNode->toGroupNode());
                UIChooserItemGroup *pNewGroupItem = new UIChooserItemGroup(this, pNewGroupNode);
                if (isClosed())
                    open(false);

                /* If proposed action is 'move': */
                if (pEvent->proposedAction() == Qt::MoveAction)
                {
                    /* Delete passed item: */
                    delete pNode;
                }

                /* Update model: */
                pModel->wipeOutEmptyGroups();
                pModel->updateNavigationItemList();
                pModel->updateLayout();
                pModel->setSelectedItem(pNewGroupItem);
                pModel->saveGroups();
                break;
            }
            default:
                break;
        }
    }
    else if (pMime->hasFormat(UIChooserItemMachine::className()))
    {
        switch (pEvent->proposedAction())
        {
            case Qt::MoveAction:
            case Qt::CopyAction:
            {
                /* Remember scene: */
                UIChooserModel *pModel = model();

                /* Get passed machine-item: */
                const UIChooserItemMimeData *pCastedMime = qobject_cast<const UIChooserItemMimeData*>(pMime);
                AssertMsg(pCastedMime, ("Can't cast passed mime-data to UIChooserItemMimeData!"));
                UIChooserNode *pNode = pCastedMime->item()->node();

                /* Check if we have position information: */
                int iPosition = m_machineItems.size();
                if (pFromWho && where != UIChooserItemDragToken_Off)
                {
                    /* Make sure sender item if our child: */
                    AssertMsg(m_machineItems.contains(pFromWho), ("Sender item is NOT our child!"));
                    if (m_machineItems.contains(pFromWho))
                    {
                        iPosition = m_machineItems.indexOf(pFromWho);
                        if (where == UIChooserItemDragToken_Down)
                            ++iPosition;
                    }
                }

                /* Copy passed machine-item into this group: */
                UIChooserNodeMachine *pNewMachineNode = new UIChooserNodeMachine(node(), iPosition, pNode->toMachineNode());
                UIChooserItemMachine *pNewMachineItem = new UIChooserItemMachine(this, pNewMachineNode);
                if (isClosed())
                    open(false);

                /* If proposed action is 'move': */
                if (pEvent->proposedAction() == Qt::MoveAction)
                {
                    /* Delete passed item: */
                    delete pNode;
                }

                /* Update model: */
                pModel->wipeOutEmptyGroups();
                pModel->updateNavigationItemList();
                pModel->updateLayout();
                pModel->setSelectedItem(pNewMachineItem);
                pModel->saveGroups();
                break;
            }
            default:
                break;
        }
    }
}

void UIChooserItemGroup::resetDragToken()
{
    /* Reset drag token for this item: */
    if (dragTokenPlace() != UIChooserItemDragToken_Off)
    {
        setDragTokenPlace(UIChooserItemDragToken_Off);
        update();
    }
    /* Reset drag tokens for all the items: */
    foreach (UIChooserItem *pItem, items())
        pItem->resetDragToken();
}

QMimeData* UIChooserItemGroup::createMimeData()
{
    return new UIChooserItemMimeData(this);
}

void UIChooserItemGroup::sltHandleWindowRemapped()
{
    /* Update pixmaps: */
    updatePixmaps();
}

void UIChooserItemGroup::sltNameEditingFinished()
{
    /* Not for root: */
    if (isRoot())
        return;

    /* Close name-editor: */
    m_pNameEditorWidget->close();

    /* Enumerate all the used machine and group names: */
    QStringList usedNames;
    foreach (UIChooserItem *pItem, parentItem()->items())
    {
        AssertPtrReturnVoid(pItem);
        if (   pItem->type() == UIChooserNodeType_Machine
            || (   pItem->type() == UIChooserNodeType_Group
                && pItem->toGroupItem()->groupType() == UIChooserNodeGroupType_Local))
            usedNames << pItem->name();
    }
    /* If proposed name is empty or not unique, reject it: */
    QString strNewName = m_pNameEditorWidget->text().trimmed();
    if (strNewName.isEmpty() || usedNames.contains(strNewName))
        return;

    /* We should replace forbidden symbols
     * with ... well, probably underscores: */
    strNewName.replace(QRegularExpression("[\\\\/:*?\"<>]"), "_");

    /* Set new name, save settings: */
    nodeToGroupType()->setName(strNewName);
    model()->saveGroups();
}

void UIChooserItemGroup::sltGroupToggleStart()
{
    /* Not for root: */
    if (isRoot())
        return;

    /* Toggle started: */
    emit sigToggleStarted();

    /* Setup animation: */
    updateAnimationParameters();

    /* Group closed, we are opening it: */
    if (nodeToGroupType()->isClosed())
    {
        /* Toggle-state and navigation will be
         * updated on toggle-finish signal! */
    }
    /* Group opened, we are closing it: */
    else
    {
        /* Update toggle-state: */
        nodeToGroupType()->close();
        /* Update geometry: */
        updateGeometry();
        /* Update navigation: */
        model()->updateNavigationItemList();
        /* Relayout model: */
        model()->updateLayout();
    }
}

void UIChooserItemGroup::sltGroupToggleFinish(bool fToggled)
{
    /* Not for root: */
    if (isRoot())
        return;

    /* Update toggle-state: */
    fToggled ? nodeToGroupType()->open() : nodeToGroupType()->close();
    /* Update geometry: */
    updateGeometry();
    /* Update navigation: */
    model()->updateNavigationItemList();
    /* Relayout model: */
    model()->updateLayout();
    /* Update toggle-button tool-tip: */
    updateToggleButtonToolTip();
    /* Repaint finally: */
    update();
    /* Save changes: */
    model()->saveGroups();

    /* Toggle finished: */
    emit sigToggleFinished();
}

void UIChooserItemGroup::prepare()
{
    /* Color tones: */
    m_iRootBackgroundDarknessStart = 115;
    m_iRootBackgroundDarknessFinal = 150;
    m_iItemBackgroundDarknessStart = 100;
    m_iItemBackgroundDarknessFinal = 105;
#if defined(VBOX_WS_MAC)
    m_iHighlightLightness = 105;
#elif defined(VBOX_WS_WIN)
    m_iHighlightLightness = 190;
#else /* !VBOX_WS_MAC && !VBOX_WS_WIN */
    m_iHighlightLightness = 105;
#endif /* !VBOX_WS_MAC && !VBOX_WS_WIN */

    /* Prepare self: */
    m_nameFont = font();
    m_nameFont.setWeight(QFont::Bold);
    m_infoFont = font();
    m_minimumHeaderSize = QSize(0, 0);

    /* Prepare header widgets of non-root item: */
    if (!isRoot())
    {
        /* Setup toggle-button: */
        m_pToggleButton = new UIGraphicsRotatorButton(this, "additionalHeight", isOpened());
        if (m_pToggleButton)
        {
            m_pToggleButton->hide();
            connect(m_pToggleButton, &UIGraphicsRotatorButton::sigRotationStart,
                    this, &UIChooserItemGroup::sltGroupToggleStart);
            connect(m_pToggleButton, &UIGraphicsRotatorButton::sigRotationFinish,
                    this, &UIChooserItemGroup::sltGroupToggleFinish);
        }
        m_toggleButtonSize = m_pToggleButton ? m_pToggleButton->minimumSizeHint().toSize() : QSize(0, 0);

        /* Setup name-editor: */
        m_pNameEditorWidget = new UIEditorGroupRename(name());
        if (m_pNameEditorWidget)
        {
            m_pNameEditorWidget->setFont(m_nameFont);
            connect(m_pNameEditorWidget, &UIEditorGroupRename::sigEditingFinished,
                    this, &UIChooserItemGroup::sltNameEditingFinished);
        }
    }

    /* Prepare favorite children container: */
    m_pContainerFavorite = new QIGraphicsWidget(this);
    if (m_pContainerFavorite)
    {
        /* Make it always above other children scroll-area: */
        m_pContainerFavorite->setZValue(1);

        /* Prepare favorite children layout: */
        m_pLayoutFavorite = new QGraphicsLinearLayout(Qt::Vertical, m_pContainerFavorite);
        if (m_pLayoutFavorite)
        {
            m_pLayoutFavorite->setContentsMargins(0, 0, 0, 0);
            m_pLayoutFavorite->setSpacing(0);
        }
    }

    /* Prepare scroll-area: */
    m_pScrollArea = new UIGraphicsScrollArea(Qt::Vertical, this);
    if (m_pScrollArea)
    {
        /* Prepare container: */
        m_pContainer = new QIGraphicsWidget;
        if (m_pContainer)
        {
            /* Prepare layout: */
            m_pLayout = new QGraphicsLinearLayout(Qt::Vertical, m_pContainer);
            if (m_pLayout)
            {
                m_pLayout->setContentsMargins(0, 0, 0, 0);
                m_pLayout->setSpacing(0);

                /* Prepare global layout: */
                m_pLayoutGlobal = new QGraphicsLinearLayout(Qt::Vertical);
                if (m_pLayoutGlobal)
                {
                    m_pLayoutGlobal->setContentsMargins(0, 0, 0, 0);
                    m_pLayoutGlobal->setSpacing(1);
                    m_pLayout->addItem(m_pLayoutGlobal);
                }

                /* Prepare group layout: */
                m_pLayoutGroup = new QGraphicsLinearLayout(Qt::Vertical);
                if (m_pLayoutGroup)
                {
                    m_pLayoutGroup->setContentsMargins(0, 0, 0, 0);
                    m_pLayoutGroup->setSpacing(1);
                    m_pLayout->addItem(m_pLayoutGroup);
                }

                /* Prepare machine layout: */
                m_pLayoutMachine = new QGraphicsLinearLayout(Qt::Vertical);
                if (m_pLayoutMachine)
                {
                    m_pLayoutMachine->setContentsMargins(0, 0, 0, 0);
                    m_pLayoutMachine->setSpacing(1);
                    m_pLayout->addItem(m_pLayoutMachine);
                }
            }

            /* Assign to scroll-area: */
            m_pScrollArea->setViewport(m_pContainer);
        }
    }

    /* Add item directly to the scene (if passed): */
    if (m_pScene)
        m_pScene->addItem(this);
    /* Add item to the parent instead (if passed),
     * it will be added to the scene indirectly: */
    else if (parentItem())
        parentItem()->addItem(this, isFavorite(), position());
    /* Otherwise sombody forgot to pass scene or parent. */
    else
        AssertFailedReturnVoid();

    /* Copy contents: */
    copyContents(nodeToGroupType());

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserItemGroup::sltRetranslateUI);

    /* Initialize non-root items: */
    if (!isRoot())
    {
        updatePixmaps();
        updateItemCountInfo();
        updateVisibleName();
        updateToolTip();
    }

    /* Configure connections: */
    connect(this, &UIChooserItemGroup::sigMinimumWidthHintChanged,
            model(), &UIChooserModel::sigRootItemMinimumWidthHintChanged);
    if (!isRoot())
    {
        /* Non-root items can be toggled: */
        connect(this, &UIChooserItemGroup::sigToggleStarted,
                model(), &UIChooserModel::sigToggleStarted);
        connect(this, &UIChooserItemGroup::sigToggleFinished,
                model(), &UIChooserModel::sigToggleFinished,
                Qt::QueuedConnection);
        /* Non-root items requires pixmap updates: */
        connect(gpManager, &UIVirtualBoxManager::sigWindowRemapped,
                this, &UIChooserItemGroup::sltHandleWindowRemapped);
    }

    /* Invalidate minimum width hint
     * after we isntalled listener: */
    m_iPreviousMinimumWidthHint = 0;
    /* Update geometry finally: */
    updateGeometry();
}

void UIChooserItemGroup::cleanup()
{
    /* Delete group name editor: */
    delete m_pNameEditorWidget;
    m_pNameEditorWidget = 0;

    /* Delete all the items: */
    while (!m_groupItems.isEmpty()) { delete m_groupItems.last(); }
    while (!m_globalItems.isEmpty()) { delete m_globalItems.last(); }
    while (!m_machineItems.isEmpty()) { delete m_machineItems.last(); }

    /* If that item is current: */
    if (model()->currentItem() == this)
    {
        /* Unset current-item: */
        model()->setCurrentItem(0);
    }
    /* If that item is in selection list: */
    if (model()->selectedItems().contains(this))
    {
        /* Remove item from the selection list: */
        model()->removeFromSelectedItems(this);
    }
    /* If that item is in navigation list: */
    if (model()->navigationItems().contains(this))
    {
        /* Remove item from the navigation list: */
        model()->removeFromNavigationItems(this);
    }

    /* Remove item from the parent: */
    if (parentItem())
        parentItem()->removeItem(this);
}

QVariant UIChooserItemGroup::data(int iKey) const
{
    /* Provide other members with required data: */
    switch (iKey)
    {
        /* Layout hints: */
        case GroupItemData_MarginHL:        return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 2;
        case GroupItemData_MarginHR:        return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4 * 5;
        case GroupItemData_MarginV:         return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 2;
        case GroupItemData_HeaderSpacing:   return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 2;
        case GroupItemData_ChildrenSpacing: return 1;
        case GroupItemData_ParentIndent:    return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 2;

        /* Default: */
        default: break;
    }
    return QVariant();
}

int UIChooserItemGroup::additionalHeight() const
{
    return m_iAdditionalHeight;
}

void UIChooserItemGroup::setAdditionalHeight(int iAdditionalHeight)
{
    m_iAdditionalHeight = iAdditionalHeight;
    updateGeometry();
    model()->updateLayout();
}

void UIChooserItemGroup::updateAnimationParameters()
{
    /* Only for item with button: */
    if (!m_pToggleButton)
        return;

    /* Recalculate animation parameters: */
    QSizeF openedSize = minimumSizeHintForGroup(true);
    QSizeF closedSize = minimumSizeHintForGroup(false);
    int iAdditionalHeight = (int)(openedSize.height() - closedSize.height());
    m_pToggleButton->setAnimationRange(0, iAdditionalHeight);
}

void UIChooserItemGroup::updateToggleButtonToolTip()
{
    /* Only for item with button: */
    if (!m_pToggleButton)
        return;

    /* Update toggle-button tool-tip: */
    m_pToggleButton->setToolTip(isOpened() ? tr("Collapse group") : tr("Expand group"));
}

void UIChooserItemGroup::copyContents(UIChooserNodeGroup *pCopyFrom)
{
    foreach (UIChooserNode *pNode, pCopyFrom->nodes(UIChooserNodeType_Group))
        new UIChooserItemGroup(this, pNode->toGroupNode());
    foreach (UIChooserNode *pNode, pCopyFrom->nodes(UIChooserNodeType_Global))
        new UIChooserItemGlobal(this, pNode->toGlobalNode());
    foreach (UIChooserNode *pNode, pCopyFrom->nodes(UIChooserNodeType_Machine))
        new UIChooserItemMachine(this, pNode->toMachineNode());
}

bool UIChooserItemGroup::isContainsMachine(const QUuid &uId) const
{
    /* Check each machine-item: */
    foreach (UIChooserItem *pItem, m_machineItems)
    {
        /* Make sure it's really machine node: */
        AssertPtrReturn(pItem, false);
        UIChooserItemMachine *pMachineItem = pItem->toMachineItem();
        AssertPtrReturn(pMachineItem, false);
        if (pMachineItem->id() == uId)
            return true;
    }
    /* Found nothing? */
    return false;
}

bool UIChooserItemGroup::isContainsLockedMachine()
{
    /* Check each machine-item: */
    foreach (UIChooserItem *pItem, items(UIChooserNodeType_Machine))
        if (pItem->toMachineItem()->isLockedMachine())
            return true;
    /* Check each group-item: */
    foreach (UIChooserItem *pItem, items(UIChooserNodeType_Group))
        if (pItem->toGroupItem()->isContainsLockedMachine())
            return true;
    /* Found nothing? */
    return false;
}

void UIChooserItemGroup::updateItemCountInfo()
{
    /* Not for root item: */
    if (isRoot())
        return;

    /* Update item info attributes: */
    QPaintDevice *pPaintDevice = model()->paintDevice();
    QString strInfoGroups = m_groupItems.isEmpty() ? QString() : QString::number(m_groupItems.size());
    QString strInfoMachines = m_machineItems.isEmpty() ? QString() : QString::number(m_machineItems.size());
    QSize infoSizeGroups = textSize(m_infoFont, pPaintDevice, strInfoGroups);
    QSize infoSizeMachines = textSize(m_infoFont, pPaintDevice, strInfoMachines);

    /* Update linked values: */
    bool fSomethingChanged = false;
    if (m_strInfoGroups != strInfoGroups)
    {
        m_strInfoGroups = strInfoGroups;
        fSomethingChanged = true;
    }
    if (m_strInfoMachines != strInfoMachines)
    {
        m_strInfoMachines = strInfoMachines;
        fSomethingChanged = true;
    }
    if (m_infoSizeGroups != infoSizeGroups)
    {
        m_infoSizeGroups = infoSizeGroups;
        fSomethingChanged = true;
    }
    if (m_infoSizeMachines != infoSizeMachines)
    {
        m_infoSizeMachines = infoSizeMachines;
        fSomethingChanged = true;
    }
    if (fSomethingChanged)
    {
        updateVisibleName();
        updateMinimumHeaderSize();
    }
}

int UIChooserItemGroup::minimumWidthHintForGroup(bool fGroupOpened) const
{
    /* Calculating proposed width: */
    int iProposedWidth = 0;

    /* For root item: */
    if (isRoot())
    {
        /* Main root-item always takes body into account: */
        if (node()->hasNodes())
        {
            /* We have to take maximum children width into account: */
            iProposedWidth = qMax(m_pContainerFavorite->minimumSizeHint().width(),
                                  m_pContainer->minimumSizeHint().width());
        }
    }
    /* For other items: */
    else
    {
        /* Prepare variables: */
        const int iMarginHL = data(GroupItemData_MarginHL).toInt();
        const int iMarginHR = data(GroupItemData_MarginHR).toInt();

        /* Basically we have to take header width into account: */
        iProposedWidth += m_minimumHeaderSize.width();

        /* But if group-item is opened: */
        if (fGroupOpened)
        {
            /* We have to take maximum children width into account: */
            iProposedWidth = qMax(m_pContainerFavorite->minimumSizeHint().width(),
                                  m_pContainer->minimumSizeHint().width());
        }

        /* And 2 margins at last - left and right: */
        iProposedWidth += iMarginHL + iMarginHR;
    }

    /* Return result: */
    return iProposedWidth;
}

int UIChooserItemGroup::minimumHeightHintForGroup(bool fGroupOpened) const
{
    /* Calculating proposed height: */
    int iProposedHeight = 0;

    /* For root item: */
    if (isRoot())
    {
        /* Main root-item always takes body into account: */
        if (node()->hasNodes())
        {
            /* Prepare variables: */
            const int iSpacingV = data(GroupItemData_ChildrenSpacing).toInt();

            /* We have to take maximum children height into account: */
            iProposedHeight += m_pContainerFavorite->minimumSizeHint().height();
            iProposedHeight += m_pContainer->minimumSizeHint().height();
            iProposedHeight += iSpacingV;
        }
    }
    /* For other items: */
    else
    {
        /* Prepare variables: */
        const int iMarginV = data(GroupItemData_MarginV).toInt();

        /* Group-item header have 2 margins - top and bottom: */
        iProposedHeight += 2 * iMarginV;
        /* And header content height to take into account: */
        iProposedHeight += m_minimumHeaderSize.height();

        /* But if group-item is opened: */
        if (fGroupOpened)
        {
            /* We have to take maximum children height into account: */
            iProposedHeight += m_pContainerFavorite->minimumSizeHint().height();
            iProposedHeight += m_pContainer->minimumSizeHint().height();
        }

        /* Finally, additional height during animation: */
        if (!fGroupOpened && m_pToggleButton && m_pToggleButton->isAnimationRunning())
            iProposedHeight += m_iAdditionalHeight;
    }

    /* Return result: */
    return iProposedHeight;
}

QSizeF UIChooserItemGroup::minimumSizeHintForGroup(bool fGroupOpened) const
{
    return QSizeF(minimumWidthHintForGroup(fGroupOpened), minimumHeightHintForGroup(fGroupOpened));
}

void UIChooserItemGroup::updateVisibleName()
{
    /* Not for root item: */
    if (isRoot())
        return;

    /* Prepare variables: */
    int iMarginHL = data(GroupItemData_MarginHL).toInt();
    int iMarginHR = data(GroupItemData_MarginHR).toInt();
    int iHeaderSpacing = data(GroupItemData_HeaderSpacing).toInt();
    int iToggleButtonWidth = m_toggleButtonSize.width();
    int iGroupPixmapWidth = m_pixmapSizeGroups.width();
    int iMachinePixmapWidth = m_pixmapSizeMachines.width();
    int iGroupCountTextWidth = m_infoSizeGroups.width();
    int iMachineCountTextWidth = m_infoSizeMachines.width();
    int iMaximumWidth = (int)geometry().width();

    /* Left margin: */
    iMaximumWidth -= iMarginHL;
    /* Button width: */
    if (!isRoot())
        iMaximumWidth -= iToggleButtonWidth;
    /* Spacing between button and name: */
    iMaximumWidth -= iHeaderSpacing;
    if (isHovered())
    {
        /* Spacing between name and info: */
        iMaximumWidth -= iHeaderSpacing;
        /* Group info width: */
        if (!m_groupItems.isEmpty())
            iMaximumWidth -= (iGroupPixmapWidth + iGroupCountTextWidth);
        /* Machine info width: */
        if (!m_machineItems.isEmpty())
            iMaximumWidth -= (iMachinePixmapWidth + iMachineCountTextWidth);
    }
    /* Right margin: */
    iMaximumWidth -= iMarginHR;
    /* Calculate new visible name and name-size: */
    QPaintDevice *pPaintDevice = model()->paintDevice();
    QString strVisibleName = compressText(m_nameFont, pPaintDevice, name(), iMaximumWidth);
    QSize visibleNameSize = textSize(m_nameFont, pPaintDevice, strVisibleName);

    /* Update linked values: */
    if (m_visibleNameSize != visibleNameSize)
    {
        m_visibleNameSize = visibleNameSize;
        updateGeometry();
    }
    if (m_strVisibleName != strVisibleName)
    {
        m_strVisibleName = strVisibleName;
        update();
    }
}

void UIChooserItemGroup::updatePixmaps()
{
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    const qreal fDevicePixelRatio = gpManager->windowHandle() ? gpManager->windowHandle()->devicePixelRatio() : 1;
    m_groupsPixmap = UIIconPool::iconSet(":/group_abstract_16px.png").pixmap(QSize(iIconMetric, iIconMetric),
                                                                             fDevicePixelRatio);
    m_machinesPixmap = UIIconPool::iconSet(":/machine_abstract_16px.png").pixmap(QSize(iIconMetric, iIconMetric),
                                                                                 fDevicePixelRatio);
    m_pixmapSizeGroups = m_groupsPixmap.size() / m_groupsPixmap.devicePixelRatio();
    m_pixmapSizeMachines = m_machinesPixmap.size() / m_machinesPixmap.devicePixelRatio();
}

void UIChooserItemGroup::updateMinimumHeaderSize()
{
    /* Not for root item: */
    if (isRoot())
        return;

    /* Prepare variables: */
    const int iHeaderSpacing = data(GroupItemData_HeaderSpacing).toInt();

    /* Calculate minimum visible name size: */
    QPaintDevice *pPaintDevice = model()->paintDevice();
    const QFontMetrics fm(m_nameFont, pPaintDevice);
    const int iMaximumNameWidth = textWidth(m_nameFont, pPaintDevice, 20);
    const QString strCompressedName = compressText(m_nameFont, pPaintDevice, name(), iMaximumNameWidth);
    const int iMinimumNameWidth = fm.horizontalAdvance(strCompressedName);
    const int iMinimumNameHeight = fm.height();

    /* Calculate minimum width: */
    int iHeaderWidth = 0;
    /* Button width: */
    if (!isRoot())
        iHeaderWidth += m_toggleButtonSize.width();
    iHeaderWidth += /* Spacing between button and name: */
                    iHeaderSpacing +
                    /* Minimum name width: */
                    iMinimumNameWidth +
                    /* Spacing between name and info: */
                    iHeaderSpacing;
    /* Group info width: */
    if (!m_groupItems.isEmpty())
        iHeaderWidth += (m_pixmapSizeGroups.width() + m_infoSizeGroups.width());
    /* Machine info width: */
    if (!m_machineItems.isEmpty())
        iHeaderWidth += (m_pixmapSizeMachines.width() + m_infoSizeMachines.width());

    /* Calculate maximum height: */
    QList<int> heights;
    /* Button height: */
    if (!isRoot())
        heights << m_toggleButtonSize.height();
    heights /* Minimum name height: */
            << iMinimumNameHeight
            /* Group info heights: */
            << m_pixmapSizeGroups.height() << m_infoSizeGroups.height()
            /* Machine info heights: */
            << m_pixmapSizeMachines.height() << m_infoSizeMachines.height();
    /* Button height: */
    int iHeaderHeight = 0;
    foreach (int iHeight, heights)
        iHeaderHeight = qMax(iHeaderHeight, iHeight);

    /* Calculate new minimum header size: */
    const QSize minimumHeaderSize = QSize(iHeaderWidth, iHeaderHeight);

    /* Is there something changed? */
    if (m_minimumHeaderSize == minimumHeaderSize)
        return;

    /* Update linked values: */
    m_minimumHeaderSize = minimumHeaderSize;
    updateGeometry();
}

void UIChooserItemGroup::updateLayoutSpacings()
{
    m_pLayout->setItemSpacing(0, m_globalItems.isEmpty() ? 0 : 1);
    m_pLayout->setItemSpacing(1, m_groupItems.isEmpty() ? 0 : 1);
    m_pLayout->setItemSpacing(2, m_machineItems.isEmpty() ? 0 : 1);
}

void UIChooserItemGroup::paintBackground(QPainter *pPainter, const QRect &rect)
{
    /* Save painter: */
    pPainter->save();

    /* Root-item: */
    if (isRoot())
    {
        /* Acquire background color: */
        const QColor backgroundColor = QApplication::palette().color(QPalette::Active, QPalette::Window);

        /* Paint default background: */
        QLinearGradient gradientDefault(rect.topRight(), rect.bottomLeft());
        gradientDefault.setColorAt(0, backgroundColor.darker(m_iRootBackgroundDarknessStart));
        gradientDefault.setColorAt(1, backgroundColor.darker(m_iRootBackgroundDarknessFinal));
        pPainter->fillRect(rect, gradientDefault);
    }
    else
    {
        /* Acquire background color: */
        const QColor backgroundColor = model()->selectedItems().contains(this)
                                     ? QApplication::palette().color(QPalette::Active, QPalette::Highlight).lighter(m_iHighlightLightness)
                                     : QApplication::palette().color(QPalette::Active, QPalette::Window);

        /* Paint default background: */
        QLinearGradient gradientDefault(rect.topRight(), rect.bottomLeft());
        gradientDefault.setColorAt(0, backgroundColor.darker(m_iItemBackgroundDarknessStart));
        gradientDefault.setColorAt(1, backgroundColor.darker(m_iItemBackgroundDarknessFinal));
        pPainter->fillRect(rect, gradientDefault);

        /* If element is hovered: */
        if (animatedValue())
        {
            /* Calculate header rectangle: */
            const int iMarginV = data(GroupItemData_MarginV).toInt();
            const int iFullHeaderHeight = 2 * iMarginV + m_minimumHeaderSize.height();
            QRect headRect = rect;
            headRect.setHeight(iFullHeaderHeight);

            /* Acquire header color: */
            QColor headColor = backgroundColor.lighter(130);

            /* Paint hovered background: */
            QColor hcTone1 = headColor;
            QColor hcTone2 = headColor;
            hcTone1.setAlpha(255 * animatedValue() / 100);
            hcTone2.setAlpha(0);
            QLinearGradient gradientHovered(headRect.topLeft(), headRect.bottomLeft());
            gradientHovered.setColorAt(0, hcTone1);
            gradientHovered.setColorAt(1, hcTone2);
            pPainter->fillRect(headRect, gradientHovered);
        }

        /* Paint drag token UP? */
        if (dragTokenPlace() != UIChooserItemDragToken_Off)
        {
            QLinearGradient dragTokenGradient;
            QRect dragTokenRect = rect;
            if (dragTokenPlace() == UIChooserItemDragToken_Up)
            {
                dragTokenRect.setHeight(5);
                dragTokenGradient.setStart(dragTokenRect.bottomLeft());
                dragTokenGradient.setFinalStop(dragTokenRect.topLeft());
            }
            else if (dragTokenPlace() == UIChooserItemDragToken_Down)
            {
                dragTokenRect.setTopLeft(dragTokenRect.bottomLeft() - QPoint(0, 5));
                dragTokenGradient.setStart(dragTokenRect.topLeft());
                dragTokenGradient.setFinalStop(dragTokenRect.bottomLeft());
            }
            dragTokenGradient.setColorAt(0, backgroundColor.darker(dragTokenDarkness()));
            dragTokenGradient.setColorAt(1, backgroundColor.darker(dragTokenDarkness() + 40));
            pPainter->fillRect(dragTokenRect, dragTokenGradient);
        }
    }

    /* Restore painter: */
    pPainter->restore();
}

void UIChooserItemGroup::paintFrame(QPainter *pPainter, const QRect &rectangle)
{
    /* Not for roots: */
    if (isRoot())
        return;

    /* Only selected item should have a frame: */
    if (!model()->selectedItems().contains(this))
        return;

    /* Save painter: */
    pPainter->save();

    /* Prepare variables: */
    const int iMarginV = data(GroupItemData_MarginV).toInt();
    const int iParentIndent = data(GroupItemData_ParentIndent).toInt();
    const int iFullHeaderHeight = 2 * iMarginV + m_minimumHeaderSize.height();

    /* Prepare color: */
    const QColor frameColor = QApplication::palette().color(QPalette::Active, QPalette::Highlight).lighter(m_iHighlightLightness - 40);

    /* Create/assign pen: */
    QPen pen(frameColor);
    pen.setWidth(0);
    pPainter->setPen(pen);

    /* Calculate top rectangle: */
    QRect topRect = rectangle;
    if (nodeToGroupType()->isOpened())
        topRect.setBottom(topRect.top() + iFullHeaderHeight - 1);

    /* Draw borders: */
    pPainter->drawLine(rectangle.topLeft(), rectangle.topRight());
    if (node()->hasNodes() && nodeToGroupType()->isOpened())
        pPainter->drawLine(topRect.bottomLeft() + QPoint(iParentIndent, 0), topRect.bottomRight() + QPoint(1, 0));
    else
        pPainter->drawLine(topRect.bottomLeft(), topRect.bottomRight() + QPoint(1, 0));
    pPainter->drawLine(rectangle.topLeft(), rectangle.bottomLeft());

    /* Restore painter: */
    pPainter->restore();
}

void UIChooserItemGroup::paintHeader(QPainter *pPainter, const QRect &rect)
{
    /* Not for root item: */
    if (isRoot())
        return;

    /* Prepare variables: */
    const int iMarginHL = data(GroupItemData_MarginHL).toInt();
    const int iMarginHR = data(GroupItemData_MarginHR).toInt();
    const int iMarginV = data(GroupItemData_MarginV).toInt();
    const int iHeaderSpacing = data(GroupItemData_HeaderSpacing).toInt();
    const int iFullHeaderHeight = m_minimumHeaderSize.height();

    /* Selected item foreground: */
    if (model()->selectedItems().contains(this))
    {
        /* Prepare palette: */
        const QPalette pal = QApplication::palette();

        /* Get background color: */
        const QColor background = pal.color(QPalette::Active, QPalette::Highlight).lighter(m_iHighlightLightness);

        /* Gather foreground color for background one: */
        const QColor foreground = suitableForegroundColor(pal, background);
        pPainter->setPen(foreground);
    }

    /* Paint name: */
    int iNameX = iMarginHL;
    if (!isRoot())
        iNameX += m_toggleButtonSize.width();
    iNameX += iHeaderSpacing;
    int iNameY = m_visibleNameSize.height() == iFullHeaderHeight ? iMarginV :
                 iMarginV + (iFullHeaderHeight - m_visibleNameSize.height()) / 2;
    paintText(/* Painter: */
              pPainter,
              /* Point to paint in: */
              QPoint(iNameX, iNameY),
              /* Font to paint text: */
              m_nameFont,
              /* Paint device: */
              model()->paintDevice(),
              /* Text to paint: */
              m_strVisibleName);

    /* Should we add more info? */
    if (isHovered())
    {
        /* Indent: */
        int iHorizontalIndent = rect.right() - iMarginHR;

        /* Should we draw machine count info? */
        if (!m_strInfoMachines.isEmpty())
        {
            iHorizontalIndent -= m_infoSizeMachines.width();
            int iMachineCountTextX = iHorizontalIndent;
            int iMachineCountTextY = m_infoSizeMachines.height() == iFullHeaderHeight ?
                                     iMarginV : iMarginV + (iFullHeaderHeight - m_infoSizeMachines.height()) / 2;
            paintText(/* Painter: */
                      pPainter,
                      /* Point to paint in: */
                      QPoint(iMachineCountTextX, iMachineCountTextY),
                      /* Font to paint text: */
                      m_infoFont,
                      /* Paint device: */
                      model()->paintDevice(),
                      /* Text to paint: */
                      m_strInfoMachines);

            iHorizontalIndent -= m_pixmapSizeMachines.width();
            int iMachinePixmapX = iHorizontalIndent;
            int iMachinePixmapY = m_pixmapSizeMachines.height() == iFullHeaderHeight ?
                                  iMarginV : iMarginV + (iFullHeaderHeight - m_pixmapSizeMachines.height()) / 2;
            paintPixmap(/* Painter: */
                        pPainter,
                        /* Point to paint in: */
                        QPoint(iMachinePixmapX, iMachinePixmapY),
                        /* Pixmap to paint: */
                        m_machinesPixmap);
        }

        /* Should we draw group count info? */
        if (!m_strInfoGroups.isEmpty())
        {
            iHorizontalIndent -= m_infoSizeGroups.width();
            int iGroupCountTextX = iHorizontalIndent;
            int iGroupCountTextY = m_infoSizeGroups.height() == iFullHeaderHeight ?
                                   iMarginV : iMarginV + (iFullHeaderHeight - m_infoSizeGroups.height()) / 2;
            paintText(/* Painter: */
                      pPainter,
                      /* Point to paint in: */
                      QPoint(iGroupCountTextX, iGroupCountTextY),
                      /* Font to paint text: */
                      m_infoFont,
                      /* Paint device: */
                      model()->paintDevice(),
                      /* Text to paint: */
                      m_strInfoGroups);

            iHorizontalIndent -= m_pixmapSizeGroups.width();
            int iGroupPixmapX = iHorizontalIndent;
            int iGroupPixmapY = m_pixmapSizeGroups.height() == iFullHeaderHeight ?
                                iMarginV : iMarginV + (iFullHeaderHeight - m_pixmapSizeGroups.height()) / 2;
            paintPixmap(/* Painter: */
                        pPainter,
                        /* Point to paint in: */
                        QPoint(iGroupPixmapX, iGroupPixmapY),
                        /* Pixmap to paint: */
                        m_groupsPixmap);
        }
    }
}


/*********************************************************************************************************************************
*   Class UIEditorGroupRename implementation.                                                                                    *
*********************************************************************************************************************************/

UIEditorGroupRename::UIEditorGroupRename(const QString &strName)
    : QWidget(0, Qt::Popup)
    , m_pLineEdit(0)
{
    /* Create layout: */
    QHBoxLayout *pLayout = new QHBoxLayout(this);
    if (pLayout)
    {
        /* Configure layout: */
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Create line-edit: */
        m_pLineEdit = new QLineEdit(strName);
        if (m_pLineEdit)
        {
            setFocusProxy(m_pLineEdit);
            m_pLineEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            m_pLineEdit->setTextMargins(0, 0, 0, 0);
            connect(m_pLineEdit, &QLineEdit::returnPressed,
                    this, &UIEditorGroupRename::sigEditingFinished);
        }

        /* Add into layout: */
        pLayout->addWidget(m_pLineEdit);
    }
}

QString UIEditorGroupRename::text() const
{
    return m_pLineEdit->text();
}

void UIEditorGroupRename::setText(const QString &strText)
{
    m_pLineEdit->setText(strText);
}

void UIEditorGroupRename::setFont(const QFont &font)
{
    QWidget::setFont(font);
    m_pLineEdit->setFont(font);
}
