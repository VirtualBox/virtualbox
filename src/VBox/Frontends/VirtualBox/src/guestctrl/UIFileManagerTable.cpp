/* $Id: UIFileManagerTable.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileManagerTable class implementation.
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
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemDelegate>
#include <QGridLayout>
#include <QTextEdit>

/* GUI includes: */
#include "QIDialog.h"
#include "QIDialogButtonBox.h"
#include "QILabel.h"
#include "QILineEdit.h"
#include "QIToolBar.h"
#include "UIActionPool.h"
#include "UIFileSystemModel.h"
#include "UIErrorString.h"
#include "UIFileManager.h"
#include "UIFileManagerTable.h"
#include "UIFileTableNavigationWidget.h"
#include "UIIconPool.h"
#include "UIPathOperations.h"
#include "UITranslator.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CFsObjInfo.h"
#include "CGuestFsObjInfo.h"
#include "CGuestDirectory.h"
#include "CProgress.h"


/*********************************************************************************************************************************
*   UIGuestControlFileView definition.                                                                                           *
*********************************************************************************************************************************/

/** Using QTableView causes the following problem when I click on the table items
    Qt WARNING: Cannot creat accessible child interface for object:  UIGuestControlFileView.....
    so for now subclass QTableView */
class UIGuestControlFileView : public QTableView
{

    Q_OBJECT;

signals:

    void sigSelectionChanged(const QItemSelection & selected, const QItemSelection & deselected);
    /* Emitted upon mouse double click while Alt key is pressed.  */
    void sigAltDoubleClick();

public:

    UIGuestControlFileView(QWidget * parent = 0);
    bool hasSelection() const;
    bool isInEditState() const;

protected:

    virtual void selectionChanged(const QItemSelection & selected, const QItemSelection & deselected) RT_OVERRIDE RT_FINAL;
    virtual void mouseDoubleClickEvent(QMouseEvent *event) RT_OVERRIDE RT_FINAL;
    virtual void mousePressEvent(QMouseEvent *event) RT_OVERRIDE RT_FINAL;

private:

    void configure();
    QWidget *m_pParent;
};


/*********************************************************************************************************************************
*   UIFileDelegate definition.                                                                                                   *
*********************************************************************************************************************************/
/** A QItemDelegate child class to disable dashed lines drawn around selected cells in QTableViews */
class UIFileDelegate : public QItemDelegate
{

    Q_OBJECT;

public:

    UIFileDelegate(QObject *pParent)
        : QItemDelegate(pParent){}

protected:

    virtual void drawFocus ( QPainter * /*painter*/, const QStyleOptionViewItem & /*option*/, const QRect & /*rect*/ ) const  RT_OVERRIDE RT_FINAL{}
};


/*********************************************************************************************************************************
*   UStringInputDialog definition.                                                                                               *
*********************************************************************************************************************************/

/** A QIDialog child including a line edit whose text exposed when the dialog is accepted */
class UIStringInputDialog : public QIDialog
{

    Q_OBJECT;

public:

    UIStringInputDialog(QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());
    QString getString() const;

private:

    QILineEdit      *m_pLineEdit;
};


/*********************************************************************************************************************************
*   UIFileDeleteConfirmationDialog definition.                                                                                   *
*********************************************************************************************************************************/

/** A QIDialog child including a line edit whose text exposed when the dialog is accepted */
class UIFileDeleteConfirmationDialog : public QIDialog
{

    Q_OBJECT;

public:

    UIFileDeleteConfirmationDialog(QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());
    /** Returns whether m_pAskNextTimeCheckBox is checked or not. */
    bool askDeleteConfirmationNextTime() const;

private:

    QCheckBox *m_pAskNextTimeCheckBox;
    QILabel   *m_pQuestionLabel;
};


/*********************************************************************************************************************************
*   UIHostDirectoryDiskUsageComputer implementation.                                                                             *
*********************************************************************************************************************************/

UIDirectoryDiskUsageComputer::UIDirectoryDiskUsageComputer(QObject *parent, QStringList pathList)
    :QThread(parent)
    , m_pathList(pathList)
    , m_fOkToContinue(true)
{
}

void UIDirectoryDiskUsageComputer::run()
{
    for (int i = 0; i < m_pathList.size(); ++i)
        directoryStatisticsRecursive(m_pathList[i], m_resultStatistics);
}

void UIDirectoryDiskUsageComputer::stopRecursion()
{
    m_mutex.lock();
    m_fOkToContinue = false;
    m_mutex.unlock();
}

bool UIDirectoryDiskUsageComputer::isOkToContinue() const
{
    return m_fOkToContinue;
}


/*********************************************************************************************************************************
*   UIGuestControlFileView implementation.                                                                                       *
*********************************************************************************************************************************/

UIGuestControlFileView::UIGuestControlFileView(QWidget *parent)
    :QTableView(parent)
    , m_pParent(parent)
{
    configure();
}

