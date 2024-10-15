/* $Id: UICloudProfileManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudProfileManager class implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <QPushButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIInputDialog.h"
#include "QIMessageBox.h"
#include "QIToolBar.h"
#include "QITreeWidget.h"
#include "UIActionPoolManager.h"
#include "UICloudNetworkingStuff.h"
#include "UICloudProfileDetailsWidget.h"
#include "UICloudProfileManager.h"
#include "UICommon.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UIShortcutPool.h"
#include "UITranslationEventListener.h"
#include "UIVirtualBoxEventHandler.h"

/* COM includes: */
#include "CCloudProfile.h"
#include "CCloudProvider.h"
#include "CCloudProviderManager.h"


/** Tree-widget item types. */
enum CloudItemType
{
    CloudItemType_Invalid  = 0,
    CloudItemType_Provider = 1,
    CloudItemType_Profile  = 2
};
Q_DECLARE_METATYPE(CloudItemType);

/** Tree-widget data types. */
enum
{
    Data_ItemType          = Qt::UserRole + 1,
    Data_ProviderShortName = Qt::UserRole + 2,
    Data_Definition        = Qt::UserRole + 3,
};

/** Tree-widget column types. */
enum
{
    Column_Name,
    Column_ListVMs,
    Column_Max
};


/** Cloud Profile Manager provider's tree-widget item. */
class UIItemCloudProvider : public QITreeWidgetItem, public UIDataCloudProvider
{
    Q_OBJECT;

public:

    /** Constructs item. */
    UIItemCloudProvider();

    /** Updates item fields from base-class data. */
    void updateFields();

    /** Returns item name. */
    QString name() const { return m_strName; }

    /** Returns definition composed on the basis of @a strShortName. */
    static QString definition(const QString &strShortName);
};

/** Cloud Profile Manager profile's tree-widget item. */
class UIItemCloudProfile : public QITreeWidgetItem, public UIDataCloudProfile
{
    Q_OBJECT;

public:

    /** Constructs item. */
    UIItemCloudProfile();

    /** Updates item fields from base-class data. */
    void updateFields();

    /** Returns item name. */
    QString name() const { return m_strName; }

    /** Returns definition composed on the basis of @a strProviderShortName and @a strName. */
    static QString definition(const QString &strProviderShortName, const QString &strName);
};


/*********************************************************************************************************************************
*   Class UIItemCloudProvider implementation.                                                                                    *
*********************************************************************************************************************************/

UIItemCloudProvider::UIItemCloudProvider()
{
    /* Assign icon: */
    setIcon(Column_Name, UIIconPool::iconSet(":/provider_oracle_16px.png"));
    /* Assign item type: */
    setData(Column_Name, Data_ItemType, QVariant::fromValue(CloudItemType_Provider));
}

void UIItemCloudProvider::updateFields()
{
    /* Update item fields: */
    setText(Column_Name, m_strName);
    setData(Column_Name, Data_ProviderShortName, m_strShortName);
    setData(Column_Name, Data_Definition, QVariant::fromValue(definition(m_strShortName)));
    setCheckState(Column_ListVMs, m_fRestricted ? Qt::Unchecked : Qt::Checked);
}

/* static */
QString UIItemCloudProvider::definition(const QString &strShortName)
{
    return QString("/%1").arg(strShortName);
}


/*********************************************************************************************************************************
*   Class UIItemCloudProfile implementation.                                                                                     *
*********************************************************************************************************************************/

UIItemCloudProfile::UIItemCloudProfile()
{
    /* Assign icon: */
    setIcon(Column_Name, UIIconPool::iconSet(":/profile_16px.png"));
    /* Assign item type: */
    setData(Column_Name, Data_ItemType, QVariant::fromValue(CloudItemType_Profile));
}

void UIItemCloudProfile::updateFields()
{
    /* Update item fields: */
    setText(Column_Name, m_strName);
    setData(Column_Name, Data_Definition, QVariant::fromValue(definition(m_strProviderShortName, m_strName)));
    setCheckState(Column_ListVMs, m_fRestricted ? Qt::Unchecked : Qt::Checked);
}

/* static */
QString UIItemCloudProfile::definition(const QString &strProviderShortName, const QString &strName)
{
    return QString("/%1/%2").arg(strProviderShortName, strName);
}


/*********************************************************************************************************************************
*   Class UICloudProfileManagerWidget implementation.                                                                            *
*********************************************************************************************************************************/

UICloudProfileManagerWidget::UICloudProfileManagerWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                                                         bool fShowToolbar /* = true */, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_pActionPool(pActionPool)
    , m_fShowToolbar(fShowToolbar)
    , m_pToolBar(0)
    , m_pTreeWidget(0)
    , m_pDetailsWidget(0)
{
    prepare();
}

