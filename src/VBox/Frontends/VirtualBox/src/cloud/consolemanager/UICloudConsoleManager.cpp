/* $Id: UICloudConsoleManager.cpp 107666 2025-01-09 15:52:29Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudConsoleManager class implementation.
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
#include <QDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QUuid>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QITreeWidget.h"
#include "UIActionPoolManager.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UICloudConsoleDetailsWidget.h"
#include "UICloudConsoleManager.h"
#include "UIMessageCenter.h"
#include "UIShortcutPool.h"
#include "UITranslationEventListener.h"
#include "QIToolBar.h"


/** Tree-widget item types. */
enum CloudConsoleItemType
{
    CloudConsoleItemType_Invalid     = 0,
    CloudConsoleItemType_Application = 1,
    CloudConsoleItemType_Profile     = 2
};
Q_DECLARE_METATYPE(CloudConsoleItemType);

/** Tree-widget data types. */
enum
{
    Data_ItemType   = Qt::UserRole + 1,
    Data_ItemID     = Qt::UserRole + 2,
    Data_Definition = Qt::UserRole + 3,
};

/** Tree-widget column types. */
enum
{
    Column_Name,
    Column_ListInMenu,
    Column_Max
};


/** Cloud Console Manager application's tree-widget item. */
class UIItemCloudConsoleApplication : public QITreeWidgetItem, public UIDataCloudConsoleApplication
{
    Q_OBJECT;

public:

    /** Constructs item. */
    UIItemCloudConsoleApplication();

    /** Updates item fields from base-class data. */
    void updateFields();

    /** Returns item id. */
    QString id() const { return m_strId; }
    /** Returns item name. */
    QString name() const { return m_strName; }
    /** Returns item path. */
    QString path() const { return m_strPath; }
    /** Returns item argument. */
    QString argument() const { return m_strArgument; }
};

/** Cloud Console Manager profile's tree-widget item. */
class UIItemCloudConsoleProfile : public QITreeWidgetItem, public UIDataCloudConsoleProfile
{
    Q_OBJECT;

public:

    /** Constructs item. */
    UIItemCloudConsoleProfile();

    /** Updates item fields from base-class data. */
    void updateFields();

    /** Returns item application id. */
    QString applicationId() const { return m_strApplicationId; }
    /** Returns item id. */
    QString id() const { return m_strId; }
    /** Returns item name. */
    QString name() const { return m_strName; }
    /** Returns item argument. */
    QString argument() const { return m_strArgument; }
};

/** QDialog extension used to acquire newly created console application parameters. */
class UIInputDialogCloudConsoleApplication : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs dialog passing @a pParent to the base-class. */
    UIInputDialogCloudConsoleApplication(QWidget *pParent);

    /** Returns application name. */
    QString name() const;
    /** Returns application path. */
    QString path() const;
    /** Returns application argument. */
    QString argument() const;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();

    /** Holds the name label instance. */
    QLabel    *m_pLabelName;
    /** Holds the name editor instance. */
    QLineEdit *m_pEditorName;
    /** Holds the path label instance. */
    QLabel    *m_pLabelPath;
    /** Holds the path editor instance. */
    QLineEdit *m_pEditorPath;
    /** Holds the argument label instance. */
    QLabel    *m_pLabelArgument;
    /** Holds the argument editor instance. */
    QLineEdit *m_pEditorArgument;

    /** Holds the button-box instance. */
    QIDialogButtonBox *m_pButtonBox;
};

/** QDialog extension used to acquire newly created console profile parameters. */
class UIInputDialogCloudConsoleProfile : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs dialog passing @a pParent to the base-class. */
    UIInputDialogCloudConsoleProfile(QWidget *pParent);

    /** Returns profile name. */
    QString name() const;
    /** Returns profile argument. */
    QString argument() const;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();

    /** Holds the name label instance. */
    QLabel    *m_pLabelName;
    /** Holds the name editor instance. */
    QLineEdit *m_pEditorName;
    /** Holds the argument label instance. */
    QLabel    *m_pLabelArgument;
    /** Holds the argument editor instance. */
    QLineEdit *m_pEditorArgument;

    /** Holds the button-box instance. */
    QIDialogButtonBox *m_pButtonBox;
};


/*********************************************************************************************************************************
*   Class UIItemCloudConsoleApplication implementation.                                                                          *
*********************************************************************************************************************************/

UIItemCloudConsoleApplication::UIItemCloudConsoleApplication()
{
    /* Assign icon: */
    setIcon(Column_Name, UIIconPool::iconSet(":/cloud_console_application_16px.png"));
    /* Assign item data: */
    setData(Column_Name, Data_ItemType, QVariant::fromValue(CloudConsoleItemType_Application));
}

void UIItemCloudConsoleApplication::updateFields()
{
    /* Update item fields: */
    setText(Column_Name, m_strName);
    setData(Column_Name, Data_ItemID, m_strId);
    setData(Column_Name, Data_Definition, QVariant::fromValue(QString("/%1").arg(m_strId)));
    setCheckState(Column_ListInMenu, m_fRestricted ? Qt::Unchecked : Qt::Checked);
}


/*********************************************************************************************************************************
*   Class UIItemCloudConsoleProfile implementation.                                                                              *
*********************************************************************************************************************************/

