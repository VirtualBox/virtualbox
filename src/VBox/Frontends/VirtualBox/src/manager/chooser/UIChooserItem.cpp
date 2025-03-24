/* $Id: UIChooserItem.cpp 108918 2025-03-24 16:07:18Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserItem class definition.
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
#include <QAccessibleObject>
#include <QApplication>
#include <QStyle>
#include <QPainter>
#include <QGraphicsScene>
#include <QStyleOptionFocusRect>
#include <QGraphicsSceneMouseEvent>
#include <QStateMachine>
#include <QPropertyAnimation>
#include <QSignalTransition>
#include <QDrag>

/* GUI includes: */
#include "UIChooserItem.h"
#include "UIChooserItemGroup.h"
#include "UIChooserItemGlobal.h"
#include "UIChooserItemMachine.h"
#include "UIChooserView.h"
#include "UIChooserModel.h"
#include "UIChooserNode.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIImageTools.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** QAccessibleObject extension used as an accessibility interface for Chooser-view items. */
class UIAccessibilityInterfaceForUIChooserItem : public QAccessibleObject
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating Chooser-view accessibility interface: */
        if (pObject && strClassname == QLatin1String("UIChooserItem"))
            return new UIAccessibilityInterfaceForUIChooserItem(pObject);

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pObject to the base-class. */
    UIAccessibilityInterfaceForUIChooserItem(QObject *pObject)
        : QAccessibleObject(pObject)
    {}

    /** Returns the parent. */
    virtual QAccessibleInterface *parent() const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(item(), 0);

        /* Return the parent: */
        return QAccessible::queryAccessibleInterface(item()->model()->view());
    }

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(item(), 0);

        /* Return the number of group children: */
        if (item()->type() == UIChooserNodeType_Group)
            return item()->items().size();

        /* Zero by default: */
        return 0;
    }

    /** Returns the child with the passed @a iIndex. */
    virtual QAccessibleInterface *child(int iIndex) const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(item(), 0);
        /* Make sure index is valid: */
        AssertReturn(iIndex >= 0 && iIndex < childCount(), 0);

        /* Return the child with the passed iIndex: */
        return QAccessible::queryAccessibleInterface(item()->items().at(iIndex));
    }

    /** Returns the index of the passed @a pChild. */
    virtual int indexOfChild(const QAccessibleInterface *pChild) const RT_OVERRIDE
    {
        /* Search for corresponding child: */
        for (int i = 0; i < childCount(); ++i)
            if (child(i) == pChild)
                return i;

        /* -1 by default: */
        return -1;
    }

    /** Returns the rect. */
    virtual QRect rect() const RT_OVERRIDE
    {
        /* Now goes the mapping: */
        const QSize   itemSize         = item()->size().toSize();
        const QPointF itemPosInScene   = item()->mapToScene(QPointF(0, 0));
        const QPoint  itemPosInView    = item()->model()->view()->mapFromScene(itemPosInScene);
        const QPoint  itemPosInScreen  = item()->model()->view()->mapToGlobal(itemPosInView);
        const QRect   itemRectInScreen = QRect(itemPosInScreen, itemSize);
        return itemRectInScreen;
    }

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(item(), QString());

        switch (enmTextRole)
        {
            case QAccessible::Name:        return item()->name();
            case QAccessible::Description: return item()->description();
            default: break;
        }

        /* Null-string by default: */
        return QString();
    }

    /** Returns the role. */
    virtual QAccessible::Role role() const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(item(), QAccessible::NoRole);

        /* Return the role of group: */
        if (item()->type() == UIChooserNodeType_Group)
            return QAccessible::List;

        /* ListItem by default: */
        return QAccessible::ListItem;
    }

    /** Returns the state. */
    virtual QAccessible::State state() const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(item(), QAccessible::State());

        /* Compose the state: */
        QAccessible::State state;
        state.focusable = true;
        state.selectable = true;

        /* Compose the state of first selected-item: */
        if (item() && item() == item()->model()->firstSelectedItem())
        {
            state.active = true;
            state.focused = true;
            state.selected = true;
        }

        /* Compose the state of group: */
        if (item()->type() == UIChooserNodeType_Group)
        {
            state.expandable = true;
            if (!item()->toGroupItem()->isClosed())
                state.expanded = true;
        }

        /* Return the state: */
        return state;
    }

