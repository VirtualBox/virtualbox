/* $Id: UIChooserItemMachine.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserItemMachine class implementation.
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
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWindow>

/* GUI includes: */
#include "UIChooserItemGroup.h"
#include "UIChooserItemMachine.h"
#include "UIChooserModel.h"
#include "UIChooserNodeGroup.h"
#include "UIChooserNodeMachine.h"
#include "UIIconPool.h"
#include "UIImageTools.h"
#include "UITranslationEventListener.h"
#include "UIVirtualBoxManager.h"
#include "UIVirtualMachineItemCloud.h"
#include "UIVirtualMachineItemLocal.h"


UIChooserItemMachine::UIChooserItemMachine(UIChooserItem *pParent, UIChooserNodeMachine *pNode)
    : UIChooserItem(pParent, pNode)
    , m_iDefaultLightnessStart(0)
    , m_iDefaultLightnessFinal(0)
    , m_iHoverLightnessStart(0)
    , m_iHoverLightnessFinal(0)
    , m_iHighlightLightnessStart(0)
    , m_iHighlightLightnessFinal(0)
    , m_iFirstRowMaximumWidth(0)
    , m_iMinimumNameWidth(0)
    , m_iMaximumNameWidth(0)
    , m_iMinimumSnapshotNameWidth(0)
    , m_iMaximumSnapshotNameWidth(0)
{
    prepare();
}

UIChooserItemMachine::~UIChooserItemMachine()
{
    cleanup();
}

UIChooserNodeMachine *UIChooserItemMachine::nodeToMachineType() const
{
    return node() ? node()->toMachineNode() : 0;
}

QUuid UIChooserItemMachine::id() const
{
    return nodeToMachineType() ? nodeToMachineType()->id() : QUuid();
}

bool UIChooserItemMachine::accessible() const
{
    return nodeToMachineType() ? nodeToMachineType()->accessible() : false;
}

UIVirtualMachineItem *UIChooserItemMachine::cache() const
{
    return nodeToMachineType() ? nodeToMachineType()->cache() : 0;
}

UIVirtualMachineItemType UIChooserItemMachine::cacheType() const
{
    return cache() ? cache()->itemType() : UIVirtualMachineItemType_Invalid;
}

void UIChooserItemMachine::recache()
{
    if (cache())
        cache()->recache();
}

bool UIChooserItemMachine::isLockedMachine() const
{
    /* For local machines only, others always unlocked: */
    if (cacheType() != UIVirtualMachineItemType_Local)
        return false;

    /* Acquire local machine state: */
    AssertPtrReturn(cache()->toLocal(), true);
    const KMachineState enmState = cache()->toLocal()->machineState();
    return    enmState != KMachineState_PoweredOff
           && enmState != KMachineState_Saved
           && enmState != KMachineState_Teleported
           && enmState != KMachineState_Aborted
           && enmState != KMachineState_AbortedSaved;
}

bool UIChooserItemMachine::isToolButtonArea(const QPoint &position, int iMarginMultiplier /* = 1 */) const
{
    const int iFullWidth = geometry().width();
    const int iFullHeight = geometry().height();
    const int iMarginHR = data(MachineItemData_MarginHR).toInt();
    const int iButtonMargin = data(MachineItemData_ButtonMargin).toInt();
    const int iToolPixmapX = iFullWidth - iMarginHR - 1 - m_toolPixmap.width() / m_toolPixmap.devicePixelRatio();
    const int iToolPixmapY = (iFullHeight - m_toolPixmap.height() / m_toolPixmap.devicePixelRatio()) / 2;
    QRect rect = QRect(iToolPixmapX,
                       iToolPixmapY,
                       m_toolPixmap.width() / m_toolPixmap.devicePixelRatio(),
                       m_toolPixmap.height() / m_toolPixmap.devicePixelRatio());
    rect.adjust(-iMarginMultiplier * iButtonMargin, -iMarginMultiplier * iButtonMargin,
                 iMarginMultiplier * iButtonMargin,  iMarginMultiplier * iButtonMargin);
    return rect.contains(position);
}

/* static */
QString UIChooserItemMachine::className()
{
    return "UIChooserItemMachine";
}

/* static */
void UIChooserItemMachine::enumerateMachineItems(const QList<UIChooserItem*> &il,
                                                 QList<UIChooserItemMachine*> &ol,
                                                 int iEnumerationFlags /* = 0 */)
{
    /* Enumerate all the passed items: */
    foreach (UIChooserItem *pItem, il)
    {
        /* If that is machine-item: */
        AssertPtrReturnVoid(pItem);
        if (pItem->type() == UIChooserNodeType_Machine)
        {
            /* Get the iterated machine-item: */
            UIChooserItemMachine *pMachineItem = pItem->toMachineItem();
            AssertPtrReturnVoid(pMachineItem);
            /* Skip if exactly this item is already enumerated: */
            if (ol.contains(pMachineItem))
                continue;
            /* Skip if item with same ID is already enumerated but we need unique: */
            if ((iEnumerationFlags & UIChooserItemMachineEnumerationFlag_Unique) &&
                checkIfContains(ol, pMachineItem))
                continue;
            /* Skip if this item is accessible and we no need it: */
            if ((iEnumerationFlags & UIChooserItemMachineEnumerationFlag_Inaccessible) &&
                pMachineItem->accessible())
                continue;
            /* Add it: */
            ol << pMachineItem;
        }
        /* If that is group-item: */
        else if (pItem->type() == UIChooserNodeType_Group)
        {
            /* Enumerate all the machine-items recursively: */
            enumerateMachineItems(pItem->items(UIChooserNodeType_Machine), ol, iEnumerationFlags);
            /* Enumerate all the group-items recursively: */
            enumerateMachineItems(pItem->items(UIChooserNodeType_Group), ol, iEnumerationFlags);
        }
    }
}

