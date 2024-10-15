/* $Id: UIFileSystemModel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileSystemModel class implementation.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <QDateTime>
#include <QHeaderView>
#include <QIODevice>
#include <QMimeData>

/* GUI includes: */
#include "UIFileSystemModel.h"
#include "UIErrorString.h"
#include "UIPathOperations.h"
#include "UITranslator.h"

/* Other VBox includes: */
#include "iprt/assert.h"

const char *UIFileSystemModel::strUpDirectoryString = "..";


/*********************************************************************************************************************************
*   UIFileSystemItem implementation.                                                                                       *
*********************************************************************************************************************************/

UIFileSystemItem::UIFileSystemItem(const QString &strFileObjectName, UIFileSystemItem *parent, KFsObjType type)
    : m_parentItem(parent)
    , m_bIsOpened(false)
    , m_fIsTargetADirectory(false)
    , m_type(type)
    , m_fIsDriveItem(false)
    , m_fIsHidden(false)
{
    for (int i = static_cast<int>(UIFileSystemModelData_Name);
         i < static_cast<int>(UIFileSystemModelData_Max); ++i)
        m_itemData[static_cast<UIFileSystemModelData>(i)] = QVariant();
    m_itemData[UIFileSystemModelData_Name] = strFileObjectName;

    if (parent)
    {
        parent->appendChild(this);
        setParentModel(parent->parentModel());
    }
}

UIFileSystemItem::~UIFileSystemItem()
{
    reset();
}

void UIFileSystemItem::appendChild(UIFileSystemItem *item)
{
    if (!item)
        return;
    if (m_childItems.contains(item))
        return;
    m_childItems.append(item);
}

void UIFileSystemItem::reset()
{
    qDeleteAll(m_childItems);
    m_childItems.clear();
    m_bIsOpened = false;
}

UIFileSystemItem *UIFileSystemItem::child(int row) const
{
    return m_childItems.value(row);
}

UIFileSystemItem *UIFileSystemItem::child(const QString &fileObjectName) const
{
    foreach (UIFileSystemItem *pItem, m_childItems)
        if (pItem && pItem->fileObjectName() == fileObjectName)
            return pItem;
    return 0;
}

int UIFileSystemItem::childCount() const
{
    return m_childItems.count();
}

QList<UIFileSystemItem*> UIFileSystemItem::children() const
{
    QList<UIFileSystemItem*> childList;
    foreach (UIFileSystemItem *child, m_childItems)
        childList << child;
    return childList;
}

void UIFileSystemItem::removeChild(UIFileSystemItem *pItem)
{
    int iIndex = m_childItems.indexOf(pItem);
    if (iIndex == -1 || iIndex > m_childItems.size())
        return;
    m_childItems.removeAt(iIndex);
    delete pItem;
    pItem = 0;
}

void UIFileSystemItem::removeChildren()
{
    reset();
}

int UIFileSystemItem::columnCount() const
{
    return m_itemData.count();
}

QVariant UIFileSystemItem::data(int column) const
{
    return m_itemData.value(static_cast<UIFileSystemModelData>(column), QVariant());
}

QString UIFileSystemItem::fileObjectName() const
{
    QVariant data = m_itemData.value(UIFileSystemModelData_Name, QVariant());
    if (!data.canConvert(QMetaType(QMetaType::QString)))
        return QString();
    return data.toString();
}

void UIFileSystemItem::setData(const QVariant &data, int index)
{
    if (m_itemData[static_cast<UIFileSystemModelData>(index)] == data)
        return;
    m_itemData[static_cast<UIFileSystemModelData>(index)] = data;
}

void UIFileSystemItem::setData(const QVariant &data, UIFileSystemModelData enmColumn)
{
    m_itemData[enmColumn] = data;
}

UIFileSystemItem *UIFileSystemItem::parentItem()
{
    return m_parentItem;
}