private:

    /** Returns corresponding Chooser-view item. */
    UIChooserItem *item() const { return qobject_cast<UIChooserItem*>(object()); }
};


/*********************************************************************************************************************************
*   Class UIChooserDisabledItemEffect implementation.                                                                            *
*********************************************************************************************************************************/

UIChooserDisabledItemEffect::UIChooserDisabledItemEffect(int iBlurRadius, QObject *pParent /* = 0 */)
    : QGraphicsEffect(pParent)
    , m_iBlurRadius(iBlurRadius)
{
}

void UIChooserDisabledItemEffect::draw(QPainter *pPainter)
{
    QPoint offset;
    QPixmap pixmap;
    /* Get the original pixmap: */
    pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset);
    QImage resultImage;
    /* Apply our blur and grayscale filters to the original pixmap: */
    UIImageTools::blurImage(pixmap.toImage(), resultImage, m_iBlurRadius);
    pixmap.convertFromImage(UIImageTools::toGray(resultImage));
    QWidget *pParentWidget = qobject_cast<QWidget*>(parent());
    pixmap.setDevicePixelRatio(  pParentWidget
                               ? UIDesktopWidgetWatchdog::devicePixelRatio(pParentWidget)
                               : UIDesktopWidgetWatchdog::devicePixelRatio());
    /* Use the filtered pixmap: */
    pPainter->drawPixmap(offset, pixmap);
}


/*********************************************************************************************************************************
*   Class UIChooserItem implementation.                                                                                          *
*********************************************************************************************************************************/

UIChooserItem::UIChooserItem(UIChooserItem *pParent, UIChooserNode *pNode,
                             int iDefaultValue /* = 0 */, int iHoveredValue /* = 100 */)
    : QIGraphicsWidget(pParent)
    , m_pParent(pParent)
    , m_pNode(pNode)
    , m_fHovered(false)
    , m_fSelected(false)
    , m_pHoveringMachine(0)
    , m_pHoveringAnimationForward(0)
    , m_pHoveringAnimationBackward(0)
    , m_iAnimationDuration(400)
    , m_iDefaultValue(iDefaultValue)
    , m_iHoveredValue(iHoveredValue)
    , m_iAnimatedValue(m_iDefaultValue)
    , m_pDisabledEffect(0)
    , m_enmDragTokenPlace(UIChooserItemDragToken_Off)
    , m_iDragTokenDarkness(110)
{
    /* Install Chooser-view item accessibility interface factory: */
    QAccessible::installFactory(UIAccessibilityInterfaceForUIChooserItem::pFactory);

    /* Assign item for passed node: */
    node()->setItem(this);

    /* Basic item setup: */
    setOwnedByLayout(false);
    setAcceptDrops(true);
    setFocusPolicy(Qt::NoFocus);
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setAcceptHoverEvents(!isRoot());

    /* Non-root item? */
    if (!isRoot())
    {
        /* Create hovering animation machine: */
        m_pHoveringMachine = new QStateMachine(this);
        if (m_pHoveringMachine)
        {
            /* Create 'default' state: */
            QState *pStateDefault = new QState(m_pHoveringMachine);
            /* Create 'hovered' state: */
            QState *pStateHovered = new QState(m_pHoveringMachine);

            /* Configure 'default' state: */
            if (pStateDefault)
            {
                /* When we entering default state => we assigning animatedValue to m_iDefaultValue: */
                pStateDefault->assignProperty(this, "animatedValue", m_iDefaultValue);

                /* Add state transitions: */
                QSignalTransition *pDefaultToHovered = pStateDefault->addTransition(this, SIGNAL(sigHoverEnter()), pStateHovered);
                if (pDefaultToHovered)
                {
                    /* Create forward animation: */
                    m_pHoveringAnimationForward = new QPropertyAnimation(this, "animatedValue", this);
                    if (m_pHoveringAnimationForward)
                    {
                        m_pHoveringAnimationForward->setDuration(m_iAnimationDuration);
                        m_pHoveringAnimationForward->setStartValue(m_iDefaultValue);
                        m_pHoveringAnimationForward->setEndValue(m_iHoveredValue);

                        /* Add to transition: */
                        pDefaultToHovered->addAnimation(m_pHoveringAnimationForward);
                    }
                }
            }

            /* Configure 'hovered' state: */
            if (pStateHovered)
            {
                /* When we entering hovered state => we assigning animatedValue to m_iHoveredValue: */
                pStateHovered->assignProperty(this, "animatedValue", m_iHoveredValue);

                /* Add state transitions: */
                QSignalTransition *pHoveredToDefault = pStateHovered->addTransition(this, SIGNAL(sigHoverLeave()), pStateDefault);
                if (pHoveredToDefault)
                {
                    /* Create backward animation: */
                    m_pHoveringAnimationBackward = new QPropertyAnimation(this, "animatedValue", this);
                    if (m_pHoveringAnimationBackward)
                    {
                        m_pHoveringAnimationBackward->setDuration(m_iAnimationDuration);
                        m_pHoveringAnimationBackward->setStartValue(m_iHoveredValue);
                        m_pHoveringAnimationBackward->setEndValue(m_iDefaultValue);

                        /* Add to transition: */
                        pHoveredToDefault->addAnimation(m_pHoveringAnimationBackward);
                    }
                }
            }

            /* Initial state is 'default': */
            m_pHoveringMachine->setInitialState(pStateDefault);
            /* Start state-machine: */
            m_pHoveringMachine->start();
        }

        /* Allocate the effect instance which we use when the item is marked as disabled: */
        m_pDisabledEffect = new UIChooserDisabledItemEffect(1 /* Blur Radius */, model()->view());
        if (m_pDisabledEffect)
        {
            setGraphicsEffect(m_pDisabledEffect);
            m_pDisabledEffect->setEnabled(node()->isDisabled());
        }
    }
}

