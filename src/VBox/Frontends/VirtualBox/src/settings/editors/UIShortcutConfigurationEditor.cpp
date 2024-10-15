/* $Id: UIShortcutConfigurationEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIShortcutConfigurationEditor class implementation.
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
#include <QHeaderView>
#include <QItemEditorFactory>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIStyledItemDelegate.h"
#include "QITableView.h"
#include "UIActionPool.h"
#include "UICommon.h"
#include "UIExtraDataManager.h"
#include "UIHostComboEditor.h"
#include "UIHotKeyEditor.h"
#include "UIMessageCenter.h"
#include "UIShortcutConfigurationEditor.h"
#include "UIShortcutPool.h"

/* Namespaces: */
using namespace UIExtraDataDefs;


/** Table column indexes. */
enum TableColumnIndex
{
    TableColumnIndex_Description,
    TableColumnIndex_Sequence,
    TableColumnIndex_Max
};


/** QITableViewCell subclass for shortcut configuration editor. */
class UIShortcutTableViewCell : public QITableViewCell
{
    Q_OBJECT;

public:

    /** Constructs table cell on the basis of passed arguments.
      * @param  pParent  Brings the row this cell belongs too.
      * @param  strText  Brings the text describing this cell. */
    UIShortcutTableViewCell(QITableViewRow *pParent, const QString &strText)
        : QITableViewCell(pParent)
        , m_strText(strText)
    {}

    /** Returns the cell text. */
    virtual QString text() const RT_OVERRIDE { return m_strText; }

private:

    /** Holds the cell text. */
    QString m_strText;
};


/** QITableViewRow subclass for shortcut configuration editor. */
class UIShortcutTableViewRow : public QITableViewRow, public UIShortcutConfigurationItem
{
    Q_OBJECT;

public:

    /** Constructs table row on the basis of passed arguments.
      * @param  pParent  Brings the table this row belongs too.
      * @param  item     Brings the item this row is based on. */
    UIShortcutTableViewRow(QITableView *pParent = 0, const UIShortcutConfigurationItem &item = UIShortcutConfigurationItem())
        : QITableViewRow(pParent)
        , UIShortcutConfigurationItem(item)
    {
        createCells();
    }

    /** Constructs table row on the basis of @a another one. */
    UIShortcutTableViewRow(const UIShortcutTableViewRow &another)
        : QITableViewRow(another.table())
        , UIShortcutConfigurationItem(another)
    {
        createCells();
    }

    /** Destructs table row. */
    virtual ~UIShortcutTableViewRow() RT_OVERRIDE
    {
        destroyCells();
    }

    /** Copies a table row from @a another one. */
    UIShortcutTableViewRow &operator=(const UIShortcutTableViewRow &another)
    {
        /* Reassign variables: */
        setTable(another.table());
        UIShortcutConfigurationItem::operator=(another);

        /* Recreate cells: */
        destroyCells();
        createCells();

        /* Return this: */
        return *this;
    }

    /** Returns whether this row equals to @a another one. */
    bool operator==(const UIShortcutTableViewRow &another) const
    {
        /* Compare variables: */
        return UIShortcutConfigurationItem::operator==(another);
    }

protected:

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE
    {
        return TableColumnIndex_Max;
    }

    /** Returns the child item with @a iIndex. */
    virtual QITableViewCell *childItem(int iIndex) const RT_OVERRIDE
    {
        switch (iIndex)
        {
            case TableColumnIndex_Description: return m_cells.first;
            case TableColumnIndex_Sequence: return m_cells.second;
            default: break;
        }
        return 0;
    }

private:

    /** Creates cells. */
    void createCells()
    {
        /* Create cells on the basis of description and current sequence: */
        m_cells = qMakePair(new UIShortcutTableViewCell(this, description()),
                            new UIShortcutTableViewCell(this, currentSequence()));
    }

    /** Destroys cells. */
    void destroyCells()
    {
        /* Destroy cells: */
        delete m_cells.first;
        delete m_cells.second;
        m_cells.first = 0;
        m_cells.second = 0;
    }

    /** Holds the cell instances. */
    QPair<UIShortcutTableViewCell*, UIShortcutTableViewCell*> m_cells;
};