void UIGuestControlFileView::configure()
{
    setContextMenuPolicy(Qt::CustomContextMenu);
    setShowGrid(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    verticalHeader()->setVisible(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    /* Minimize the row height: */
    verticalHeader()->setDefaultSectionSize(verticalHeader()->minimumSectionSize());
    setAlternatingRowColors(true);
    installEventFilter(m_pParent);
}

bool UIGuestControlFileView::hasSelection() const
{
    QItemSelectionModel *pSelectionModel =  selectionModel();
    if (!pSelectionModel)
        return false;
    return pSelectionModel->hasSelection();
}

bool UIGuestControlFileView::isInEditState() const
{
    return state() == QAbstractItemView::EditingState;
}

void UIGuestControlFileView::selectionChanged(const QItemSelection & selected, const QItemSelection & deselected)
{
    emit sigSelectionChanged(selected, deselected);
    QTableView::selectionChanged(selected, deselected);
}

void UIGuestControlFileView::mousePressEvent(QMouseEvent *event)
{
    if (qApp->queryKeyboardModifiers() & Qt::AltModifier)
        return;
    QTableView::mousePressEvent(event);
}

void UIGuestControlFileView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (qApp->queryKeyboardModifiers() & Qt::AltModifier)
    {
        printf("dou\n");
        emit sigAltDoubleClick();
        return;
    }
    QTableView::mouseDoubleClickEvent(event);
}


/*********************************************************************************************************************************
*   UIFileStringInputDialog implementation.                                                                                      *
*********************************************************************************************************************************/

UIStringInputDialog::UIStringInputDialog(QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    :QIDialog(pParent, enmFlags)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    m_pLineEdit = new QILineEdit(this);
    layout->addWidget(m_pLineEdit);

    QIDialogButtonBox *pButtonBox =
        new QIDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    layout->addWidget(pButtonBox);
    connect(pButtonBox, &QIDialogButtonBox::accepted, this, &UIStringInputDialog::accept);
    connect(pButtonBox, &QIDialogButtonBox::rejected, this, &UIStringInputDialog::reject);
}

QString UIStringInputDialog::getString() const
{
    if (!m_pLineEdit)
        return QString();
    return m_pLineEdit->text();
}


/*********************************************************************************************************************************
*   UIPropertiesDialog implementation.                                                                                           *
*********************************************************************************************************************************/

UIPropertiesDialog::UIPropertiesDialog(QWidget *pParent, Qt::WindowFlags enmFlags)
    :QIDialog(pParent, enmFlags)
    , m_pMainLayout(new QVBoxLayout)
    , m_pInfoEdit(new QTextEdit)
{
    setLayout(m_pMainLayout);

    if (m_pMainLayout)
        m_pMainLayout->addWidget(m_pInfoEdit);
    if (m_pInfoEdit)
    {
        m_pInfoEdit->setReadOnly(true);
        m_pInfoEdit->setFrameStyle(QFrame::NoFrame);
    }
    QIDialogButtonBox *pButtonBox =
        new QIDialogButtonBox(QDialogButtonBox::Ok, Qt::Horizontal, this);
    m_pMainLayout->addWidget(pButtonBox);
    connect(pButtonBox, &QIDialogButtonBox::accepted, this, &UIStringInputDialog::accept);
}

void UIPropertiesDialog::setPropertyText(const QString &strProperty)
{
    if (!m_pInfoEdit)
        return;
    m_strProperty = strProperty;
    m_pInfoEdit->setHtml(strProperty);
}

void UIPropertiesDialog::addDirectoryStatistics(UIDirectoryStatistics directoryStatistics)
{
    if (!m_pInfoEdit)
        return;
    // QString propertyString = m_pInfoEdit->toHtml();
    // propertyString += "<b>Total Size:</b> " + QString::number(directoryStatistics.m_totalSize) + QString(" bytes");
    // if (directoryStatistics.m_totalSize >= UIFileManagerTable::m_iKiloByte)
    //     propertyString += " (" + UIFileManagerTable::humanReadableSize(directoryStatistics.m_totalSize) + ")";
    // propertyString += "<br/>";
    // propertyString += "<b>File Count:</b> " + QString::number(directoryStatistics.m_uFileCount);

    // m_pInfoEdit->setHtml(propertyString);

    QString detailsString(m_strProperty);
    detailsString += "<br/>";
    detailsString += "<b>" + UIFileManager::tr("Total Size") + "</b> " +
        QString::number(directoryStatistics.m_totalSize) + UIFileManager::tr(" bytes");
    if (directoryStatistics.m_totalSize >= UIFileManagerTable::m_iKiloByte)
        detailsString += " (" + UIFileManagerTable::humanReadableSize(directoryStatistics.m_totalSize) + ")";
    detailsString += "<br/>";

    detailsString += "<b>" + UIFileManager::tr("File Count") + ":</b> " +
        QString::number(directoryStatistics.m_uFileCount);

    m_pInfoEdit->setHtml(detailsString);
}


/*********************************************************************************************************************************
*   UIDirectoryStatistics implementation.                                                                                        *
*********************************************************************************************************************************/

UIDirectoryStatistics::UIDirectoryStatistics()
    : m_totalSize(0)
    , m_uFileCount(0)
    , m_uDirectoryCount(0)
    , m_uSymlinkCount(0)
{
}


/*********************************************************************************************************************************
*   UIFileDeleteConfirmationDialog implementation.                                                                               *
*********************************************************************************************************************************/

UIFileDeleteConfirmationDialog::UIFileDeleteConfirmationDialog(QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    :QIDialog(pParent, enmFlags)
    , m_pAskNextTimeCheckBox(0)
    , m_pQuestionLabel(0)
{
    QVBoxLayout *pLayout = new QVBoxLayout(this);

    m_pQuestionLabel = new QILabel;
    if (m_pQuestionLabel)
    {
        pLayout->addWidget(m_pQuestionLabel);
        m_pQuestionLabel->setText(UIFileManager::tr("Delete the selected file(s) and/or folder(s)"));
    }

    QIDialogButtonBox *pButtonBox =
        new QIDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    if (pButtonBox)
    {
        pLayout->addWidget(pButtonBox, 0, Qt::AlignCenter);
        connect(pButtonBox, &QIDialogButtonBox::accepted, this, &UIStringInputDialog::accept);
        connect(pButtonBox, &QIDialogButtonBox::rejected, this, &UIStringInputDialog::reject);
    }

    m_pAskNextTimeCheckBox = new QCheckBox;

    if (m_pAskNextTimeCheckBox)
    {
        UIFileManagerOptions *pFileManagerOptions = UIFileManagerOptions::instance();
        if (pFileManagerOptions)
            m_pAskNextTimeCheckBox->setChecked(pFileManagerOptions->fAskDeleteConfirmation);

        pLayout->addWidget(m_pAskNextTimeCheckBox);
        m_pAskNextTimeCheckBox->setText(UIFileManager::tr("Ask for this confirmation next time"));
        m_pAskNextTimeCheckBox->setToolTip(UIFileManager::tr("Delete confirmation can be "
                                                                         "disabled/enabled also from the Options panel."));
    }
}

bool UIFileDeleteConfirmationDialog::askDeleteConfirmationNextTime() const
{
    if (!m_pAskNextTimeCheckBox)
        return true;
    return m_pAskNextTimeCheckBox->isChecked();
}


/*********************************************************************************************************************************
*   UIFileManagerTable implementation.                                                                                           *
*********************************************************************************************************************************/
const unsigned UIFileManagerTable::m_iKiloByte = 1024; /**< Our kilo bytes are a power of two! (bird) */

UIFileManagerTable::UIFileManagerTable(UIActionPool *pActionPool, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_eFileOperationType(FileOperationType_None)
    , m_pLocationLabel(0)
    , m_pPropertiesDialog(0)
    , m_pActionPool(pActionPool)
    , m_pToolBar(0)
    , m_pMainLayout(0)
    , m_pNavigationWidget(0)
    , m_pModel(0)
    , m_pView(0)
    , m_pProxyModel(0)
    , m_pathSeparator('/')
    , m_pToolBarLayout(0)
{
    prepareObjects();
}

UIFileManagerTable::~UIFileManagerTable()
{
}

void UIFileManagerTable::reset()
{
    if (m_pModel)
        m_pModel->reset();

    if (m_pNavigationWidget)
        m_pNavigationWidget->reset();
}

void UIFileManagerTable::prepareObjects()
{
    m_pMainLayout = new QGridLayout();
    if (!m_pMainLayout)
        return;
    m_pMainLayout->setSpacing(0);
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(m_pMainLayout);

    m_pToolBarLayout = new QHBoxLayout;
    if (m_pToolBarLayout)
    {
        m_pToolBarLayout->setSpacing(0);
        m_pToolBarLayout->setContentsMargins(0, 0, 0, 0);

        m_pToolBar = new QIToolBar;
        if (m_pToolBar)
        {
            m_pToolBarLayout->addWidget(m_pToolBar);
            m_sessionWidgets << m_pToolBar;
        }

        m_pMainLayout->addLayout(m_pToolBarLayout, 0, 0, 1, 7);
    }

    m_pLocationLabel = new QILabel;
    if (m_pLocationLabel)
    {
        m_pMainLayout->addWidget(m_pLocationLabel, 1, 0, 1, 1);
        m_sessionWidgets << m_pLocationLabel;
    }

    m_pNavigationWidget = new UIFileTableNavigationWidget;
    if (m_pNavigationWidget)
    {
        m_pNavigationWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Maximum);
        connect(m_pNavigationWidget, &UIFileTableNavigationWidget::sigPathChanged,
                this, &UIFileManagerTable::sltHandleNavigationWidgetPathChange);
        connect(m_pNavigationWidget, &UIFileTableNavigationWidget::sigHistoryListChanged,
                this, &UIFileManagerTable::sltHandleNavigationWidgetHistoryListChanged);
        m_pMainLayout->addWidget(m_pNavigationWidget, 1, 1, 1, 6);
        m_sessionWidgets << m_pNavigationWidget;
    }

    m_pModel = new UIFileSystemModel(this);
    if (!m_pModel)
        return;

    connect(m_pModel, &UIFileSystemModel::sigItemRenamed,
            this, &UIFileManagerTable::sltHandleItemRenameAttempt);

    m_pProxyModel = new UIFileSystemProxyModel(this);
    if (!m_pProxyModel)
        return;
    m_pProxyModel->setSourceModel(m_pModel);

    m_pView = new UIGuestControlFileView(this);
    if (m_pView)
    {
        m_pView->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
        m_pMainLayout->addWidget(m_pView, 2, 0, 5, 7);

        QHeaderView *pHorizontalHeader = m_pView->horizontalHeader();
        if (pHorizontalHeader)
        {
            pHorizontalHeader->setHighlightSections(false);
            pHorizontalHeader->setSectionResizeMode(QHeaderView::ResizeToContents);
            pHorizontalHeader->setStretchLastSection(true);
        }

        m_pView->setModel(m_pProxyModel);
        m_pView->setItemDelegate(new UIFileDelegate(this));
        m_pView->setSortingEnabled(true);
        m_pView->sortByColumn(0, Qt::AscendingOrder);

        connect(m_pView, &UIGuestControlFileView::doubleClicked,
                this, &UIFileManagerTable::sltItemDoubleClicked);
        connect(m_pView, &UIGuestControlFileView::sigAltDoubleClick,
                this, &UIFileManagerTable::sltAltDoubleClick);
        connect(m_pView, &UIGuestControlFileView::clicked,
                this, &UIFileManagerTable::sltItemClicked);
        connect(m_pView, &UIGuestControlFileView::sigSelectionChanged,
                this, &UIFileManagerTable::sltSelectionChanged);
        connect(m_pView, &UIGuestControlFileView::customContextMenuRequested,
                this, &UIFileManagerTable::sltCreateFileViewContextMenu);
        m_pView->hideColumn(UIFileSystemModelData_LocalPath);
        m_pView->hideColumn(UIFileSystemModelData_ISOFilePath);
        m_pView->hideColumn(UIFileSystemModelData_RemovedFromVISO);
        m_pView->hideColumn(UIFileSystemModelData_DescendantRemovedFromVISO);

        m_sessionWidgets << m_pView;
    }

    m_pSearchLineEdit = new QILineEdit;
    if (m_pSearchLineEdit)
    {
        m_pMainLayout->addWidget(m_pSearchLineEdit, 8, 0, 1, 7);
        m_pSearchLineEdit->hide();
        m_pSearchLineEdit->setClearButtonEnabled(true);
        m_searchLineUnmarkColor = m_pSearchLineEdit->palette().color(QPalette::Base);
        m_searchLineMarkColor = QColor(m_searchLineUnmarkColor.green(),
                                       0.5 * m_searchLineUnmarkColor.green(),
                                       0.5 * m_searchLineUnmarkColor.blue());
        connect(m_pSearchLineEdit, &QLineEdit::textChanged,
                this, &UIFileManagerTable::sltSearchTextChanged);
    }

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIFileManagerTable::sltRetranslateUI);

    optionsUpdated();
}

void UIFileManagerTable::updateCurrentLocationEdit(const QString& strLocation)
{
    if (m_pNavigationWidget)
        m_pNavigationWidget->setPath(strLocation);
}

void UIFileManagerTable::changeLocation(const QModelIndex &index)
{
    if (!index.isValid() || !m_pView)
        return;
    m_pView->setRootIndex(m_pProxyModel->mapFromSource(index));

    if (m_pView->selectionModel())
        m_pView->selectionModel()->reset();

    UIFileSystemItem *item = static_cast<UIFileSystemItem*>(index.internalPointer());
    if (item)
    {
        updateCurrentLocationEdit(item->path());
    }
    setSelectionDependentActionsEnabled(false);

    m_pView->scrollToTop();
}

void UIFileManagerTable::initializeFileTree()
{
    if (m_pModel)
        m_pModel->reset();
    if (!rootItem())
        return;

    QString startPath("/");
    /* On Unix-like systems startItem represents the root directory. On Windows it is like "my computer" under which
     * drives are listed: */
    UIFileSystemItem* startItem = new UIFileSystemItem(startPath, rootItem(), KFsObjType_Directory);

    startItem->setIsOpened(false);
    populateStartDirectory(startItem);

    m_pModel->signalUpdate();
    m_pView->setRootIndex(m_pProxyModel->mapFromSource(m_pModel->rootIndex()));
    updateCurrentLocationEdit(currentDirectoryPath());
}

void UIFileManagerTable::populateStartDirectory(UIFileSystemItem *startItem)
{
    determineDriveLetters();
    if (m_driveLetterList.isEmpty())
    {
        /* Read the root directory and get the list: */
        readDirectory(startItem->path(), startItem, true);
    }
    else
    {
        for (int i = 0; i < m_driveLetterList.size(); ++i)
        {
            UIFileSystemItem* driveItem = new UIFileSystemItem(UIPathOperations::removeTrailingDelimiters(m_driveLetterList[i]),
                                                                           startItem, KFsObjType_Directory);
            driveItem->setIsOpened(false);
            driveItem->setIsDriveItem(true);
            startItem->setIsOpened(true);
        }
    }
}

void UIFileManagerTable::checkDotDot(QMap<QString,UIFileSystemItem*> &map,
                                     UIFileSystemItem *parent, bool isStartDir)
{
    if (!parent)
        return;
    /* Make sure we have an item representing up directory, and make sure it is not there for the start dir: */
    if (!map.contains(UIFileSystemModel::strUpDirectoryString)  && !isStartDir)
    {
        UIFileSystemItem *item = new UIFileSystemItem(UIFileSystemModel::strUpDirectoryString,
                                                                  parent, KFsObjType_Directory);
        item->setIsOpened(false);
        map.insert(UIFileSystemModel::strUpDirectoryString, item);
    }
    else if (map.contains(UIFileSystemModel::strUpDirectoryString)  && isStartDir)
    {
        map.remove(UIFileSystemModel::strUpDirectoryString);
    }
}

void UIFileManagerTable::sltItemDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid() ||  !m_pModel || !m_pView)
        return;
    QModelIndex nIndex = m_pProxyModel ? m_pProxyModel->mapToSource(index) : index;
    goIntoDirectory(nIndex);
}