void UIChooserItemMachine::sltRetranslateUI()
{
}

void UIChooserItemMachine::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    UIChooserItem::showEvent(pEvent);

    /* Recache and update pixmaps: */
    AssertPtrReturnVoid(cache());
    cache()->recachePixmap();
    updatePixmaps();
}

void UIChooserItemMachine::resizeEvent(QGraphicsSceneResizeEvent *pEvent)
{
    /* Call to base-class: */
    UIChooserItem::resizeEvent(pEvent);

    /* What is the new geometry? */
    const QRectF newGeometry = geometry();

    /* Should we update visible name? */
    if (previousGeometry().width() != newGeometry.width())
        updateFirstRowMaximumWidth();

    /* Remember the new geometry: */
    setPreviousGeometry(newGeometry);
}

void UIChooserItemMachine::mousePressEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Call to base-class: */
    UIChooserItem::mousePressEvent(pEvent);
    /* No drag for inaccessible: */
    if (!accessible())
        pEvent->ignore();
}

void UIChooserItemMachine::paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget* /* pWidget = 0 */)
{
    /* Acquire rectangle: */
    const QRect rectangle = pOptions->rect;

    /* Paint background: */
    paintBackground(pPainter, rectangle);
    /* Paint frame: */
    paintFrame(pPainter, rectangle);
    /* Paint machine info: */
    paintMachineInfo(pPainter, rectangle);
}

void UIChooserItemMachine::setSelected(bool fSelected)
{
    /* Call to base-class: */
    UIChooserItem::setSelected(fSelected);

    /* Special treatment for real cloud items: */
    if (cacheType() == UIVirtualMachineItemType_CloudReal)
    {
        UIVirtualMachineItemCloud *pCloudMachineItem = cache()->toCloud();
        AssertPtrReturnVoid(pCloudMachineItem);
        const bool fUpdateRequired = fSelected && pCloudMachineItem->accessible();
        pCloudMachineItem->setUpdateRequiredByLocalReason(fUpdateRequired);
        if (fUpdateRequired)
            pCloudMachineItem->updateInfoAsync(false /* delayed? */);
    }
}

void UIChooserItemMachine::startEditing()
{
    AssertMsgFailed(("Machine graphics item do NOT support editing yet!"));
}

void UIChooserItemMachine::updateItem()
{
    /* Update this machine-item: */
    updatePixmaps();
    updateMinimumNameWidth();
    updateVisibleName();
    updateMinimumSnapshotNameWidth();
    updateVisibleSnapshotName();
    updateStateTextSize();
    updateToolTip();
    update();

    /* Update parent group-item: */
    parentItem()->updateToolTip();
    parentItem()->update();
}

void UIChooserItemMachine::updateToolTip()
{
    AssertPtrReturnVoid(cache());
    setToolTip(cache()->toolTipText());
}

QList<UIChooserItem*> UIChooserItemMachine::items(UIChooserNodeType) const
{
    AssertMsgFailed(("Machine graphics item do NOT support children!"));
    return QList<UIChooserItem*>();
}

void UIChooserItemMachine::addItem(UIChooserItem*, bool, int)
{
    AssertMsgFailed(("Machine graphics item do NOT support children!"));
}

void UIChooserItemMachine::removeItem(UIChooserItem*)
{
    AssertMsgFailed(("Machine graphics item do NOT support children!"));
}

UIChooserItem* UIChooserItemMachine::searchForItem(const QString &strSearchTag, int iSearchFlags)
{
    /* Ignore if we are not searching for the machine-item: */
    if (!(iSearchFlags & UIChooserItemSearchFlag_Machine))
        return 0;

    /* Are we searching by the exact ID? */
    if (iSearchFlags & UIChooserItemSearchFlag_ExactId)
    {
        /* Exact ID doesn't match? */
        if (id() != QUuid(strSearchTag))
            return 0;
    }
    /* Are we searching by the exact name? */
    else if (iSearchFlags & UIChooserItemSearchFlag_ExactName)
    {
        /* Exact name doesn't match? */
        if (name() != strSearchTag)
            return 0;
    }
    /* Are we searching by the few first symbols? */
    else
    {
        /* Name doesn't start with passed symbols? */
        if (!name().startsWith(strSearchTag, Qt::CaseInsensitive))
            return 0;
    }

    /* Returning this: */
    return this;
}

UIChooserItem *UIChooserItemMachine::firstMachineItem()
{
    return this;
}

void UIChooserItemMachine::updateLayout()
{
    // Just do nothing ..
}

int UIChooserItemMachine::minimumWidthHint() const
{
    /* Prepare variables: */
    const int iMarginHL = data(MachineItemData_MarginHL).toInt();
    const int iMarginHR = data(MachineItemData_MarginHR).toInt();
    const int iMajorSpacing = data(MachineItemData_MajorSpacing).toInt();
    const int iMinorSpacing = data(MachineItemData_MinorSpacing).toInt();
    const int iButtonMargin = data(MachineItemData_ButtonMargin).toInt();

    /* Calculating proposed width: */
    int iProposedWidth = 0;

    /* Two margins: */
    iProposedWidth += iMarginHL + iMarginHR;
    /* And machine-item content to take into account: */
    int iTopLineWidth = m_iMinimumNameWidth;
    /* Only local items can have snapshots: */
    if (   cacheType() == UIVirtualMachineItemType_Local
        && !cache()->toLocal()->snapshotName().isEmpty())
        iTopLineWidth += (iMinorSpacing +
                          m_iMinimumSnapshotNameWidth);
    int iBottomLineWidth = m_statePixmapSize.width() +
                           iMinorSpacing +
                           m_stateTextSize.width();
    int iMiddleColumnWidth = qMax(iTopLineWidth, iBottomLineWidth);
    int iMachineItemWidth = m_pixmapSize.width() +
                            iMajorSpacing +
                            iMiddleColumnWidth +
                            iMajorSpacing +
                            m_toolPixmapSize.width() + 2 * iButtonMargin;
    iProposedWidth += iMachineItemWidth;

    /* Return result: */
    return iProposedWidth;
}