QMenu *UICloudProfileManagerWidget::menu() const
{
    return m_pActionPool->action(UIActionIndexMN_M_CloudWindow)->menu();
}

void UICloudProfileManagerWidget::sltRetranslateUI()
{
    /* Translate tree-widget: */
    m_pTreeWidget->setHeaderLabels(   QStringList()
                                   << UICloudProfileManager::tr("Source")
                                   << UICloudProfileManager::tr("List VMs"));
    m_pTreeWidget->setWhatsThis(UICloudProfileManager::tr("Registered cloud providers and profiles"));
}

bool UICloudProfileManagerWidget::makeSureChangesResolved()
{
    /* Check if currently selected item is of profile type: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemCloudProfile *pProfileItem = qobject_cast<UIItemCloudProfile*>(pItem);
    if (!pProfileItem)
        return true;

    /* Get item data: */
    UIDataCloudProfile oldData = *pProfileItem;
    UIDataCloudProfile newData = m_pDetailsWidget->data();

    /* Check if data has changed: */
    if (newData == oldData)
        return true;

    /* Ask whether user wants to Accept/Reset changes or still not sure: */
    const int iResult = msgCenter().confirmCloudProfileManagerClosing(window());
    switch (iResult)
    {
        case AlertButton_Choice1:
        {
            sltApplyCloudProfileDetailsChanges();
            return true;
        }
        case AlertButton_Choice2:
        {
            sltResetCloudProfileDetailsChanges();
            return true;
        }
        default:
            break;
    }

    /* False by default: */
    return false;
}

void UICloudProfileManagerWidget::sltResetCloudProfileDetailsChanges()
{
    /* Just push the current-item data there again: */
    sltHandleCurrentItemChange();
}

void UICloudProfileManagerWidget::sltApplyCloudProfileDetailsChanges()
{
    /* It can be that this is provider item, not profile item currently selected.
     * In such case we are not applying parameters, we are creating new one profile. */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemCloudProvider *pMaybeProviderItem = qobject_cast<UIItemCloudProvider*>(pItem);
    if (pMaybeProviderItem)
        return sltAddCloudProfile();

    /* Get profile item: */
    UIItemCloudProfile *pProfileItem = qobject_cast<UIItemCloudProfile*>(pItem);
    AssertPtrReturnVoid(pProfileItem);
    /* Get provider item: */
    UIItemCloudProvider *pProviderItem = qobject_cast<UIItemCloudProvider*>(pProfileItem->parentItem());
    AssertPtrReturnVoid(pProviderItem);

    /* Acquire provider short name: */
    const QString strShortName = pProviderItem->data(Column_Name, Data_ProviderShortName).toString();

    /* Look for corresponding provider: */
    CCloudProvider comCloudProvider = cloudProviderByShortName(strShortName);
    if (comCloudProvider.isNotNull())
    {
        /* Get old/new data: */
        UIDataCloudProfile oldData = *pProfileItem;
        UIDataCloudProfile newData = m_pDetailsWidget->data();

        /* Look for corresponding profile: */
        CCloudProfile comCloudProfile = cloudProfileByName(strShortName, oldData.m_strName);
        if (comCloudProfile.isNotNull())
        {
            /* Set profile name, if necessary: */
            if (newData.m_strName != oldData.m_strName)
                comCloudProfile.SetName(newData.m_strName);
            /* Show error message if necessary: */
            if (!comCloudProfile.isOk())
                UINotificationMessage::cannotChangeCloudProfileParameter(comCloudProfile);
            else
            {
                /* Iterate through old/new data: */
                foreach (const QString &strKey, oldData.m_data.keys())
                {
                    /* Get values: */
                    const QString strOldValue = oldData.m_data.value(strKey).first;
                    const QString strNewValue = newData.m_data.value(strKey).first;
                    if (strNewValue != strOldValue)
                    {
                        /* Apply property: */
                        comCloudProfile.SetProperty(strKey, strNewValue);
                        /* Show error message if necessary: */
                        if (!comCloudProfile.isOk())
                        {
                            UINotificationMessage::cannotChangeCloudProfileParameter(comCloudProfile);
                            break;
                        }
                    }
                }
            }

            /* If profile is Ok finally: */
            if (comCloudProfile.isOk())
            {
                /* Save profile changes: */
                comCloudProvider.SaveProfiles();
                /* Show error message if necessary: */
                if (!comCloudProvider.isOk())
                    UINotificationMessage::cannotSaveCloudProfiles(comCloudProvider);
            }
        }
    }
}