void UIFileManagerTable::sltAltDoubleClick()
{
    emit sigAltDoubleClick();
}

void UIFileManagerTable::sltItemClicked(const QModelIndex &index)
{
    Q_UNUSED(index);
    disableSelectionSearch();
}

void UIFileManagerTable::sltGoUp()
{
    if (!m_pView || !m_pModel)
        return;
    QModelIndex currentRoot = currentRootIndex();

    if (!currentRoot.isValid())
        return;
    if (currentRoot != m_pModel->rootIndex())
    {
        QModelIndex parentIndex = currentRoot.parent();
        if (parentIndex.isValid())
        {
            changeLocation(currentRoot.parent());
            m_pView->selectRow(currentRoot.row());
        }
    }
}

void UIFileManagerTable::sltGoHome()
{
    goToHomeDirectory();
}

void UIFileManagerTable::sltGoForward()
{
    if (m_pNavigationWidget)
        m_pNavigationWidget->goForwardInHistory();
}

void UIFileManagerTable::sltGoBackward()
{
    if (m_pNavigationWidget)
        m_pNavigationWidget->goBackwardInHistory();
}

void UIFileManagerTable::sltRefresh()
{
    refresh();
}

void UIFileManagerTable::goIntoDirectory(const QModelIndex &itemIndex)
{
    if (!m_pModel)
        return;

    /* Make sure the colum is 0: */
    QModelIndex index = m_pModel->index(itemIndex.row(), 0, itemIndex.parent());
    if (!index.isValid())
        return;

    UIFileSystemItem *item = static_cast<UIFileSystemItem*>(index.internalPointer());
    if (!item)
        return;

    /* check if we need to go up: */
    if (item->isUpDirectory())
    {
        QModelIndex parentIndex = m_pModel->parent(m_pModel->parent(index));
        if (parentIndex.isValid())
            changeLocation(parentIndex);
        return;
    }

    if (item->isDirectory() || item->isSymLinkToADirectory())
    {
        if (!item->isOpened())
        {
            if (readDirectory(item->path(),item))
                changeLocation(index);
        }
        else
            changeLocation(index);
    }
}