UIItemCloudConsoleProfile::UIItemCloudConsoleProfile()
{
    /* Assign icon: */
    setIcon(Column_Name, UIIconPool::iconSet(":/cloud_console_profile_16px.png"));
    /* Assign item data: */
    setData(Column_Name, Data_ItemType, QVariant::fromValue(CloudConsoleItemType_Profile));
}

void UIItemCloudConsoleProfile::updateFields()
{
    /* Update item fields: */
    setText(Column_Name, m_strName);
    setData(Column_Name, Data_ItemID, m_strId);
    setData(Column_Name, Data_Definition, QVariant::fromValue(QString("/%1/%2").arg(m_strApplicationId, m_strId)));
    setCheckState(Column_ListInMenu, m_fRestricted ? Qt::Unchecked : Qt::Checked);
}


/*********************************************************************************************************************************
*   Class UIInputDialogCloudConsoleApplication implementation.                                                                   *
*********************************************************************************************************************************/

UIInputDialogCloudConsoleApplication::UIInputDialogCloudConsoleApplication(QWidget *pParent)
    : QDialog(pParent)
    , m_pLabelName(0)
    , m_pEditorName(0)
    , m_pLabelPath(0)
    , m_pEditorPath(0)
    , m_pLabelArgument(0)
    , m_pEditorArgument(0)
    , m_pButtonBox(0)
{
    prepare();
}

QString UIInputDialogCloudConsoleApplication::name() const
{
    return m_pEditorName->text();
}

QString UIInputDialogCloudConsoleApplication::path() const
{
    return m_pEditorPath->text();
}

QString UIInputDialogCloudConsoleApplication::argument() const
{
    return m_pEditorArgument->text();
}

void UIInputDialogCloudConsoleApplication::sltRetranslateUI()
{
    setWindowTitle(UICloudConsoleManager::tr("Add Application"));
    m_pLabelName->setText(UICloudConsoleManager::tr("Name:"));
    m_pLabelPath->setText(UICloudConsoleManager::tr("Path:"));
    m_pLabelArgument->setText(UICloudConsoleManager::tr("Argument:"));
}

void UIInputDialogCloudConsoleApplication::prepare()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/cloud_console_application_add_32px.png" ,":/cloud_console_application_add_16px.png"));
#endif

    /* Prepare main layout: */
    QGridLayout *pMainLayout = new QGridLayout(this);
    if (pMainLayout)
    {
        pMainLayout->setRowStretch(3, 1);

        /* Prepare name editor: */
        m_pEditorName = new QLineEdit(this);
        if (m_pEditorName)
        {
            pMainLayout->addWidget(m_pEditorName, 0, 1);
        }
        /* Prepare name editor label: */
        m_pLabelName = new QLabel(this);
        if (m_pLabelName)
        {
            m_pLabelName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLabelName->setBuddy(m_pEditorName);
            pMainLayout->addWidget(m_pLabelName, 0, 0);
        }

        /* Prepare path editor: */
        m_pEditorPath = new QLineEdit(this);
        if (m_pEditorPath)
        {
            pMainLayout->addWidget(m_pEditorPath, 1, 1);
        }
        /* Prepare path editor label: */
        m_pLabelPath = new QLabel(this);
        if (m_pLabelPath)
        {
            m_pLabelPath->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLabelPath->setBuddy(m_pEditorPath);
            pMainLayout->addWidget(m_pLabelPath, 1, 0);
        }

        /* Prepare argument editor: */
        m_pEditorArgument = new QLineEdit(this);
        if (m_pEditorArgument)
        {
            pMainLayout->addWidget(m_pEditorArgument, 2, 1);
        }
        /* Prepare argument editor label: */
        m_pLabelArgument = new QLabel(this);
        if (m_pLabelArgument)
        {
            m_pLabelArgument->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLabelArgument->setBuddy(m_pEditorArgument);
            pMainLayout->addWidget(m_pLabelArgument, 2, 0);
        }

        /* Prepare button-box: */
        m_pButtonBox = new QIDialogButtonBox(this);
        if  (m_pButtonBox)
        {
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
            connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &UIInputDialogCloudConsoleApplication::reject);
            connect(m_pButtonBox, &QIDialogButtonBox::accepted, this, &UIInputDialogCloudConsoleApplication::accept);
            pMainLayout->addWidget(m_pButtonBox, 4, 0, 1, 2);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIInputDialogCloudConsoleApplication::sltRetranslateUI);

    /* Resize to suitable size: */
    const int iMinimumHeightHint = minimumSizeHint().height();
    resize(iMinimumHeightHint * 3, iMinimumHeightHint);
}


/*********************************************************************************************************************************
*   Class UIInputDialogCloudConsoleProfile implementation.                                                                       *
*********************************************************************************************************************************/

UIInputDialogCloudConsoleProfile::UIInputDialogCloudConsoleProfile(QWidget *pParent)
    : QDialog(pParent)
    , m_pLabelName(0)
    , m_pEditorName(0)
    , m_pLabelArgument(0)
    , m_pEditorArgument(0)
    , m_pButtonBox(0)
{
    prepare();
}

QString UIInputDialogCloudConsoleProfile::name() const
{
    return m_pEditorName->text();
}

QString UIInputDialogCloudConsoleProfile::argument() const
{
    return m_pEditorArgument->text();
}

