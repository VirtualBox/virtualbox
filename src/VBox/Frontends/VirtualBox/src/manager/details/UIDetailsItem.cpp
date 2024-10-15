/* $Id: UIDetailsItem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsItem class definition.
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
#include <QGraphicsScene>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

/* GUI includes: */
#include "UIGraphicsTextPane.h"
#include "UIDetails.h"
#include "UIDetailsElement.h"
#include "UIDetailsGroup.h"
#include "UIDetailsModel.h"
#include "UIDetailsSet.h"
#include "UIDetailsView.h"


/** QAccessibleObject extension used as an accessibility interface for Details-view items. */
class UIAccessibilityInterfaceForUIDetailsItem : public QAccessibleObject
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating Details-view accessibility interface: */
        if (pObject && strClassname == QLatin1String("UIDetailsItem"))
            return new UIAccessibilityInterfaceForUIDetailsItem(pObject);

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pObject to the base-class. */
    UIAccessibilityInterfaceForUIDetailsItem(QObject *pObject)
        : QAccessibleObject(pObject)
    {}

    /** Returns the parent. */
    virtual QAccessibleInterface *parent() const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(item(), 0);

        /* Return the parent: */
        switch (item()->type())
        {
            /* For a set: */
            case UIDetailsItemType_Set:
            {
                /* Always return parent view: */
                return QAccessible::queryAccessibleInterface(item()->model()->details()->view());
            }
            /* For an element: */
            case UIDetailsItemType_Element:
            {
                /* What amount of children root has? */
                const int cChildCount = item()->model()->root()->items().size();

                /* Return our parent (if root has many of children): */
                if (cChildCount > 1)
                    return QAccessible::queryAccessibleInterface(item()->parentItem());

                /* Return parent view (otherwise): */
                return QAccessible::queryAccessibleInterface(item()->model()->details()->view());
            }
            default:
                break;
        }

        /* Null by default: */
        return 0;
    }

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(item(), 0);

        /* Return the number of children: */
        switch (item()->type())
        {
            case UIDetailsItemType_Set:     return item()->items().size();
            case UIDetailsItemType_Element: return item()->toElement()->text().size();
            default: break;
        }

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

        /* Return the child with the iIndex: */
        switch (item()->type())
        {
            case UIDetailsItemType_Set:     return QAccessible::queryAccessibleInterface(item()->items().at(iIndex));
            case UIDetailsItemType_Element: return QAccessible::queryAccessibleInterface(&item()->toElement()->text()[iIndex]);
            default: break;
        }

        /* Null be default: */
        return 0;
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
        const QPoint  itemPosInView    = item()->model()->details()->view()->mapFromScene(itemPosInScene);
        const QPoint  itemPosInScreen  = item()->model()->details()->view()->mapToGlobal(itemPosInView);
        const QRect   itemRectInScreen = QRect(itemPosInScreen, itemSize);
        return itemRectInScreen;
    }

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(item(), QString());

        /* Return the description: */
        if (enmTextRole == QAccessible::Description)
            return item()->description();

        /* Null-string by default: */
        return QString();
    }

    /** Returns the role. */
    virtual QAccessible::Role role() const RT_OVERRIDE
    {
        /* Return the role: */
        return QAccessible::List;
    }

    /** Returns the state. */
    virtual QAccessible::State state() const RT_OVERRIDE
    {
        /* Return the state: */
        return QAccessible::State();
    }

private:

    /** Returns corresponding Details-view item. */
    UIDetailsItem *item() const { return qobject_cast<UIDetailsItem*>(object()); }
};


/*********************************************************************************************************************************
*   Class UIDetailsItem implementation.                                                                                          *
*********************************************************************************************************************************/

UIDetailsItem::UIDetailsItem(UIDetailsItem *pParent)
    : QIGraphicsWidget(pParent)
    , m_pParent(pParent)
{
    /* Install Details-view item accessibility interface factory: */
    QAccessible::installFactory(UIAccessibilityInterfaceForUIDetailsItem::pFactory);

    /* Basic item setup: */
    setOwnedByLayout(false);
    setFocusPolicy(Qt::NoFocus);
    setFlag(QGraphicsItem::ItemIsSelectable, false);

    /* Non-root item? */
    if (parentItem())
    {
        /* Non-root item setup: */
        setAcceptHoverEvents(true);
    }

    /* Setup connections: */
    connect(this, &UIDetailsItem::sigBuildStep,
            this, &UIDetailsItem::sltBuildStep,
            Qt::QueuedConnection);
}

UIDetailsGroup *UIDetailsItem::toGroup()
{
    UIDetailsGroup *pItem = qgraphicsitem_cast<UIDetailsGroup*>(this);
    AssertMsg(pItem, ("Trying to cast invalid item type to UIDetailsGroup!"));
    return pItem;
}

UIDetailsSet *UIDetailsItem::toSet()
{
    UIDetailsSet *pItem = qgraphicsitem_cast<UIDetailsSet*>(this);
    AssertMsg(pItem, ("Trying to cast invalid item type to UIDetailsSet!"));
    return pItem;
}

UIDetailsElement *UIDetailsItem::toElement()
{
    UIDetailsElement *pItem = qgraphicsitem_cast<UIDetailsElement*>(this);
    AssertMsg(pItem, ("Trying to cast invalid item type to UIDetailsElement!"));
    return pItem;
}

UIDetailsModel *UIDetailsItem::model() const
{
    UIDetailsModel *pModel = qobject_cast<UIDetailsModel*>(QIGraphicsWidget::scene()->parent());
    AssertMsg(pModel, ("Incorrect graphics scene parent set!"));
    return pModel;
}

void UIDetailsItem::updateGeometry()
{
    /* Call to base-class: */
    QIGraphicsWidget::updateGeometry();

    /* Do the same for the parent: */
    if (parentItem())
        parentItem()->updateGeometry();
}

QSizeF UIDetailsItem::sizeHint(Qt::SizeHint enmWhich, const QSizeF &constraint /* = QSizeF() */) const
{
    /* If Qt::MinimumSize or Qt::PreferredSize requested: */
    if (enmWhich == Qt::MinimumSize || enmWhich == Qt::PreferredSize)
        /* Return wrappers: */
        return QSizeF(minimumWidthHint(), minimumHeightHint());
    /* Call to base-class: */
    return QIGraphicsWidget::sizeHint(enmWhich, constraint);
}

void UIDetailsItem::sltBuildStep(const QUuid &, int)
{
    AssertMsgFailed(("This item doesn't support building!"));
}


/*********************************************************************************************************************************
*   Class UIPrepareStep implementation.                                                                                          *
*********************************************************************************************************************************/

UIPrepareStep::UIPrepareStep(QObject *pParent, QObject *pBuildObject, const QUuid &uStepId, int iStepNumber)
    : QObject(pParent)
    , m_uStepId(uStepId)
    , m_iStepNumber(iStepNumber)
{
    /* Prepare connections: */
    connect(qobject_cast<UIDetailsItem*>(pBuildObject), &UIDetailsItem::sigBuildDone,
            this, &UIPrepareStep::sltStepDone,
            Qt::QueuedConnection);

    UIDetailsItem *pDetailsItem = qobject_cast<UIDetailsItem*>(pParent);
    AssertPtrReturnVoid(pDetailsItem);
    {
        connect(this, &UIPrepareStep::sigStepDone,
                pDetailsItem, &UIDetailsItem::sltBuildStep,
                Qt::QueuedConnection);
    }
}

void UIPrepareStep::sltStepDone()
{
    emit sigStepDone(m_uStepId, m_iStepNumber);
}