void UIFileManagerTable::goIntoDirectory(const QStringList &pathTrail)
{
    UIFileSystemItem *parent = getStartDirectoryItem();

    for(int i = 0; i < pathTrail.size(); ++i)
    {
        if (!parent)
            return;
        /* Make sure parent is already opened: */
        if (!parent->isOpened())
            if (!readDirectory(parent->path(), parent, parent == getStartDirectoryItem()))
                return;
        /* search the current path item among the parent's children: */
        UIFileSystemItem *item = parent->child(pathTrail.at(i));

        if (!item)
            return;
        parent = item;
    }
    if (!parent)
        return;
    if (!parent->isOpened())
    {
        if (!readDirectory(parent->path(), parent, parent == getStartDirectoryItem()))
            return;
    }
    goIntoDirectory(parent);
}

void UIFileManagerTable::goIntoDirectory(UIFileSystemItem *item)
{
    if (!item || !m_pModel)
        return;
    goIntoDirectory(m_pModel->index(item));
}

UIFileSystemItem* UIFileManagerTable::indexData(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;
    return static_cast<UIFileSystemItem*>(index.internalPointer());
}

void UIFileManagerTable::refresh()
{
    if (!m_pView || !m_pModel)
        return;
    QModelIndex currentIndex = currentRootIndex();

    UIFileSystemItem *treeItem = indexData(currentIndex);
    if (!treeItem)
        return;
    bool isRootDir = (m_pModel->rootIndex() == currentIndex);
    m_pModel->beginReset();
    /* For now we clear the whole subtree (that isrecursively) which is an overkill: */
    treeItem->clearChildren();
    if (isRootDir)
        populateStartDirectory(treeItem);
    else
        readDirectory(treeItem->path(), treeItem, isRootDir);
    m_pModel->endReset();
    m_pView->setRootIndex(m_pProxyModel->mapFromSource(currentIndex));
    setSelectionDependentActionsEnabled(m_pView->hasSelection());
}