void UIInputDialogCloudConsoleProfile::sltRetranslateUI()
{
    setWindowTitle(UICloudConsoleManager::tr("Add Profile"));
    m_pLabelName->setText(UICloudConsoleManager::tr("Name:"));
    m_pLabelArgument->setText(UICloudConsoleManager::tr("Argument:"));
}

void UIInputDialogCloudConsoleProfile::prepare()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/cloud_console_profile_add_32px.png", ":/cloud_console_profile_add_16px.png"));
#endif

    /* Prepare main layout: */
    QGridLayout *pMainLayout = new QGridLayout(this);
    if (pMainLayout)
    {
        pMainLayout->setRowStretch(0, 0);
        pMainLayout->setRowStretch(1, 0);
        pMainLayout->setRowStretch(2, 1);
        pMainLayout->setRowStretch(3, 0);

        /* Prepare name editor: */
        m_pEditorName = new QLineEdit(this);
        if (m_pEditorName)
        {
            pMainLayout->addWidget(m_pEditorName, 0, 1);
        }
        /* Prepare name editor label: */
        m_pLabelName = new QLabel(this);
        if (m_pLabelName)
        {
            m_pLabelName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLabelName->setBuddy(m_pEditorName);
            pMainLayout->addWidget(m_pLabelName, 0, 0);
        }

        /* Prepare argument editor: */
        m_pEditorArgument = new QLineEdit(this);
        if (m_pEditorArgument)
        {
            pMainLayout->addWidget(m_pEditorArgument, 1, 1);
        }
        /* Prepare argument editor label: */
        m_pLabelArgument = new QLabel(this);
        if (m_pLabelArgument)
        {
            m_pLabelArgument->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLabelArgument->setBuddy(m_pEditorArgument);
            pMainLayout->addWidget(m_pLabelArgument, 1, 0);
        }

        /* Prepare button-box: */
        m_pButtonBox = new QIDialogButtonBox(this);
        if  (m_pButtonBox)
        {
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
            connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &UIInputDialogCloudConsoleApplication::reject);
            connect(m_pButtonBox, &QIDialogButtonBox::accepted, this, &UIInputDialogCloudConsoleApplication::accept);
            pMainLayout->addWidget(m_pButtonBox, 3, 0, 1, 2);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIInputDialogCloudConsoleProfile::sltRetranslateUI);

    /* Resize to suitable size: */
    const int iMinimumHeightHint = minimumSizeHint().height();
    resize(iMinimumHeightHint * 3, iMinimumHeightHint);
}


/*********************************************************************************************************************************
*   Class UICloudConsoleManagerWidget implementation.                                                                            *
*********************************************************************************************************************************/

UICloudConsoleManagerWidget::UICloudConsoleManagerWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
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

QMenu *UICloudConsoleManagerWidget::menu() const
{
    return m_pActionPool->action(UIActionIndexMN_M_CloudConsoleWindow)->menu();
}

void UICloudConsoleManagerWidget::sltRetranslateUI()
{
    /* Translate tree-widget: */
    m_pTreeWidget->setHeaderLabels(   QStringList()
                                   << UICloudConsoleManager::tr("Application")
                                   << UICloudConsoleManager::tr("List in Menu"));
}

void UICloudConsoleManagerWidget::sltResetCloudConsoleDetailsChanges()
{
    /* Just push the current-item data there again: */
    sltHandleCurrentItemChange();
}

void UICloudConsoleManagerWidget::sltApplyCloudConsoleDetailsChanges()
{
    /* Check current-item type: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    AssertMsgReturnVoid(pItem, ("Current item must not be null!\n"));
    switch (pItem->data(Column_Name, Data_ItemType).value<CloudConsoleItemType>())
    {
        case CloudConsoleItemType_Application:
        {
            /* Save application changes: */
            UIItemCloudConsoleApplication *pItemApplication = qobject_cast<UIItemCloudConsoleApplication*>(pItem);
            AssertPtrReturnVoid(pItemApplication);
            const UIDataCloudConsoleApplication oldData = *pItemApplication;
            const UIDataCloudConsoleApplication newData = m_pDetailsWidget->applicationData();
            /* Save application settings if changed: */
            if (newData != oldData)
                gEDataManager->setCloudConsoleManagerApplication(newData.m_strId,
                                                                 QString("%1,%2,%3").arg(newData.m_strName,
                                                                                         newData.m_strPath,
                                                                                         newData.m_strArgument));
            break;
        }
        case CloudConsoleItemType_Profile:
        {
            /* Save profile changes: */
            UIItemCloudConsoleProfile *pItemProfile = qobject_cast<UIItemCloudConsoleProfile*>(pItem);
            AssertPtrReturnVoid(pItemProfile);
            const UIDataCloudConsoleProfile oldData = *pItemProfile;
            const UIDataCloudConsoleProfile newData = m_pDetailsWidget->profileData();
            /* Save profile settings if changed: */
            if (newData != oldData)
                gEDataManager->setCloudConsoleManagerProfile(newData.m_strApplicationId,
                                                             newData.m_strId,
                                                             QString("%1,%2").arg(newData.m_strName, newData.m_strArgument));
            break;
        }
        case CloudConsoleItemType_Invalid:
            break;
    }
}