void UICloudProfileManagerWidget::sltAddCloudProfile()
{
    /* Get provider item: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemCloudProvider *pProviderItem = qobject_cast<UIItemCloudProvider*>(pItem);
    AssertPtrReturnVoid(pProviderItem);

    /* Acquire profile name if not proposed by details widget: */
    QString strProfileName = m_pDetailsWidget->data().m_strName;
    if (strProfileName.isEmpty())
    {
        bool fCancelled = true;
        QISafePointerInputDialog pDialog = new QIInputDialog(this);
        if (pDialog)
        {
#ifndef VBOX_WS_MAC
            pDialog->setWindowIcon(UIIconPool::iconSetFull(":/cloud_profile_add_32px.png", ":/cloud_profile_add_16px.png"));
#endif
            pDialog->setWindowTitle(UICloudProfileManager::tr("Add Profile"));
            if (pDialog->exec() == QDialog::Accepted)
            {
                strProfileName = pDialog->textValue();
                fCancelled = false;
            }
            delete pDialog;
        }
        if (fCancelled)
            return;
    }

    /* Acquire provider short name: */
    const QString strShortName = pProviderItem->data(Column_Name, Data_ProviderShortName).toString();

    /* Look for corresponding provider: */
    CCloudProvider comCloudProvider = cloudProviderByShortName(strShortName);
    if (comCloudProvider.isNotNull())
    {
        /* Create new profile: */
        const QVector<QString> keys = pProviderItem->m_propertyDescriptions.keys().toVector();
        const QVector<QString> values(keys.size());
        comCloudProvider.CreateProfile(strProfileName, keys, values);
        /* Show error message if necessary: */
        if (!comCloudProvider.isOk())
            UINotificationMessage::cannotCreateCloudProfile(comCloudProvider);
        else
        {
            /* Save profile changes: */
            comCloudProvider.SaveProfiles();
            /* Show error message if necessary: */
            if (!comCloudProvider.isOk())
                UINotificationMessage::cannotSaveCloudProfiles(comCloudProvider);
        }
    }
}

void UICloudProfileManagerWidget::sltImportCloudProfiles()
{
    /* Get provider item: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemCloudProvider *pProviderItem = qobject_cast<UIItemCloudProvider*>(pItem);
    AssertPtrReturnVoid(pProviderItem);

    /* If there are profiles exist => confirm cloud profile import. */
    if (   pProviderItem->childCount() != 0
        && !msgCenter().confirmCloudProfilesImport(this))
        return;

    /* Acquire provider short name: */
    const QString strShortName = pProviderItem->data(Column_Name, Data_ProviderShortName).toString();

    /* Look for corresponding provider: */
    CCloudProvider comCloudProvider = cloudProviderByShortName(strShortName);
    if (comCloudProvider.isNotNull())
    {
        /* Import profiles: */
        comCloudProvider.ImportProfiles();
        /* Show error message if necessary: */
        if (!comCloudProvider.isOk())
            UINotificationMessage::cannotImportCloudProfiles(comCloudProvider);
    }
}

void UICloudProfileManagerWidget::sltRemoveCloudProfile()
{
    /* Get profile item: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemCloudProfile *pProfileItem = qobject_cast<UIItemCloudProfile*>(pItem);
    AssertPtrReturnVoid(pProfileItem);
    /* Get provider item: */
    UIItemCloudProvider *pProviderItem = qobject_cast<UIItemCloudProvider*>(pProfileItem->parentItem());
    AssertPtrReturnVoid(pProviderItem);

    /* Acquire profile name: */
    const QString strProfileName = pProfileItem->name();

    /* Confirm cloud profile removal: */
    if (!msgCenter().confirmCloudProfileRemoval(strProfileName, this))
        return;

    /* Acquire provider short name: */
    const QString strShortName = pProviderItem->data(Column_Name, Data_ProviderShortName).toString();

    /* Look for corresponding provider: */
    CCloudProvider comCloudProvider = cloudProviderByShortName(strShortName);
    if (comCloudProvider.isNotNull())
    {
        /* Look for corresponding profile: */
        CCloudProfile comCloudProfile = cloudProfileByName(strShortName, strProfileName);
        if (comCloudProfile.isNotNull())
        {
            /* Remove current profile: */
            comCloudProfile.Remove();
            /* Show error message if necessary: */
            if (!comCloudProfile.isOk())
                UINotificationMessage::cannotRemoveCloudProfile(comCloudProfile);
            else
            {
                /* Save profile changes: */
                comCloudProvider.SaveProfiles();
                /* Show error message if necessary: */
                if (!comCloudProvider.isOk())
                    UINotificationMessage::cannotSaveCloudProfiles(comCloudProvider);
            }
        }
    }
}

