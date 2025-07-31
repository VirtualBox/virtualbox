/* $Id: UIWizardNewVMSummaryPage.cpp 110477 2025-07-30 12:07:17Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMSummaryPage class implementation.
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
#include <QList>
#include <QFileInfo>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "QITreeView.h"
#include "UIGlobalSession.h"
#include "UIGuestOSType.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UITranslator.h"
#include "UIWizardNewVMSummaryPage.h"
#include "UIWizardDiskEditors.h"
#include "UIWizardNewVM.h"

/*********************************************************************************************************************************
 *   UIWizardNewVMSummaryItem definition.                                                                                  *
*********************************************************************************************************************************/

class UIWizardNewVMSummaryItem : public QITreeViewItem
{
    Q_OBJECT;

public:

    UIWizardNewVMSummaryItem(QITreeView *pParentTree, const QString &strText,
                             const QVariant &data = QVariant(), const QIcon &icon = QIcon());
    ~UIWizardNewVMSummaryItem();
    virtual UIWizardNewVMSummaryItem *childItem(int iIndex) const RT_OVERRIDE RT_FINAL;
    virtual int childCount() const RT_OVERRIDE RT_FINAL;
    virtual QString text() const RT_OVERRIDE RT_FINAL;
    const QString &name() const;
    const QVariant &data() const;
    const QIcon &icon() const;

    /** Returns the index of this item within its parent's children list. */
    int row() const;
    int childIndex(const UIWizardNewVMSummaryItem *pChild) const;
    int columnCount() const;


    UIWizardNewVMSummaryItem *addChild(const QString &strText,
                                       const QVariant &data = QVariant(), const QIcon &icon = QIcon());


    bool isSectionTitle() const;
    void setIsSectionTitle(bool fIsSectionTitle);

private:

    UIWizardNewVMSummaryItem(UIWizardNewVMSummaryItem *pParentItem, const QString &strText,
                             const QVariant &data = QVariant(), const QIcon &icon = QIcon());

    QString m_strText;
    QVariant m_data;
    QIcon m_icon;
    QList<UIWizardNewVMSummaryItem*> m_childList;
    bool m_fIsSectionTitle;
};

/*********************************************************************************************************************************
 *   UIWizardNewVMSummaryModel definition.                                                                                  *
 *********************************************************************************************************************************/

class UIWizardNewVMSummaryModel : public QAbstractItemModel
{
    Q_OBJECT;

public:

    UIWizardNewVMSummaryModel(QITreeView *pParentTree);
    ~UIWizardNewVMSummaryModel();
    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const RT_OVERRIDE RT_FINAL;

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const  RT_OVERRIDE RT_FINAL;
    QModelIndex parent(const QModelIndex &index) const  RT_OVERRIDE RT_FINAL;
    int rowCount(const QModelIndex &parent = QModelIndex()) const  RT_OVERRIDE RT_FINAL;
    int columnCount(const QModelIndex &parent = QModelIndex()) const  RT_OVERRIDE RT_FINAL;

    void populateData(UIWizardNewVM *pWizard);

private:

    UIWizardNewVMSummaryItem *m_pRootItem;

};


/*********************************************************************************************************************************
*   UIWizardNewVMSummaryItem implementation.                                                                                     *
*********************************************************************************************************************************/


UIWizardNewVMSummaryItem::UIWizardNewVMSummaryItem(QITreeView *pParentTree, const QString &strText,
                                                   const QVariant &data /* = QVariant() */, const QIcon &icon /* = QIcon() */)
    : QITreeViewItem(pParentTree)
    , m_strText(strText)
    , m_data(data)
    , m_icon(icon)
    , m_fIsSectionTitle(false)
{
}

UIWizardNewVMSummaryItem::UIWizardNewVMSummaryItem(UIWizardNewVMSummaryItem *pParentItem, const QString &strText,
                                                   const QVariant &data /* = QVariant() */, const QIcon &icon /* = QIcon() */)
    : QITreeViewItem(pParentItem)
    , m_strText(strText)
    , m_data(data)
    , m_icon(icon)
    , m_fIsSectionTitle(false)
{
}

UIWizardNewVMSummaryItem::~UIWizardNewVMSummaryItem()
{
    qDeleteAll(m_childList);
}

UIWizardNewVMSummaryItem *UIWizardNewVMSummaryItem::childItem(int iIndex) const
{
    if (iIndex >= m_childList.size())
        return 0;
    return m_childList[iIndex];
}