void UICloudConsoleManagerWidget::sltAddCloudConsoleApplication()
{
    /* Acquire application attributes: */
    QString strId;
    QString strApplicationName;
    QString strApplicationPath;
    QString strApplicationArgument;
    bool fCancelled = true;
    QPointer<UIInputDialogCloudConsoleApplication> pDialog = new UIInputDialogCloudConsoleApplication(this);
    if (pDialog)
    {
        if (pDialog->exec() == QDialog::Accepted)
        {
            strId = QUuid::createUuid().toString().remove(QRegularExpression("[{}]"));
            strApplicationName = pDialog->name();
            strApplicationPath = pDialog->path();
            strApplicationArgument = pDialog->argument();
            fCancelled = false;
        }
        delete pDialog;
    }
    if (fCancelled)
        return;

    /* Update current-item definition: */
    m_strDefinition = QString("/%1").arg(strId);
    /* Compose extra-data superset: */
    const QString strValue = QString("%1,%2,%3").arg(strApplicationName, strApplicationPath, strApplicationArgument);

    /* Save new console application to extra-data: */
    gEDataManager->setCloudConsoleManagerApplication(strId, strValue);
}

void UICloudConsoleManagerWidget::sltRemoveCloudConsoleApplication()
{
    /* Get console application item: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemCloudConsoleApplication *pItemApplication = qobject_cast<UIItemCloudConsoleApplication*>(pItem);
    AssertMsgReturnVoid(pItemApplication, ("Application item must not be null!\n"));
    const QString strApplicationId = pItemApplication->id();

    /* Confirm cloud console application removal: */
    if (!msgCenter().confirmCloudConsoleApplicationRemoval(pItemApplication->name(), this))
        return;

    /* Enumerate all the application profiles: */
    for (int i = 0; i < pItemApplication->childCount(); ++i)
    {
        /* Get console profile item: */
        QITreeWidgetItem *pItem = pItemApplication->childItem(i);
        UIItemCloudConsoleProfile *pItemProfile = qobject_cast<UIItemCloudConsoleProfile*>(pItem);
        AssertMsgReturnVoid(pItemProfile, ("Profile item must not be null!\n"));

        /* Delete profile from extra-data: */
        gEDataManager->setCloudConsoleManagerProfile(strApplicationId, pItemProfile->id(), QString());
    }

    /* Delete application from extra-data: */
    gEDataManager->setCloudConsoleManagerApplication(strApplicationId, QString());
}

void UICloudConsoleManagerWidget::sltAddCloudConsoleProfile()
{
    /* Check current-item type: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    AssertMsgReturnVoid(pItem, ("Current item must not be null!\n"));
    UIItemCloudConsoleApplication *pItemApplication = 0;
    switch (pItem->data(Column_Name, Data_ItemType).value<CloudConsoleItemType>())
    {
        case CloudConsoleItemType_Application:
            pItemApplication = qobject_cast<UIItemCloudConsoleApplication*>(pItem);
            break;
        case CloudConsoleItemType_Profile:
            pItemApplication = qobject_cast<UIItemCloudConsoleApplication*>(pItem->parentItem());
            break;
        case CloudConsoleItemType_Invalid:
            break;
    }
    AssertMsgReturnVoid(pItemApplication, ("Application item must not be null!\n"));
    const QString strApplicationId = pItemApplication->id();

    /* Acquire profile attributes: */
    QString strId;
    QString strProfileName;
    QString strProfileArgument;
    bool fCancelled = true;
    QPointer<UIInputDialogCloudConsoleProfile> pDialog = new UIInputDialogCloudConsoleProfile(this);
    if (pDialog)
    {
        if (pDialog->exec() == QDialog::Accepted)
        {
            strId = QUuid::createUuid().toString().remove(QRegularExpression("[{}]"));
            strProfileName = pDialog->name();
            strProfileArgument = pDialog->argument();
            fCancelled = false;
        }
        delete pDialog;
    }
    if (fCancelled)
        return;

    /* Update current-item definition: */
    m_strDefinition = QString("/%1/%2").arg(strApplicationId, strId);
    /* Compose extra-data superset: */
    const QString strValue = QString("%1,%2").arg(strProfileName, strProfileArgument);

    /* Save new console profile to extra-data: */
    gEDataManager->setCloudConsoleManagerProfile(strApplicationId, strId, strValue);
}

void UICloudConsoleManagerWidget::sltRemoveCloudConsoleProfile()
{
    /* Get console profile item: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemCloudConsoleProfile *pItemProfile = qobject_cast<UIItemCloudConsoleProfile*>(pItem);
    AssertMsgReturnVoid(pItemProfile, ("Profile item must not be null!\n"));

    /* Confirm cloud console profile removal: */
    if (!msgCenter().confirmCloudConsoleProfileRemoval(pItemProfile->name(), this))
        return;

    /* Delete profile from extra-data: */
    gEDataManager->setCloudConsoleManagerProfile(pItemProfile->applicationId(), pItemProfile->id(), QString());
}

void UICloudConsoleManagerWidget::sltToggleCloudConsoleDetailsVisibility(bool fVisible)
{
    /* Save the setting: */
    gEDataManager->setCloudConsoleManagerDetailsExpanded(fVisible);
    /* Show/hide details area and Apply button: */
    m_pDetailsWidget->setVisible(fVisible);
    /* Notify external lsiteners: */
    emit sigCloudConsoleDetailsVisibilityChanged(fVisible);
}

