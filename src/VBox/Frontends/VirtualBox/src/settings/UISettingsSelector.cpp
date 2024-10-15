/* $Id: UISettingsSelector.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISettingsSelector class implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <QAbstractItemModel>
#include <QAccessibleWidget>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QItemDelegate>
#include <QGuiApplication>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QToolButton>

/* GUI includes: */
#include "QITabWidget.h"
#include "QITreeView.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIIconPool.h"
#include "UIImageTools.h"
#include "UISettingsPage.h"
#include "UISettingsSelector.h"
#include "QIToolBar.h"


/** QAccessibleWidget extension used as an accessibility interface for UISettingsSelectorToolBar buttons. */
class UIAccessibilityInterfaceForUISettingsSelectorToolBarButton : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating toolbar button accessibility interface: */
        if (   pObject
            && strClassname == QLatin1String("QToolButton")
            && pObject->property("Belongs to") == "UISettingsSelectorToolBar")
            return new UIAccessibilityInterfaceForUISettingsSelectorToolBarButton(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    UIAccessibilityInterfaceForUISettingsSelectorToolBarButton(QWidget *pWidget)
        : QAccessibleWidget(pWidget, QAccessible::Button)
    {}

    /** Returns the role. */
    virtual QAccessible::Role role() const RT_OVERRIDE
    {
        /* Make sure button still alive: */
        AssertPtrReturn(button(), QAccessible::NoRole);

        /* Return role for checkable button: */
        if (button()->isCheckable())
            return QAccessible::RadioButton;

        /* Return default role: */
        return QAccessible::Button;
    }

    /** Returns the state. */
    virtual QAccessible::State state() const RT_OVERRIDE
    {
        /* Prepare the button state: */
        QAccessible::State state;

        /* Make sure button still alive: */
        AssertPtrReturn(button(), state);

        /* Compose the button state: */
        state.checkable = button()->isCheckable();
        state.checked = button()->isChecked();

        /* Return the button state: */
        return state;
    }

private:

    /** Returns corresponding toolbar button. */
    QToolButton *button() const { return qobject_cast<QToolButton*>(widget()); }
};


/** QITreeViewItem subclass used as selector tree-view item. */
class UISelectorTreeViewItem : public QITreeViewItem
{
    Q_OBJECT;

public:

    /** Constructs selector item passing @a pParent to the base-class.
      * This is constructor for root-item only. */
    UISelectorTreeViewItem(QITreeView *pParent);
    /** Constructs selector item passing @a pParentItem to the base-class.
      * This is constructor for rest of items we need.
      * @param  iID      Brings the item ID.
      * @param  icon     Brings the item icon.
      * @param  strLink  Brings the item link. */
    UISelectorTreeViewItem(UISelectorTreeViewItem *pParentItem,
                           int iID,
                           const QIcon &icon,
                           const QString &strLink);

    /** Returns item ID. */
    int id() const { return m_iID; }
    /** Returns item icon. */
    QIcon icon() const { return m_icon; }
    /** Returns item link. */
    QString link() const { return m_strLink; }

    /** Returns item text. */
    virtual QString text() const RT_OVERRIDE { return m_strText; }
    /** Defines item @a strText. */
    virtual void setText(const QString &strText) { m_strText = strText; }

    /** Returns whether item is hidden. */
    virtual bool isHidden() const { return m_fHidden; }
    /** Defines item is @a fHidden. */
    virtual void setHidden(bool fHidden) { m_fHidden = fHidden; }

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE { return m_children.size(); }
    /** Returns the child item with @a iIndex. */
    virtual UISelectorTreeViewItem *childItem(int iIndex) const RT_OVERRIDE { return m_children.at(iIndex); }

    /** Adds @a pChild to this item. */
    void addChild(UISelectorTreeViewItem *pChild) { m_children << pChild; }
    /** Assigns @a children for this item. */
    void setChildren(const QList<UISelectorTreeViewItem*> &children) { m_children = children; }
    /** Returns position for the passed @a pChild. */
    int posOfChild(UISelectorTreeViewItem *pChild) const { return m_children.indexOf(pChild); }

    /** Searches for the child with @a iID specified. */
    UISelectorTreeViewItem *childItemById(int iID) const;
    /** Searches for the child with @a strLink specified. */
    UISelectorTreeViewItem *childItemByLink(const QString &strLink) const;

private:

    /** Holds the item ID. */
    int      m_iID;
    /** Holds the item icon. */
    QIcon    m_icon;
    /** Holds the item link. */
    QString  m_strLink;
    /** Holds the item text. */
    QString  m_strText;
    /** Holds whether item is hidden. */
    bool     m_fHidden;

    /** Holds the list of children. */
    QList<UISelectorTreeViewItem*>  m_children;
};


/** QItemDelegate subclass used as selector tree-view item delegate. */
class UISelectorDelegate : public QItemDelegate
{
    Q_OBJECT;

public:

    /** Constructs selector item delegate passing @a pParent to the base-class. */
    UISelectorDelegate(QObject *pParent);

private:

    /** Paints @a index item with specified @a option using specified @a pPainter. */
    void paint(QPainter *pPainter, const QStyleOptionViewItem &option, const QModelIndex &index) const RT_OVERRIDE RT_FINAL;
};


/** QAbstractItemModel subclass used as selector model. */
class UISelectorModel : public QAbstractItemModel
{
    Q_OBJECT;

public:

    /** Data roles. */
    enum DataRole
    {
        R_Margin = Qt::UserRole + 1,
        R_Spacing,
        R_IconSize,

        R_ItemId,
        R_ItemPixmap,
        R_ItemPixmapRect,
        R_ItemName,
        R_ItemNamePoint,
        R_ItemHidden,
    };

    /** Constructs selector model passing @a pParent to the base-class. */
    UISelectorModel(QITreeView *pParent);
    /** Destructs selector model. */
    virtual ~UISelectorModel() RT_OVERRIDE;

    /** Returns row count for the passed @a parentIndex. */
    virtual int rowCount(const QModelIndex &parentIndex = QModelIndex()) const RT_OVERRIDE;
    /** Returns column count for the passed @a parentIndex. */
    virtual int columnCount(const QModelIndex &parentIndex = QModelIndex()) const RT_OVERRIDE;

    /** Returns parent item of @a specifiedIndex item. */
    virtual QModelIndex parent(const QModelIndex &specifiedIndex) const RT_OVERRIDE;
    /** Returns item specified by @a iRow, @a iColum and @a parentIndex. */
    virtual QModelIndex index(int iRow, int iColumn, const QModelIndex &parentIndex = QModelIndex()) const RT_OVERRIDE;
    /** Returns root item. */
    QModelIndex root() const;

    /** Returns model data for @a specifiedIndex and @a iRole. */
    virtual QVariant data(const QModelIndex &specifiedIndex, int iRole) const RT_OVERRIDE;
    /** Defines model data for @a specifiedIndex and @a iRole as @a value. */
    virtual bool setData(const QModelIndex &specifiedIndex, const QVariant &value, int iRole) RT_OVERRIDE;

    /** Adds item with certain @a strId. */
    QModelIndex addItem(int iID, const QIcon &icon, const QString &strLink);
    /** Deletes item with certain @a iID. */
    void delItem(int iID);

    /** Returns item with certain @a iID. */
    QModelIndex findItem(int iID);
    /** Returns item with certain @a strLink. */
    QModelIndex findItem(const QString &strLink);

private:

    /** Returns model flags for @a specifiedIndex. */
    virtual Qt::ItemFlags flags(const QModelIndex &specifiedIndex) const RT_OVERRIDE;

    /** Returns a pointer to item containing within @a specifiedIndex. */
    static UISelectorTreeViewItem *indexToItem(const QModelIndex &specifiedIndex);

    /** Holds the parent tree-view reference. */
    QITreeView *m_pParentTree;

    /** Holds the root item instance. */
    UISelectorTreeViewItem *m_pRootItem;
};


/** QITreeView extension used as selector tree-view. */
class UISelectorTreeView : public QITreeView
{
    Q_OBJECT;

public:

    /** Constructs selector tree-view passing @a pParent to the base-class. */
    UISelectorTreeView(QWidget *pParent);

    /** Calculates widget minimum size-hint. */
    virtual QSize minimumSizeHint() const RT_OVERRIDE;
    /** Calculates widget size-hint. */
    virtual QSize sizeHint() const RT_OVERRIDE;

private:

    /** Prepares all. */
    void prepare();
};


/** Tree-widget column sections. */
enum TreeWidgetSection
{
    TreeWidgetSection_Category = 0,
    TreeWidgetSection_Id,
    TreeWidgetSection_Link,
    TreeWidgetSection_Max
};


/** Simple container of all the selector item data. */
class UISelectorItem
{
public:

    /** Constructs selector item.
      * @param  icon       Brings the item icon.
      * @param  iID        Brings the item ID.
      * @param  strLink    Brings the item link.
      * @param  pPage      Brings the item page reference.
      * @param  iParentID  Brings the item parent ID. */
    UISelectorItem(const QIcon &icon, int iID, const QString &strLink, UISettingsPage *pPage, int iParentID)
        : m_icon(icon)
        , m_iID(iID)
        , m_strLink(strLink)
        , m_pPage(pPage)
        , m_iParentID(iParentID)
    {}

    /** Destructs selector item. */
    virtual ~UISelectorItem() {}

    /** Returns the item icon. */
    QIcon icon() const { return m_icon; }
    /** Returns the item text. */
    QString text() const { return m_strText; }
    /** Defines the item @a strText. */
    void setText(const QString &strText) { m_strText = strText; }
    /** Returns the item ID. */
    int id() const { return m_iID; }
    /** Returns the item link. */
    QString link() const { return m_strLink; }
    /** Returns the item page reference. */
    UISettingsPage *page() const { return m_pPage; }
    /** Returns the item parent ID. */
    int parentID() const { return m_iParentID; }

protected:

    /** Holds the item icon. */
    QIcon m_icon;
    /** Holds the item text. */
    QString m_strText;
    /** Holds the item ID. */
    int m_iID;
    /** Holds the item link. */
    QString m_strLink;
    /** Holds the item page reference. */
    UISettingsPage *m_pPage;
    /** Holds the item parent ID. */
    int m_iParentID;
};


/** UISelectorItem subclass used as tab-widget selector item. */
class UISelectorActionItem : public UISelectorItem
{
public:

    /** Constructs selector item.
      * @param  icon       Brings the item icon.
      * @param  iID        Brings the item ID.
      * @param  strLink    Brings the item link.
      * @param  pPage      Brings the item page reference.
      * @param  iParentID  Brings the item parent ID.
      * @param  pParent    Brings the item parent. */
    UISelectorActionItem(const QIcon &icon, int iID, const QString &strLink, UISettingsPage *pPage, int iParentID, QObject *pParent)
        : UISelectorItem(icon, iID, strLink, pPage, iParentID)
        , m_pAction(new QAction(icon, QString(), pParent))
        , m_pTabWidget(0)
    {
        m_pAction->setCheckable(true);
    }

    /** Returns the action instance. */
    QAction *action() const { return m_pAction; }

    /** Defines the @a pTabWidget instance. */
    void setTabWidget(QTabWidget *pTabWidget) { m_pTabWidget = pTabWidget; }
    /** Returns the tab-widget instance. */
    QTabWidget *tabWidget() const { return m_pTabWidget; }

protected:

    /** Holds the action instance. */
    QAction *m_pAction;
    /** Holds the tab-widget instance. */
    QTabWidget *m_pTabWidget;
};


/*********************************************************************************************************************************
*   Class UISelectorTreeViewItem implementation.                                                                                 *
*********************************************************************************************************************************/

UISelectorTreeViewItem::UISelectorTreeViewItem(QITreeView *pParent)
    : QITreeViewItem(pParent)
    , m_fHidden(false)
{
}

UISelectorTreeViewItem::UISelectorTreeViewItem(UISelectorTreeViewItem *pParentItem,
                                               int iID,
                                               const QIcon &icon,
                                               const QString &strLink)
    : QITreeViewItem(pParentItem)
    , m_iID(iID)
    , m_icon(icon)
    , m_strLink(strLink)
    , m_fHidden(false)
{
    if (pParentItem)
        pParentItem->addChild(this);
}

UISelectorTreeViewItem *UISelectorTreeViewItem::childItemById(int iID) const
{
    for (int i = 0; i < childCount(); ++i)
        if (UISelectorTreeViewItem *pItem = childItem(i))
            if (pItem->id() == iID)
                return pItem;
    return 0;
}

UISelectorTreeViewItem *UISelectorTreeViewItem::childItemByLink(const QString &strLink) const
{
    for (int i = 0; i < childCount(); ++i)
        if (UISelectorTreeViewItem *pItem = childItem(i))
            if (pItem->link() == strLink)
                return pItem;
    return 0;
}


/*********************************************************************************************************************************
*   Class UISelectorDelegate implementation.                                                                                     *
*********************************************************************************************************************************/

UISelectorDelegate::UISelectorDelegate(QObject *pParent)
    : QItemDelegate(pParent)
{
}