void UIFileManagerTable::relist()
{
    if (!m_pProxyModel)
        return;
    m_pProxyModel->invalidate();
}

void UIFileManagerTable::sltDelete()
{
    if (!checkIfDeleteOK())
        return;

    if (!m_pView || !m_pModel)
        return;

    if (!m_pView || !m_pModel)
        return;
    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
    if (!selectionModel)
        return;

    QModelIndexList selectedItemIndices = selectionModel->selectedRows();
    for(int i = 0; i < selectedItemIndices.size(); ++i)
    {
        QModelIndex index =
            m_pProxyModel ? m_pProxyModel->mapToSource(selectedItemIndices.at(i)) : selectedItemIndices.at(i);
        deleteByIndex(index);
    }
    /** @todo dont refresh here, just delete the rows and update the table view: */
    refresh();
}

void UIFileManagerTable::sltRename()
{
    if (!m_pView || !m_pModel)
        return;
    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
    if (!selectionModel)
        return;

    QModelIndexList selectedItemIndices = selectionModel->selectedRows();
    if (selectedItemIndices.size() == 0)
        return;
    QModelIndex modelIndex =
        m_pProxyModel ? m_pProxyModel->mapToSource(selectedItemIndices.at(0)) : selectedItemIndices.at(0);
    UIFileSystemItem *item = indexData(modelIndex);
    if (!item || item->isUpDirectory())
        return;
    m_pView->edit(selectedItemIndices.at(0));
}

void UIFileManagerTable::sltCreateNewDirectory()
{
    if (!m_pModel || !m_pView)
        return;
    QModelIndex currentIndex = currentRootIndex();
    if (!currentIndex.isValid())
        return;
    UIFileSystemItem *parentFolderItem = static_cast<UIFileSystemItem*>(currentIndex.internalPointer());
    if (!parentFolderItem)
        return;
    QString strBase(UIFileManager::tr("NewDirectory"));
    QString newDirectoryName(strBase);
    QStringList nameList = currentDirectoryListing();
    int iSuffix = 1;
    while (nameList.contains(newDirectoryName))
        newDirectoryName = QString("%1_%2").arg(strBase).arg(QString::number(iSuffix++));

    if (!createDirectory(parentFolderItem->path(), newDirectoryName))
        return;

    /* Refesh the current directory so that we correctly populate the child list of parentFolderItem: */
    /** @todo instead of refreshing here (an overkill) just add the
        rows and update the tabel view: */
    sltRefresh();

    /* Now we try to edit the newly created item thereby enabling the user to rename the new item: */
    QList<UIFileSystemItem*> content = parentFolderItem->children();
    UIFileSystemItem* newItem = 0;
    /* Search the new item: */
    foreach (UIFileSystemItem* childItem, content)
    {

        if (childItem && newDirectoryName == childItem->fileObjectName())
            newItem = childItem;
    }

    if (!newItem)
        return;
    QModelIndex newItemIndex = m_pProxyModel->mapFromSource(m_pModel->index(newItem));
    if (!newItemIndex.isValid())
        return;
    m_pView->edit(newItemIndex);
}