void UICloudProfileManagerWidget::sltToggleCloudProfileDetailsVisibility(bool fVisible)
{
    /* Save the setting: */
    gEDataManager->setCloudProfileManagerDetailsExpanded(fVisible);
    /* Show/hide details area and Apply button: */
    m_pDetailsWidget->setVisible(fVisible);
    /* Notify external lsiteners: */
    emit sigCloudProfileDetailsVisibilityChanged(fVisible);
}

void UICloudProfileManagerWidget::sltShowCloudProfileTryPage()
{
    uiCommon().openURL("https://signup.cloud.oracle.com/");
}

void UICloudProfileManagerWidget::sltShowCloudProfileHelp()
{
    uiCommon().openURL("https://docs.cloud.oracle.com/iaas/Content/API/Concepts/sdkconfig.htm");
}

void UICloudProfileManagerWidget::sltPerformTableAdjustment()
{
    AssertPtrReturnVoid(m_pTreeWidget);
    AssertPtrReturnVoid(m_pTreeWidget->header());
    AssertPtrReturnVoid(m_pTreeWidget->viewport());
    m_pTreeWidget->header()->resizeSection(0, m_pTreeWidget->viewport()->width() - m_pTreeWidget->header()->sectionSize(1));
}

void UICloudProfileManagerWidget::sltHandleCurrentItemChange()
{
    /* Check current-item type: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemCloudProvider *pItemProvider = qobject_cast<UIItemCloudProvider*>(pItem);
    UIItemCloudProfile *pItemProfile = qobject_cast<UIItemCloudProfile*>(pItem);

    /* Update actions availability: */
    m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Add)->setEnabled(pItemProvider);
    m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Import)->setEnabled(pItemProvider);
    m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Remove)->setEnabled(pItemProfile);
    m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details)->setEnabled(pItemProvider || pItemProfile);

    /* If there is an item => update details data: */
    if (pItemProfile)
        m_pDetailsWidget->setData(*pItemProfile);
    /* Otherwise => clear details data: */
    else
        m_pDetailsWidget->setData(UIDataCloudProfile());

    /* Update details area visibility: */
    sltToggleCloudProfileDetailsVisibility(pItem && m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details)->isChecked());
}

void UICloudProfileManagerWidget::sltHandleContextMenuRequest(const QPoint &position)
{
    /* Check clicked-item type: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->itemAt(position));
    UIItemCloudProvider *pItemProvider = qobject_cast<UIItemCloudProvider*>(pItem);
    UIItemCloudProfile *pItemProfile = qobject_cast<UIItemCloudProfile*>(pItem);

    /* Compose temporary context-menu: */
    QMenu menu;
    if (pItemProfile)
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Remove));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details));
    }
    else if (pItemProvider)
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Add));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Import));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details));
    }

    /* And show it: */
    menu.exec(m_pTreeWidget->viewport()->mapToGlobal(position));
}

void UICloudProfileManagerWidget::sltHandleItemChange(QTreeWidgetItem *pItem)
{
    /* Check item type: */
    QITreeWidgetItem *pChangedItem = QITreeWidgetItem::toItem(pItem);
    UIItemCloudProvider *pProviderItem = qobject_cast<UIItemCloudProvider*>(pChangedItem);
    UIItemCloudProfile *pProfileItem = qobject_cast<UIItemCloudProfile*>(pChangedItem);

    /* Check whether item is of provider or profile type, then check whether it changed: */
    bool fChanged = false;
    if (pProviderItem)
    {
        const UIDataCloudProvider oldData = *pProviderItem;
        if (   (oldData.m_fRestricted && pProviderItem->checkState(Column_ListVMs) == Qt::Checked)
            || (!oldData.m_fRestricted && pProviderItem->checkState(Column_ListVMs) == Qt::Unchecked))
            fChanged = true;
    }
    else if (pProfileItem)
    {
        const UIDataCloudProfile oldData = *pProfileItem;
        if (   (oldData.m_fRestricted && pProfileItem->checkState(Column_ListVMs) == Qt::Checked)
            || (!oldData.m_fRestricted && pProfileItem->checkState(Column_ListVMs) == Qt::Unchecked))
            fChanged = true;
    }

    /* Gather Cloud Profile Manager restrictions and save them to extra-data: */
    if (fChanged)
        gEDataManager->setCloudProfileManagerRestrictions(gatherCloudProfileManagerRestrictions(m_pTreeWidget->invisibleRootItem()));
}

void UICloudProfileManagerWidget::prepare()
{
    /* Prepare actions: */
    prepareActions();
    /* Prepare widgets: */
    prepareWidgets();

    /* Load settings: */
    loadSettings();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloudProfileManagerWidget::sltRetranslateUI);

    /* Load cloud stuff: */
    loadCloudStuff();

    /* Set help keyowrd for context sensitive help: */
    uiCommon().setHelpKeyword(this, "cloud-using-cloud-profile-manager");

}