void UISelectorDelegate::paint(QPainter *pPainter, const QStyleOptionViewItem &option, const QModelIndex &proxyIndex) const
{
    /* Sanity checks: */
    AssertPtrReturnVoid(pPainter);
    AssertReturnVoid(proxyIndex.isValid());

    /* Acquire model: */
    const QSortFilterProxyModel *pProxyModel = qobject_cast<const QSortFilterProxyModel*>(proxyIndex.model());
    AssertPtrReturnVoid(pProxyModel);
    const UISelectorModel *pModel = qobject_cast<const UISelectorModel*>(pProxyModel->sourceModel());
    AssertPtrReturnVoid(pModel);

    /* Acquire model index: */
    const QModelIndex index = pProxyModel->mapToSource(proxyIndex);
    AssertReturnVoid(index.isValid());

    /* Acquire item properties: */
    const QPalette palette = QGuiApplication::palette();
    const QRect itemRectangle = option.rect;
    const QStyle::State enmState = option.state;
    const bool fChosen = enmState & QStyle::State_Selected;

    /* Draw background: */
    QColor backColor;
    if (fChosen)
    {
        /* Prepare painter path: */
        QPainterPath painterPath;
        painterPath.lineTo(itemRectangle.width() - itemRectangle.height(), 0);
        painterPath.lineTo(itemRectangle.width(),                          itemRectangle.height());
        painterPath.lineTo(0,                                              itemRectangle.height());
        painterPath.closeSubpath();
        painterPath.translate(itemRectangle.topLeft());

        /* Prepare painting gradient: */
        backColor = palette.color(QPalette::Active, QPalette::Highlight);
        const QColor bcTone1 = backColor.lighter(100);
        const QColor bcTone2 = backColor.lighter(120);
        QLinearGradient grad(itemRectangle.topLeft(), itemRectangle.bottomRight());
        grad.setColorAt(0, bcTone1);
        grad.setColorAt(1, bcTone2);

        /* Paint fancy shape: */
        pPainter->save();
        pPainter->setClipPath(painterPath);
        pPainter->setRenderHint(QPainter::Antialiasing);
        pPainter->fillPath(painterPath, grad);
        pPainter->strokePath(painterPath, uiCommon().isInDarkMode() ? backColor.lighter(120) : backColor.darker(110));
        pPainter->restore();
    }
    else
    {
        /* Just init painting color: */
        backColor = palette.color(QPalette::Active, QPalette::Window);
    }

    /* Draw icon: */
    const QRect itemPixmapRect = pModel->data(index, UISelectorModel::R_ItemPixmapRect).toRect();
    const QPixmap itemPixmap = pModel->data(index, UISelectorModel::R_ItemPixmap).value<QPixmap>();
    pPainter->save();
    pPainter->translate(itemRectangle.topLeft());
    pPainter->drawPixmap(itemPixmapRect, itemPixmap);
    pPainter->restore();

    /* Draw name: */
    const QColor foreground = suitableForegroundColor(palette, backColor);
    const QFont font = pModel->data(index, Qt::FontRole).value<QFont>();
    const QPoint namePoint = pModel->data(index, UISelectorModel::R_ItemNamePoint).toPoint();
    const QString strName = pModel->data(index, UISelectorModel::R_ItemName).toString();
    pPainter->save();
    pPainter->translate(itemRectangle.topLeft());
    pPainter->setPen(foreground);
    pPainter->setFont(font);
    pPainter->drawText(namePoint, strName);
    pPainter->restore();
}


/*********************************************************************************************************************************
*   Class UISelectorModel implementation.                                                                                        *
*********************************************************************************************************************************/

UISelectorModel::UISelectorModel(QITreeView *pParentTree)
    : QAbstractItemModel(pParentTree)
    , m_pParentTree(pParentTree)
    , m_pRootItem(new UISelectorTreeViewItem(pParentTree))
{
    AssertPtr(m_pParentTree);
}

UISelectorModel::~UISelectorModel()
{
    delete m_pRootItem;
}

int UISelectorModel::rowCount(const QModelIndex &parentIndex) const
{
    UISelectorTreeViewItem *pParentItem = indexToItem(parentIndex);
    return pParentItem ? pParentItem->childCount() : 1 /* root item */;
}

int UISelectorModel::columnCount(const QModelIndex & /* parentIndex */) const
{
    return 1;
}