int UIChooserItemMachine::minimumHeightHint() const
{
    /* Prepare variables: */
    const int iMarginV = data(MachineItemData_MarginV).toInt();
    const int iMachineItemTextSpacing = data(MachineItemData_TextSpacing).toInt();
    const int iButtonMargin = data(MachineItemData_ButtonMargin).toInt();

    /* Calculating proposed height: */
    int iProposedHeight = 0;

    /* Two margins: */
    iProposedHeight += 2 * iMarginV;
    /* And machine-item content to take into account: */
    int iTopLineHeight = qMax(m_visibleNameSize.height(), m_visibleSnapshotNameSize.height());
    int iBottomLineHeight = qMax(m_statePixmapSize.height(), m_stateTextSize.height());
    int iMiddleColumnHeight = iTopLineHeight +
                              iMachineItemTextSpacing +
                              iBottomLineHeight;
    QList<int> heights;
    heights << m_pixmapSize.height() << iMiddleColumnHeight << m_toolPixmapSize.height() + 2 * iButtonMargin;
    int iMaxHeight = 0;
    foreach (int iHeight, heights)
        iMaxHeight = qMax(iMaxHeight, iHeight);
    iProposedHeight += iMaxHeight;

    /* Return result: */
    return iProposedHeight;
}

QSizeF UIChooserItemMachine::sizeHint(Qt::SizeHint enmWhich, const QSizeF &constraint /* = QSizeF() */) const
{
    /* If Qt::MinimumSize requested: */
    if (enmWhich == Qt::MinimumSize)
        return QSizeF(minimumWidthHint(), minimumHeightHint());
    /* Else call to base-class: */
    return UIChooserItem::sizeHint(enmWhich, constraint);
}

QPixmap UIChooserItemMachine::toPixmap()
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

bool UIChooserItemMachine::isDropAllowed(QGraphicsSceneDragDropEvent *pEvent, UIChooserItemDragToken where) const
{
    /* No drops while saving groups: */
    if (model()->isGroupSavingInProgress())
        return false;
    /* If drag token is shown, its up to parent to decide: */
    if (where != UIChooserItemDragToken_Off)
        return parentItem()->isDropAllowed(pEvent);

    /* No drops for immutable item: */
    if (isLockedMachine())
        return false;
    /* No drops for inaccessible item: */
    if (!accessible())
        return false;

    /* Else we should try to cast mime to known classes: */
    const QMimeData *pMimeData = pEvent->mimeData();
    if (pMimeData->hasFormat(UIChooserItemMachine::className()))
    {
        /* Get passed machine-item: */
        const UIChooserItemMimeData *pCastedMimeData = qobject_cast<const UIChooserItemMimeData*>(pMimeData);
        AssertPtrReturn(pCastedMimeData, false);
        UIChooserItem *pItem = pCastedMimeData->item();
        AssertPtrReturn(pItem, false);
        UIChooserItemMachine *pMachineItem = pItem->toMachineItem();
        AssertPtrReturn(pMachineItem, false);

        /* No drops for cloud items: */
        if (   cacheType() != UIVirtualMachineItemType_Local
            || pMachineItem->cacheType() != UIVirtualMachineItemType_Local)
            return false;
        /* No drops for immutable item: */
        if (pMachineItem->isLockedMachine())
            return false;
        /* No drops for the same item: */
        if (pMachineItem->id() == id())
            return false;

        /* Allow finally: */
        return true;
    }
    /* That was invalid mime: */
    return false;
}