/** Shortcut configuration editor row list. */
typedef QList<UIShortcutTableViewRow*> UIShortcutTableViewContent;


/** QAbstractTableModel subclass representing shortcut configuration model. */
class UIShortcutConfigurationModel : public QAbstractTableModel
{
    Q_OBJECT;

signals:

    /** Notifies about data changed. */
    void sigDataChanged();

public:

    /** Holds the data roles. */
    enum DataRoles
    {
        IsHostCombo = Qt::UserRole + 1,
    };

    /** Constructs model passing @a pParent to the base-class.
      * @param  enmType  Brings the action-pool type this model is related to. */
    UIShortcutConfigurationModel(UIShortcutConfigurationEditor *pParent, UIType enmType);
    /** Destructs model. */
    virtual ~UIShortcutConfigurationModel() RT_OVERRIDE RT_FINAL;

    /** Loads a @a list of shortcuts to the model. */
    void load(const UIShortcutConfigurationList &list);
    /** Saves the model shortcuts to a @a list. */
    void save(UIShortcutConfigurationList &list);

    /** Returns whether all shortcuts unique. */
    bool isAllShortcutsUnique();

    /** Returns the index of the item in the model specified by the given @a iRow, @a iColumn and @a parentIdx index. */
    virtual QModelIndex index(int iRow, int iColumn, const QModelIndex &parentIdx = QModelIndex()) const RT_OVERRIDE RT_FINAL;

    /** Returns the item flags for the given @a index. */
    virtual Qt::ItemFlags flags(const QModelIndex &index) const RT_OVERRIDE RT_FINAL;

    /** Returns the number of rows under the given @a parent. */
    virtual int rowCount(const QModelIndex &parent = QModelIndex()) const RT_OVERRIDE RT_FINAL;
    /** Returns the number of columns under the given @a parent. */
    virtual int columnCount(const QModelIndex &parent = QModelIndex()) const RT_OVERRIDE RT_FINAL;

    /** Returns the data for the given @a iRole and @a iSection in the header with the specified @a enmOrientation. */
    virtual QVariant headerData(int iSection, Qt::Orientation enmOrientation, int iRole) const RT_OVERRIDE RT_FINAL;

    /** Sets the @a iRole data for the item at @a index to @a value. */
    virtual bool setData(const QModelIndex &index, const QVariant &value, int iRole) RT_OVERRIDE RT_FINAL;
    /** Returns the data stored under the given @a iRole for the item referred to by the @a index. */
    virtual QVariant data(const QModelIndex &index, int iRole = Qt::DisplayRole) const RT_OVERRIDE RT_FINAL;

private:

    /** Return the parent table-view reference. */
    QITableView *view() const;

    /** Holds the parent shortcut-configuration editor instance. */
    UIShortcutConfigurationEditor *m_pShortcutConfigurationEditor;

    /** Holds the action-pool type this model is related to. */
    UIType  m_enmType;

    /** Holds current shortcut list. */
    UIShortcutTableViewContent  m_shortcuts;

    /** Holds a set of currently duplicated sequences. */
    QSet<QString>  m_duplicatedSequences;
};


/** QSortFilterProxyModel subclass representing shortcut configuration proxy-model. */
class UIShortcutConfigurationProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT;

public:

    /** Constructs the shortcut configuration proxy-model passing @a pParent to the base-class. */
    UIShortcutConfigurationProxyModel(QObject *pParent = 0);

public slots:

    /** Defines model @a strFilter. */
    void setFilter(const QString &strFilter);

protected:

    /** Returns whether item in the row indicated by the given @a iSourceRow and @a srcParentIdx should be included in the model. */
    virtual bool filterAcceptsRow(int iSourceRow, const QModelIndex &srcParentIdx) const RT_OVERRIDE;

    /** Returns whether value of the item @a srcIdxLeft is less than the value of the item @a srcIdxRight. */
    virtual bool lessThan(const QModelIndex &srcIdxLeft, const QModelIndex &srcIdxRight) const RT_OVERRIDE;

private:

    /** Holds the model filter. */
    QString  m_strFilter;
};