UIFileSystemItem *UIFileSystemItem::parentItem() const
{
    return m_parentItem;
}

int UIFileSystemItem::row() const
{
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<UIFileSystemItem*>(this));
    return 0;
}

bool UIFileSystemItem::isDirectory() const
{
    return m_type == KFsObjType_Directory;
}

bool UIFileSystemItem::isSymLink() const
{
    return m_type == KFsObjType_Symlink;
}

bool UIFileSystemItem::isFile() const
{
    return m_type == KFsObjType_File;
}

void UIFileSystemItem::clearChildren()
{
    qDeleteAll(m_childItems);
    m_childItems.clear();
}

bool UIFileSystemItem::isOpened() const
{
    return m_bIsOpened;
}

void UIFileSystemItem::setIsOpened(bool flag)
{
    m_bIsOpened = flag;
}

QString UIFileSystemItem::path() const
{
    const QChar delimiter('/');

    const UIFileSystemItem *pParent = this;
    QStringList path;
    while(pParent && pParent->parentItem())
    {
        path.prepend(pParent->fileObjectName());
        pParent = pParent->parentItem();
    }
    QString strPath = UIPathOperations::removeMultipleDelimiters(path.join(delimiter));
    if (m_pParentModel && m_pParentModel->isWindowsFileSystem())
    {
        if (!strPath.isEmpty() && strPath.at(0) == delimiter)
            strPath.remove(0, 1);
    }
    return UIPathOperations::removeTrailingDelimiters(strPath);
}

bool UIFileSystemItem::isUpDirectory() const
{
    if (!isDirectory())
        return false;
    if (data(0) == UIFileSystemModel::strUpDirectoryString)
        return true;
    return false;
}

KFsObjType UIFileSystemItem::type() const
{
    return m_type;
}

const QString &UIFileSystemItem::targetPath() const
{
    return m_strTargetPath;
}

void UIFileSystemItem::setTargetPath(const QString &path)
{
    m_strTargetPath = path;
}

bool UIFileSystemItem::isSymLinkToADirectory() const
{
    return m_fIsTargetADirectory;
}

void UIFileSystemItem::setIsSymLinkToADirectory(bool flag)
{
    m_fIsTargetADirectory = flag;
}

bool UIFileSystemItem::isSymLinkToAFile() const
{
    return isSymLink() && !m_fIsTargetADirectory;
}

void UIFileSystemItem::setIsDriveItem(bool flag)
{
    m_fIsDriveItem = flag;
}

bool UIFileSystemItem::isDriveItem() const
{
    return m_fIsDriveItem;
}

void UIFileSystemItem::setIsHidden(bool flag)
{
    m_fIsHidden = flag;
}

bool UIFileSystemItem::isHidden() const
{
    return m_fIsHidden;
}

void UIFileSystemItem::setRemovedFromViso(bool fRemoved)
{
    m_itemData[UIFileSystemModelData_RemovedFromVISO] = fRemoved;
}

bool UIFileSystemItem::isRemovedFromViso() const
{
    return m_itemData[UIFileSystemModelData_RemovedFromVISO].toBool();
}

void UIFileSystemItem::setToolTip(const QString &strToolTip)
{
    m_strToolTip = strToolTip;
}

const QString &UIFileSystemItem::toolTip() const
{
    return m_strToolTip;
}

void UIFileSystemItem::setParentModel(UIFileSystemModel *pModel)
{
    m_pParentModel = pModel;
}

UIFileSystemModel *UIFileSystemItem::parentModel()
{
    return m_pParentModel;
}


/*********************************************************************************************************************************
*   UIFileSystemProxyModel implementation.                                                                                 *
*********************************************************************************************************************************/

UIFileSystemProxyModel::UIFileSystemProxyModel(QObject *parent /* = 0 */)
    :QSortFilterProxyModel(parent)
    , m_fListDirectoriesOnTop(false)
    , m_fShowHiddenObjects(true)
{
}

