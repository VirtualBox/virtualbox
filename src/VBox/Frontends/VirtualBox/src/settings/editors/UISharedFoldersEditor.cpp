/* $Id: UISharedFoldersEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISharedFoldersEditor class implementation.
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
#include <QMenu>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIToolBar.h"
#include "QITreeWidget.h"
#include "UIIconPool.h"
#include "UISharedFolderDetailsEditor.h"
#include "UISharedFoldersEditor.h"
#include "VBoxUtils.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** Shared Folder tree-widget item. */
class SFTreeViewItem : public QITreeWidgetItem, public UIDataSharedFolder
{
    Q_OBJECT;

public:

    /** Format type. */
    enum FormatType
    {
        FormatType_Invalid,
        FormatType_EllipsisStart,
        FormatType_EllipsisMiddle,
        FormatType_EllipsisEnd,
        FormatType_EllipsisFile
    };

    /** Constructs shared folder type (root) item.
      * @param  pParent    Brings the item parent.
      * @param  enmFormat  Brings the item format type. */
    SFTreeViewItem(QITreeWidget *pParent, FormatType enmFormat);
    /** Constructs shared folder (child) item.
      * @param  pParent    Brings the item parent.
      * @param  enmFormat  Brings the item format type. */
    SFTreeViewItem(SFTreeViewItem *pParent, FormatType enmFormat);

    /** Returns whether this item is less than the @a other one. */
    bool operator<(const QTreeWidgetItem &other) const RT_OVERRIDE;

    /** Returns child item number @a iIndex. */
    SFTreeViewItem *child(int iIndex) const;

    /** Returns text of item number @a iIndex. */
    QString getText(int iIndex) const;

    /** Updates item fields. */
    void updateFields();

    /** Adjusts item layout. */
    void adjustText();

protected:

    /** Returns default text. */
    virtual QString defaultText() const RT_OVERRIDE;

private:

    /** Performs item @a iColumn processing. */
    void processColumn(int iColumn);

    /** Holds the item format type. */
    FormatType   m_enmFormat;
    /** Holds the item text fields. */
    QStringList  m_fields;
};


/*********************************************************************************************************************************
*   Class SFTreeViewItem implementation.                                                                                         *
*********************************************************************************************************************************/

SFTreeViewItem::SFTreeViewItem(QITreeWidget *pParent, FormatType enmFormat)
    : QITreeWidgetItem(pParent)
    , m_enmFormat(enmFormat)
{
    setFirstColumnSpanned(true);
    setFlags(flags() ^ Qt::ItemIsSelectable);
}

SFTreeViewItem::SFTreeViewItem(SFTreeViewItem *pParent, FormatType enmFormat)
    : QITreeWidgetItem(pParent)
    , m_enmFormat(enmFormat)
{
}

bool SFTreeViewItem::operator<(const QTreeWidgetItem &other) const
{
    /* Root items should always been sorted by type field: */
    return parentItem() ? text(0) < other.text(0) :
                          text(1) < other.text(1);
}

SFTreeViewItem *SFTreeViewItem::child(int iIndex) const
{
    QTreeWidgetItem *pItem = QTreeWidgetItem::child(iIndex);
    return pItem ? static_cast<SFTreeViewItem*>(pItem) : 0;
}

QString SFTreeViewItem::getText(int iIndex) const
{
    return iIndex >= 0 && iIndex < m_fields.size() ? m_fields.at(iIndex) : QString();
}

void SFTreeViewItem::updateFields()
{
    /* Clear fields: */
    m_fields.clear();

    /* For root items: */
    if (!parentItem())
        m_fields << m_strName
                 << QString::number((int)m_enmType);
    /* For child items: */
    else
        m_fields << m_strName
                 << m_strPath
                 << (m_fWritable ? tr("Full") : tr("Read-only"))
                 << (m_fAutoMount ? tr("Yes") : "")
                 << m_strAutoMountPoint;

    /* Adjust item layout: */
    adjustText();
}

void SFTreeViewItem::adjustText()
{
    for (int i = 0; i < treeWidget()->columnCount(); ++i)
        processColumn(i);
}