int UIWizardNewVMSummaryItem::childIndex(const UIWizardNewVMSummaryItem *pChild) const
{
    if (!pChild)
        return 0;
    return m_childList.indexOf(const_cast<UIWizardNewVMSummaryItem*>(pChild));
}

int UIWizardNewVMSummaryItem::row() const
{
    UIWizardNewVMSummaryItem *pParent = qobject_cast<UIWizardNewVMSummaryItem*>(parentItem());
    if (!pParent)
        return 0;
    return pParent->childIndex(this);
}


int UIWizardNewVMSummaryItem::childCount() const
{
    return m_childList.size();
}

QString UIWizardNewVMSummaryItem::text() const
{
    return   m_data.isValid()
           ? QString("%1: %2").arg(m_strText, m_data.toString())
           : m_strText;
}

const QString &UIWizardNewVMSummaryItem::name() const
{
    return m_strText;
}

const QVariant &UIWizardNewVMSummaryItem::data() const
{
    return m_data;
}

const QIcon &UIWizardNewVMSummaryItem::icon() const
{
    return m_icon;
}

int UIWizardNewVMSummaryItem::columnCount() const
{
    if (m_data.isValid())
        return 2;
    return 1;
}

UIWizardNewVMSummaryItem *UIWizardNewVMSummaryItem::addChild(const QString &strText, const QVariant &data /* = QVariant() */, const QIcon &icon /* = QIcon() */)
{
    UIWizardNewVMSummaryItem *pNewItem = new UIWizardNewVMSummaryItem(this, strText, data, icon);
    m_childList << pNewItem;
    return pNewItem;
}

void UIWizardNewVMSummaryItem::setIsSectionTitle(bool fIsSectionTitle)
{
    m_fIsSectionTitle = fIsSectionTitle;
}

bool UIWizardNewVMSummaryItem::isSectionTitle() const
{
    return m_fIsSectionTitle;
}


/*********************************************************************************************************************************
*   UIWizardNewVMSummaryModel implementation.                                                                                    *
*********************************************************************************************************************************/

UIWizardNewVMSummaryModel::UIWizardNewVMSummaryModel(QITreeView *pParentTree)
    : QAbstractItemModel(pParentTree)
    , m_pRootItem(0)
{
}

UIWizardNewVMSummaryModel::~UIWizardNewVMSummaryModel()
{
    delete m_pRootItem;
}

QVariant UIWizardNewVMSummaryModel::data(const QModelIndex &index, int role /* = Qt::DisplayRole */) const
{
    if (!index.isValid())
        return QVariant();
    UIWizardNewVMSummaryItem *pItem = static_cast<UIWizardNewVMSummaryItem*>(index.internalPointer());
    if (!pItem)
        return QVariant();
    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
            case 0:
                return pItem->name();
                break;
            case 1:
                return pItem->data();
                break;
            default:
                break;
        }
    }
    else if (role == Qt::DecorationRole)
    {
        if (index.column() == 0 && !pItem->icon().isNull())
            return pItem->icon();
    }
    else if (role == Qt::FontRole)
    {
        UIWizardNewVMSummaryItem *pItem = static_cast<UIWizardNewVMSummaryItem*>(index.internalPointer());
        if (pItem && pItem->isSectionTitle())
        {
            QFont font = qApp->font();
            font.setBold(true);
            return font;
        }
    }
    return QVariant();
}

QModelIndex UIWizardNewVMSummaryModel::index(int row, int column, const QModelIndex &parent /* = QModelIndex() */) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    UIWizardNewVMSummaryItem *pParentItem;

    if (!parent.isValid())
        pParentItem = m_pRootItem;
    else
        pParentItem = static_cast<UIWizardNewVMSummaryItem*>(parent.internalPointer());

    UIWizardNewVMSummaryItem *pChildItem = pParentItem->childItem(row);
    if (pChildItem)
        return createIndex(row, column, pChildItem);
    else
        return QModelIndex();
}

QModelIndex UIWizardNewVMSummaryModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    UIWizardNewVMSummaryItem *pChildItem = static_cast<UIWizardNewVMSummaryItem*>(index.internalPointer());
    if (!pChildItem)
        return QModelIndex();

    UIWizardNewVMSummaryItem *pParentItem = static_cast<UIWizardNewVMSummaryItem*>(pChildItem->parentItem());

    if (pParentItem == m_pRootItem)
        return QModelIndex();

    return createIndex(pParentItem->row(), 0, pParentItem);
}