bool UIFileSystemProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    UIFileSystemItem *pLeftItem = static_cast<UIFileSystemItem*>(left.internalPointer());
    UIFileSystemItem *pRightItem = static_cast<UIFileSystemItem*>(right.internalPointer());

    if (pLeftItem && pRightItem)
    {
        /* List the directories before the files if options say so: */
        if (m_fListDirectoriesOnTop)
        {
            if ((pLeftItem->isDirectory() || pLeftItem->isSymLinkToADirectory()) && !pRightItem->isDirectory())
                return (sortOrder() == Qt::AscendingOrder);
            if ((pRightItem->isDirectory() || pRightItem->isSymLinkToADirectory()) && !pLeftItem->isDirectory())
                return (sortOrder() == Qt::DescendingOrder);
        }
        /* Up directory item should be always the first item: */
        if (pLeftItem->isUpDirectory())
            return (sortOrder() == Qt::AscendingOrder);
        else if (pRightItem->isUpDirectory())
            return (sortOrder() == Qt::DescendingOrder);

        /* If the sort column is QDateTime than handle it correctly: */
        if (sortColumn() == UIFileSystemModelData_ChangeTime)
        {
            QVariant dataLeft = pLeftItem->data(UIFileSystemModelData_ChangeTime);
            QVariant dataRight = pRightItem->data(UIFileSystemModelData_ChangeTime);
            QDateTime leftDateTime = dataLeft.toDateTime();
            QDateTime rightDateTime = dataRight.toDateTime();
            return (leftDateTime < rightDateTime);
        }
        /* When we show human readble sizes in size column sorting gets confused, so do it here: */
        else if(sortColumn() == UIFileSystemModelData_Size)
        {
            qulonglong leftSize = pLeftItem->data(UIFileSystemModelData_Size).toULongLong();
            qulonglong rightSize = pRightItem->data(UIFileSystemModelData_Size).toULongLong();
            return (leftSize < rightSize);

        }
    }
    return QSortFilterProxyModel::lessThan(left, right);
}

bool UIFileSystemProxyModel::filterAcceptsRow(int iSourceRow, const QModelIndex &sourceParent) const
{
    if (m_fShowHiddenObjects)
        return true;

    QModelIndex itemIndex = sourceModel()->index(iSourceRow, 0, sourceParent);
    if (!itemIndex.isValid())
        return false;

    UIFileSystemItem *item = static_cast<UIFileSystemItem*>(itemIndex.internalPointer());
    if (!item)
        return false;

    if (item->isHidden())
        return false;

    return true;
}

void UIFileSystemProxyModel::setListDirectoriesOnTop(bool fListDirectoriesOnTop)
{
    m_fListDirectoriesOnTop = fListDirectoriesOnTop;
}

bool UIFileSystemProxyModel::listDirectoriesOnTop() const
{
    return m_fListDirectoriesOnTop;
}

void UIFileSystemProxyModel::setShowHiddenObjects(bool fShowHiddenObjects)
{
    m_fShowHiddenObjects = fShowHiddenObjects;
}

bool UIFileSystemProxyModel::showHiddenObjects() const
{
    return m_fShowHiddenObjects;
}


/*********************************************************************************************************************************
*   UIFileSystemModel implementation.                                                                                      *
*********************************************************************************************************************************/

UIFileSystemModel::UIFileSystemModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_fShowHumanReadableSizes(false)
    , m_fIsWindowFileSystemModel(false)
{
    m_pRootItem = new UIFileSystemItem(QString(), 0, KFsObjType_Directory);
    m_pRootItem->setParentModel(this);
}

QStringList UIFileSystemModel::mimeTypes() const
{
    QStringList types;
    types << "application/vnd.text.list";
    return types;
}