QString SFTreeViewItem::defaultText() const
{
    return parentItem()
         ? tr("%1, %2: %3, %4: %5, %6: %7, %8: %9",
              "col.1 text, col.2 name: col.2 text, col.3 name: col.3 text, col.4 name: col.4 text, col.5 name: col.5 text")
              .arg(text(0))
              .arg(parentTree()->headerItem()->text(1)).arg(text(1))
              .arg(parentTree()->headerItem()->text(2)).arg(text(2))
              .arg(parentTree()->headerItem()->text(3)).arg(text(3))
              .arg(parentTree()->headerItem()->text(4)).arg(text(4))
         : text(0);
}

void SFTreeViewItem::processColumn(int iColumn)
{
    QString strOneString = getText(iColumn);
    if (strOneString.isNull())
        return;
    const QFontMetrics fm = treeWidget()->fontMetrics();
    const int iOldSize = fm.horizontalAdvance(strOneString);
    const int iItemIndent = parentItem() ? treeWidget()->indentation() * 2 : treeWidget()->indentation();
    int iIndentSize = fm.horizontalAdvance(" ... ");
    if (iColumn == 0)
        iIndentSize += iItemIndent;
    const int cWidth = !parentItem() ? treeWidget()->viewport()->width() : treeWidget()->columnWidth(iColumn);

    /* Compress text: */
    int iStart = 0;
    int iFinish = 0;
    int iPosition = 0;
    int iTextWidth = 0;
    do
    {
        iTextWidth = fm.horizontalAdvance(strOneString);
        if (   iTextWidth
            && iTextWidth + iIndentSize > cWidth)
        {
            iStart = 0;
            iFinish = strOneString.length();

            /* Selecting remove position: */
            switch (m_enmFormat)
            {
                case FormatType_EllipsisStart:
                    iPosition = iStart;
                    break;
                case FormatType_EllipsisMiddle:
                    iPosition = (iFinish - iStart) / 2;
                    break;
                case FormatType_EllipsisEnd:
                    iPosition = iFinish - 1;
                    break;
                case FormatType_EllipsisFile:
                {
                    const QRegularExpression re("([\\\\/][^\\\\^/]+[\\\\/]?$)");
                    const QRegularExpressionMatch mt = re.match(strOneString);
                    const int iNewFinish = mt.capturedStart();
                    if (iNewFinish != -1)
                        iFinish = iNewFinish;
                    iPosition = (iFinish - iStart) / 2;
                    break;
                }
                default:
                    AssertMsgFailed(("Invalid format type\n"));
            }

            if (iPosition == iFinish)
               break;

            strOneString.remove(iPosition, 1);
        }
    }
    while (   iTextWidth
           && (iTextWidth + iIndentSize > cWidth));

    if (iPosition || m_enmFormat == FormatType_EllipsisFile)
        strOneString.insert(iPosition, "...");
    const int iNewSize = fm.horizontalAdvance(strOneString);
    setText(iColumn, iNewSize < iOldSize ? strOneString : m_fields.at(iColumn));
    setToolTip(iColumn, text(iColumn) == getText(iColumn) ? QString() : getText(iColumn));

    /* Calculate item's size-hint: */
    setSizeHint(iColumn, QSize(fm.horizontalAdvance(QString("  %1  ").arg(getText(iColumn))), fm.height()));
}


/*********************************************************************************************************************************
*   Class UISharedFoldersEditor implementation.                                                                                  *
*********************************************************************************************************************************/

UISharedFoldersEditor::UISharedFoldersEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_pLayoutTree(0)
    , m_pTreeWidget(0)
    , m_pToolbar(0)
    , m_pActionAdd(0)
    , m_pActionEdit(0)
    , m_pActionRemove(0)
{
    prepare();
}

void UISharedFoldersEditor::setValue(const QList<UIDataSharedFolder> &guiValue)
{
    /* Update cached value and
     * tree-widget if value has changed: */
    if (m_guiValue != guiValue)
    {
        m_guiValue = guiValue;
        reloadTree();
    }
}