void UICloudConsoleManagerWidget::sltPerformTableAdjustment()
{
    AssertPtrReturnVoid(m_pTreeWidget);
    AssertPtrReturnVoid(m_pTreeWidget->header());
    AssertPtrReturnVoid(m_pTreeWidget->viewport());
    m_pTreeWidget->header()->resizeSection(0, m_pTreeWidget->viewport()->width() - m_pTreeWidget->header()->sectionSize(1));
}

void UICloudConsoleManagerWidget::sltHandleCurrentItemChange()
{
    /* Check current-item type: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemCloudConsoleApplication *pItemApplication = qobject_cast<UIItemCloudConsoleApplication*>(pItem);
    UIItemCloudConsoleProfile *pItemProfile = qobject_cast<UIItemCloudConsoleProfile*>(pItem);

    /* Update actions availability: */
    m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationAdd)->setEnabled(!pItem || pItemApplication);
    m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationRemove)->setEnabled(pItemApplication);
    m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileAdd)->setEnabled(pItemApplication || pItemProfile);
    m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileRemove)->setEnabled(pItemProfile);
    m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details)->setEnabled(pItemApplication || pItemProfile);

    /* Update current-item definition: */
    if (pItem)
        m_strDefinition = pItem->data(Column_Name, Data_Definition).toString();

    /* Update details data: */
    if (pItemApplication)
        m_pDetailsWidget->setApplicationData(*pItemApplication);
    else if (pItemProfile)
        m_pDetailsWidget->setProfileData(*pItemProfile);
    else
        m_pDetailsWidget->clearData();

    /* Update details area visibility: */
    sltToggleCloudConsoleDetailsVisibility(pItem && m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details)->isChecked());
}

void UICloudConsoleManagerWidget::sltHandleContextMenuRequest(const QPoint &position)
{
    /* Check item type: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->itemAt(position));
    UIItemCloudConsoleApplication *pItemApplication = qobject_cast<UIItemCloudConsoleApplication*>(pItem);
    UIItemCloudConsoleProfile *pItemProfile = qobject_cast<UIItemCloudConsoleProfile*>(pItem);

    /* Compose temporary context-menu: */
    QMenu menu;
    if (pItemApplication)
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationRemove));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileAdd));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details));
    }
    else if (pItemProfile)
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileRemove));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details));
    }
    else
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationAdd));

    /* And show it: */
    menu.exec(m_pTreeWidget->viewport()->mapToGlobal(position));
}

void UICloudConsoleManagerWidget::sltHandleItemChange(QTreeWidgetItem *pItem)
{
    /* Check item type: */
    QITreeWidgetItem *pChangedItem = QITreeWidgetItem::toItem(pItem);
    UIItemCloudConsoleApplication *pItemApplication = qobject_cast<UIItemCloudConsoleApplication*>(pChangedItem);
    UIItemCloudConsoleProfile *pItemProfile = qobject_cast<UIItemCloudConsoleProfile*>(pChangedItem);

    /* Check whether item is of application or profile type, then check whether it changed: */
    bool fChanged = false;
    if (pItemApplication)
    {
        const UIDataCloudConsoleApplication oldData = *pItemApplication;
        if (   (oldData.m_fRestricted && pItemApplication->checkState(Column_ListInMenu) == Qt::Checked)
            || (!oldData.m_fRestricted && pItemApplication->checkState(Column_ListInMenu) == Qt::Unchecked))
            fChanged = true;
    }
    else if (pItemProfile)
    {
        const UIDataCloudConsoleProfile oldData = *pItemProfile;
        if (   (oldData.m_fRestricted && pItemProfile->checkState(Column_ListInMenu) == Qt::Checked)
            || (!oldData.m_fRestricted && pItemProfile->checkState(Column_ListInMenu) == Qt::Unchecked))
            fChanged = true;
    }

    /* Gather Cloud Console Manager restrictions and save them to extra-data: */
    if (fChanged)
        gEDataManager->setCloudConsoleManagerRestrictions(gatherCloudConsoleManagerRestrictions(m_pTreeWidget->invisibleRootItem()));
}

void UICloudConsoleManagerWidget::prepare()
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
            this, &UICloudConsoleManagerWidget::sltRetranslateUI);

    /* Load cloud console stuff: */
    loadCloudConsoleStuff();
}

void UICloudConsoleManagerWidget::prepareActions()
{
    /* First of all, add actions which has smaller shortcut scope: */
    addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationAdd));
    addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationRemove));
    addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileAdd));
    addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileRemove));
    addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details));
}

void UICloudConsoleManagerWidget::prepareWidgets()
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

void UICloudConsoleManagerWidget::prepareToolBar()
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
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationAdd));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationRemove));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileAdd));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileRemove));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details));

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

void UICloudConsoleManagerWidget::prepareTreeWidget()
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

void UICloudConsoleManagerWidget::prepareDetailsWidget()
{
    /* Create details-widget: */
    m_pDetailsWidget = new UICloudConsoleDetailsWidget(m_enmEmbedding);
    if (m_pDetailsWidget)
    {
        /* Configure details-widget: */
        m_pDetailsWidget->setVisible(false);
        m_pDetailsWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

        /* Add into layout: */
        layout()->addWidget(m_pDetailsWidget);
    }
}