QMimeData *UIFileSystemModel::mimeData(const QModelIndexList &indexes) const
{
    QMimeData *mimeData = new QMimeData();
    QByteArray encodedData;

    QDataStream stream(&encodedData, QIODevice::WriteOnly);

    foreach (const QModelIndex &index, indexes) {
        if (index.isValid() && index.column() == 0)
        {
            UIFileSystemItem *pItem = static_cast<UIFileSystemItem*>(index.internalPointer());
            if (!pItem)
                continue;

            QString strPath = pItem->path();
            if (!strPath.contains(".."))
                stream << strPath;
        }
    }

    mimeData->setData("application/vnd.text.list", encodedData);
    return mimeData;
}

UIFileSystemItem* UIFileSystemModel::rootItem()
{
    return m_pRootItem;
}

const UIFileSystemItem* UIFileSystemModel::rootItem() const
{
    return m_pRootItem;
}

UIFileSystemModel::~UIFileSystemModel()
{
    delete m_pRootItem;
}

int UIFileSystemModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return static_cast<UIFileSystemItem*>(parent.internalPointer())->columnCount();
    else
    {
        if (!rootItem())
            return 0;
        else
            return rootItem()->columnCount();
    }
}

bool UIFileSystemModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole)
    {
        if (index.column() == 0 && value.canConvert(QMetaType(QMetaType::QString)))
        {
            UIFileSystemItem *pItem = static_cast<UIFileSystemItem*>(index.internalPointer());
            if (!pItem)
                return false;
            QString strOldName = pItem->fileObjectName();
            QString strOldPath = pItem->path();
            pItem->setData(value, index.column());
            emit dataChanged(index, index);
            emit sigItemRenamed(pItem, strOldPath, strOldName, value.toString());
            return true;
        }
    }
    return false;
}

QVariant UIFileSystemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    UIFileSystemItem *item = static_cast<UIFileSystemItem*>(index.internalPointer());
    if (!item)
        return QVariant();

    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        /* dont show anything but the name for up directories: */
        if (item->isUpDirectory() && index.column() != UIFileSystemModelData_Name)
            return QVariant();
        /* Format date/time column: */
        if (item->data(index.column()).canConvert(QMetaType(QMetaType::QDateTime)))
        {
            QDateTime dateTime = item->data(index.column()).toDateTime();
            if (dateTime.isValid())
                return dateTime.toString("dd.MM.yyyy hh:mm:ss");
        }
        /* Decide whether to show human-readable file object sizes: */
        if (index.column() == UIFileSystemModelData_Size)
        {
            if (m_fShowHumanReadableSizes)
            {
                qulonglong size = item->data(index.column()).toULongLong();
                return UITranslator::formatSize(size);
            }
            else
                return item->data(index.column());
        }
        if (index.column() == UIFileSystemModelData_DescendantRemovedFromVISO)
        {
            if (item->data(UIFileSystemModelData_DescendantRemovedFromVISO).toBool())
                return QString(QApplication::translate("QIMessageBox", "Yes"));
            else
                return QString(QApplication::translate("QIMessageBox", "No"));
        }
        return item->data(index.column());
    }
    /* Show file object icons: */
    QString strContainingISOFile = item->data(UIFileSystemModelData_ISOFilePath).toString();
    if (role == Qt::DecorationRole && index.column() == 0)
    {
        if (item->isDirectory())
        {
            if (item->isUpDirectory())
                return QIcon(":/arrow_up_10px_x2.png");
            else if(item->isDriveItem())
                return QIcon(":/hd_32px.png");
            else if (item->isRemovedFromViso())
                return QIcon(":/file_manager_folder_remove_16px.png");
            else if (!strContainingISOFile.isEmpty())
                return QIcon(":/file_manager_folder_cd_16px.png");
            else
                return QIcon(":/file_manager_folder_16px.png");
        }
        else if (item->isFile())
        {
            if (item->isRemovedFromViso())
                return QIcon(":/file_manager_file_remove_16px.png");
            else if (!strContainingISOFile.isEmpty())
                return QIcon(":/file_manager_file_cd_16px.png");
            else
                return QIcon(":/file_manager_file_16px.png");
        }
        else if (item->isSymLink())
        {
            if (item->isSymLinkToADirectory())
                return QIcon(":/file_manager_folder_symlink_16px.png");
            else
                return QIcon(":/file_manager_file_symlink_16px.png");
        }
    }
    if (role == Qt::ToolTipRole)
    {
        if (!item->toolTip().isEmpty())
            return QString(item->toolTip());
    }
    return QVariant();
}