QList<UIDataSharedFolder> UISharedFoldersEditor::value() const
{
    /* Sanity check: */
    if (!m_pTreeWidget)
        return m_guiValue;

    /* Prepare result: */
    QList<UIDataSharedFolder> result;

    /* For each folder type: */
    QTreeWidgetItem *pMainRootItem = m_pTreeWidget->invisibleRootItem();
    for (int iFolderTypeIndex = 0; iFolderTypeIndex < pMainRootItem->childCount(); ++iFolderTypeIndex)
    {
        /* Get folder root item: */
        const SFTreeViewItem *pFolderTypeRoot = static_cast<SFTreeViewItem*>(pMainRootItem->child(iFolderTypeIndex));

        /* For each folder of current type: */
        for (int iFolderIndex = 0; iFolderIndex < pFolderTypeRoot->childCount(); ++iFolderIndex)
        {
            /* Gather and cache new data: */
            const SFTreeViewItem *pItem = static_cast<SFTreeViewItem*>(pFolderTypeRoot->child(iFolderIndex));
            result << *pItem;
        }
    }

    /* Return result: */
    return result;
}

void UISharedFoldersEditor::setFeatureAvailable(bool fAvailable)
{
    if (m_pTreeWidget)
        m_pTreeWidget->setEnabled(fAvailable);
    if (m_pToolbar)
        m_pToolbar->setEnabled(fAvailable);
}

void UISharedFoldersEditor::setFoldersAvailable(UISharedFolderType enmType, bool fAvailable)
{
    m_foldersAvailable[enmType] = fAvailable;
    updateRootItemsVisibility();
}

void UISharedFoldersEditor::sltRetranslateUI()
{
    /* Translate tree-widget: */
    if (m_pTreeWidget)
    {
        m_pTreeWidget->setWhatsThis(tr("Lists all shared folders accessible to this machine. Use 'net use x: \\\\vboxsvr\\share' "
                                       "to access a shared folder named 'share' from a DOS-like OS, or 'mount -t vboxsf "
                                       "share mount_point' to access it from a Linux OS. This feature requires Guest Additions."));

        /* Translate tree-widget header: */
        QTreeWidgetItem *pTreeWidgetHeaderItem = m_pTreeWidget->headerItem();
        if (pTreeWidgetHeaderItem)
        {
            pTreeWidgetHeaderItem->setText(4, tr("At"));
            pTreeWidgetHeaderItem->setText(3, tr("Auto Mount"));
            pTreeWidgetHeaderItem->setText(2, tr("Access"));
            pTreeWidgetHeaderItem->setText(1, tr("Path"));
            pTreeWidgetHeaderItem->setText(0, tr("Name"));
        }

        /* Update tree-widget contents finally: */
        reloadTree();
    }

    /* Translate actions: */
    if (m_pActionAdd)
    {
        m_pActionAdd->setText(tr("Add Shared Folder"));
        m_pActionAdd->setToolTip(tr("Adds new shared folder."));
    }
    if (m_pActionEdit)
    {
        m_pActionEdit->setText(tr("Edit Shared Folder"));
        m_pActionEdit->setToolTip(tr("Edits selected shared folder."));
    }
    if (m_pActionRemove)
    {
        m_pActionRemove->setText(tr("Remove Shared Folder"));
        m_pActionRemove->setToolTip(tr("Removes selected shared folder."));
    }
}

void UISharedFoldersEditor::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    UIEditor::showEvent(pEvent);

    /* Connect header-resize signal just before widget is shown
     * after all the items properly loaded and initialized: */
    connect(m_pTreeWidget->header(), &QHeaderView::sectionResized,
            this, &UISharedFoldersEditor::sltAdjustTreeFields,
            Qt::UniqueConnection);

    /* Adjusting size after all pending show events are processed: */
    QTimer::singleShot(0, this, SLOT(sltAdjustTree()));
}

void UISharedFoldersEditor::resizeEvent(QResizeEvent * /* pEvent */)
{
    sltAdjustTree();
}