/** QITableView subclass representing shortcut configuration table. */
class UIShortcutConfigurationView : public QITableView
{
    Q_OBJECT;

public:

    /** Constructs table passing @a pParent to the base-class.
      * @param  strObjectName  Brings the object name this table has, required for fast referencing. */
    UIShortcutConfigurationView(QWidget *pParent, const QString &strObjectName);
    /** Destructs table. */
    virtual ~UIShortcutConfigurationView() RT_OVERRIDE;

    /** Sets the @a pModel for the view to present. */
    virtual void setModel(QAbstractItemModel *pModel) RT_OVERRIDE;

protected slots:

    /** Handles rows being inserted.
      * @param  parent  Brings the parent under which new rows being inserted.
      * @param  iStart  Brings the starting position (inclusive).
      * @param  iStart  Brings the end position (inclusive). */
    virtual void rowsInserted(const QModelIndex &parent, int iStart, int iEnd) RT_OVERRIDE;

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Holds whether the view is polished. */
    bool  m_fPolished;

    /** Holds the item editor factory instance. */
    QItemEditorFactory *m_pItemEditorFactory;
};


/*********************************************************************************************************************************
*   Class UIShortcutConfigurationModel implementation.                                                                           *
*********************************************************************************************************************************/

UIShortcutConfigurationModel::UIShortcutConfigurationModel(UIShortcutConfigurationEditor *pParent, UIType enmType)
    : QAbstractTableModel(pParent)
    , m_pShortcutConfigurationEditor(pParent)
    , m_enmType(enmType)
{
}

UIShortcutConfigurationModel::~UIShortcutConfigurationModel()
{
    /* Delete the cached data: */
    qDeleteAll(m_shortcuts);
    m_shortcuts.clear();
}

void UIShortcutConfigurationModel::load(const UIShortcutConfigurationList &list)
{
    /* Erase rows first if necessary: */
    if (!m_shortcuts.isEmpty())
    {
        beginRemoveRows(QModelIndex(), 0, m_shortcuts.size() - 1);
        m_shortcuts.clear();
        endRemoveRows();
    }

    /* Load a list of passed shortcuts: */
    foreach (const UIShortcutConfigurationItem &item, list)
    {
        /* Filter out unnecessary items: */
        if (   (m_enmType == UIType_ManagerUI && item.key().startsWith(GUI_Input_MachineShortcuts))
            || (m_enmType == UIType_RuntimeUI && item.key().startsWith(GUI_Input_SelectorShortcuts)))
            continue;
        /* Add suitable item to the model as a new shortcut: */
        m_shortcuts << new UIShortcutTableViewRow(view(), item);
    }

    /* Add rows finally if necessary: */
    if (!m_shortcuts.isEmpty())
    {
        beginInsertRows(QModelIndex(), 0, m_shortcuts.size() - 1);
        endInsertRows();
    }
}

void UIShortcutConfigurationModel::save(UIShortcutConfigurationList &list)
{
    /* Save cached model shortcuts: */
    foreach (UIShortcutTableViewRow *pRow, m_shortcuts)
    {
        /* Search for corresponding item position: */
        const int iShortcutItemPosition = UIShortcutSearchFunctor<UIShortcutConfigurationItem>()(list, pRow);
        /* Make sure position is valid: */
        if (iShortcutItemPosition == -1)
            continue;
        /* Save cached model shortcut to a list: */
        list[iShortcutItemPosition] = *pRow;
    }
}

bool UIShortcutConfigurationModel::isAllShortcutsUnique()
{
    /* Enumerate all the sequences: */
    QMultiMap<QString, QString> usedSequences;
    foreach (UIShortcutTableViewRow *pRow, m_shortcuts)
    {
        QString strKey = pRow->currentSequence();
        if (!strKey.isEmpty())
        {
            const QString strScope = pRow->scope();
            strKey = strScope.isNull() ? strKey : QString("%1: %2").arg(strScope, strKey);
            usedSequences.insert(strKey, pRow->key());
        }
    }
    /* Enumerate all the duplicated sequences: */
    QSet<QString> duplicatedSequences;
    foreach (const QString &strKey, usedSequences.keys())
        if (usedSequences.count(strKey) > 1)
        {
            foreach (const QString &strValue, usedSequences.values(strKey))
                duplicatedSequences |= strValue;
        }
    /* Is there something changed? */
    if (m_duplicatedSequences != duplicatedSequences)
    {
        m_duplicatedSequences = duplicatedSequences;
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
    }
    /* Are there duplicated shortcuts? */
    if (!m_duplicatedSequences.isEmpty())
        return false;
    /* True by default: */
    return true;
}