void UICloudProfileManagerWidget::prepareActions()
{
    /* First of all, add actions which has smaller shortcut scope: */
    addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Add));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Import));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Remove));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_TryPage));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Help));
}

void UICloudProfileManagerWidget::prepareWidgets()
{
    /* Create main-layout: */
    new QVBoxLayout(this);
    if (layout())
    {
        /* Configure layout: */
        layout()->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
        layout()->setSpacing(10);
#else
        layout()->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif

        /* Prepare toolbar, if requested: */
        if (m_fShowToolbar)
            prepareToolBar();
        /* Prepare tree-widget: */
        prepareTreeWidget();
        /* Prepare details-widget: */
        prepareDetailsWidget();
        /* Prepare connections: */
        prepareConnections();
    }
}

void UICloudProfileManagerWidget::prepareToolBar()
{
    /* Create toolbar: */
    m_pToolBar = new QIToolBar(parentWidget());
    if (m_pToolBar)
    {
        /* Configure toolbar: */
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        /* Add toolbar actions: */
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Add));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Import));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Remove));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_TryPage));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Help));

#ifdef VBOX_WS_MAC
        /* Check whether we are embedded into a stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Add into layout: */
            layout()->addWidget(m_pToolBar);
        }
#else
        /* Add into layout: */
        layout()->addWidget(m_pToolBar);
#endif
    }
}

void UICloudProfileManagerWidget::prepareTreeWidget()
{
    /* Create tree-widget: */
    m_pTreeWidget = new QITreeWidget;
    if (m_pTreeWidget)
    {
        /* Configure tree-widget: */
        m_pTreeWidget->header()->setStretchLastSection(false);
        m_pTreeWidget->setRootIsDecorated(false);
        m_pTreeWidget->setAlternatingRowColors(true);
        m_pTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        m_pTreeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_pTreeWidget->setColumnCount(Column_Max);
        m_pTreeWidget->setSortingEnabled(true);
        m_pTreeWidget->sortByColumn(Column_Name, Qt::AscendingOrder);
        m_pTreeWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

        /* Add into layout: */
        layout()->addWidget(m_pTreeWidget);
    }
}

void UICloudProfileManagerWidget::prepareDetailsWidget()
{
    /* Create details-widget: */
    m_pDetailsWidget = new UICloudProfileDetailsWidget(m_enmEmbedding);
    if (m_pDetailsWidget)
    {
        /* Configure details-widget: */
        m_pDetailsWidget->setVisible(false);
        m_pDetailsWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

        /* Add into layout: */
        layout()->addWidget(m_pDetailsWidget);
    }
}

void UICloudProfileManagerWidget::prepareConnections()
{
    /* Action connections: */
    connect(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Add), &QAction::triggered,
            this, &UICloudProfileManagerWidget::sltAddCloudProfile);
    connect(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Import), &QAction::triggered,
            this, &UICloudProfileManagerWidget::sltImportCloudProfiles);
    connect(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Remove), &QAction::triggered,
            this, &UICloudProfileManagerWidget::sltRemoveCloudProfile);
    connect(m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details), &QAction::toggled,
            this, &UICloudProfileManagerWidget::sltToggleCloudProfileDetailsVisibility);
    connect(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_TryPage), &QAction::triggered,
            this, &UICloudProfileManagerWidget::sltShowCloudProfileTryPage);
    connect(m_pActionPool->action(UIActionIndexMN_M_Cloud_S_Help), &QAction::triggered,
            this, &UICloudProfileManagerWidget::sltShowCloudProfileHelp);

    /* Tree-widget connections: */
    connect(m_pTreeWidget, &QITreeWidget::resized,
            this, &UICloudProfileManagerWidget::sltPerformTableAdjustment, Qt::QueuedConnection);
    connect(m_pTreeWidget->header(), &QHeaderView::sectionResized,
            this, &UICloudProfileManagerWidget::sltPerformTableAdjustment, Qt::QueuedConnection);
    connect(m_pTreeWidget, &QITreeWidget::currentItemChanged,
            this, &UICloudProfileManagerWidget::sltHandleCurrentItemChange);
    connect(m_pTreeWidget, &QITreeWidget::customContextMenuRequested,
            this, &UICloudProfileManagerWidget::sltHandleContextMenuRequest);
    connect(m_pTreeWidget, &QITreeWidget::itemDoubleClicked,
            m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details), &QAction::setChecked);
    connect(m_pTreeWidget, &QITreeWidget::itemChanged,
            this, &UICloudProfileManagerWidget::sltHandleItemChange);

    /* Details-widget connections: */
    connect(m_pDetailsWidget, &UICloudProfileDetailsWidget::sigDataChanged,
            this, &UICloudProfileManagerWidget::sigCloudProfileDetailsDataChanged);
    connect(m_pDetailsWidget, &UICloudProfileDetailsWidget::sigDataChangeRejected,
            this, &UICloudProfileManagerWidget::sltResetCloudProfileDetailsChanges);
    connect(m_pDetailsWidget, &UICloudProfileDetailsWidget::sigDataChangeAccepted,
            this, &UICloudProfileManagerWidget::sltApplyCloudProfileDetailsChanges);

    /* Extra-data connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProviderListChanged,
            this, &UICloudProfileManagerWidget::sltLoadCloudStuff);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileRegistered,
            this, &UICloudProfileManagerWidget::sltLoadCloudStuff);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileChanged,
            this, &UICloudProfileManagerWidget::sltLoadCloudStuff);
    connect(gEDataManager, &UIExtraDataManager::sigCloudProfileManagerRestrictionChange,
            this, &UICloudProfileManagerWidget::sltLoadCloudStuff);
}

void UICloudProfileManagerWidget::loadSettings()
{
    /* Details action/widget: */
    m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details)->setChecked(gEDataManager->cloudProfileManagerDetailsExpanded());
    sltToggleCloudProfileDetailsVisibility(m_pActionPool->action(UIActionIndexMN_M_Cloud_T_Details)->isChecked());
}