void UISharedFoldersEditor::sltAdjustTree()
{
    /*
     * Calculates required columns sizes to max out column 2
     * and let all other columns stay at their minimum sizes.
     *
     * Columns
     * 0 = Tree view / name
     * 1 = Path
     * 2 = Writable flag
     * 3 = Auto-mount flag
     * 4 = Auto mount point
     */
    QAbstractItemView *pItemView = m_pTreeWidget;
    QHeaderView *pItemHeader = m_pTreeWidget->header();
    const int iTotal = m_pTreeWidget->viewport()->width();

    const int mw0 = qMax(pItemView->sizeHintForColumn(0), pItemHeader->sectionSizeHint(0));
    const int mw2 = qMax(pItemView->sizeHintForColumn(2), pItemHeader->sectionSizeHint(2));
    const int mw3 = qMax(pItemView->sizeHintForColumn(3), pItemHeader->sectionSizeHint(3));
    const int mw4 = qMax(pItemView->sizeHintForColumn(4), pItemHeader->sectionSizeHint(4));
#if 0 /** @todo Neither approach is perfect.  Short folder names, short paths, plenty of white space, but there is often '...' in column 0. */

    const int w0 = mw0 < iTotal / 5 ? mw0 : iTotal / 5;
    const int w2 = mw2 < iTotal / 5 ? mw2 : iTotal / 5;
    const int w3 = mw3 < iTotal / 5 ? mw3 : iTotal / 5;
    const int w4 = mw4 < iTotal / 5 ? mw4 : iTotal / 5;

    /* Giving 1st column all the available space. */
    const int w1 = iTotal - w0 - w2 - w3 - w4;
#else
    const int mw1 = qMax(pItemView->sizeHintForColumn(1), pItemHeader->sectionSizeHint(1));
    const int iHintTotal = mw0 + mw1 + mw2 + mw3 + mw4;
    int w0, w1, w2, w3, w4;
    int cExcess = iTotal - iHintTotal;
    if (cExcess >= 0)
    {
        /* give excess width to column 1 (path) */
        w0 = mw0;
        w1 = mw1 + cExcess;
        w2 = mw2;
        w3 = mw3;
        w4 = mw4;
    }
    else
    {
        w0 = mw0 < iTotal / 5 ? mw0 : iTotal / 5;
        w2 = mw2 < iTotal / 5 ? mw2 : iTotal / 5;
        w3 = mw3 < iTotal / 5 ? mw3 : iTotal / 5;
        w4 = mw4 < iTotal / 5 ? mw4 : iTotal / 5;
        w1 = iTotal - w0 - w2 - w3 - w4;
    }
#endif
    m_pTreeWidget->setColumnWidth(0, w0);
    m_pTreeWidget->setColumnWidth(1, w1);
    m_pTreeWidget->setColumnWidth(2, w2);
    m_pTreeWidget->setColumnWidth(3, w3);
    m_pTreeWidget->setColumnWidth(4, w4);
}

void UISharedFoldersEditor::sltAdjustTreeFields()
{
    QTreeWidgetItem *pMainRoot = m_pTreeWidget->invisibleRootItem();
    for (int i = 0; i < pMainRoot->childCount(); ++i)
    {
        SFTreeViewItem *pSubRoot = static_cast<SFTreeViewItem*>(pMainRoot->child(i));
        pSubRoot->adjustText();
        for (int j = 0; j < pSubRoot->childCount(); ++j)
        {
            SFTreeViewItem *pItem = static_cast<SFTreeViewItem*>(pSubRoot->child(j));
            pItem->adjustText();
        }
    }
}

void UISharedFoldersEditor::sltHandleCurrentItemChange(QTreeWidgetItem *pCurrentItem)
{
    if (pCurrentItem && pCurrentItem->parent() && !pCurrentItem->isSelected())
        pCurrentItem->setSelected(true);
    const bool fAddEnabled = pCurrentItem;
    const bool fRemoveEnabled = fAddEnabled && pCurrentItem->parent();
    m_pActionAdd->setEnabled(fAddEnabled);
    m_pActionEdit->setEnabled(fRemoveEnabled);
    m_pActionRemove->setEnabled(fRemoveEnabled);
}

void UISharedFoldersEditor::sltHandleDoubleClick(QTreeWidgetItem *pItem)
{
    const bool fEditEnabled = pItem && pItem->parent();
    if (fEditEnabled)
        sltEditFolder();
}

void UISharedFoldersEditor::sltHandleContextMenuRequest(const QPoint &position)
{
    QMenu menu;
    QTreeWidgetItem *pItem = m_pTreeWidget->itemAt(position);
    if (m_pTreeWidget->isEnabled() && pItem && pItem->flags() & Qt::ItemIsSelectable)
    {
        menu.addAction(m_pActionEdit);
        menu.addAction(m_pActionRemove);
    }
    else
    {
        menu.addAction(m_pActionAdd);
    }
    if (!menu.isEmpty())
        menu.exec(m_pTreeWidget->viewport()->mapToGlobal(position));
}