QModelIndex UISelectorModel::parent(const QModelIndex &specifiedIndex) const
{
    if (!specifiedIndex.isValid())
        return QModelIndex();

    UISelectorTreeViewItem *pItem = indexToItem(specifiedIndex);
    UISelectorTreeViewItem *pParentOfItem =
        pItem ? qobject_cast<UISelectorTreeViewItem*>(pItem->parentItem()) : 0;
    UISelectorTreeViewItem *pParentOfParent =
        pParentOfItem ? qobject_cast<UISelectorTreeViewItem*>(pParentOfItem->parentItem()) : 0;
    const int iParentPosition = pParentOfParent ? pParentOfParent->posOfChild(pParentOfItem) : 0;

    return pParentOfItem ? createIndex(iParentPosition, 0, pParentOfItem) : QModelIndex();
}

QModelIndex UISelectorModel::index(int iRow, int iColumn, const QModelIndex &parentIndex) const
{
    if (!hasIndex(iRow, iColumn, parentIndex))
        return QModelIndex();

    UISelectorTreeViewItem *pItem = !parentIndex.isValid()
                                  ? m_pRootItem
                                  : indexToItem(parentIndex)->childItem(iRow);

    return pItem ? createIndex(iRow, iColumn, pItem) : QModelIndex();
}

QModelIndex UISelectorModel::root() const
{
    return index(0, 0);
}

QVariant UISelectorModel::data(const QModelIndex &specifiedIndex, int iRole) const
{
    if (!specifiedIndex.isValid())
        return QVariant();

    switch (iRole)
    {
        /* Basic Attributes: */
        case Qt::FontRole:
        {
            QFont font = m_pParentTree->font();
            font.setBold(true);
            return font;
        }
        case Qt::SizeHintRole:
        {
            const QFontMetrics fm(data(specifiedIndex, Qt::FontRole).value<QFont>());
            const int iMargin = data(specifiedIndex, R_Margin).toInt();
            const int iSpacing = data(specifiedIndex, R_Spacing).toInt();
            const int iIconSize = data(specifiedIndex, R_IconSize).toInt();
            const QString strName = data(specifiedIndex, R_ItemName).toString();
            const int iMinimumContentWidth = iIconSize /* icon width */
                                           + iSpacing
                                           + fm.horizontalAdvance(strName) /* name width */;
            const int iMinimumContentHeight = qMax(fm.height() /* font height */,
                                                   iIconSize /* icon height */);
            const int iMinimumWidth = iMinimumContentWidth + iMargin * 2;
            const int iMinimumHeight = iMinimumContentHeight + iMargin * 2;
            return QSize(iMinimumWidth, iMinimumHeight);
        }

        /* Advanced Attributes: */
        case R_Margin:
        {
            return QApplication::style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 1.5;
        }
        case R_Spacing:
        {
            return qMax(QApplication::style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing), 5) * 2;
        }
        case R_IconSize:
        {
            return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) * 1.5;
        }
        case R_ItemId:
        {
            if (UISelectorTreeViewItem *pItem = indexToItem(specifiedIndex))
                return pItem->id();
            return 0;
        }
        case R_ItemPixmap:
        {
            if (UISelectorTreeViewItem *pItem = indexToItem(specifiedIndex))
            {
                const QIcon icon = pItem->icon();
                const int iIconSize = data(specifiedIndex, R_IconSize).toInt();
                const qreal fDpr = m_pParentTree->window() && m_pParentTree->window()->windowHandle()
                                 ? m_pParentTree->window()->windowHandle()->devicePixelRatio() : 1;
                return icon.pixmap(QSize(iIconSize, iIconSize), fDpr);
            }
            return QPixmap();
        }
        case R_ItemPixmapRect:
        {
            const int iMargin = data(specifiedIndex, R_Margin).toInt();
            const int iWidth = data(specifiedIndex, R_IconSize).toInt();
            return QRect(iMargin, iMargin, iWidth, iWidth);
        }
        case R_ItemName:
        {
            if (UISelectorTreeViewItem *pItem = indexToItem(specifiedIndex))
                return pItem->text();
            return QString();
        }
        case R_ItemNamePoint:
        {
            const int iMargin = data(specifiedIndex, R_Margin).toInt();
            const int iSpacing = data(specifiedIndex, R_Spacing).toInt();
            const int iIconSize = data(specifiedIndex, R_IconSize).toInt();
            const QFontMetrics fm(data(specifiedIndex, Qt::FontRole).value<QFont>());
            const QSize sizeHint = data(specifiedIndex, Qt::SizeHintRole).toSize();
            return QPoint(iMargin + iIconSize + iSpacing,
                          sizeHint.height() / 2 + fm.ascent() / 2 - 1 /* base line */);
        }
        case R_ItemHidden:
        {
            if (UISelectorTreeViewItem *pItem = indexToItem(specifiedIndex))
                return pItem->isHidden() ? "hide" : "show";
            return QString();
        }

        default:
            break;
    }

    return QVariant();
}