QModelIndex UIShortcutConfigurationModel::index(int iRow, int iColumn, const QModelIndex &parentIdx /* = QModelIndex() */) const
{
    /* No index for unknown items: */
    if (!hasIndex(iRow, iColumn, parentIdx))
        return QModelIndex();

    /* Provide index users with packed item pointer: */
    UIShortcutTableViewRow *pItem = iRow >= 0 && iRow < m_shortcuts.size() ? m_shortcuts.at(iRow) : 0;
    return pItem ? createIndex(iRow, iColumn, pItem) : QModelIndex();
}

Qt::ItemFlags UIShortcutConfigurationModel::flags(const QModelIndex &index) const
{
    /* Check index validness: */
    if (!index.isValid())
        return Qt::NoItemFlags;
    /* Switch for different columns: */
    switch (index.column())
    {
        case TableColumnIndex_Description: return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        case TableColumnIndex_Sequence: return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
        default: return Qt::NoItemFlags;
    }
}

int UIShortcutConfigurationModel::rowCount(const QModelIndex& /* parent = QModelIndex() */) const
{
    return m_shortcuts.size();
}

int UIShortcutConfigurationModel::columnCount(const QModelIndex& /* parent = QModelIndex() */) const
{
    return TableColumnIndex_Max;
}

QVariant UIShortcutConfigurationModel::headerData(int iSection,
                                                  Qt::Orientation enmOrientation,
                                                  int iRole /* = Qt::DisplayRole */) const
{
    /* Switch for different roles: */
    switch (iRole)
    {
        case Qt::DisplayRole:
        {
            /* Invalid for vertical header: */
            if (enmOrientation == Qt::Vertical)
                return QString();
            /* Switch for different columns: */
            switch (iSection)
            {
                case TableColumnIndex_Description: return UIShortcutConfigurationEditor::tr("Name");
                case TableColumnIndex_Sequence: return UIShortcutConfigurationEditor::tr("Shortcut");
                default: break;
            }
            /* Invalid for other cases: */
            return QString();
        }
        default:
            break;
    }
    /* Invalid by default: */
    return QVariant();
}