void UISharedFoldersEditor::sltAddFolder()
{
    /* Configure shared folder details editor: */
    UISharedFolderDetailsEditor dlgFolderDetails(UISharedFolderDetailsEditor::EditorType_Add,
                                                 m_foldersAvailable.value(UISharedFolderType_Console),
                                                 usedList(true),
                                                 this);

    /* Run folder details dialog: */
    if (dlgFolderDetails.exec() == QDialog::Accepted)
    {
        const QString strName = dlgFolderDetails.name();
        const QString strPath = dlgFolderDetails.path();
        const UISharedFolderType enmType = dlgFolderDetails.isPermanent() ? UISharedFolderType_Machine : UISharedFolderType_Console;
        /* Shared folder's name & path could not be empty: */
        Assert(!strName.isEmpty() && !strPath.isEmpty());

        /* Prepare new data: */
        UIDataSharedFolder newFolderData;
        newFolderData.m_enmType = enmType;
        newFolderData.m_strName = strName;
        newFolderData.m_strPath = strPath;
        newFolderData.m_fWritable = dlgFolderDetails.isWriteable();
        newFolderData.m_fAutoMount = dlgFolderDetails.isAutoMounted();
        newFolderData.m_strAutoMountPoint = dlgFolderDetails.autoMountPoint();

        /* Add new folder item: */
        addSharedFolderItem(newFolderData, true /* its new? */);

        /* Sort tree-widget before adjusting: */
        m_pTreeWidget->sortItems(0, Qt::AscendingOrder);
        /* Adjust tree-widget finally: */
        sltAdjustTree();
    }
}

void UISharedFoldersEditor::sltEditFolder()
{
    /* Check current folder item: */
    SFTreeViewItem *pItem = static_cast<SFTreeViewItem*>(m_pTreeWidget->currentItem());
    AssertPtrReturnVoid(pItem);
    AssertPtrReturnVoid(pItem->parentItem());

    /* Configure shared folder details editor: */
    UISharedFolderDetailsEditor dlgFolderDetails(UISharedFolderDetailsEditor::EditorType_Edit,
                                                 m_foldersAvailable.value(UISharedFolderType_Console),
                                                 usedList(false),
                                                 this);
    dlgFolderDetails.setPath(pItem->m_strPath);
    dlgFolderDetails.setName(pItem->m_strName);
    dlgFolderDetails.setPermanent(pItem->m_enmType == UISharedFolderType_Machine);
    dlgFolderDetails.setWriteable(pItem->m_fWritable);
    dlgFolderDetails.setAutoMount(pItem->m_fAutoMount);
    dlgFolderDetails.setAutoMountPoint(pItem->m_strAutoMountPoint);

    /* Run folder details dialog: */
    if (dlgFolderDetails.exec() == QDialog::Accepted)
    {
        const QString strName = dlgFolderDetails.name();
        const QString strPath = dlgFolderDetails.path();
        const UISharedFolderType enmType = dlgFolderDetails.isPermanent() ? UISharedFolderType_Machine : UISharedFolderType_Console;
        /* Shared folder's name & path could not be empty: */
        Assert(!strName.isEmpty() && !strPath.isEmpty());

        /* Update edited tree-widget item: */
        pItem->m_enmType = enmType;
        pItem->m_strName = strName;
        pItem->m_strPath = strPath;
        pItem->m_fWritable = dlgFolderDetails.isWriteable();
        pItem->m_fAutoMount = dlgFolderDetails.isAutoMounted();
        pItem->m_strAutoMountPoint = dlgFolderDetails.autoMountPoint();
        pItem->updateFields();

        /* Searching for a root of the edited tree-widget item: */
        SFTreeViewItem *pRoot = root(enmType);
        if (pItem->parentItem() != pRoot)
        {
            /* Move the tree-widget item to a new location: */
            pItem->parentItem()->takeChild(pItem->parentItem()->indexOfChild(pItem));
            pRoot->insertChild(pRoot->childCount(), pItem);

            /* Update tree-widget: */
            m_pTreeWidget->scrollToItem(pItem);
            m_pTreeWidget->setCurrentItem(pItem);
            sltHandleCurrentItemChange(pItem);
        }

        /* Sort tree-widget before adjusting: */
        m_pTreeWidget->sortItems(0, Qt::AscendingOrder);
        /* Adjust tree-widget finally: */
        sltAdjustTree();
    }
}