void UICloudConsoleManagerWidget::prepareConnections()
{
    /* Action connections: */
    connect(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationAdd), &QAction::triggered,
            this, &UICloudConsoleManagerWidget::sltAddCloudConsoleApplication);
    connect(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ApplicationRemove), &QAction::triggered,
            this, &UICloudConsoleManagerWidget::sltRemoveCloudConsoleApplication);
    connect(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileAdd), &QAction::triggered,
            this, &UICloudConsoleManagerWidget::sltAddCloudConsoleProfile);
    connect(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_S_ProfileRemove), &QAction::triggered,
            this, &UICloudConsoleManagerWidget::sltRemoveCloudConsoleProfile);
    connect(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details), &QAction::toggled,
            this, &UICloudConsoleManagerWidget::sltToggleCloudConsoleDetailsVisibility);

    /* Tree-widget connections: */
    connect(m_pTreeWidget, &QITreeWidget::resized,
            this, &UICloudConsoleManagerWidget::sltPerformTableAdjustment, Qt::QueuedConnection);
    connect(m_pTreeWidget->header(), &QHeaderView::sectionResized,
            this, &UICloudConsoleManagerWidget::sltPerformTableAdjustment, Qt::QueuedConnection);
    connect(m_pTreeWidget, &QITreeWidget::currentItemChanged,
            this, &UICloudConsoleManagerWidget::sltHandleCurrentItemChange);
    connect(m_pTreeWidget, &QITreeWidget::customContextMenuRequested,
            this, &UICloudConsoleManagerWidget::sltHandleContextMenuRequest);
    connect(m_pTreeWidget, &QITreeWidget::itemDoubleClicked,
            m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details), &QAction::setChecked);
    connect(m_pTreeWidget, &QITreeWidget::itemChanged,
            this, &UICloudConsoleManagerWidget::sltHandleItemChange);

    /* Details-widget connections: */
    connect(m_pDetailsWidget, &UICloudConsoleDetailsWidget::sigDataChanged,
            this, &UICloudConsoleManagerWidget::sigCloudConsoleDetailsDataChanged);
    connect(m_pDetailsWidget, &UICloudConsoleDetailsWidget::sigDataChangeRejected,
            this, &UICloudConsoleManagerWidget::sltResetCloudConsoleDetailsChanges);
    connect(m_pDetailsWidget, &UICloudConsoleDetailsWidget::sigDataChangeAccepted,
            this, &UICloudConsoleManagerWidget::sltApplyCloudConsoleDetailsChanges);

    /* Extra-data connections: */
    connect(gEDataManager, &UIExtraDataManager::sigCloudConsoleManagerDataChange,
            this, &UICloudConsoleManagerWidget::sltLoadCloudConsoleStuff);
    connect(gEDataManager, &UIExtraDataManager::sigCloudConsoleManagerRestrictionChange,
            this, &UICloudConsoleManagerWidget::sltLoadCloudConsoleStuff);
}

void UICloudConsoleManagerWidget::loadSettings()
{
    /* Details action/widget: */
    m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details)->setChecked(gEDataManager->cloudConsoleManagerDetailsExpanded());
    sltToggleCloudConsoleDetailsVisibility(m_pActionPool->action(UIActionIndexMN_M_CloudConsole_T_Details)->isChecked());
}

void UICloudConsoleManagerWidget::loadCloudConsoleStuff()
{
    /* Clear tree first of all: */
    m_pTreeWidget->clear();

    /* Acquire cloud console manager restrictions: */
    const QStringList restrictions = gEDataManager->cloudConsoleManagerRestrictions();

    /* Iterate through existing console applications: */
    foreach (const QString &strApplicationId, gEDataManager->cloudConsoleManagerApplications())
    {
        /* Skip if we have nothing to populate: */
        if (strApplicationId.isEmpty())
            continue;

        /* Compose extra-data superset: */
        const QString strApplicationValue = gEDataManager->cloudConsoleManagerApplication(strApplicationId);
        const QString strApplicationSuperset = QString("%1,%2").arg(strApplicationId, strApplicationValue);

        /* Load console application data: */
        UIDataCloudConsoleApplication applicationData;
        loadCloudConsoleApplication(strApplicationSuperset, applicationData);
        const QString strApplicationDefinition = QString("/%1").arg(applicationData.m_strId);
        applicationData.m_fRestricted = restrictions.contains(strApplicationDefinition);
        createItemForCloudConsoleApplication(applicationData, false);

        /* Make sure console applications item is properly inserted: */
        UIItemCloudConsoleApplication *pItem = searchApplicationItem(applicationData.m_strId);

        /* Iterate through applications's profiles: */
        foreach (const QString &strProfileId, gEDataManager->cloudConsoleManagerProfiles(strApplicationId))
        {
            /* Skip if we have nothing to populate: */
            if (strProfileId.isEmpty())
                continue;

            /* Compose extra-data superset: */
            const QString strProfileValue = gEDataManager->cloudConsoleManagerProfile(strApplicationId, strProfileId);
            const QString strProfileSuperset = QString("%1,%2").arg(strProfileId, strProfileValue);

            /* Load console profile data: */
            UIDataCloudConsoleProfile profileData;
            loadCloudConsoleProfile(strProfileSuperset, applicationData, profileData);
            const QString strProfileDefinition = QString("/%1/%2").arg(applicationData.m_strId).arg(profileData.m_strId);
            profileData.m_fRestricted = restrictions.contains(strProfileDefinition);
            createItemForCloudConsoleProfile(pItem, profileData, false);
        }

        /* Expand console application item finally: */
        pItem->setExpanded(true);
    }

    /* Choose previous current-item if possible: */
    if (!m_strDefinition.isEmpty())
        m_pTreeWidget->setCurrentItem(searchItemByDefinition(m_strDefinition));
    /* Choose the 1st item as current if nothing chosen: */
    if (!m_pTreeWidget->currentItem())
        m_pTreeWidget->setCurrentItem(m_pTreeWidget->childItem(0));
    /* Make sure current-item is fetched: */
    sltHandleCurrentItemChange();
}