bool UIShortcutConfigurationModel::setData(const QModelIndex &index, const QVariant &value, int iRole)
{
    /* Nothing to set for invalid index: */
    if (!index.isValid())
        return false;
    /* Switch for different roles: */
    switch (iRole)
    {
        case Qt::EditRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case TableColumnIndex_Sequence:
                {
                    /* Get index: */
                    const int iIndex = index.row();
                    /* Set sequence to shortcut: */
                    UIShortcutTableViewRow *pFilteredShortcut = m_shortcuts.at(iIndex);
                    const int iShortcutIndex = UIShortcutSearchFunctor<UIShortcutTableViewRow>()(m_shortcuts, pFilteredShortcut);
                    if (iShortcutIndex != -1)
                    {
                        pFilteredShortcut->setCurrentSequence(  pFilteredShortcut->key() == UIHostCombo::hostComboCacheKey()
                                                              ? value.value<UIHostComboWrapper>().toString()
                                                              : value.value<UIHotKey>().sequence());
                        emit sigDataChanged();
                        return true;
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
    /* Nothing to set by default: */
    return false;
}

QVariant UIShortcutConfigurationModel::data(const QModelIndex &index, int iRole) const
{
    /* No data for invalid index: */
    if (!index.isValid())
        return QVariant();
    const int iIndex = index.row();
    /* Switch for different roles: */
    switch (iRole)
    {
        case Qt::DisplayRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case TableColumnIndex_Description:
                {
                    /* Return shortcut scope and description: */
                    const QString strScope = m_shortcuts.at(iIndex)->scope();
                    const QString strDescription = m_shortcuts.at(iIndex)->description();
                    return strScope.isNull() ? strDescription : QString("%1: %2").arg(strScope, strDescription);
                }
                case TableColumnIndex_Sequence:
                {
                    /* If that is host-combo cell: */
                    if (m_shortcuts.at(iIndex)->key() == UIHostCombo::hostComboCacheKey())
                        /* We should return host-combo: */
                        return UIHostCombo::toReadableString(m_shortcuts.at(iIndex)->currentSequence());
                    /* In other cases we should return hot-combo: */
                    QString strHotCombo = m_shortcuts.at(iIndex)->currentSequence();
                    /* But if that is machine table and hot-combo is not empty: */
                    if (m_enmType == UIType_RuntimeUI && !strHotCombo.isEmpty())
                        /* We should prepend it with Host+ prefix: */
                        strHotCombo.prepend(UIHostCombo::hostComboModifierName());
                    /* Return what we've got: */
                    return strHotCombo;
                }
                default: break;
            }
            /* Invalid for other cases: */
            return QString();
        }
        case Qt::EditRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case TableColumnIndex_Sequence:
                    return   m_shortcuts.at(iIndex)->key() == UIHostCombo::hostComboCacheKey()
                           ? QVariant::fromValue(UIHostComboWrapper(m_shortcuts.at(iIndex)->currentSequence()))
                           : QVariant::fromValue(UIHotKey(  m_enmType == UIType_RuntimeUI
                                                          ? UIHotKeyType_Simple
                                                          : UIHotKeyType_WithModifiers,
                                                          m_shortcuts.at(iIndex)->currentSequence(),
                                                          m_shortcuts.at(iIndex)->defaultSequence()));
                default:
                    break;
            }
            /* Invalid for other cases: */
            return QString();
        }
        case Qt::FontRole:
        {
            /* Do we have a default font? */
            QFont font(QApplication::font());
            /* Switch for different columns: */
            switch (index.column())
            {
                case TableColumnIndex_Sequence:
                {
                    if (   m_shortcuts.at(iIndex)->key() != UIHostCombo::hostComboCacheKey()
                        && m_shortcuts.at(iIndex)->currentSequence() != m_shortcuts.at(iIndex)->defaultSequence())
                        font.setBold(true);
                    break;
                }
                default: break;
            }
            /* Return resulting font: */
            return font;
        }
        case Qt::ForegroundRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case TableColumnIndex_Sequence:
                {
                    if (m_duplicatedSequences.contains(m_shortcuts.at(iIndex)->key()))
                        return QBrush(Qt::red);
                    break;
                }
                default: break;
            }
            /* Default for other cases: */
            return QString();
        }
        case IsHostCombo:
        {
            return m_shortcuts.at(iIndex)->key() == UIHostCombo::hostComboCacheKey();
        }
        default: break;
    }
    /* Invalid by default: */
    return QVariant();
}

QITableView *UIShortcutConfigurationModel::view() const
{
    switch (m_enmType)
    {
        case UIType_ManagerUI: return m_pShortcutConfigurationEditor->viewManager();
        case UIType_RuntimeUI: return m_pShortcutConfigurationEditor->viewRuntime();
        default: return 0;
    }
}


/*********************************************************************************************************************************
*   Class UIShortcutConfigurationProxyModel implementation.                                                                      *
*********************************************************************************************************************************/

UIShortcutConfigurationProxyModel::UIShortcutConfigurationProxyModel(QObject *pParent /* = 0 */)
    : QSortFilterProxyModel(pParent)
{
}

void UIShortcutConfigurationProxyModel::setFilter(const QString &strFilter)
{
    m_strFilter = strFilter;
    invalidate();
}