void UICloudProfileManagerWidget::loadCloudStuff()
{
    /* Save current item definition: */
    QITreeWidgetItem *pCurrentItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    const QString strDefinition = pCurrentItem ? pCurrentItem->data(Column_Name, Data_Definition).toString() : QString();

    /* Clear tree first of all: */
    m_pTreeWidget->clear();

    /* Acquire cloud profile manager restrictions: */
    const QStringList restrictions = gEDataManager->cloudProfileManagerRestrictions();

    /* Iterate through existing providers: */
    foreach (const CCloudProvider &comCloudProvider, listCloudProviders())
    {
        /* Skip if we have nothing to populate: */
        if (comCloudProvider.isNull())
            continue;

        /* Load provider data: */
        UIDataCloudProvider providerData;
        loadCloudProvider(comCloudProvider, restrictions, providerData);
        createItemForCloudProvider(providerData);

        /* Make sure provider item is properly inserted: */
        QTreeWidgetItem *pItem = searchItem(UIItemCloudProvider::definition(providerData.m_strShortName));
        AssertPtrReturnVoid(pItem);

        /* Iterate through provider's profiles: */
        foreach (const CCloudProfile &comCloudProfile, listCloudProfiles(comCloudProvider))
        {
            /* Skip if we have nothing to populate: */
            if (comCloudProfile.isNull())
                continue;

            /* Load profile data: */
            UIDataCloudProfile profileData;
            loadCloudProfile(comCloudProfile, restrictions, providerData, profileData);
            createItemForCloudProfile(pItem, profileData);
        }

        /* Expand provider item finally: */
        pItem->setExpanded(true);
    }

    /* Try to restore current item by definition: */
    if (!strDefinition.isEmpty())
        m_pTreeWidget->setCurrentItem(searchItem(strDefinition));
    /* Choose the 1st item as current if nothing chosen: */
    if (!m_pTreeWidget->currentItem())
        m_pTreeWidget->setCurrentItem(m_pTreeWidget->topLevelItem(0));
    /* Handle current item change in any case: */
    sltHandleCurrentItemChange();
}

void UICloudProfileManagerWidget::loadCloudProvider(const CCloudProvider &comProvider,
                                                    const QStringList &restrictions,
                                                    UIDataCloudProvider &providerData)
{
    /* Gather provider settings: */
    if (comProvider.isOk())
        cloudProviderId(comProvider, providerData.m_uId);
    if (comProvider.isOk())
        cloudProviderShortName(comProvider, providerData.m_strShortName);
    if (comProvider.isOk())
        cloudProviderName(comProvider, providerData.m_strName);
    providerData.m_fRestricted = restrictions.contains(UIItemCloudProvider::definition(providerData.m_strShortName));
    foreach (const QString &strSupportedPropertyName, comProvider.GetSupportedPropertyNames())
        providerData.m_propertyDescriptions[strSupportedPropertyName] = comProvider.GetPropertyDescription(strSupportedPropertyName);
}