void UISharedFoldersEditor::sltRemoveFolder()
{
    /* Check current folder item: */
    QTreeWidgetItem *pItem = m_pTreeWidget->currentItem();
    AssertPtrReturnVoid(pItem);

    /* Delete corresponding item: */
    delete pItem;

    /* Adjust tree-widget finally: */
    sltAdjustTree();
}

void UISharedFoldersEditor::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UISharedFoldersEditor::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare view layout: */
        m_pLayoutTree = new QHBoxLayout;
        if (m_pLayoutTree)
        {
            m_pLayoutTree->setContentsMargins(0, 0, 0, 0);
            m_pLayoutTree->setSpacing(3);

            /* Prepare tree-widget: */
            prepareTreeWidget();
            /* Prepare toolbar: */
            prepareToolbar();

            pLayout->addLayout(m_pLayoutTree);
        }
    }
}

void UISharedFoldersEditor::prepareTreeWidget()
{
    /* Prepare shared folders tree-widget: */
    m_pTreeWidget = new QITreeWidget(this);
    if (m_pTreeWidget)
    {
        m_pTreeWidget->header()->setSectionsMovable(false);
        m_pTreeWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Ignored);
        m_pTreeWidget->setMinimumHeight(150);
        m_pTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        m_pTreeWidget->setUniformRowHeights(true);
        m_pTreeWidget->setAllColumnsShowFocus(true);

        m_pLayoutTree->addWidget(m_pTreeWidget);
    }
}

void UISharedFoldersEditor::prepareToolbar()
{
    /* Prepare shared folders toolbar: */
    m_pToolbar = new QIToolBar(this);
    if (m_pToolbar)
    {
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_pToolbar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolbar->setOrientation(Qt::Vertical);

        /* Prepare 'add shared folder' action: */
        m_pActionAdd = m_pToolbar->addAction(UIIconPool::iconSet(":/sf_add_16px.png",
                                                                 ":/sf_add_disabled_16px.png"),
                                             QString(), this, SLOT(sltAddFolder()));
        if (m_pActionAdd)
            m_pActionAdd->setShortcuts(QList<QKeySequence>() << QKeySequence("Ins") << QKeySequence("Ctrl+N"));

        /* Prepare 'edit shared folder' action: */
        m_pActionEdit = m_pToolbar->addAction(UIIconPool::iconSet(":/sf_edit_16px.png",
                                                                  ":/sf_edit_disabled_16px.png"),
                                              QString(), this, SLOT(sltEditFolder()));
        if (m_pActionEdit)
            m_pActionEdit->setShortcuts(QList<QKeySequence>() << QKeySequence("Space") << QKeySequence("F2"));

        /* Prepare 'remove shared folder' action: */
        m_pActionRemove = m_pToolbar->addAction(UIIconPool::iconSet(":/sf_remove_16px.png",
                                                                    ":/sf_remove_disabled_16px.png"),
                                                QString(), this, SLOT(sltRemoveFolder()));
        if (m_pActionRemove)
            m_pActionRemove->setShortcuts(QList<QKeySequence>() << QKeySequence("Del") << QKeySequence("Ctrl+R"));

        m_pLayoutTree->addWidget(m_pToolbar);
    }
}

void UISharedFoldersEditor::prepareConnections()
{
    /* Configure tree-widget connections: */
    connect(m_pTreeWidget, &QITreeWidget::currentItemChanged,
            this, &UISharedFoldersEditor::sltHandleCurrentItemChange);
    connect(m_pTreeWidget, &QITreeWidget::itemDoubleClicked,
            this, &UISharedFoldersEditor::sltHandleDoubleClick);
    connect(m_pTreeWidget, &QITreeWidget::customContextMenuRequested,
            this, &UISharedFoldersEditor::sltHandleContextMenuRequest);
}

QStringList UISharedFoldersEditor::usedList(bool fIncludeSelected)
{
    /* Make the used names list: */
    QStringList list;
    QTreeWidgetItemIterator it(m_pTreeWidget);
    while (*it)
    {
        if ((*it)->parent() && (fIncludeSelected || !(*it)->isSelected()))
            list << static_cast<SFTreeViewItem*>(*it)->getText(0);
        ++it;
    }
    return list;
}