bool UIShortcutConfigurationProxyModel::filterAcceptsRow(int iSourceRow, const QModelIndex &srcIdxParent) const
{
    /* Acquire child index of source model: */
    QModelIndex srcIdxChild = sourceModel()->index(iSourceRow, 0, srcIdxParent);
    if (srcIdxChild.isValid())
    {
        /* Perform QString case-insensitive filtering, default Qt::DisplayRole is Ok: */
        if (!sourceModel()->data(srcIdxChild).toString().contains(m_strFilter, Qt::CaseInsensitive))
            return false;
    }
    /* Everything else is allowed: */
    return true;
}

bool UIShortcutConfigurationProxyModel::lessThan(const QModelIndex &srcIdxLeft, const QModelIndex &srcIdxRight) const
{
    /* Check if left or right index contains the Host-combo shortcut: */
    if (sourceModel()->data(srcIdxLeft, UIShortcutConfigurationModel::IsHostCombo).toBool())
        return true;
    else if (sourceModel()->data(srcIdxRight, UIShortcutConfigurationModel::IsHostCombo).toBool())
        return false;

    /* Perform QString case-insensitive comparition otherwise: */
    return QString::compare(sourceModel()->data(srcIdxLeft).toString(),
                            sourceModel()->data(srcIdxRight).toString(),
                            Qt::CaseInsensitive) < 0;
}


/*********************************************************************************************************************************
*   Class UIShortcutConfigurationView implementation.                                                                            *
*********************************************************************************************************************************/

UIShortcutConfigurationView::UIShortcutConfigurationView(QWidget *pParent,
                                                         const QString &strObjectName)
    : QITableView(pParent)
    , m_fPolished(false)
    , m_pItemEditorFactory(0)
{
    /* Set object name: */
    setObjectName(strObjectName);

    /* Prepare all: */
    prepare();
}

UIShortcutConfigurationView::~UIShortcutConfigurationView()
{
    /* Cleanup all: */
    cleanup();
}

void UIShortcutConfigurationView::setModel(QAbstractItemModel *pModel)
{
    /* Call to base-class: */
    QITableView::setModel(pModel);

    /* Some stuff to be polished after a model is assigned: */
    if (model() && !m_fPolished)
    {
        m_fPolished = true;

        // WORKAROUND:
        // Due to internal Qt bug QHeaderView::::setSectionResizeMode is only possible
        // to be used after model is already assigned. Getting Qt crash otherwise.
        horizontalHeader()->setSectionResizeMode(TableColumnIndex_Description, QHeaderView::Interactive);
        horizontalHeader()->setSectionResizeMode(TableColumnIndex_Sequence, QHeaderView::Stretch);
    }
}

void UIShortcutConfigurationView::rowsInserted(const QModelIndex &parent, int iStart, int iEnd)
{
    /* Call to base-class: */
    QITableView::rowsInserted(parent, iStart, iEnd);

    /* Resize columns to fit contents: */
    resizeColumnsToContents();

    /* Reapply sorting: */
    sortByColumn(TableColumnIndex_Description, Qt::AscendingOrder);
}

void UIShortcutConfigurationView::prepare()
{
    /* Configure self: */
    setSortingEnabled(true);
    setTabKeyNavigation(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::CurrentChanged | QAbstractItemView::SelectedClicked);

    /* Configure headers: */
    verticalHeader()->hide();
    verticalHeader()->setDefaultSectionSize((int)(verticalHeader()->minimumSectionSize() * 1.33));
    horizontalHeader()->setStretchLastSection(false);
    // WORKAROUND:
    // Rest of horizontalHeader stuff is in setModel() above..

    /* Check if we do have proper item delegate: */
    QIStyledItemDelegate *pStyledItemDelegate = qobject_cast<QIStyledItemDelegate*>(itemDelegate());
    if (pStyledItemDelegate)
    {
        /* Configure item delegate: */
        pStyledItemDelegate->setWatchForEditorDataCommits(true);

        /* Create new item editor factory: */
        m_pItemEditorFactory = new QItemEditorFactory;
        if (m_pItemEditorFactory)
        {
            /* Register UIHotKeyEditor as the UIHotKey editor: */
            int iHotKeyTypeId = qRegisterMetaType<UIHotKey>();
            QStandardItemEditorCreator<UIHotKeyEditor> *pHotKeyItemEditorCreator = new QStandardItemEditorCreator<UIHotKeyEditor>();
            m_pItemEditorFactory->registerEditor(iHotKeyTypeId, pHotKeyItemEditorCreator);

            /* Register UIHostComboEditor as the UIHostComboWrapper editor: */
            int iHostComboTypeId = qRegisterMetaType<UIHostComboWrapper>();
            QStandardItemEditorCreator<UIHostComboEditor> *pHostComboItemEditorCreator = new QStandardItemEditorCreator<UIHostComboEditor>();
            m_pItemEditorFactory->registerEditor(iHostComboTypeId, pHostComboItemEditorCreator);

            /* Assign configured item editor factory to item delegate: */
            pStyledItemDelegate->setItemEditorFactory(m_pItemEditorFactory);
        }
    }
}