void UICloudConsoleManagerWidget::loadCloudConsoleApplication(const QString &strSuperset,
                                                              UIDataCloudConsoleApplication &applicationData)
{
    /* Parse superset: */
    const QStringList values = strSuperset.split(',');

    /* Gather application settings: */
    applicationData.m_strId = values.value(0);
    applicationData.m_strName = values.value(1);
    applicationData.m_strPath = values.value(2);
    applicationData.m_strArgument = values.value(3);
}

void UICloudConsoleManagerWidget::loadCloudConsoleProfile(const QString &strSuperset,
                                                          const UIDataCloudConsoleApplication &applicationData,
                                                          UIDataCloudConsoleProfile &profileData)
{
    /* Gather application settings: */
    profileData.m_strApplicationId = applicationData.m_strId;

    /* Parse superset: */
    const QStringList values = strSuperset.split(',');

    /* Gather profile settings: */
    profileData.m_strId = values.value(0);
    profileData.m_strName = values.value(1);
    profileData.m_strArgument = values.value(2);
}

UIItemCloudConsoleApplication *UICloudConsoleManagerWidget::searchApplicationItem(const QString &strApplicationId) const
{
    /* Iterate through tree-widget children: */
    for (int i = 0; i < m_pTreeWidget->childCount(); ++i)
        if (m_pTreeWidget->childItem(i)->data(Column_Name, Data_ItemID).toString() == strApplicationId)
            return qobject_cast<UIItemCloudConsoleApplication*>(m_pTreeWidget->childItem(i));
    /* Null by default: */
    return 0;
}

UIItemCloudConsoleProfile *UICloudConsoleManagerWidget::searchProfileItem(const QString &strApplicationId,
                                                                          const QString &strProfileId) const
{
    /* Search for application item first: */
    UIItemCloudConsoleApplication *pItemApplication = searchApplicationItem(strApplicationId);
    /* Iterate through application children: */
    for (int i = 0; i < pItemApplication->childCount(); ++i)
        if (pItemApplication->childItem(i)->data(Column_Name, Data_ItemID).toString() == strProfileId)
            return qobject_cast<UIItemCloudConsoleProfile*>(pItemApplication->childItem(i));
    /* Null by default: */
    return 0;
}

QITreeWidgetItem *UICloudConsoleManagerWidget::searchItemByDefinition(const QString &strDefinition) const
{
    /* Parse definition: */
    const QStringList parts = strDefinition.split('/', Qt::SkipEmptyParts);
    /* Depending on parts amount: */
    switch (parts.size())
    {
        case 1: return searchApplicationItem(parts.at(0));
        case 2: return searchProfileItem(parts.at(0), parts.at(1));
        default: break;
    }
    /* Null by default: */
    return 0;
}

void UICloudConsoleManagerWidget::createItemForCloudConsoleApplication(const UIDataCloudConsoleApplication &applicationData,
                                                                       bool fChooseItem)
{
    /* Create new console application item: */
    UIItemCloudConsoleApplication *pItem = new UIItemCloudConsoleApplication;
    if (pItem)
    {
        /* Configure item: */
        pItem->UIDataCloudConsoleApplication::operator=(applicationData);
        pItem->updateFields();
        /* Add item to the tree: */
        m_pTreeWidget->addTopLevelItem(pItem);
        /* And choose it as current if necessary: */
        if (fChooseItem)
            m_pTreeWidget->setCurrentItem(pItem);
    }
}

void UICloudConsoleManagerWidget::createItemForCloudConsoleProfile(QTreeWidgetItem *pParent,
                                                                   const UIDataCloudConsoleProfile &profileData,
                                                                   bool fChooseItem)
{
    /* Create new console profile item: */
    UIItemCloudConsoleProfile *pItem = new UIItemCloudConsoleProfile;
    if (pItem)
    {
        /* Configure item: */
        pItem->UIDataCloudConsoleProfile::operator=(profileData);
        pItem->updateFields();
        /* Add item to the parent: */
        pParent->addChild(pItem);
        /* And choose it as current if necessary: */
        if (fChooseItem)
            m_pTreeWidget->setCurrentItem(pItem);
    }
}

QStringList UICloudConsoleManagerWidget::gatherCloudConsoleManagerRestrictions(QTreeWidgetItem *pParentItem)
{
    /* Prepare result: */
    QStringList result;
    AssertPtrReturn(pParentItem, result);

    /* Process unchecked QITreeWidgetItem(s) only: */
    QITreeWidgetItem *pChangedItem = QITreeWidgetItem::toItem(pParentItem);
    if (   pChangedItem
        && pChangedItem->checkState(Column_ListInMenu) == Qt::Unchecked)
        result << pChangedItem->data(Column_Name, Data_Definition).toString();

    /* Iterate through children recursively: */
    for (int i = 0; i < pParentItem->childCount(); ++i)
        result << gatherCloudConsoleManagerRestrictions(pParentItem->child(i));

    /* Return result: */
    return result;
}