int UIWizardNewVMSummaryModel::rowCount(const QModelIndex &parent /* = QModelIndex() */) const
{
    if (parent.column() > 0)
        return 0;
    UIWizardNewVMSummaryItem *pItem = 0;
    if (!parent.isValid())
        pItem = m_pRootItem;
    else
        pItem = static_cast<UIWizardNewVMSummaryItem*>(parent.internalPointer());

    if (pItem)
        return pItem->childCount();
    return 0;
}

int UIWizardNewVMSummaryModel::columnCount(const QModelIndex &parentIndex /* = QModelIndex() */) const
{
    Q_UNUSED(parentIndex);
    return 2;
#if 0
    AssertReturn(m_pRootItem, 0);
    if (parentIndex.isValid())
    {
        UIWizardNewVMSummaryItem *pParent = static_cast<UIWizardNewVMSummaryItem*>(parentIndex.internalPointer());
        if (pParent)
            return pParent->columnCount();

    }
    return m_pRootItem->columnCount();
#endif
}

void UIWizardNewVMSummaryModel::populateData(UIWizardNewVM *pWizard)
{
    QITreeView *pParentTree = qobject_cast<QITreeView*>(QObject::parent());

    AssertReturnVoid(pWizard && pParentTree);
    if (m_pRootItem)
        delete m_pRootItem;
    m_pRootItem = new UIWizardNewVMSummaryItem(pParentTree, "root");

    UIWizardNewVMSummaryItem *pNameRoot = m_pRootItem->addChild(UIWizardNewVM::tr("Virtual Machine Name and Operating System"),
                                                                QVariant(), UIIconPool::iconSet(":/name_16px.png"));
    pNameRoot->setIsSectionTitle(true);

    /* Name and OS Type page stuff: */
    pNameRoot->addChild(UIWizardNewVM::tr("VM Name"), pWizard->machineBaseName());
    pNameRoot->addChild(UIWizardNewVM::tr("VM Folder"), pWizard->machineFolder());
    pNameRoot->addChild(UIWizardNewVM::tr("ISO Image"), pWizard->ISOFilePath());
    pNameRoot->addChild(UIWizardNewVM::tr("Guest OS Type"), gpGlobalSession->guestOSTypeManager().getDescription(pWizard->guestOSTypeId()));

    const QString &ISOPath = pWizard->ISOFilePath();
    if (!ISOPath.isNull() && !ISOPath.isEmpty())
        pNameRoot->addChild(UIWizardNewVM::tr("Proceed with Unattended Installation"), !pWizard->skipUnattendedInstall());

    /* Unattended install related info: */
    if (pWizard->isUnattendedEnabled())
    {
        UIWizardNewVMSummaryItem *pUnattendedRoot = m_pRootItem->addChild(UIWizardNewVM::tr("Unattended Installation of Guest OS"), QVariant(),
                                                                          UIIconPool::iconSet(":/extension_pack_install_16px.png"));
        pUnattendedRoot->setIsSectionTitle(true);

        pUnattendedRoot->addChild(UIWizardNewVM::tr("User Name"), pWizard->userName());
        pUnattendedRoot->addChild(UIWizardNewVM::tr("Product Key"), pWizard->productKey());
        pUnattendedRoot->addChild(UIWizardNewVM::tr("Host Name/Domain Name"), pWizard->hostnameDomainName());
        pUnattendedRoot->addChild(UIWizardNewVM::tr("Install in Background"), pWizard->startHeadless());
        pUnattendedRoot->addChild(UIWizardNewVM::tr("Install Guest Additions"), pWizard->installGuestAdditions());
        if (pWizard->installGuestAdditions())
            pUnattendedRoot->addChild(UIWizardNewVM::tr("Guest Additions ISO Image"), pWizard->guestAdditionsISOPath());
    }

    UIWizardNewVMSummaryItem *pHardwareRoot = m_pRootItem->addChild(UIWizardNewVM::tr("Virtual Hardware"), QVariant(),
                                                                    UIIconPool::iconSet(":/cpu_16px.png"));
    pHardwareRoot->setIsSectionTitle(true);
    pHardwareRoot->addChild(UIWizardNewVM::tr("Base Memory"), pWizard->memorySize());
    pHardwareRoot->addChild(UIWizardNewVM::tr("Processors"), pWizard->CPUCount());
    pHardwareRoot->addChild(UIWizardNewVM::tr("Use EFI"), pWizard->EFIEnabled());

    /* Disk related info: */
    if (pWizard->diskSource() == SelectedDiskSource_New)
        pHardwareRoot->addChild(UIWizardNewVM::tr("Hard Disk Size"), UITranslator::formatSize(pWizard->mediumSize()));
    else if (pWizard->diskSource() == SelectedDiskSource_Existing)
        pHardwareRoot->addChild(UIWizardNewVM::tr("Attached Disk"), pWizard->mediumPath());
    else if (pWizard->diskSource() == SelectedDiskSource_Empty)
        pHardwareRoot->addChild(UIWizardNewVM::tr("Attached Disk"), UIWizardNewVM::tr("None"));
}