void UICloudProfileManagerWidget::loadCloudProfile(const CCloudProfile &comProfile,
                                                   const QStringList &restrictions,
                                                   const UIDataCloudProvider &providerData,
                                                   UIDataCloudProfile &profileData)
{
    /* Gather provider settings: */
    profileData.m_strProviderShortName = providerData.m_strShortName;

    /* Gather profile settings: */
    if (comProfile.isOk())
        cloudProfileName(comProfile, profileData.m_strName);
    profileData.m_fRestricted = restrictions.contains(UIItemCloudProfile::definition(providerData.m_strShortName, profileData.m_strName));
    if (comProfile.isOk())
    {
        QVector<QString> keys;
        QVector<QString> values;
        if (cloudProfileProperties(comProfile, keys, values))
            for (int i = 0; i < keys.size(); ++i)
                profileData.m_data[keys.at(i)] = qMakePair(values.at(i), providerData.m_propertyDescriptions.value(keys.at(i)));
    }
}

QTreeWidgetItem *UICloudProfileManagerWidget::searchItem(const QString &strDefinition,
                                                         QTreeWidgetItem *pParentItem /* = 0 */) const
{
    /* If no parent-item passed => we will start from the invisible-root-item: */
    if (!pParentItem)
        pParentItem = m_pTreeWidget->invisibleRootItem();

    /* Check whether parent-item is of required type: */
    QITreeWidgetItem *pParentItemOfType = QITreeWidgetItem::toItem(pParentItem);
    if (pParentItemOfType)
    {
        /* Check if parent-item has required definition: */
        if (pParentItemOfType->data(Column_Name, Data_Definition).toString() == strDefinition)
            return pParentItem;
    }

    /* Iterate through parent-item children: */
    for (int i = 0; i < pParentItem->childCount(); ++i)
        if (QTreeWidgetItem *pChildItem = searchItem(strDefinition, pParentItem->child(i)))
            return pChildItem;

    /* Null by default: */
    return 0;
}

void UICloudProfileManagerWidget::createItemForCloudProvider(const UIDataCloudProvider &providerData)
{
    /* Create new provider item: */
    UIItemCloudProvider *pItem = new UIItemCloudProvider;
    if (pItem)
    {
        /* Configure item: */
        pItem->UIDataCloudProvider::operator=(providerData);
        pItem->updateFields();
        /* Add item to the tree: */
        m_pTreeWidget->addTopLevelItem(pItem);
    }
}

void UICloudProfileManagerWidget::createItemForCloudProfile(QTreeWidgetItem *pParent,
                                                            const UIDataCloudProfile &profileData)
{
    /* Create new profile item: */
    UIItemCloudProfile *pItem = new UIItemCloudProfile;
    if (pItem)
    {
        /* Configure item: */
        pItem->UIDataCloudProfile::operator=(profileData);
        pItem->updateFields();
        /* Add item to the parent: */
        pParent->addChild(pItem);
    }
}

QStringList UICloudProfileManagerWidget::gatherCloudProfileManagerRestrictions(QTreeWidgetItem *pParentItem)
{
    /* Prepare result: */
    QStringList result;
    AssertPtrReturn(pParentItem, result);

    /* Process unchecked QITreeWidgetItem(s) only: */
    QITreeWidgetItem *pChangedItem = QITreeWidgetItem::toItem(pParentItem);
    if (   pChangedItem
        && pChangedItem->checkState(Column_ListVMs) == Qt::Unchecked)
        result << pChangedItem->data(Column_Name, Data_Definition).toString();

    /* Iterate through children recursively: */
    for (int i = 0; i < pParentItem->childCount(); ++i)
        result << gatherCloudProfileManagerRestrictions(pParentItem->child(i));

    /* Return result: */
    return result;
}


/*********************************************************************************************************************************
*   Class UICloudProfileManagerFactory implementation.                                                                           *
*********************************************************************************************************************************/

UICloudProfileManagerFactory::UICloudProfileManagerFactory(UIActionPool *pActionPool /* = 0 */)
    : m_pActionPool(pActionPool)
{
}

void UICloudProfileManagerFactory::create(QIManagerDialog *&pDialog, QWidget *pCenterWidget)
{
    pDialog = new UICloudProfileManager(pCenterWidget, m_pActionPool);
}


/*********************************************************************************************************************************
*   Class UICloudProfileManager implementation.                                                                                  *
*********************************************************************************************************************************/

UICloudProfileManager::UICloudProfileManager(QWidget *pCenterWidget, UIActionPool *pActionPool)
    : QIManagerDialog(pCenterWidget)
    , m_pActionPool(pActionPool)
{
}

void UICloudProfileManager::sltHandleButtonBoxClick(QAbstractButton *pButton)
{
    /* Disable buttons first of all: */
    button(ButtonType_Reset)->setEnabled(false);
    button(ButtonType_Apply)->setEnabled(false);

    /* Compare with known buttons: */
    if (pButton == button(ButtonType_Reset))
        emit sigDataChangeRejected();
    else
    if (pButton == button(ButtonType_Apply))
        emit sigDataChangeAccepted();
}