SFTreeViewItem *UISharedFoldersEditor::root(UISharedFolderType enmSharedFolderType)
{
    /* Search for the corresponding root item among all the top-level items: */
    SFTreeViewItem *pRootItem = 0;
    QTreeWidgetItem *pMainRootItem = m_pTreeWidget->invisibleRootItem();
    for (int iFolderTypeIndex = 0; iFolderTypeIndex < pMainRootItem->childCount(); ++iFolderTypeIndex)
    {
        /* Get iterated item: */
        SFTreeViewItem *pIteratedItem = static_cast<SFTreeViewItem*>(pMainRootItem->child(iFolderTypeIndex));
        /* If iterated item type is what we are looking for: */
        if (pIteratedItem->m_enmType == enmSharedFolderType)
        {
            /* Remember the item: */
            pRootItem = static_cast<SFTreeViewItem*>(pIteratedItem);
            /* And break further search: */
            break;
        }
    }
    /* Return root item: */
    return pRootItem;
}

void UISharedFoldersEditor::setRootItemVisible(UISharedFolderType enmSharedFolderType, bool fVisible)
{
    /* Search for the corresponding root item among all the top-level items: */
    SFTreeViewItem *pRootItem = root(enmSharedFolderType);
    /* If root item, we are looking for, still not found: */
    if (!pRootItem)
    {
        /* Create new shared folder type item: */
        pRootItem = new SFTreeViewItem(m_pTreeWidget, SFTreeViewItem::FormatType_EllipsisEnd);
        if (pRootItem)
        {
            /* Configure item: */
            pRootItem->m_enmType = enmSharedFolderType;
            switch (enmSharedFolderType)
            {
                case UISharedFolderType_Machine: pRootItem->m_strName = tr(" Machine Folders"); break;
                case UISharedFolderType_Console: pRootItem->m_strName = tr(" Transient Folders"); break;
                default: break;
            }
            pRootItem->updateFields();
        }
    }
    /* Expand/collaps it if necessary: */
    pRootItem->setExpanded(fVisible);
    /* And hide/show it if necessary: */
    pRootItem->setHidden(!fVisible);
}

void UISharedFoldersEditor::updateRootItemsVisibility()
{
    /* Update (show/hide) machine (permanent) root item: */
    setRootItemVisible(UISharedFolderType_Machine, m_foldersAvailable.value(UISharedFolderType_Machine));
    /* Update (show/hide) console (temporary) root item: */
    setRootItemVisible(UISharedFolderType_Console, m_foldersAvailable.value(UISharedFolderType_Console));
}

void UISharedFoldersEditor::addSharedFolderItem(const UIDataSharedFolder &sharedFolderData, bool fChoose)
{
    /* Create shared folder item: */
    SFTreeViewItem *pItem = new SFTreeViewItem(root(sharedFolderData.m_enmType), SFTreeViewItem::FormatType_EllipsisFile);
    if (pItem)
    {
        /* Configure item: */
        pItem->m_enmType = sharedFolderData.m_enmType;
        pItem->m_strName = sharedFolderData.m_strName;
        pItem->m_strPath = sharedFolderData.m_strPath;
        pItem->m_fWritable = sharedFolderData.m_fWritable;
        pItem->m_fAutoMount = sharedFolderData.m_fAutoMount;
        pItem->m_strAutoMountPoint = sharedFolderData.m_strAutoMountPoint;
        pItem->updateFields();

        /* Select this item if it's new: */
        if (fChoose)
        {
            m_pTreeWidget->scrollToItem(pItem);
            m_pTreeWidget->setCurrentItem(pItem);
            sltHandleCurrentItemChange(pItem);
        }
    }
}

void UISharedFoldersEditor::reloadTree()
{
    /* Sanity check: */
    if (!m_pTreeWidget)
        return;

    /* Clear list initially: */
    m_pTreeWidget->clear();

    /* Update root items visibility: */
    updateRootItemsVisibility();

    /* For each folder => load it from cache: */
    foreach (const UIDataSharedFolder &guiData, m_guiValue)
        addSharedFolderItem(guiData, false /* its new? */);

    /* Choose first folder as current: */
    m_pTreeWidget->setCurrentItem(m_pTreeWidget->topLevelItem(0));
    sltHandleCurrentItemChange(m_pTreeWidget->currentItem());
}


#include "UISharedFoldersEditor.moc"