void UIShortcutConfigurationView::cleanup()
{
    /* Cleanup item editor factory: */
    delete m_pItemEditorFactory;
    m_pItemEditorFactory = 0;
}


/*********************************************************************************************************************************
*   Class UIShortcutConfigurationEditor implementation.                                                                          *
*********************************************************************************************************************************/

UIShortcutConfigurationEditor::UIShortcutConfigurationEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_pModelManager(0)
    , m_pModelRuntime(0)
    , m_pProxyModelManager(0)
    , m_pProxyModelRuntime(0)
    , m_pTabWidget(0)
    , m_pEditorFilterManager(0), m_pTableManager(0)
    , m_pEditorFilterRuntime(0), m_pTableRuntime(0)
{
    prepare();
}

QITableView *UIShortcutConfigurationEditor::viewManager()
{
    return m_pTableManager;
}

QITableView *UIShortcutConfigurationEditor::viewRuntime()
{
    return m_pTableRuntime;
}

void UIShortcutConfigurationEditor::load(const UIShortcutConfigurationList &value)
{
    m_pModelManager->load(value);
    m_pModelRuntime->load(value);
}

void UIShortcutConfigurationEditor::save(UIShortcutConfigurationList &value) const
{
    m_pModelManager->save(value);
    m_pModelRuntime->save(value);
}

bool UIShortcutConfigurationEditor::isShortcutsUniqueManager() const
{
    return m_pModelManager->isAllShortcutsUnique();
}

bool UIShortcutConfigurationEditor::isShortcutsUniqueRuntime() const
{
    return m_pModelRuntime->isAllShortcutsUnique();
}

QString UIShortcutConfigurationEditor::tabNameManager() const
{
    return m_pTabWidget->tabText(TableIndex_Manager);
}

QString UIShortcutConfigurationEditor::tabNameRuntime() const
{
    return m_pTabWidget->tabText(TableIndex_Runtime);
}

void UIShortcutConfigurationEditor::sltRetranslateUI()
{
    m_pTabWidget->setTabText(TableIndex_Manager, tr("&VirtualBox Manager"));
    m_pTabWidget->setTabText(TableIndex_Runtime, tr("Virtual &Machine"));
    m_pTableManager->setWhatsThis(tr("Lists all available shortcuts which can be configured."));
    m_pTableRuntime->setWhatsThis(tr("Lists all available shortcuts which can be configured."));
    m_pEditorFilterManager->setToolTip(tr("Holds a sequence to filter the shortcut list."));
    m_pEditorFilterRuntime->setToolTip(tr("Holds a sequence to filter the shortcut list."));
}

void UIShortcutConfigurationEditor::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIShortcutConfigurationEditor::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        pMainLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare tab-widget: */
        m_pTabWidget = new QTabWidget(this);
        if (m_pTabWidget)
        {
            /* Prepare 'Manager UI' tab: */
            prepareTabManager();
            /* Prepare 'Runtime UI' tab: */
            prepareTabRuntime();

            /* Add tab-widget into layout: */
            pMainLayout->addWidget(m_pTabWidget);
        }
    }
}