void UIFileManagerTable::sltCopy()
{
    m_copyCutBuffer = selectedItemPathList();
    m_eFileOperationType = FileOperationType_Copy;
    setPasteActionEnabled(true);
}

void UIFileManagerTable::sltCut()
{
    m_copyCutBuffer = selectedItemPathList();
    m_eFileOperationType = FileOperationType_Cut;
    setPasteActionEnabled(true);
}

void UIFileManagerTable::sltPaste()
{
    m_copyCutBuffer.clear();

    m_eFileOperationType = FileOperationType_None;
    setPasteActionEnabled(false);
}

void UIFileManagerTable::sltShowProperties()
{
    showProperties();
}

void UIFileManagerTable::sltSelectionChanged(const QItemSelection & selected, const QItemSelection & deselected)
{
    Q_UNUSED(selected);
    Q_UNUSED(deselected);
    setSelectionDependentActionsEnabled(m_pView->hasSelection());
}

void UIFileManagerTable::sltSelectAll()
{
    if (!m_pModel || !m_pView)
        return;
    m_pView->selectAll();
    deSelectUpDirectoryItem();
}

void UIFileManagerTable::sltInvertSelection()
{
    setSelectionForAll(QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    deSelectUpDirectoryItem();
}

void UIFileManagerTable::sltSearchTextChanged(const QString &strText)
{
    performSelectionSearch(strText);
}

void UIFileManagerTable::sltHandleItemRenameAttempt(UIFileSystemItem *pItem, const QString &strOldPath,
                                                    const QString &strOldName, const QString &strNewName)
{
    Q_UNUSED(strNewName);
    if (!pItem)
        return;
    /* Attempt to chage item name in the file system: */
    if (!renameItem(pItem, strOldPath))
    {
        /* Restore the previous name. relist the view: */
        pItem->setData(strOldName, static_cast<int>(UIFileSystemModelData_Name));
        relist();
        emit sigLogOutput(QString(pItem->path()).append(" could not be renamed"), QString(), FileManagerLogType_Error);
    }
}

void UIFileManagerTable::sltCreateFileViewContextMenu(const QPoint &point)
{
    QWidget *pSender = qobject_cast<QWidget*>(sender());
    if (!pSender)
        return;
    createFileViewContextMenu(pSender, point);
}

void UIFileManagerTable::sltHandleNavigationWidgetPathChange(const QString& strPath)
{
    goIntoDirectory(UIPathOperations::pathTrail(strPath));
}

void UIFileManagerTable::sltHandleNavigationWidgetHistoryListChanged()
{
    toggleForwardBackwardActions();
}

void UIFileManagerTable::deSelectUpDirectoryItem()
{
    if (!m_pView)
        return;
    QItemSelectionModel *pSelectionModel = m_pView->selectionModel();
    if (!pSelectionModel)
        return;
    QModelIndex currentRoot = currentRootIndex();
    if (!currentRoot.isValid())
        return;

    /* Make sure that "up directory item" (if exists) is deselected: */
    for (int i = 0; i < m_pModel->rowCount(currentRoot); ++i)
    {
        QModelIndex index = m_pModel->index(i, 0, currentRoot);
        if (!index.isValid())
            continue;

        UIFileSystemItem *item = static_cast<UIFileSystemItem*>(index.internalPointer());
        if (item && item->isUpDirectory())
        {
            QModelIndex indexToDeselect = m_pProxyModel ? m_pProxyModel->mapFromSource(index) : index;
            pSelectionModel->select(indexToDeselect, QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
        }
    }
}

void UIFileManagerTable::setSelectionForAll(QItemSelectionModel::SelectionFlags flags)
{
    if (!m_pView)
        return;
    QItemSelectionModel *pSelectionModel = m_pView->selectionModel();
    if (!pSelectionModel)
        return;
    QModelIndex currentRoot = currentRootIndex();
    if (!currentRoot.isValid())
        return;

    for (int i = 0; i < m_pModel->rowCount(currentRoot); ++i)
    {
        QModelIndex index = m_pModel->index(i, 0, currentRoot);
        if (!index.isValid())
            continue;
        QModelIndex indexToSelect = m_pProxyModel ? m_pProxyModel->mapFromSource(index) : index;
        pSelectionModel->select(indexToSelect, flags);
    }
}

void UIFileManagerTable::setSelection(const QModelIndex &indexInProxyModel)
{
    if (!m_pView)
        return;
    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
    if (!selectionModel)
        return;
    selectionModel->select(indexInProxyModel, QItemSelectionModel::Current | QItemSelectionModel::Rows | QItemSelectionModel::Select);
    m_pView->scrollTo(indexInProxyModel, QAbstractItemView::EnsureVisible);
}

void UIFileManagerTable::deleteByIndex(const QModelIndex &itemIndex)
{
    UIFileSystemItem *treeItem = indexData(itemIndex);
    if (!treeItem)
        return;
    deleteByItem(treeItem);
}

void UIFileManagerTable::sltRetranslateUI()
{
    UIFileSystemItem *pRootItem = rootItem();
    if (pRootItem)
    {
        pRootItem->setData(UIFileManager::tr("Name"), UIFileSystemModelData_Name);
        pRootItem->setData(UIFileManager::tr("Size"), UIFileSystemModelData_Size);
        pRootItem->setData(UIFileManager::tr("Change Time"), UIFileSystemModelData_ChangeTime);
        pRootItem->setData(UIFileManager::tr("Owner"), UIFileSystemModelData_Owner);
        pRootItem->setData(UIFileManager::tr("Permissions"), UIFileSystemModelData_Permissions);
    }
}

bool UIFileManagerTable::eventFilter(QObject *pObject, QEvent *pEvent) /* override */
{
    /* Handle only events sent to m_pView: */
    if (pObject != m_pView)
        return QWidget::eventFilter(pObject, pEvent);

    if (pEvent->type() == QEvent::KeyPress)
    {
        QKeyEvent *pKeyEvent = dynamic_cast<QKeyEvent*>(pEvent);
        if (pKeyEvent)
        {
            if (pKeyEvent->key() == Qt::Key_Enter || pKeyEvent->key() == Qt::Key_Return)
            {
                if (m_pView && m_pModel && !m_pView->isInEditState())
                {
                    /* Get the selected item. If there are 0 or more than 1 selection do nothing: */
                    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
                    if (selectionModel)
                    {
                        QModelIndexList selectedItemIndices = selectionModel->selectedRows();
                        if (selectedItemIndices.size() == 1 && m_pModel)
                            goIntoDirectory( m_pProxyModel->mapToSource(selectedItemIndices.at(0)));
                    }
                }
                return true;
            }
            else if (pKeyEvent->key() == Qt::Key_Delete)
            {
                sltDelete();
                return true;
            }
            else if (pKeyEvent->key() == Qt::Key_Backspace)
            {
                sltGoUp();
                return true;
            }
            else if (pKeyEvent->text().length() == 1 &&
                     (pKeyEvent->text().at(0).isDigit() ||
                      pKeyEvent->text().at(0).isLetter()))
            {
                if (m_pSearchLineEdit)
                {
                    markUnmarkSearchLineEdit(false);
                    m_pSearchLineEdit->clear();
                    m_pSearchLineEdit->show();
                    m_pSearchLineEdit->setFocus();
                    m_pSearchLineEdit->setText(pKeyEvent->text());
                }
            }
            else if (pKeyEvent->key() == Qt::Key_Tab)
            {
                return true;
            }
        }
    }
    else if (pEvent->type() == QEvent::FocusOut)
    {
        disableSelectionSearch();
    }

    /* Call to base-class: */
    return QWidget::eventFilter(pObject, pEvent);
}

UIFileSystemItem *UIFileManagerTable::getStartDirectoryItem()
{
    UIFileSystemItem* pRootItem = rootItem();
    if (!pRootItem)
        return 0;
    if (pRootItem->childCount() <= 0)
        return 0;
    return pRootItem->child(0);
}

QString UIFileManagerTable::currentDirectoryPath() const
{
    if (!m_pView)
        return QString();
    QModelIndex currentRoot = currentRootIndex();
    if (!currentRoot.isValid())
        return QString();
    UIFileSystemItem *item = static_cast<UIFileSystemItem*>(currentRoot.internalPointer());
    if (!item)
        return QString();
    /* be paranoid: */
    if (!item->isDirectory())
        return QString();
    return item->path();
}

QStringList UIFileManagerTable::selectedItemPathList()
{
    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
    if (!selectionModel)
        return QStringList();

    QStringList pathList;
    QModelIndexList selectedItemIndices = selectionModel->selectedRows();
    for(int i = 0; i < selectedItemIndices.size(); ++i)
    {
        QModelIndex index =
            m_pProxyModel ? m_pProxyModel->mapToSource(selectedItemIndices.at(i)) : selectedItemIndices.at(i);
        UIFileSystemItem *item = static_cast<UIFileSystemItem*>(index.internalPointer());
        if (!item)
            continue;
        pathList.push_back(item->path());
    }
    return pathList;
}

CGuestFsObjInfo UIFileManagerTable::guestFsObjectInfo(const QString& path, CGuestSession &comGuestSession) const
{
    if (comGuestSession.isNull())
        return CGuestFsObjInfo();
    CGuestFsObjInfo comFsObjInfo = comGuestSession.FsObjQueryInfo(path, true /*aFollowSymlinks*/);
    if (!comFsObjInfo.isOk())
        return CGuestFsObjInfo();
    return comFsObjInfo;
}

void UIFileManagerTable::setSelectionDependentActionsEnabled(bool fIsEnabled)
{
    foreach (QAction *pAction, m_selectionDependentActions)
        pAction->setEnabled(fIsEnabled);
    if (m_pView)
        emit sigSelectionChanged(m_pView->hasSelection());
}

UIFileSystemItem* UIFileManagerTable::rootItem()
{
    if (!m_pModel)
        return 0;
    return m_pModel->rootItem();
}

void UIFileManagerTable::setPathSeparator(const QChar &separator)
{
    m_pathSeparator = separator;
    if (m_pNavigationWidget)
        m_pNavigationWidget->setPathSeparator(m_pathSeparator);
}

QHBoxLayout* UIFileManagerTable::toolBarLayout()
{
    return m_pToolBarLayout;
}

bool UIFileManagerTable::event(QEvent *pEvent)
{
    if (pEvent->type() == QEvent::EnabledChange)
        sltRetranslateUI();
    return QWidget::event(pEvent);
}

QString UIFileManagerTable::fileTypeString(KFsObjType type)
{
    QString strType = UIFileManager::tr("Unknown");
    switch (type)
    {
        case KFsObjType_File:
            strType = UIFileManager::tr("File");
            break;
        case KFsObjType_Directory:
            strType = UIFileManager::tr("Directory");
            break;
        case KFsObjType_Symlink:
            strType = UIFileManager::tr("Symbolic Link");
            break;
        case KFsObjType_Unknown:
        default:
            strType = UIFileManager::tr("Unknown");
            break;
    }
    return strType;
}

/* static */ QString UIFileManagerTable::humanReadableSize(ULONG64 size)
{
    return UITranslator::formatSize(size);
}

void UIFileManagerTable::optionsUpdated()
{
    UIFileManagerOptions *pOptions = UIFileManagerOptions::instance();
    if (pOptions)
    {
        if (m_pProxyModel)
        {
            m_pProxyModel->setListDirectoriesOnTop(pOptions->fListDirectoriesOnTop);
            m_pProxyModel->setShowHiddenObjects(pOptions->fShowHiddenObjects);
        }
        if (m_pModel)
            m_pModel->setShowHumanReadableSizes(pOptions->fShowHumanReadableSizes);
    }
    relist();
}

bool UIFileManagerTable::hasSelection() const
{
    if (m_pView)
        return m_pView->hasSelection();
    return false;
}

void UIFileManagerTable::setDragDropMode(QAbstractItemView::DragDropMode behavior)
{
    if (m_pView)
        m_pView->setDragDropMode(behavior);
}

void UIFileManagerTable::sltReceiveDirectoryStatistics(UIDirectoryStatistics statistics)
{
    if (!m_pPropertiesDialog)
        return;
    m_pPropertiesDialog->addDirectoryStatistics(statistics);
}

QModelIndex UIFileManagerTable::currentRootIndex() const
{
    if (!m_pView)
        return QModelIndex();
    if (!m_pProxyModel)
        return m_pView->rootIndex();
    return m_pProxyModel->mapToSource(m_pView->rootIndex());
}

void UIFileManagerTable::performSelectionSearch(const QString &strSearchText)
{
    if (!m_pProxyModel | !m_pView)
        return;

    if (strSearchText.isEmpty())
    {
        markUnmarkSearchLineEdit(false);
        return;
    }

    int rowCount = m_pProxyModel->rowCount(m_pView->rootIndex());
    UIFileSystemItem *pFoundItem = 0;
    QModelIndex index;
    for (int i = 0; i < rowCount && !pFoundItem; ++i)
    {
        index = m_pProxyModel->index(i, 0, m_pView->rootIndex());
        if (!index.isValid())
            continue;
        pFoundItem = static_cast<UIFileSystemItem*>(m_pProxyModel->mapToSource(index).internalPointer());
        if (!pFoundItem)
            continue;
        const QString &strName = pFoundItem->fileObjectName();
        if (!strName.startsWith(m_pSearchLineEdit->text(), Qt::CaseInsensitive))
            pFoundItem = 0;
    }
    if (pFoundItem)
    {
        /* Deselect anything that is already selected: */
        m_pView->clearSelection();
        setSelection(index);
    }
    markUnmarkSearchLineEdit(!pFoundItem);
}

void UIFileManagerTable::disableSelectionSearch()
{
    if (!m_pSearchLineEdit)
        return;
    m_pSearchLineEdit->blockSignals(true);
    m_pSearchLineEdit->clear();
    m_pSearchLineEdit->hide();
    m_pSearchLineEdit->blockSignals(false);
}

bool UIFileManagerTable::checkIfDeleteOK()
{
    UIFileManagerOptions *pFileManagerOptions = UIFileManagerOptions::instance();
    if (!pFileManagerOptions)
        return true;
    if (!pFileManagerOptions->fAskDeleteConfirmation)
        return true;
    UIFileDeleteConfirmationDialog *pDialog =
        new UIFileDeleteConfirmationDialog(this);

    bool fContinueWithDelete = (pDialog->execute() == QDialog::Accepted);
    bool bAskNextTime = pDialog->askDeleteConfirmationNextTime();

    /* Update the file manager options only if it is necessary: */
    if (pFileManagerOptions->fAskDeleteConfirmation != bAskNextTime)
    {
        pFileManagerOptions->fAskDeleteConfirmation = bAskNextTime;
        /* Notify file manager options panel so that the check box there is updated: */
        emit sigDeleteConfirmationOptionChanged();
    }

    delete pDialog;

    return fContinueWithDelete;

}

void UIFileManagerTable::markUnmarkSearchLineEdit(bool fMark)
{
    if (!m_pSearchLineEdit)
        return;
    QPalette palette = m_pSearchLineEdit->palette();

    if (fMark)
        palette.setColor(QPalette::Base, m_searchLineMarkColor);
    else
        palette.setColor(QPalette::Base, m_searchLineUnmarkColor);
    m_pSearchLineEdit->setPalette(palette);
}

void UIFileManagerTable::setSessionWidgetsEnabled(bool fEnabled)
{
    foreach (QWidget *pWidget, m_sessionWidgets)
    {
        if (pWidget)
            pWidget->setEnabled(fEnabled);
    }
}

QStringList UIFileManagerTable::currentDirectoryListing() const
{
    UIFileSystemItem *pItem = static_cast<UIFileSystemItem*>(currentRootIndex().internalPointer());
    if (!pItem)
        return QStringList();
    QStringList list;
    foreach (const UIFileSystemItem *pChild, pItem->children())
    {
        if (pChild)
            list << pChild->fileObjectName();
    }
    return list;
}

void UIFileManagerTable::setModelFileSystem(bool fIsWindowsFileSystem)
{
    if (m_pModel)
        m_pModel->setIsWindowsFileSystem(fIsWindowsFileSystem);
    /* On Windows it is generally desired to sort file objects case insensitively: */
    if (m_pProxyModel)
    {
        if (fIsWindowsFileSystem)
            m_pProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        else
            m_pProxyModel->setSortCaseSensitivity(Qt::CaseSensitive);
    }
}

#include "UIFileManagerTable.moc"