UIChooserItemGroup *UIChooserItem::toGroupItem()
{
    UIChooserItemGroup *pItem = qgraphicsitem_cast<UIChooserItemGroup*>(this);
    AssertMsg(pItem, ("Trying to cast invalid item type to UIChooserItemGroup!"));
    return pItem;
}

UIChooserItemGlobal *UIChooserItem::toGlobalItem()
{
    UIChooserItemGlobal *pItem = qgraphicsitem_cast<UIChooserItemGlobal*>(this);
    AssertMsg(pItem, ("Trying to cast invalid item type to UIChooserItemGlobal!"));
    return pItem;
}

UIChooserItemMachine *UIChooserItem::toMachineItem()
{
    UIChooserItemMachine *pItem = qgraphicsitem_cast<UIChooserItemMachine*>(this);
    AssertMsg(pItem, ("Trying to cast invalid item type to UIChooserItemMachine!"));
    return pItem;
}

UIChooserModel *UIChooserItem::model() const
{
    UIChooserModel *pModel = qobject_cast<UIChooserModel*>(QIGraphicsWidget::scene()->parent());
    AssertMsg(pModel, ("Incorrect graphics scene parent set!"));
    return pModel;
}

bool UIChooserItem::isRoot() const
{
    return node()->isRoot();
}

QString UIChooserItem::name() const
{
    return node()->name();
}

QString UIChooserItem::fullName() const
{
    return node()->fullName();
}

QString UIChooserItem::description() const
{
    return node()->description();
}

QString UIChooserItem::definition() const
{
    return node()->definition();
}

bool UIChooserItem::isFavorite() const
{
    return node()->isFavorite();
}

void UIChooserItem::setFavorite(bool fFavorite)
{
    node()->setFavorite(fFavorite);
    if (m_pParent)
        m_pParent->toGroupItem()->updateFavorites();
}

int UIChooserItem::position() const
{
    return node()->position();
}

bool UIChooserItem::isHovered() const
{
    return m_fHovered;
}

bool UIChooserItem::isSelected() const
{
    return m_fSelected;
}

void UIChooserItem::setSelected(bool fSelected)
{
    m_fSelected = fSelected;
}

void UIChooserItem::setDisabledEffect(bool fOn)
{
    if (m_pDisabledEffect)
        m_pDisabledEffect->setEnabled(fOn);
}