void UICloudProfileManager::sltRetranslateUI()
{
    /* Translate window title: */
    setWindowTitle(tr("Cloud Profile Manager"));

    /* Translate buttons: */
    button(ButtonType_Reset)->setText(tr("Reset"));
    button(ButtonType_Apply)->setText(tr("Apply"));
    button(ButtonType_Close)->setText(tr("Close"));
    button(ButtonType_Help)->setText(tr("Help"));
    button(ButtonType_Reset)->setStatusTip(tr("Reset changes in current cloud profile details"));
    button(ButtonType_Apply)->setStatusTip(tr("Apply changes in current cloud profile details"));
    button(ButtonType_Close)->setStatusTip(tr("Close dialog without saving"));
    button(ButtonType_Help)->setStatusTip(tr("Show dialog help"));
    button(ButtonType_Reset)->setShortcut(QString("Ctrl+Backspace"));
    button(ButtonType_Apply)->setShortcut(QString("Ctrl+Return"));
    button(ButtonType_Close)->setShortcut(Qt::Key_Escape);
    button(ButtonType_Help)->setShortcut(UIShortcutPool::standardSequence(QKeySequence::HelpContents));
    button(ButtonType_Reset)->setToolTip(tr("Reset Changes (%1)").arg(button(ButtonType_Reset)->shortcut().toString()));
    button(ButtonType_Apply)->setToolTip(tr("Apply Changes (%1)").arg(button(ButtonType_Apply)->shortcut().toString()));
    button(ButtonType_Close)->setToolTip(tr("Close Window (%1)").arg(button(ButtonType_Close)->shortcut().toString()));
    button(ButtonType_Help)->setToolTip(tr("Show Help (%1)").arg(button(ButtonType_Help)->shortcut().toString()));
}

void UICloudProfileManager::configure()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/cloud_profile_manager_32px.png", ":/cloud_profile_manager_16px.png"));
#endif
}

void UICloudProfileManager::configureCentralWidget()
{
    /* Create widget: */
    UICloudProfileManagerWidget *pWidget = new UICloudProfileManagerWidget(EmbedTo_Dialog, m_pActionPool, true, this);
    if (pWidget)
    {
        /* Configure widget: */
        setWidget(pWidget);
        setWidgetMenu(pWidget->menu());
#ifdef VBOX_WS_MAC
        setWidgetToolbar(pWidget->toolbar());
#endif
        connect(this, &UICloudProfileManager::sigDataChangeRejected,
                pWidget, &UICloudProfileManagerWidget::sltResetCloudProfileDetailsChanges);
        connect(this, &UICloudProfileManager::sigDataChangeAccepted,
                pWidget, &UICloudProfileManagerWidget::sltApplyCloudProfileDetailsChanges);

        /* Add into layout: */
        centralWidget()->layout()->addWidget(pWidget);
    }
}

void UICloudProfileManager::configureButtonBox()
{
    /* Configure button-box: */
    connect(widget(), &UICloudProfileManagerWidget::sigCloudProfileDetailsVisibilityChanged,
            button(ButtonType_Apply), &QPushButton::setVisible);
    connect(widget(), &UICloudProfileManagerWidget::sigCloudProfileDetailsVisibilityChanged,
            button(ButtonType_Reset), &QPushButton::setVisible);
    connect(widget(), &UICloudProfileManagerWidget::sigCloudProfileDetailsDataChanged,
            button(ButtonType_Apply), &QPushButton::setEnabled);
    connect(widget(), &UICloudProfileManagerWidget::sigCloudProfileDetailsDataChanged,
            button(ButtonType_Reset), &QPushButton::setEnabled);
    connect(buttonBox(), &QIDialogButtonBox::clicked,
            this, &UICloudProfileManager::sltHandleButtonBoxClick);
    // WORKAROUND:
    // Since we connected signals later than extra-data loaded
    // for signals above, we should handle that stuff here again:
    button(ButtonType_Apply)->setVisible(gEDataManager->cloudProfileManagerDetailsExpanded());
    button(ButtonType_Reset)->setVisible(gEDataManager->cloudProfileManagerDetailsExpanded());
}

void UICloudProfileManager::finalize()
{
    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloudProfileManager::sltRetranslateUI);
}

UICloudProfileManagerWidget *UICloudProfileManager::widget()
{
    return qobject_cast<UICloudProfileManagerWidget*>(QIManagerDialog::widget());
}

void UICloudProfileManager::closeEvent(QCloseEvent *pEvent)
{
    /* Make sure all changes resolved: */
    if (widget()->makeSureChangesResolved())
    {
        /* Call to base class: */
        QIManagerDialog::closeEvent(pEvent);
    }
    else
    {
        /* Just ignore the event otherwise: */
        pEvent->ignore();
    }
}


#include "UICloudProfileManager.moc"