bool UISelectorModel::setData(const QModelIndex &specifiedIndex, const QVariant &aValue, int iRole)
{
    if (!specifiedIndex.isValid())
        return QAbstractItemModel::setData(specifiedIndex, aValue, iRole);

    switch (iRole)
    {
        /* Advanced Attributes: */
        case R_ItemName:
        {
            if (UISelectorTreeViewItem *pItem = indexToItem(specifiedIndex))
            {
                pItem->setText(aValue.toString());
                emit dataChanged(specifiedIndex, specifiedIndex);
                return true;
            }
            return false;
        }
        case R_ItemHidden:
        {
            if (UISelectorTreeViewItem *pItem = indexToItem(specifiedIndex))
            {
                pItem->setHidden(aValue.toBool());
                emit dataChanged(specifiedIndex, specifiedIndex);
                return true;
            }
            return false;
        }

        default:
            break;
    }

    return false;
}

QModelIndex UISelectorModel::addItem(int iID, const QIcon &icon, const QString &strLink)
{
    beginInsertRows(root(), m_pRootItem->childCount(), m_pRootItem->childCount());
    new UISelectorTreeViewItem(m_pRootItem, iID, icon, strLink);
    endInsertRows();
    return index(m_pRootItem->childCount() - 1, 0, root());
}

void UISelectorModel::delItem(int iID)
{
    if (UISelectorTreeViewItem *pItem = m_pRootItem->childItemById(iID))
    {
        const int iItemPosition = m_pRootItem->posOfChild(pItem);
        beginRemoveRows(root(), iItemPosition, iItemPosition);
        delete pItem;
        endRemoveRows();
    }
}

QModelIndex UISelectorModel::findItem(int iID)
{
    UISelectorTreeViewItem *pItem = m_pRootItem->childItemById(iID);
    if (!pItem)
        return QModelIndex();

    const int iItemPosition = m_pRootItem->posOfChild(pItem);
    return pItem ? createIndex(iItemPosition, 0, pItem) : QModelIndex();
}

QModelIndex UISelectorModel::findItem(const QString &strLink)
{
    UISelectorTreeViewItem *pItem = m_pRootItem->childItemByLink(strLink);
    if (!pItem)
        return QModelIndex();

    const int iItemPosition = m_pRootItem->posOfChild(pItem);
    return pItem ? createIndex(iItemPosition, 0, pItem) : QModelIndex();
}

Qt::ItemFlags UISelectorModel::flags(const QModelIndex &specifiedIndex) const
{
    return !specifiedIndex.isValid() ? QAbstractItemModel::flags(specifiedIndex)
                                     : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

/* static */
UISelectorTreeViewItem *UISelectorModel::indexToItem(const QModelIndex &specifiedIndex)
{
    if (!specifiedIndex.isValid())
        return 0;
    return static_cast<UISelectorTreeViewItem*>(specifiedIndex.internalPointer());
}


/*********************************************************************************************************************************
*   Class UISelectorTreeView implementation.                                                                                     *
*********************************************************************************************************************************/

UISelectorTreeView::UISelectorTreeView(QWidget *pParent)
    : QITreeView(pParent)
{
    prepare();
}

QSize UISelectorTreeView::minimumSizeHint() const
{
    /* Sanity check: */
    QSortFilterProxyModel *pProxyModel = qobject_cast<QSortFilterProxyModel*>(model());
    AssertPtrReturn(pProxyModel, QITreeView::minimumSizeHint());
    UISelectorModel *pModel = qobject_cast<UISelectorModel*>(pProxyModel->sourceModel());
    AssertPtrReturn(pModel, QITreeView::minimumSizeHint());

    /* Calculate largest column width: */
    int iMaximumColumnWidth = 0;
    int iCumulativeColumnHeight = 0;
    for (int i = 0; i < pModel->rowCount(pModel->root()); ++i)
    {
        const QModelIndex iteratedIndex = pModel->index(i, 0, pModel->root());
        const QSize sizeHint = pModel->data(iteratedIndex, Qt::SizeHintRole).toSize();
        const int iHeightHint = sizeHint.height();
        iMaximumColumnWidth = qMax(iMaximumColumnWidth, sizeHint.width() + iHeightHint /* to get the fancy shape */);
        iCumulativeColumnHeight += iHeightHint;
    }

    /* Return selector size-hint: */
    return QSize(iMaximumColumnWidth, iCumulativeColumnHeight);
}

QSize UISelectorTreeView::sizeHint() const
{
    // WORKAROUND:
    // By default QTreeView uses own size-hint
    // which we don't like and want to ignore:
    return minimumSizeHint();
}

void UISelectorTreeView::prepare()
{
    /* Configure tree-view: */
    setSortingEnabled(true);
    sortByColumn(0, Qt::AscendingOrder);
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(false);
    setContextMenuPolicy(Qt::PreventContextMenu);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::MinimumExpanding);

    /* Prepare selector delegate: */
    UISelectorDelegate *pDelegate = new UISelectorDelegate(this);
    if (pDelegate)
        setItemDelegate(pDelegate);
}