void UIChooserItem::updateGeometry()
{
    /* Call to base-class: */
    QIGraphicsWidget::updateGeometry();

    /* Update parent's geometry: */
    if (parentItem())
        parentItem()->updateGeometry();
}

void UIChooserItem::makeSureItsVisible()
{
    /* Get parrent item: */
    UIChooserItemGroup *pParentItem = parentItem()->toGroupItem();
    if (!pParentItem)
        return;
    /* If item is not visible. That is all the parent group(s) are opened (expanded): */
    if (!isVisible())
    {
        /* We should make parent visible: */
        pParentItem->makeSureItsVisible();
        /* And make sure its opened: */
        if (pParentItem->isClosed())
            pParentItem->open(false);
    }
}

UIChooserItemDragToken UIChooserItem::dragTokenPlace() const
{
    return m_enmDragTokenPlace;
}

void UIChooserItem::setDragTokenPlace(UIChooserItemDragToken enmPlace)
{
    /* Something changed? */
    if (m_enmDragTokenPlace != enmPlace)
    {
        m_enmDragTokenPlace = enmPlace;
        update();
    }
}

void UIChooserItem::hoverMoveEvent(QGraphicsSceneHoverEvent *)
{
    if (!m_fHovered)
    {
        m_fHovered = true;
        emit sigHoverEnter();
    }
    update();
}

void UIChooserItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    if (m_fHovered)
    {
        m_fHovered = false;
        emit sigHoverLeave();
        update();
    }
}

void UIChooserItem::mousePressEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* By default, non-moveable and non-selectable items
     * can't grab mouse-press events which is required
     * to grab further mouse-move events which we wants... */
    if (isRoot())
        pEvent->ignore();
    else
        pEvent->accept();
}

void UIChooserItem::mouseMoveEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Make sure item is really dragged: */
    if (QLineF(pEvent->screenPos(),
               pEvent->buttonDownScreenPos(Qt::LeftButton)).length() <
        QApplication::startDragDistance())
        return;

    /* Initialize dragging: */
    QDrag *pDrag = new QDrag(pEvent->widget());
    model()->setCurrentDragObject(pDrag);
    pDrag->setPixmap(toPixmap());
    pDrag->setMimeData(createMimeData());
    pDrag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
}

void UIChooserItem::dragMoveEvent(QGraphicsSceneDragDropEvent *pEvent)
{
    /* Make sure we are non-root: */
    if (!isRoot())
    {
        /* Allow drag tokens only for the same item type as current: */
        bool fAllowDragToken = false;
        if ((type() == UIChooserNodeType_Group &&
             pEvent->mimeData()->hasFormat(UIChooserItemGroup::className())) ||
            (type() == UIChooserNodeType_Machine &&
             pEvent->mimeData()->hasFormat(UIChooserItemMachine::className())))
            fAllowDragToken = true;
        /* Do we need a drag-token? */
        if (fAllowDragToken)
        {
            QPoint p = pEvent->pos().toPoint();
            if (p.y() < 10)
                setDragTokenPlace(UIChooserItemDragToken_Up);
            else if (p.y() > minimumSizeHint().toSize().height() - 10)
                setDragTokenPlace(UIChooserItemDragToken_Down);
            else
                setDragTokenPlace(UIChooserItemDragToken_Off);
        }
    }
    /* Check if drop is allowed: */
    pEvent->setAccepted(isDropAllowed(pEvent, dragTokenPlace()));
}

void UIChooserItem::dragLeaveEvent(QGraphicsSceneDragDropEvent *)
{
    resetDragToken();
}

void UIChooserItem::dropEvent(QGraphicsSceneDragDropEvent *pEvent)
{
    /* Do we have token active? */
    switch (dragTokenPlace())
    {
        case UIChooserItemDragToken_Off:
        {
            /* Its our drop, processing: */
            processDrop(pEvent);
            break;
        }
        default:
        {
            /* Its parent drop, passing: */
            parentItem()->processDrop(pEvent, this, dragTokenPlace());
            break;
        }
    }
}