/*********************************************************************************************************************************
*   UIWizardNewVMSummaryPage implementation.                                                                                     *
*********************************************************************************************************************************/

UIWizardNewVMSummaryPage::UIWizardNewVMSummaryPage(const QString strHelpKeyword /* = QString() */)
    : UINativeWizardPage(strHelpKeyword)
    , m_pLabel(0)
    , m_pTree(0)
{
    prepare();
}


void UIWizardNewVMSummaryPage::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);

    m_pLabel = new QIRichTextLabel(this);
    pMainLayout->addWidget(m_pLabel);

    m_pTree = new QITreeView;

    if (m_pTree)
    {
        m_pTree->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::MinimumExpanding);
        m_pTree->setAlternatingRowColors(true);
        m_pModel = new UIWizardNewVMSummaryModel(m_pTree);
        m_pTree->setModel(m_pModel);
        pMainLayout->addWidget(m_pTree);
    }

    //pMainLayout->addStretch();

    createConnections();
}

void UIWizardNewVMSummaryPage::createConnections()
{
}

void UIWizardNewVMSummaryPage::sltRetranslateUI()
{
    setTitle(UIWizardNewVM::tr("Summary"));
    if (m_pLabel)
        m_pLabel->setText(UIWizardNewVM::tr("A new VM will be created with the following configuration."));

    if (m_pTree)
        m_pTree->setWhatsThis(UIWizardNewVM::tr("Lists chosen configuration of the guest system."));
}

void UIWizardNewVMSummaryPage::initializePage()
{
    sltRetranslateUI();
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard && m_pModel);
    m_pModel->populateData(pWizard);
    if (m_pTree)
    {
        m_pTree->expandToDepth(4);
        m_pTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    }
}

bool UIWizardNewVMSummaryPage::isComplete() const
{
    return true;
}

bool UIWizardNewVMSummaryPage::validatePage()
{
    bool fResult = true;
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);

    /* Make sure user really intents to creae a vm with no hard drive: */
    if (pWizard->diskSource() == SelectedDiskSource_Empty)
    {
        /* Ask user about disk-less machine unless that's the recommendation: */
        if (!pWizard->emptyDiskRecommended())
        {
            if (!msgCenter().confirmHardDisklessMachine(this))
                return false;
        }
    }
    else if (pWizard->diskSource() == SelectedDiskSource_New)
    {
        /* Check if the path we will be using for hard drive creation exists: */
        const QString &strMediumPath = pWizard->mediumPath();
        fResult = !QFileInfo(strMediumPath).exists();
        if (!fResult)
        {
            UINotificationMessage::cannotOverwriteMediumStorage(strMediumPath, wizard()->notificationCenter());
            return fResult;
        }
        /* Check FAT size limitation of the host hard drive: */
        fResult = UIWizardDiskEditors::checkFATSizeLimitation(pWizard->mediumVariant(),
                                                              strMediumPath,
                                                              pWizard->mediumSize());
        if (!fResult)
        {
            UINotificationMessage::cannotCreateMediumStorageInFAT(strMediumPath, wizard()->notificationCenter());
            return fResult;
        }

        /* Try to create the hard drive:*/
        fResult = pWizard->createVirtualDisk();
        /*Don't show any error message here since UIWizardNewVM::createVirtualDisk already does so: */
        if (!fResult)
            return fResult;
    }

    return pWizard->createVM();
}


#include "UIWizardNewVMSummaryPage.moc"