/*********************************************************************************************************************************
*   Class UISettingsSelector implementation.                                                                                     *
*********************************************************************************************************************************/

UISettingsSelector::UISettingsSelector(QWidget *pParent /* = 0 */)
    : QObject(pParent)
{
}

UISettingsSelector::~UISettingsSelector()
{
    qDeleteAll(m_list);
    m_list.clear();
}

void UISettingsSelector::setItemText(int iID, const QString &strText)
{
    if (UISelectorItem *pTtem = findItem(iID))
        pTtem->setText(strText);
}

QString UISettingsSelector::itemTextByPage(UISettingsPage *pPage) const
{
    QString strText;
    if (UISelectorItem *pItem = findItemByPage(pPage))
        strText = pItem->text();
    return strText;
}

QWidget *UISettingsSelector::idToPage(int iID) const
{
    UISettingsPage *pPage = 0;
    if (UISelectorItem *pItem = findItem(iID))
        pPage = pItem->page();
    return pPage;
}

QList<UISettingsPage*> UISettingsSelector::settingPages() const
{
    QList<UISettingsPage*> list;
    foreach (UISelectorItem *pItem, m_list)
        if (pItem->page())
            list << pItem->page();
    return list;
}

UISelectorItem *UISettingsSelector::findItem(int iID) const
{
    UISelectorItem *pResult = 0;
    foreach (UISelectorItem *pItem, m_list)
        if (pItem->id() == iID)
        {
            pResult = pItem;
            break;
        }
    return pResult;
}

UISelectorItem *UISettingsSelector::findItemByLink(const QString &strLink) const
{
    UISelectorItem *pResult = 0;
    foreach (UISelectorItem *pItem, m_list)
        if (pItem->link() == strLink)
        {
            pResult = pItem;
            break;
        }
    return pResult;
}

UISelectorItem *UISettingsSelector::findItemByPage(UISettingsPage *pPage) const
{
    UISelectorItem *pResult = 0;
    foreach (UISelectorItem *pItem, m_list)
        if (pItem->page() == pPage)
        {
            pResult = pItem;
            break;
        }
    return pResult;
}


/*********************************************************************************************************************************
*   Class UISettingsSelectorTreeView implementation.                                                                             *
*********************************************************************************************************************************/

UISettingsSelectorTreeView::UISettingsSelectorTreeView(QWidget *pParent /* = 0 */)
    : UISettingsSelector(pParent)
    , m_pTreeView(0)
    , m_pModel(0)
    , m_pModelProxy(0)
{
    prepare();
}

UISettingsSelectorTreeView::~UISettingsSelectorTreeView()
{
    cleanup();
}

QWidget *UISettingsSelectorTreeView::widget() const
{
    return m_pTreeView;
}

QWidget *UISettingsSelectorTreeView::addItem(const QString & /* strBigIcon */,
                                             const QString &strMediumIcon ,
                                             const QString & /* strSmallIcon */,
                                             int iID,
                                             const QString &strLink,
                                             UISettingsPage *pPage /* = 0 */,
                                             int iParentID /* = -1 */)
{
    QWidget *pResult = 0;
    if (pPage)
    {
        /* Adjust page a bit: */
        pPage->setContentsMargins(0, 0, 0, 0);
        if (pPage->layout())
            pPage->layout()->setContentsMargins(0, 0, 0, 0);
        pResult = pPage;

        /* Add selector-item object: */
        const QIcon icon = UIIconPool::iconSet(strMediumIcon);
        UISelectorItem *pItem = new UISelectorItem(icon, iID, strLink, pPage, iParentID);
        if (pItem)
            m_list.append(pItem);

        /* Add model-item: */
        m_pModel->addItem(iID, pItem->icon(), strLink);
    }
    return pResult;
}