/* static */
QSize UIChooserItem::textSize(const QFont &font, QPaintDevice *pPaintDevice, const QString &strText)
{
    /* Make sure text is not empty: */
    if (strText.isEmpty())
        return QSize(0, 0);

    /* Return text size, based on font-metrics: */
    const QFontMetrics fm(font, pPaintDevice);
    return QSize(fm.horizontalAdvance(strText), fm.height());
}

/* static */
int UIChooserItem::textWidth(const QFont &font, QPaintDevice *pPaintDevice, int iCount)
{
    /* Return text width: */
    const QFontMetrics fm(font, pPaintDevice);
    QString strString;
    strString.fill('_', iCount);
    return fm.horizontalAdvance(strString);
}

/* static */
QString UIChooserItem::compressText(const QFont &font, QPaintDevice *pPaintDevice, QString strText, int iWidth)
{
    /* Check if passed text is empty: */
    if (strText.isEmpty())
        return strText;

    /* Check if passed text fits maximum width: */
    const QFontMetrics fm(font, pPaintDevice);
    if (fm.horizontalAdvance(strText) <= iWidth)
        return strText;

    /* Truncate otherwise: */
    const QString strEllipsis = QString("...");
    const int iEllipsisWidth = fm.horizontalAdvance(strEllipsis + " ");
    while (!strText.isEmpty() && fm.horizontalAdvance(strText) + iEllipsisWidth > iWidth)
        strText.truncate(strText.size() - 1);
    return strText + strEllipsis;
}

/* static */
void UIChooserItem::paintFrameRect(QPainter *pPainter, bool fIsSelected, int iRadius,
                                   const QRect &rectangle)
{
    pPainter->save();
    QPalette pal = QApplication::palette();
    QColor base = pal.color(QPalette::Active, fIsSelected ? QPalette::Highlight : QPalette::Window);
    pPainter->setPen(base.darker(160));
    if (iRadius)
        pPainter->drawRoundedRect(rectangle, iRadius, iRadius);
    else
        pPainter->drawRect(rectangle);
    pPainter->restore();
}

/* static */
void UIChooserItem::paintPixmap(QPainter *pPainter, const QPoint &point,
                                const QPixmap &pixmap)
{
    pPainter->drawPixmap(point, pixmap);
}

/* static */
void UIChooserItem::paintText(QPainter *pPainter, QPoint point,
                              const QFont &font, QPaintDevice *pPaintDevice,
                              const QString &strText)
{
    /* Prepare variables: */
    QFontMetrics fm(font, pPaintDevice);
    point += QPoint(0, fm.ascent());

    /* Draw text: */
    pPainter->save();
    pPainter->setFont(font);
    pPainter->drawText(point, strText);
    pPainter->restore();
}

/* static */
void UIChooserItem::paintFlatButton(QPainter *pPainter, const QRect &rectangle, const QPoint &cursorPosition)
{
    /* Save painter: */
    pPainter->save();

    /* Prepare colors: */
    const QColor color = QApplication::palette().color(QPalette::Active, QPalette::Button);

    /* Prepare pen: */
    QPen pen;
    pen.setColor(color);
    pen.setWidth(0);
    pPainter->setPen(pen);

    /* Apply clipping path: */
    QPainterPath path;
    path.addRect(rectangle);
    pPainter->setClipPath(path);

    /* Paint active background: */
    QRadialGradient grad(rectangle.center(), rectangle.width(), cursorPosition);
    QColor color1 = color;
    color1.setAlpha(50);
    QColor color2 = color;
    color2.setAlpha(250);
    grad.setColorAt(0, color1);
    grad.setColorAt(1, color2);
    pPainter->fillRect(rectangle.adjusted(0, 0, -1, -1), grad);

    /* Paint frame: */
    pPainter->drawRect(rectangle.adjusted(0, 0, -1, -1));

    /* Restore painter: */
    pPainter->restore();
}


/*********************************************************************************************************************************
*   Class UIChooserItemMimeData implementation.                                                                                  *
*********************************************************************************************************************************/

UIChooserItemMimeData::UIChooserItemMimeData(UIChooserItem *pItem)
    : m_pItem(pItem)
{
}

bool UIChooserItemMimeData::hasFormat(const QString &strMimeType) const
{
    if (strMimeType == QString(m_pItem->metaObject()->className()))
        return true;
    return false;
}