/*********************************************************************************************************************************
*   Class UICloudConsoleManagerFactory implementation.                                                                           *
*********************************************************************************************************************************/

UICloudConsoleManagerFactory::UICloudConsoleManagerFactory(UIActionPool *pActionPool /* = 0 */)
    : m_pActionPool(pActionPool)
{
}

void UICloudConsoleManagerFactory::create(QIManagerDialog *&pDialog, QWidget *pCenterWidget)
{
    pDialog = new UICloudConsoleManager(pCenterWidget, m_pActionPool);
}


/*********************************************************************************************************************************
*   Class UICloudConsoleManager implementation.                                                                                  *
*********************************************************************************************************************************/

UICloudConsoleManager::UICloudConsoleManager(QWidget *pCenterWidget, UIActionPool *pActionPool)
    : QIManagerDialog(pCenterWidget)
    , m_pActionPool(pActionPool)
{
}

void UICloudConsoleManager::sltHandleButtonBoxClick(QAbstractButton *pButton)
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

void UICloudConsoleManager::sltRetranslateUI()
{
    /* Translate window title: */
    setWindowTitle(tr("Cloud Console Manager"));

    /* Translate buttons: */
    button(ButtonType_Reset)->setText(tr("Reset"));
    button(ButtonType_Apply)->setText(tr("Apply"));
    button(ButtonType_Close)->setText(tr("Close"));
    button(ButtonType_Reset)->setStatusTip(tr("Reset changes in current cloud console details"));
    button(ButtonType_Apply)->setStatusTip(tr("Apply changes in current cloud console details"));
    button(ButtonType_Close)->setStatusTip(tr("Close dialog without saving"));
    button(ButtonType_Reset)->setShortcut(QString("Ctrl+Backspace"));
    button(ButtonType_Apply)->setShortcut(QString("Ctrl+Return"));
    button(ButtonType_Close)->setShortcut(Qt::Key_Escape);
    button(ButtonType_Help)->setShortcut(UIShortcutPool::standardSequence(QKeySequence::HelpContents));
    button(ButtonType_Reset)->setToolTip(tr("Reset Changes (%1)").arg(button(ButtonType_Reset)->shortcut().toString()));
    button(ButtonType_Apply)->setToolTip(tr("Apply Changes (%1)").arg(button(ButtonType_Apply)->shortcut().toString()));
    button(ButtonType_Close)->setToolTip(tr("Close Window (%1)").arg(button(ButtonType_Close)->shortcut().toString()));
}

void UICloudConsoleManager::configure()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/cloud_console_manager_32px.png", ":/cloud_console_manager_16px.png"));
#endif
}

void UICloudConsoleManager::configureCentralWidget()
{
    /* Create widget: */
    UICloudConsoleManagerWidget *pWidget = new UICloudConsoleManagerWidget(EmbedTo_Dialog, m_pActionPool, true, this);
    if (pWidget)
    {
        /* Configure widget: */
        setWidget(pWidget);
        setWidgetMenu(pWidget->menu());
#ifdef VBOX_WS_MAC
        setWidgetToolbar(pWidget->toolbar());
#endif
        connect(this, &UICloudConsoleManager::sigDataChangeRejected,
                pWidget, &UICloudConsoleManagerWidget::sltResetCloudConsoleDetailsChanges);
        connect(this, &UICloudConsoleManager::sigDataChangeAccepted,
                pWidget, &UICloudConsoleManagerWidget::sltApplyCloudConsoleDetailsChanges);

        /* Add into layout: */
        centralWidget()->layout()->addWidget(pWidget);
    }
}

void UICloudConsoleManager::configureButtonBox()
{
    /* Configure button-box: */
    connect(widget(), &UICloudConsoleManagerWidget::sigCloudConsoleDetailsVisibilityChanged,
            button(ButtonType_Apply), &QPushButton::setVisible);
    connect(widget(), &UICloudConsoleManagerWidget::sigCloudConsoleDetailsVisibilityChanged,
            button(ButtonType_Reset), &QPushButton::setVisible);
    connect(widget(), &UICloudConsoleManagerWidget::sigCloudConsoleDetailsDataChanged,
            button(ButtonType_Apply), &QPushButton::setEnabled);
    connect(widget(), &UICloudConsoleManagerWidget::sigCloudConsoleDetailsDataChanged,
            button(ButtonType_Reset), &QPushButton::setEnabled);
    connect(buttonBox(), &QIDialogButtonBox::clicked,
            this, &UICloudConsoleManager::sltHandleButtonBoxClick);
    // WORKAROUND:
    // Since we connected signals later than extra-data loaded
    // for signals above, we should handle that stuff here again:
    button(ButtonType_Apply)->setVisible(gEDataManager->cloudConsoleManagerDetailsExpanded());
    button(ButtonType_Reset)->setVisible(gEDataManager->cloudConsoleManagerDetailsExpanded());
}

void UICloudConsoleManager::finalize()
{
    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloudConsoleManager::sltRetranslateUI);
}

UICloudConsoleManagerWidget *UICloudConsoleManager::widget()
{
    return qobject_cast<UICloudConsoleManagerWidget*>(QIManagerDialog::widget());
}


#include "UICloudConsoleManager.moc"