void UISettingsSelectorTreeView::setItemVisible(int iID, bool fVisible)
{
    /* Look for the tree-view item to assign the text: */
    QModelIndex specifiedIndex = m_pModel->findItem(iID);
    if (specifiedIndex.isValid())
        m_pModel->setData(specifiedIndex, !fVisible, UISelectorModel::R_ItemHidden);
}

void UISettingsSelectorTreeView::setItemText(int iID, const QString &strText)
{
    /* Call to base-class: */
    UISettingsSelector::setItemText(iID, strText);

    /* Look for the tree-view item to assign the text: */
    QModelIndex specifiedIndex = m_pModel->findItem(iID);
    if (specifiedIndex.isValid())
        m_pModel->setData(specifiedIndex, strText, UISelectorModel::R_ItemName);
}

QString UISettingsSelectorTreeView::itemText(int iID) const
{
    QString strResult;
    if (UISelectorItem *pItem = findItem(iID))
        strResult = pItem->text();
    return strResult;
}

int UISettingsSelectorTreeView::currentId() const
{
    int iID = -1;
    const QModelIndex proxyIndex = m_pTreeView->currentIndex();
    const QModelIndex index = m_pModelProxy->mapToSource(proxyIndex);
    if (index.isValid())
        iID = m_pModel->data(index, UISelectorModel::R_ItemId).toString().toInt();
    return iID;
}

int UISettingsSelectorTreeView::linkToId(const QString &strLink) const
{
    int iID = -1;
    const QModelIndex index = m_pModel->findItem(strLink);
    if (index.isValid())
        iID = m_pModel->data(index, UISelectorModel::R_ItemId).toString().toInt();
    return iID;
}

void UISettingsSelectorTreeView::selectById(int iID, bool fSilently)
{
    const QModelIndex index = m_pModel->findItem(iID);
    const QModelIndex proxyIndex = m_pModelProxy->mapFromSource(index);
    if (proxyIndex.isValid())
    {
        if (fSilently)
            m_pTreeView->blockSignals(true);
        m_pTreeView->setCurrentIndex(proxyIndex);
        if (fSilently)
            m_pTreeView->blockSignals(false);
    }
}

void UISettingsSelectorTreeView::sltHandleCurrentChanged(const QModelIndex &proxyIndex,
                                                         const QModelIndex & /* previousIndex */)
{
    const QModelIndex index = m_pModelProxy->mapToSource(proxyIndex);
    if (index.isValid())
    {
        const int iID = m_pModel->data(index, UISelectorModel::R_ItemId).toString().toInt();
        Assert(iID >= 0);
        emit sigCategoryChanged(iID);
    }
}

void UISettingsSelectorTreeView::prepare()
{
    /* Prepare the tree-widget: */
    m_pTreeView = new UISelectorTreeView(qobject_cast<QWidget*>(parent()));
    if (m_pTreeView)
    {
        /* Prepare the model: */
        m_pModel = new UISelectorModel(m_pTreeView);
        if (m_pModel)
        {
            /* Create proxy-model: */
            m_pModelProxy = new QSortFilterProxyModel(m_pTreeView);
            if (m_pModelProxy)
            {
                /* Configure proxy-model: */
                m_pModelProxy->setSortRole(UISelectorModel::R_ItemId);
                m_pModelProxy->setFilterRole(UISelectorModel::R_ItemHidden);
                m_pModelProxy->setFilterFixedString("show");
                m_pModelProxy->setSourceModel(m_pModel);
                m_pTreeView->setModel(m_pModelProxy);
            }
            const QModelIndex proxyIndex = m_pModelProxy->mapFromSource(m_pModel->root());
            m_pTreeView->setRootIndex(proxyIndex);
        }

        /* Setup connections: */
        connect(m_pTreeView, &QITreeView::currentItemChanged,
                this, &UISettingsSelectorTreeView::sltHandleCurrentChanged);
    }
}

void UISettingsSelectorTreeView::cleanup()
{
    /* Cleanup the model: */
    delete m_pModel;
    m_pModel = 0;
    /* Cleanup the tree-view: */
    delete m_pTreeView;
    m_pTreeView = 0;
}


#include "UISettingsSelector.moc"