Qt::ItemFlags UIFileSystemModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::ItemFlags();
    UIFileSystemItem *item = static_cast<UIFileSystemItem*>(index.internalPointer());
    if (!item)
        return QAbstractItemModel::flags(index);

    if (!item->isUpDirectory() && index.column() == 0)
        return QAbstractItemModel::flags(index)  | Qt::ItemIsEditable;
    return QAbstractItemModel::flags(index);
}

QVariant UIFileSystemModel::headerData(int section, Qt::Orientation orientation,
                               int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        if (!rootItem())
            return QVariant();
        else
            return rootItem()->data(section);
    }
    return QVariant();
}

QModelIndex UIFileSystemModel::index(const UIFileSystemItem* item)
{
    if (!item)
        return QModelIndex();
    return createIndex(item->row(), 0, const_cast<UIFileSystemItem*>(item));
}

QModelIndex UIFileSystemModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    const UIFileSystemItem* parentItem = rootItem();

    if (parent.isValid())
        parentItem = static_cast<UIFileSystemItem*>(parent.internalPointer());

    if (!parentItem)
        return QModelIndex();

    UIFileSystemItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    else
        return QModelIndex();
}


QModelIndex UIFileSystemModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    UIFileSystemItem *childItem = static_cast<UIFileSystemItem*>(index.internalPointer());
    UIFileSystemItem *parentItem = childItem->parentItem();

    if (!parentItem || parentItem == rootItem())
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int UIFileSystemModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    const UIFileSystemItem *parentItem = rootItem();
    if (parent.isValid())
        parentItem = static_cast<UIFileSystemItem*>(parent.internalPointer());
    if (!parentItem)
        return 0;
    return parentItem->childCount();
}

void UIFileSystemModel::signalUpdate()
{
    emit layoutChanged();
}

QModelIndex UIFileSystemModel::rootIndex() const
{
    if (!rootItem())
        return QModelIndex();
    if (!rootItem()->child(0))
        return QModelIndex();
    return createIndex(rootItem()->child(0)->row(), 0,
                       rootItem()->child(0));
}

void UIFileSystemModel::beginReset()
{
    beginResetModel();
}

void UIFileSystemModel::endReset()
{
    endResetModel();
}

void UIFileSystemModel::reset()
{
    AssertPtrReturnVoid(m_pRootItem);
    beginResetModel();
    m_pRootItem->reset();
    endResetModel();
}

void UIFileSystemModel::setShowHumanReadableSizes(bool fShowHumanReadableSizes)
{
    m_fShowHumanReadableSizes = fShowHumanReadableSizes;
}

bool UIFileSystemModel::showHumanReadableSizes() const
{
    return m_fShowHumanReadableSizes;
}

void UIFileSystemModel::deleteItem(UIFileSystemItem* pItem)
{
    if (!pItem)
        return;
    UIFileSystemItem *pParent = pItem->parentItem();
    if (pParent)
        pParent->removeChild(pItem);
}

void UIFileSystemModel::setIsWindowsFileSystem(bool fIsWindowsFileSystem)
{
    m_fIsWindowFileSystemModel = fIsWindowsFileSystem;
}

bool UIFileSystemModel::isWindowsFileSystem() const
{
    return m_fIsWindowFileSystemModel;
}