void UIShortcutConfigurationEditor::prepareTabManager()
{
    /* Prepare Manager UI tab: */
    QWidget *pTabManager = new QWidget;
    if (pTabManager)
    {
        /* Prepare Manager UI layout: */
        QVBoxLayout *pLayoutManager = new QVBoxLayout(pTabManager);
        if (pLayoutManager)
        {
            pLayoutManager->setSpacing(1);
#ifdef VBOX_WS_MAC
            /* On Mac OS X and X11 we can do a bit of smoothness: */
            pLayoutManager->setContentsMargins(0, 0, 0, 0);
#endif

            /* Prepare Manager UI filter editor: */
            m_pEditorFilterManager = new QLineEdit(pTabManager);
            if (m_pEditorFilterManager)
                pLayoutManager->addWidget(m_pEditorFilterManager);

            /* Prepare Manager UI model: */
            m_pModelManager = new UIShortcutConfigurationModel(this, UIType_ManagerUI);

            /* Prepare Manager UI proxy-model: */
            m_pProxyModelManager = new UIShortcutConfigurationProxyModel(this);
            if (m_pProxyModelManager)
                m_pProxyModelManager->setSourceModel(m_pModelManager);

            /* Prepare Manager UI table: */
            m_pTableManager = new UIShortcutConfigurationView(pTabManager, "m_pTableManager");
            if (m_pTableManager)
            {
                m_pTableManager->setModel(m_pProxyModelManager);
                pLayoutManager->addWidget(m_pTableManager);
            }
        }

        m_pTabWidget->insertTab(TableIndex_Manager, pTabManager, QString());
    }
}

void UIShortcutConfigurationEditor::prepareTabRuntime()
{
    /* Create Runtime UI tab: */
    QWidget *pTabMachine = new QWidget;
    if (pTabMachine)
    {
        /* Prepare Runtime UI layout: */
        QVBoxLayout *pLayoutMachine = new QVBoxLayout(pTabMachine);
        if (pLayoutMachine)
        {
            pLayoutMachine->setSpacing(1);
#ifdef VBOX_WS_MAC
            /* On Mac OS X and X11 we can do a bit of smoothness: */
            pLayoutMachine->setContentsMargins(0, 0, 0, 0);
#endif

            /* Prepare Runtime UI filter editor: */
            m_pEditorFilterRuntime = new QLineEdit(pTabMachine);
            if (m_pEditorFilterRuntime)
                pLayoutMachine->addWidget(m_pEditorFilterRuntime);

            /* Prepare Runtime UI model: */
            m_pModelRuntime = new UIShortcutConfigurationModel(this, UIType_RuntimeUI);

            /* Prepare Runtime UI proxy-model: */
            m_pProxyModelRuntime = new UIShortcutConfigurationProxyModel(this);
            if (m_pProxyModelRuntime)
                m_pProxyModelRuntime->setSourceModel(m_pModelRuntime);

            /* Create Runtime UI table: */
            m_pTableRuntime = new UIShortcutConfigurationView(pTabMachine, "m_pTableRuntime");
            if (m_pTableRuntime)
            {
                m_pTableRuntime->setModel(m_pProxyModelRuntime);
                pLayoutMachine->addWidget(m_pTableRuntime);
            }
        }

        m_pTabWidget->insertTab(TableIndex_Runtime, pTabMachine, QString());

        /* In the VM process we start by displaying the Runtime UI tab: */
        if (uiCommon().uiType() == UIType_RuntimeUI)
            m_pTabWidget->setCurrentWidget(pTabMachine);
    }
}

void UIShortcutConfigurationEditor::prepareConnections()
{
    /* Configure 'Manager UI' connections: */
    connect(m_pEditorFilterManager, &QLineEdit::textChanged,
            m_pProxyModelManager, &UIShortcutConfigurationProxyModel::setFilter);
    connect(m_pModelManager, &UIShortcutConfigurationModel::sigDataChanged,
            this, &UIShortcutConfigurationEditor::sigValueChanged);

    /* Configure 'Runtime UI' connections: */
    connect(m_pEditorFilterRuntime, &QLineEdit::textChanged,
            m_pProxyModelRuntime, &UIShortcutConfigurationProxyModel::setFilter);
    connect(m_pModelRuntime, &UIShortcutConfigurationModel::sigDataChanged,
            this, &UIShortcutConfigurationEditor::sigValueChanged);
}


# include "UIShortcutConfigurationEditor.moc"