void UIChooserItemMachine::processDrop(QGraphicsSceneDragDropEvent *pEvent, UIChooserItem *pFromWho, UIChooserItemDragToken where)
{
    /* Get mime: */
    const QMimeData *pMime = pEvent->mimeData();
    /* Make sure this handler called by this item (not by children): */
    AssertMsg(!pFromWho && where == UIChooserItemDragToken_Off, ("Machine graphics item do NOT support children!"));
    Q_UNUSED(pFromWho);
    Q_UNUSED(where);
    if (pMime->hasFormat(UIChooserItemMachine::className()))
    {
        switch (pEvent->proposedAction())
        {
            case Qt::MoveAction:
            case Qt::CopyAction:
            {
                /* Remember scene: */
                UIChooserModel *pModel = model();

                /* Get passed item: */
                const UIChooserItemMimeData *pCastedMime = qobject_cast<const UIChooserItemMimeData*>(pMime);
                AssertMsg(pCastedMime, ("Can't cast passed mime-data to UIChooserItemMimeData!"));
                UIChooserNode *pNode = pCastedMime->item()->node();

                /* Group passed item with current-item into the new group: */
                UIChooserNodeGroup *pNewGroupNode = new UIChooserNodeGroup(parentItem()->node(),
                                                                           parentItem()->node()->nodes().size(),
                                                                           QUuid() /* id */,
                                                                           UIChooserModel::uniqueGroupName(parentItem()->node()),
                                                                           parentItem()->node()->toGroupNode()->groupType(),
                                                                           true /* opened */);
                UIChooserItemGroup *pNewGroupItem = new UIChooserItemGroup(parentItem(), pNewGroupNode);
                UIChooserNodeMachine *pNewMachineNode1 = new UIChooserNodeMachine(pNewGroupNode,
                                                                                  pNewGroupNode->nodes().size(),
                                                                                  nodeToMachineType());
                new UIChooserItemMachine(pNewGroupItem, pNewMachineNode1);
                UIChooserNodeMachine *pNewMachineNode2 = new UIChooserNodeMachine(pNewGroupNode,
                                                                                  pNewGroupNode->nodes().size(),
                                                                                  pNode->toMachineNode());
                new UIChooserItemMachine(pNewGroupItem, pNewMachineNode2);

                /* If proposed action is 'move': */
                if (pEvent->proposedAction() == Qt::MoveAction)
                {
                    /* Delete passed node: */
                    delete pNode;
                }
                /* Delete this node: */
                delete node();

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
}

void UIChooserItemMachine::resetDragToken()
{
    /* Reset drag token for this item: */
    if (dragTokenPlace() != UIChooserItemDragToken_Off)
    {
        setDragTokenPlace(UIChooserItemDragToken_Off);
        update();
    }
}

QMimeData* UIChooserItemMachine::createMimeData()
{
    return new UIChooserItemMimeData(this);
}

void UIChooserItemMachine::sltHandleWindowRemapped()
{
    /* Recache and update pixmaps: */
    AssertPtrReturnVoid(cache());
    cache()->recachePixmap();
    updatePixmaps();
}

void UIChooserItemMachine::prepare()
{
    /* Color tones: */
#if defined(VBOX_WS_MAC)
    m_iDefaultLightnessStart = 120;
    m_iDefaultLightnessFinal = 110;
    m_iHoverLightnessStart = 125;
    m_iHoverLightnessFinal = 115;
    m_iHighlightLightnessStart = 115;
    m_iHighlightLightnessFinal = 105;
#elif defined(VBOX_WS_WIN)
    m_iDefaultLightnessStart = 120;
    m_iDefaultLightnessFinal = 110;
    m_iHoverLightnessStart = 220;
    m_iHoverLightnessFinal = 210;
    m_iHighlightLightnessStart = 190;
    m_iHighlightLightnessFinal = 180;
#else /* !VBOX_WS_MAC && !VBOX_WS_WIN */
    m_iDefaultLightnessStart = 110;
    m_iDefaultLightnessFinal = 100;
    m_iHoverLightnessStart = 125;
    m_iHoverLightnessFinal = 115;
    m_iHighlightLightnessStart = 110;
    m_iHighlightLightnessFinal = 100;
#endif /* !VBOX_WS_MAC && !VBOX_WS_WIN */

    /* Fonts: */
    m_nameFont = font();
    m_nameFont.setWeight(QFont::Bold);
    m_snapshotNameFont = font();
    m_stateTextFont = font();

    /* Sizes: */
    m_iFirstRowMaximumWidth = 0;
    m_iMinimumNameWidth = 0;
    m_iMaximumNameWidth = 0;
    m_iMinimumSnapshotNameWidth = 0;
    m_iMaximumSnapshotNameWidth = 0;

    /* Add item to the parent: */
    AssertPtrReturnVoid(parentItem());
    parentItem()->addItem(this, isFavorite(), position());

    /* Configure connections: */
    connect(gpManager, &UIVirtualBoxManager::sigWindowRemapped,
            this, &UIChooserItemMachine::sltHandleWindowRemapped);
    connect(model(), &UIChooserModel::sigSelectionChanged,
            this, &UIChooserItemMachine::sltUpdateFirstRowMaximumWidth);
    connect(this, &UIChooserItemMachine::sigHoverEnter,
            this, &UIChooserItemMachine::sltUpdateFirstRowMaximumWidth);
    connect(this, &UIChooserItemMachine::sigHoverLeave,
            this, &UIChooserItemMachine::sltUpdateFirstRowMaximumWidth);

    /* Init: */
    updateItem();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserItemMachine::sltRetranslateUI);
}

void UIChooserItemMachine::cleanup()
{
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
    AssertPtrReturnVoid(parentItem());
    parentItem()->removeItem(this);
}

QVariant UIChooserItemMachine::data(int iKey) const
{
    /* Provide other members with required data: */
    switch (iKey)
    {
        /* Layout hints: */
        case MachineItemData_MarginHL:     return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        case MachineItemData_MarginHR:     return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4 * 5;
        case MachineItemData_MarginV:      return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4 * 3;
        case MachineItemData_MajorSpacing: return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 2;
        case MachineItemData_MinorSpacing: return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4;
        case MachineItemData_TextSpacing:  return 0;
        case MachineItemData_ButtonMargin: return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4;

        /* Default: */
        default: break;
    }
    return QVariant();
}

void UIChooserItemMachine::updatePixmaps()
{
    /* Update pixmap: */
    updatePixmap();
    /* Update state-pixmap: */
    updateStatePixmap();
    /* Update tool-pixmap: */
    updateToolPixmap();
}

void UIChooserItemMachine::updatePixmap()
{
    /* Get new pixmap and pixmap-size: */
    AssertPtrReturnVoid(cache());
    QSize pixmapSize;
    QPixmap pixmap = cache()->osPixmap(&pixmapSize);
    /* Update linked values: */
    if (m_pixmapSize != pixmapSize)
    {
        m_pixmapSize = pixmapSize;
        updateFirstRowMaximumWidth();
        updateGeometry();
    }
    if (m_pixmap.toImage() != pixmap.toImage())
    {
        m_pixmap = pixmap;
        update();
    }
}

void UIChooserItemMachine::updateStatePixmap()
{
    /* Determine icon metric: */
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    /* Get new state-pixmap and state-pixmap size: */
    AssertPtrReturnVoid(cache());
    const QIcon stateIcon = cache()->machineStateIcon();
    AssertReturnVoid(!stateIcon.isNull());
    const QSize statePixmapSize = QSize(iIconMetric, iIconMetric);
    const qreal fDevicePixelRatio = gpManager->windowHandle() ? gpManager->windowHandle()->devicePixelRatio() : 1;
    const QPixmap statePixmap = stateIcon.pixmap(statePixmapSize, fDevicePixelRatio);
    /* Update linked values: */
    if (m_statePixmapSize != statePixmapSize)
    {
        m_statePixmapSize = statePixmapSize;
        updateGeometry();
    }
    if (m_statePixmap.toImage() != statePixmap.toImage())
    {
        m_statePixmap = statePixmap;
        update();
    }
}

void UIChooserItemMachine::updateToolPixmap()
{
    /* Determine icon metric: */
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize) * .75;
    /* Create new tool-pixmap and tool-pixmap size: */
    const QIcon toolIcon = UIIconPool::iconSet(":/tools_menu_24px.png");
    AssertReturnVoid(!toolIcon.isNull());
    const QSize toolPixmapSize = QSize(iIconMetric, iIconMetric);
    const qreal fDevicePixelRatio = gpManager->windowHandle() ? gpManager->windowHandle()->devicePixelRatio() : 1;
    const QPixmap toolPixmap = toolIcon.pixmap(toolPixmapSize, fDevicePixelRatio);
    /* Update linked values: */
    if (m_toolPixmapSize != toolPixmapSize)
    {
        m_toolPixmapSize = toolPixmapSize;
        updateGeometry();
    }
    if (m_toolPixmap.toImage() != toolPixmap.toImage())
    {
        m_toolPixmap = toolPixmap;
        update();
    }
}

void UIChooserItemMachine::updateFirstRowMaximumWidth()
{
    /* Prepare variables: */
    const int iMarginHL = data(MachineItemData_MarginHL).toInt();
    const int iMarginHR = data(MachineItemData_MarginHR).toInt();
    const int iMajorSpacing = data(MachineItemData_MajorSpacing).toInt();
    const int iButtonMargin = data(MachineItemData_ButtonMargin).toInt();

    /* Calculate new maximum width for the first row: */
    int iFirstRowMaximumWidth = (int)geometry().width();
    iFirstRowMaximumWidth -= iMarginHL; /* left margin */
    iFirstRowMaximumWidth -= m_pixmapSize.width(); /* left pixmap width */
    iFirstRowMaximumWidth -= iMajorSpacing; /* spacing between left pixmap and name(s) */
    if (   model()->firstSelectedItem() == this
        || isHovered())
    {
        iFirstRowMaximumWidth -= iMajorSpacing; /* spacing between name(s) and right pixmap */
        iFirstRowMaximumWidth -= m_toolPixmapSize.width() + 2 * iButtonMargin; /* right pixmap width */
    }
    iFirstRowMaximumWidth -= iMarginHR; /* right margin */

    /* Is there something changed? */
    if (m_iFirstRowMaximumWidth == iFirstRowMaximumWidth)
        return;

    /* Update linked values: */
    m_iFirstRowMaximumWidth = iFirstRowMaximumWidth;
    updateMaximumNameWidth();
    updateMaximumSnapshotNameWidth();
}

void UIChooserItemMachine::updateMinimumNameWidth()
{
    /* Calculate new minimum name width: */
    QPaintDevice *pPaintDevice = model()->paintDevice();
    const QFontMetrics fm(m_nameFont, pPaintDevice);
    const int iMinimumNameWidth = fm.horizontalAdvance(compressText(m_nameFont, pPaintDevice, name(),
                                                                    textWidth(m_nameFont, pPaintDevice, 15)));

    /* Is there something changed? */
    if (m_iMinimumNameWidth == iMinimumNameWidth)
        return;

    /* Update linked values: */
    m_iMinimumNameWidth = iMinimumNameWidth;
    updateGeometry();
}

void UIChooserItemMachine::updateMinimumSnapshotNameWidth()
{
    /* Calculate new minimum snapshot-name width: */
    int iMinimumSnapshotNameWidth = 0;
    /* Is there any snapshot exists? */
    if (   cacheType() == UIVirtualMachineItemType_Local
        && !cache()->toLocal()->snapshotName().isEmpty())
    {
        const QFontMetrics fm(m_snapshotNameFont, model()->paintDevice());
        const int iBracketWidth = fm.horizontalAdvance("()"); /* bracket width */
        const int iActualTextWidth = fm.horizontalAdvance(cache()->toLocal()->snapshotName()); /* snapshot-name width */
        const int iMinimumTextWidth = fm.horizontalAdvance("..."); /* ellipsis width */
        iMinimumSnapshotNameWidth = iBracketWidth + qMin(iActualTextWidth, iMinimumTextWidth);
    }

    /* Is there something changed? */
    if (m_iMinimumSnapshotNameWidth == iMinimumSnapshotNameWidth)
        return;

    /* Update linked values: */
    m_iMinimumSnapshotNameWidth = iMinimumSnapshotNameWidth;
    updateMaximumNameWidth();
    updateGeometry();
}

void UIChooserItemMachine::updateMaximumNameWidth()
{
    /* Calculate new maximum name width: */
    int iMaximumNameWidth = m_iFirstRowMaximumWidth;
    /* Do we have a minimum snapshot-name width? */
    if (m_iMinimumSnapshotNameWidth != 0)
    {
        /* Prepare variables: */
        int iMinorSpacing = data(MachineItemData_MinorSpacing).toInt();
        /* Take spacing and snapshot-name into account: */
        iMaximumNameWidth -= (iMinorSpacing + m_iMinimumSnapshotNameWidth);
    }

    /* Is there something changed? */
    if (m_iMaximumNameWidth == iMaximumNameWidth)
        return;

    /* Update linked values: */
    m_iMaximumNameWidth = iMaximumNameWidth;
    updateVisibleName();
}

void UIChooserItemMachine::updateMaximumSnapshotNameWidth()
{
    /* Prepare variables: */
    int iMinorSpacing = data(MachineItemData_MinorSpacing).toInt();

    /* Calculate new maximum snapshot-name width: */
    int iMaximumSnapshotNameWidth = m_iFirstRowMaximumWidth;
    iMaximumSnapshotNameWidth -= (iMinorSpacing + m_visibleNameSize.width());

    /* Is there something changed? */
    if (m_iMaximumSnapshotNameWidth == iMaximumSnapshotNameWidth)
        return;

    /* Update linked values: */
    m_iMaximumSnapshotNameWidth = iMaximumSnapshotNameWidth;
    updateVisibleSnapshotName();
}

void UIChooserItemMachine::updateVisibleName()
{
    /* Prepare variables: */
    QPaintDevice *pPaintDevice = model()->paintDevice();

    /* Calculate new visible name and name-size: */
    QString strVisibleName = compressText(m_nameFont, pPaintDevice, name(), m_iMaximumNameWidth);
    QSize visibleNameSize = textSize(m_nameFont, pPaintDevice, strVisibleName);

    /* Update linked values: */
    if (m_visibleNameSize != visibleNameSize)
    {
        m_visibleNameSize = visibleNameSize;
        updateMaximumSnapshotNameWidth();
        updateGeometry();
    }
    if (m_strVisibleName != strVisibleName)
    {
        m_strVisibleName = strVisibleName;
        update();
    }
}

void UIChooserItemMachine::updateVisibleSnapshotName()
{
    /* Make sure this is local machine item: */
    if (cacheType() != UIVirtualMachineItemType_Local)
        return;

    /* Prepare variables: */
    QPaintDevice *pPaintDevice = model()->paintDevice();

    /* Calculate new visible snapshot-name: */
    const int iBracketWidth = QFontMetrics(m_snapshotNameFont, pPaintDevice).horizontalAdvance("()");
    QString strVisibleSnapshotName = compressText(m_snapshotNameFont, pPaintDevice, cache()->toLocal()->snapshotName(),
                                                  m_iMaximumSnapshotNameWidth - iBracketWidth);
    strVisibleSnapshotName = QString("(%1)").arg(strVisibleSnapshotName);
    const QSize visibleSnapshotNameSize = textSize(m_snapshotNameFont, pPaintDevice, strVisibleSnapshotName);

    /* Update linked values: */
    if (m_visibleSnapshotNameSize != visibleSnapshotNameSize)
    {
        m_visibleSnapshotNameSize = visibleSnapshotNameSize;
        updateGeometry();
    }
    if (m_strVisibleSnapshotName != strVisibleSnapshotName)
    {
        m_strVisibleSnapshotName = strVisibleSnapshotName;
        update();
    }
}

void UIChooserItemMachine::updateStateTextSize()
{
    /* Get new state-text and state-text size: */
    AssertPtrReturnVoid(cache());
    const QSize stateTextSize = textSize(m_stateTextFont, model()->paintDevice(), cache()->machineStateName());

    /* Update linked values: */
    if (m_stateTextSize != stateTextSize)
    {
        m_stateTextSize = stateTextSize;
        updateGeometry();
    }
}

void UIChooserItemMachine::paintBackground(QPainter *pPainter, const QRect &rectangle)
{
    /* Save painter: */
    pPainter->save();

    /* Prepare color: */
    const QPalette pal = QApplication::palette();

    /* Selected-item background: */
    if (model()->selectedItems().contains(this))
    {
        /* Prepare color: */
        QColor backgroundColor = pal.color(QPalette::Active, QPalette::Highlight);
        /* Draw gradient: */
        QLinearGradient bgGrad(rectangle.topLeft(), rectangle.bottomLeft());
        bgGrad.setColorAt(0, backgroundColor.lighter(m_iHighlightLightnessStart));
        bgGrad.setColorAt(1, backgroundColor.lighter(m_iHighlightLightnessFinal));
        pPainter->fillRect(rectangle, bgGrad);

        if (isHovered())
        {
            /* Prepare color: */
            QColor animationColor1 = QColor(Qt::white);
            QColor animationColor2 = QColor(Qt::white);
#ifdef VBOX_WS_MAC
            animationColor1.setAlpha(90);
#else
            animationColor1.setAlpha(30);
#endif
            animationColor2.setAlpha(0);
            /* Draw hovered-item animated gradient: */
            QRect animatedRect = rectangle;
            animatedRect.setWidth(animatedRect.height());
            const int iLength = 2 * animatedRect.width() + rectangle.width();
            const int iShift = - animatedRect.width() + iLength * animatedValue() / 100;
            animatedRect.moveLeft(iShift);
            QLinearGradient bgAnimatedGrad(animatedRect.topLeft(), animatedRect.bottomRight());
            bgAnimatedGrad.setColorAt(0,   animationColor2);
            bgAnimatedGrad.setColorAt(0.1, animationColor2);
            bgAnimatedGrad.setColorAt(0.5, animationColor1);
            bgAnimatedGrad.setColorAt(0.9, animationColor2);
            bgAnimatedGrad.setColorAt(1,   animationColor2);
            pPainter->fillRect(rectangle, bgAnimatedGrad);
        }
    }
    /* Hovered-item background: */
    else if (isHovered())
    {
        /* Prepare color: */
        QColor backgroundColor = pal.color(QPalette::Active, QPalette::Highlight);
        /* Draw gradient: */
        QLinearGradient bgGrad(rectangle.topLeft(), rectangle.bottomLeft());
        bgGrad.setColorAt(0, backgroundColor.lighter(m_iHoverLightnessStart));
        bgGrad.setColorAt(1, backgroundColor.lighter(m_iHoverLightnessFinal));
        pPainter->fillRect(rectangle, bgGrad);

        /* Prepare color: */
        QColor animationColor1 = QColor(Qt::white);
        QColor animationColor2 = QColor(Qt::white);
#ifdef VBOX_WS_MAC
        animationColor1.setAlpha(120);
#else
        animationColor1.setAlpha(50);
#endif
        animationColor2.setAlpha(0);
        /* Draw hovered-item animated gradient: */
        QRect animatedRect = rectangle;
        animatedRect.setWidth(animatedRect.height());
        const int iLength = 2 * animatedRect.width() + rectangle.width();
        const int iShift = - animatedRect.width() + iLength * animatedValue() / 100;
        animatedRect.moveLeft(iShift);
        QLinearGradient bgAnimatedGrad(animatedRect.topLeft(), animatedRect.bottomRight());
        bgAnimatedGrad.setColorAt(0,   animationColor2);
        bgAnimatedGrad.setColorAt(0.1, animationColor2);
        bgAnimatedGrad.setColorAt(0.5, animationColor1);
        bgAnimatedGrad.setColorAt(0.9, animationColor2);
        bgAnimatedGrad.setColorAt(1,   animationColor2);
        pPainter->fillRect(rectangle, bgAnimatedGrad);
    }
    /* Default background: */
    else
    {
        /* Prepare color: */
        QColor backgroundColor = pal.color(QPalette::Active, QPalette::Window);
        /* Draw gradient: */
        QLinearGradient bgGrad(rectangle.topLeft(), rectangle.bottomLeft());
        bgGrad.setColorAt(0, backgroundColor.lighter(m_iDefaultLightnessStart));
        bgGrad.setColorAt(1, backgroundColor.lighter(m_iDefaultLightnessFinal));
        pPainter->fillRect(rectangle, bgGrad);
    }

    /* Paint drag token UP? */
    if (dragTokenPlace() != UIChooserItemDragToken_Off)
    {
        /* Window color: */
        QColor backgroundColor;

        QLinearGradient dragTokenGradient;
        QRect dragTokenRect = rectangle;
        if (dragTokenPlace() == UIChooserItemDragToken_Up)
        {
            /* Selected-item background: */
            if (model()->selectedItems().contains(this))
                backgroundColor = pal.color(QPalette::Active, QPalette::Highlight);
            /* Default background: */
            else
                backgroundColor = pal.color(QPalette::Active, QPalette::Window);

            dragTokenRect.setHeight(5);
            dragTokenGradient.setStart(dragTokenRect.bottomLeft());
            dragTokenGradient.setFinalStop(dragTokenRect.topLeft());
        }
        else if (dragTokenPlace() == UIChooserItemDragToken_Down)
        {
            /* Selected-item background: */
            if (model()->selectedItems().contains(this))
                backgroundColor = pal.color(QPalette::Active, QPalette::Highlight);
            /* Default background: */
            else
                backgroundColor = pal.color(QPalette::Active, QPalette::Window);

            dragTokenRect.setTopLeft(dragTokenRect.bottomLeft() - QPoint(0, 4));
            dragTokenGradient.setStart(dragTokenRect.topLeft());
            dragTokenGradient.setFinalStop(dragTokenRect.bottomLeft());
        }
        QColor color1 = backgroundColor;
        QColor color2 = backgroundColor;
        color1.setAlpha(64);
        color2.setAlpha(255);
        dragTokenGradient.setColorAt(0, color1);
        dragTokenGradient.setColorAt(1, color2);
        pPainter->fillRect(dragTokenRect, dragTokenGradient);
    }

    /* Restore painter: */
    pPainter->restore();
}

void UIChooserItemMachine::paintFrame(QPainter *pPainter, const QRect &rectangle)
{
    /* Only selected and/or hovered item should have a frame: */
    if (!model()->selectedItems().contains(this) && !isHovered())
        return;

    /* Save painter: */
    pPainter->save();

    /* Prepare color: */
    const QPalette pal = QApplication::palette();
    QColor strokeColor;

    /* Selected-item frame: */
    if (model()->selectedItems().contains(this))
        strokeColor = pal.color(QPalette::Active, QPalette::Highlight).lighter(m_iHighlightLightnessStart - 40);
    /* Hovered-item frame: */
    else if (isHovered())
        strokeColor = pal.color(QPalette::Active, QPalette::Highlight).lighter(m_iHoverLightnessStart - 40);

    /* Create/assign pen: */
    QPen pen(strokeColor);
    pen.setWidth(0);
    pPainter->setPen(pen);

    /* Draw borders: */
    if (dragTokenPlace() != UIChooserItemDragToken_Up)
        pPainter->drawLine(rectangle.topLeft(),    rectangle.topRight()    + QPoint(1, 0));
    if (dragTokenPlace() != UIChooserItemDragToken_Down)
        pPainter->drawLine(rectangle.bottomLeft(), rectangle.bottomRight() + QPoint(1, 0));
    pPainter->drawLine(rectangle.topLeft(),    rectangle.bottomLeft());

    /* Restore painter: */
    pPainter->restore();
}

void UIChooserItemMachine::paintMachineInfo(QPainter *pPainter, const QRect &rectangle)
{
    /* Prepare variables: */
    const int iFullWidth = rectangle.width();
    const int iFullHeight = rectangle.height();
    const int iMarginHL = data(MachineItemData_MarginHL).toInt();
    const int iMarginHR = data(MachineItemData_MarginHR).toInt();
    const int iMajorSpacing = data(MachineItemData_MajorSpacing).toInt();
    const int iMinorSpacing = data(MachineItemData_MinorSpacing).toInt();
    const int iMachineItemTextSpacing = data(MachineItemData_TextSpacing).toInt();
    const int iButtonMargin = data(MachineItemData_ButtonMargin).toInt();

    /* Selected or hovered item foreground: */
    if (model()->selectedItems().contains(this) || isHovered())
    {
        /* Prepare palette: */
        const QPalette pal = QApplication::palette();

        /* Get background color: */
        const QColor highlight = pal.color(QPalette::Active, QPalette::Highlight);
        const QColor background = model()->selectedItems().contains(this)
                                ? highlight.lighter(m_iHighlightLightnessStart)
                                : highlight.lighter(m_iHoverLightnessStart);

        /* Gather foreground color for background one: */
        const QColor foreground = suitableForegroundColor(pal, background);
        pPainter->setPen(foreground);
    }

    /* Calculate indents: */
    int iLeftColumnIndent = iMarginHL;

    /* Paint left column: */
    {
        /* Prepare variables: */
        int iMachinePixmapX = iLeftColumnIndent;
        int iMachinePixmapY = (iFullHeight - m_pixmap.height() / m_pixmap.devicePixelRatio()) / 2;
        /* Paint pixmap: */
        paintPixmap(/* Painter: */
                    pPainter,
                    /* Point to paint in: */
                    QPoint(iMachinePixmapX, iMachinePixmapY),
                    /* Pixmap to paint: */
                    m_pixmap);
    }

    /* Calculate indents: */
    int iMiddleColumnIndent = iLeftColumnIndent +
                              m_pixmapSize.width() +
                              iMajorSpacing;

    /* Paint middle column: */
    {
        /* Calculate indents: */
        int iTopLineHeight = qMax(m_visibleNameSize.height(), m_visibleSnapshotNameSize.height());
        int iBottomLineHeight = qMax(m_statePixmapSize.height(), m_stateTextSize.height());
        int iRightColumnHeight = iTopLineHeight + iMachineItemTextSpacing + iBottomLineHeight;
        int iTopLineIndent = (iFullHeight - iRightColumnHeight) / 2 - 1;

        /* Paint top line: */
        {
            /* Paint left element: */
            {
                /* Prepare variables: */
                int iNameX = iMiddleColumnIndent;
                int iNameY = iTopLineIndent;
                /* Paint name: */
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
            }

            /* Calculate indents: */
            int iSnapshotNameIndent = iMiddleColumnIndent +
                                      m_visibleNameSize.width() +
                                      iMinorSpacing;

            /* Paint middle element: */
            if (   cacheType() == UIVirtualMachineItemType_Local
                && !cache()->toLocal()->snapshotName().isEmpty())
            {
                /* Prepare variables: */
                int iSnapshotNameX = iSnapshotNameIndent;
                int iSnapshotNameY = iTopLineIndent;
                /* Paint snapshot-name: */
                paintText(/* Painter: */
                          pPainter,
                          /* Point to paint in: */
                          QPoint(iSnapshotNameX, iSnapshotNameY),
                          /* Font to paint text: */
                          m_snapshotNameFont,
                          /* Paint device: */
                          model()->paintDevice(),
                          /* Text to paint: */
                          m_strVisibleSnapshotName);
            }
        }

        /* Calculate indents: */
        int iBottomLineIndent = iTopLineIndent + iTopLineHeight + 1;

        /* Paint bottom line: */
        {
            /* Paint left element: */
            {
                /* Prepare variables: */
                int iMachineStatePixmapX = iMiddleColumnIndent;
                int iMachineStatePixmapY = iBottomLineIndent;
                /* Paint state pixmap: */
                paintPixmap(/* Painter: */
                            pPainter,
                            /* Point to paint in: */
                            QPoint(iMachineStatePixmapX, iMachineStatePixmapY),
                            /* Pixmap to paint: */
                            m_statePixmap);
            }

            /* Calculate indents: */
            int iMachineStateTextIndent = iMiddleColumnIndent +
                                          m_statePixmapSize.width() +
                                          iMinorSpacing;

            /* Paint right element: */
            {
                /* Prepare variables: */
                int iMachineStateTextX = iMachineStateTextIndent;
                int iMachineStateTextY = iBottomLineIndent + 1;
                /* Paint state text: */
                AssertPtrReturnVoid(cache());
                paintText(/* Painter: */
                          pPainter,
                          /* Point to paint in: */
                          QPoint(iMachineStateTextX, iMachineStateTextY),
                          /* Font to paint text: */
                          m_stateTextFont,
                          /* Paint device: */
                          model()->paintDevice(),
                          /* Text to paint: */
                          cache()->machineStateName());
            }
        }
    }

    /* Calculate indents: */
    QGraphicsView *pView = model()->scene()->views().first();
    const QPointF sceneCursorPosition = pView->mapToScene(pView->mapFromGlobal(QCursor::pos()));
    const QPoint itemCursorPosition = mapFromScene(sceneCursorPosition).toPoint();
    int iRightColumnIndent = iFullWidth - iMarginHR - 1 - m_toolPixmap.width() / m_toolPixmap.devicePixelRatio();

    /* Paint right column: */
    if (   model()->firstSelectedItem() == this
        || isHovered())
    {
        /* Prepare variables: */
        const int iToolPixmapX = iRightColumnIndent;
        const int iToolPixmapY = (iFullHeight - m_toolPixmap.height() / m_toolPixmap.devicePixelRatio()) / 2;
        QRect toolButtonRectangle = QRect(iToolPixmapX,
                                          iToolPixmapY,
                                          m_toolPixmap.width() / m_toolPixmap.devicePixelRatio(),
                                          m_toolPixmap.height() / m_toolPixmap.devicePixelRatio());
        toolButtonRectangle.adjust(- iButtonMargin, -iButtonMargin, iButtonMargin, iButtonMargin);

        /* Paint tool button: */
        if (   isHovered()
            && isToolButtonArea(itemCursorPosition, 4))
            paintFlatButton(/* Painter: */
                            pPainter,
                            /* Button rectangle: */
                            toolButtonRectangle,
                            /* Cursor position: */
                            itemCursorPosition);

        /* Paint pixmap: */
        paintPixmap(/* Painter: */
                    pPainter,
                    /* Point to paint in: */
                    QPoint(iToolPixmapX, iToolPixmapY),
                    /* Pixmap to paint: */
                    m_toolPixmap);
    }
}

/* static */
bool UIChooserItemMachine::checkIfContains(const QList<UIChooserItemMachine*> &list, UIChooserItemMachine *pItem)
{
    /* Check if passed list contains passed machine-item id: */
    foreach (UIChooserItemMachine *pIteratedItem, list)
        if (pIteratedItem->id() == pItem->id())
            return true;
    /* Found nothing? */
    return false;
}
