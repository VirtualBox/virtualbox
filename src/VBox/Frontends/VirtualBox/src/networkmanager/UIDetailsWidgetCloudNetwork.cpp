/* $Id: UIDetailsWidgetCloudNetwork.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsWidgetCloudNetwork class implementation.
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
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStyleOption>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QILineEdit.h"
#include "QITabWidget.h"
#include "QIToolButton.h"
#include "UICloudNetworkingStuff.h"
#include "UIDetailsWidgetCloudNetwork.h"
#include "UIFormEditorWidget.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UINetworkManager.h"
#include "UINetworkManagerUtils.h"
#include "UINotificationCenter.h"
#include "UITranslationEventListener.h"

UISubnetSelectionDialog::UISubnetSelectionDialog(QWidget *pParent,
                                                 const QString &strShortProviderName,
                                                 const QString &strProfileName,
                                                 const QString &strSubnetId)
    : QDialog(pParent)
    , m_strProviderShortName(strShortProviderName)
    , m_strProfileName(strProfileName)
    , m_strSubnetId(strSubnetId)
    , m_pFormEditor(0)
    , m_pButtonBox(0)
    , m_pNotificationCenter(0)
{
    prepare();
}

UISubnetSelectionDialog::~UISubnetSelectionDialog()
{
    cleanup();
}

void UISubnetSelectionDialog::accept()
{
    /* Get altered description back: */
    m_comForm.GetVirtualSystemDescription();
    QVector<KVirtualSystemDescriptionType> aTypes;
    QVector<QString> aRefs, aOVFValues, aVBoxValues, aExtraConfigValues;
    m_comDescription.GetDescriptionByType(KVirtualSystemDescriptionType_CloudOCISubnet,
                                          aTypes, aRefs, aOVFValues, aVBoxValues, aExtraConfigValues);
    if (!m_comDescription.isOk())
    {
        UINotificationMessage::cannotAcquireVirtualSystemDescriptionParameter(m_comDescription,
                                                                              m_pNotificationCenter);
        return;
    }
    AssertReturnVoid(!aVBoxValues.isEmpty());
    m_strSubnetId = aVBoxValues.first();

    /* Call to base-class: */
    return QDialog::accept();
}

int UISubnetSelectionDialog::exec()
{
    /* Request to init dialog _after_ being executed: */
    QMetaObject::invokeMethod(this, "sltInit", Qt::QueuedConnection);

    /* Call to base-class: */
    return QDialog::exec();
}

void UISubnetSelectionDialog::sltRetranslateUI()
{
    setWindowTitle(UINetworkManager::tr("Select Subnet"));
}

void UISubnetSelectionDialog::sltInit()
{
    /* Create description: */
    m_comDescription = createVirtualSystemDescription(m_pNotificationCenter);
    if (m_comDescription.isNull())
        return;
    /* Update it with current subnet value: */
    m_comDescription.AddDescription(KVirtualSystemDescriptionType_CloudOCISubnet, m_strSubnetId, QString());

    /* Create cloud client: */
    CCloudClient comCloudClient = cloudClientByName(m_strProviderShortName, m_strProfileName, m_pNotificationCenter);
    if (comCloudClient.isNull())
        return;

    /* Create subnet selection VSD form: */
    UINotificationProgressSubnetSelectionVSDFormCreate *pNotification = new UINotificationProgressSubnetSelectionVSDFormCreate(comCloudClient,
                                                                                                                               m_comDescription,
                                                                                                                               m_strProviderShortName,
                                                                                                                               m_strProfileName);
    connect(pNotification, &UINotificationProgressSubnetSelectionVSDFormCreate::sigVSDFormCreated,
            this, &UISubnetSelectionDialog::sltHandleVSDFormCreated);
    m_pNotificationCenter->append(pNotification);
}

void UISubnetSelectionDialog::sltHandleVSDFormCreated(const CVirtualSystemDescriptionForm &comForm)
{
    m_comForm = comForm;
    m_pFormEditor->setVirtualSystemDescriptionForm(m_comForm);
}

void UISubnetSelectionDialog::prepare()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        /* Prepare form editor: */
        m_pFormEditor = new UIFormEditorWidget(this);
        if (m_pFormEditor)
            pLayoutMain->addWidget(m_pFormEditor);

        /* Prepare button-box: */
        m_pButtonBox = new QIDialogButtonBox(this);
        if (m_pButtonBox)
        {
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
            connect(m_pButtonBox, &QIDialogButtonBox::accepted, this, &QDialog::accept);
            connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &QDialog::reject);

            pLayoutMain->addWidget(m_pButtonBox);
        }
    }

    /* Prepare local notification-center: */
    m_pNotificationCenter = new UINotificationCenter(this);
    if (m_pNotificationCenter && m_pFormEditor)
        m_pFormEditor->setNotificationCenter(m_pNotificationCenter);

    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UISubnetSelectionDialog::sltRetranslateUI);
}

void UISubnetSelectionDialog::cleanup()
{
    /* Cleanup local notification-center: */
    delete m_pNotificationCenter;
    m_pNotificationCenter = 0;
}


UIDetailsWidgetCloudNetwork::UIDetailsWidgetCloudNetwork(EmbedTo enmEmbedding, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_pLabelNetworkName(0)
    , m_pEditorNetworkName(0)
    , m_pLabelProviderName(0)
    , m_pComboProviderName(0)
    , m_pLabelProfileName(0)
    , m_pComboProfileName(0)
    , m_pLabelNetworkId(0)
    , m_pEditorNetworkId(0)
    , m_pButtonNetworkId(0)
    , m_pButtonBoxOptions(0)
{
    prepare();
}

void UIDetailsWidgetCloudNetwork::setData(const UIDataCloudNetwork &data,
                                          const QStringList &busyNames /* = QStringList() */)
{
    /* Cache old/new data: */
    m_oldData = data;
    m_newData = m_oldData;
    m_busyNames = busyNames;

    /* Load data: */
    loadData();
}

bool UIDetailsWidgetCloudNetwork::revalidate() const
{
    /* Make sure network name isn't empty: */
    if (m_newData.m_strName.isEmpty())
    {
        UINotificationMessage::warnAboutNoNameSpecified(m_oldData.m_strName);
        return false;
    }
    else
    {
        /* Make sure item names are unique: */
        if (m_busyNames.contains(m_newData.m_strName))
        {
            UINotificationMessage::warnAboutNameAlreadyBusy(m_newData.m_strName);
            return false;
        }
    }

    return true;
}

void UIDetailsWidgetCloudNetwork::updateButtonStates()
{
//    if (m_oldData != m_newData)
//        printf("Network: %s, %s, %s, %d, %d, %d\n",
//               m_newData.m_strName.toUtf8().constData(),
//               m_newData.m_strPrefixIPv4.toUtf8().constData(),
//               m_newData.m_strPrefixIPv6.toUtf8().constData(),
//               m_newData.m_fSupportsDHCP,
//               m_newData.m_fSupportsIPv6,
//               m_newData.m_fAdvertiseDefaultIPv6Route);

    /* Update 'Apply' / 'Reset' button states: */
    if (m_pButtonBoxOptions)
    {
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setEnabled(m_oldData != m_newData);
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setEnabled(m_oldData != m_newData);
    }

    /* Notify listeners as well: */
    emit sigDataChanged(m_oldData != m_newData);
}

void UIDetailsWidgetCloudNetwork::sltRetranslateUI()
{
    if (m_pLabelNetworkName)
        m_pLabelNetworkName->setText(UINetworkManager::tr("N&ame:"));
    if (m_pEditorNetworkName)
        m_pEditorNetworkName->setToolTip(UINetworkManager::tr("Holds the name for this network."));
    if (m_pLabelProviderName)
        m_pLabelProviderName->setText(UINetworkManager::tr("&Provider:"));
    if (m_pComboProviderName)
        m_pComboProviderName->setToolTip(UINetworkManager::tr("Holds the cloud provider for this network."));
    if (m_pLabelProfileName)
        m_pLabelProfileName->setText(UINetworkManager::tr("P&rofile:"));
    if (m_pComboProfileName)
        m_pComboProfileName->setToolTip(UINetworkManager::tr("Holds the cloud profile for this network."));
    if (m_pLabelNetworkId)
        m_pLabelNetworkId->setText(UINetworkManager::tr("&Id:"));
    if (m_pEditorNetworkId)
        m_pEditorNetworkId->setToolTip(UINetworkManager::tr("Holds the id for this network."));
    if (m_pButtonNetworkId)
    {
        m_pButtonNetworkId->setText(UINetworkManager::tr("Select Network ID"));
        m_pButtonNetworkId->setToolTip(UINetworkManager::tr("Selects the id for this network."));
    }
    if (m_pButtonBoxOptions)
    {
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setText(UINetworkManager::tr("Reset"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setText(UINetworkManager::tr("Apply"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setShortcut(Qt::Key_Escape);
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setShortcut(QString("Ctrl+Return"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setStatusTip(UINetworkManager::tr("Reset changes in current "
                                                                                                 "interface details"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setStatusTip(UINetworkManager::tr("Apply changes in current "
                                                                                             "interface details"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->
            setToolTip(UINetworkManager::tr("Reset Changes (%1)").arg(m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->shortcut().toString()));
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->
            setToolTip(UINetworkManager::tr("Apply Changes (%1)").arg(m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->shortcut().toString()));
    }
}

void UIDetailsWidgetCloudNetwork::sltNetworkNameChanged(const QString &strText)
{
    m_newData.m_strName = strText;
    updateButtonStates();
}

void UIDetailsWidgetCloudNetwork::sltCloudProviderNameChanged(int iIndex)
{
    /* Store provider: */
    m_newData.m_strProvider = m_pComboProviderName->itemData(iIndex).toString();

    /* Update profiles: */
    prepareProfiles();
    /* And store selected profile: */
    sltCloudProfileNameChanged(m_pComboProfileName->currentIndex());

    /* Update button states finally: */
    updateButtonStates();
}

void UIDetailsWidgetCloudNetwork::sltCloudProfileNameChanged(int iIndex)
{
    /* Store profile: */
    m_newData.m_strProfile = m_pComboProfileName->itemData(iIndex).toString();

    /* Update button states finally: */
    updateButtonStates();
}

void UIDetailsWidgetCloudNetwork::sltNetworkIdChanged(const QString &strText)
{
    m_newData.m_strId = strText;
    updateButtonStates();
}

void UIDetailsWidgetCloudNetwork::sltNetworkIdListRequested()
{
    /* Create subnet selection dialog: */
    QPointer<UISubnetSelectionDialog> pDialog = new UISubnetSelectionDialog(this,
                                                                            m_pComboProviderName->currentData().toString(),
                                                                            m_pComboProfileName->currentData().toString(),
                                                                            m_pEditorNetworkId->text());

    /* Execute dialog to ask user for subnet: */
    if (pDialog->exec() == QDialog::Accepted)
        m_pEditorNetworkId->setText(pDialog->subnetId());

    /* Cleanup subnet dialog finally: */
    delete pDialog;
}

void UIDetailsWidgetCloudNetwork::sltHandleButtonBoxClick(QAbstractButton *pButton)
{
    /* Make sure button-box exist: */
    if (!m_pButtonBoxOptions)
        return;

    /* Disable buttons first of all: */
    m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setEnabled(false);
    m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setEnabled(false);

    /* Compare with known buttons: */
    if (pButton == m_pButtonBoxOptions->button(QDialogButtonBox::Cancel))
        emit sigDataChangeRejected();
    else
    if (pButton == m_pButtonBoxOptions->button(QDialogButtonBox::Ok))
        emit sigDataChangeAccepted();
}

void UIDetailsWidgetCloudNetwork::prepare()
{
    /* Prepare everything: */
    prepareThis();
    prepareProviders();
    prepareProfiles();

    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIDetailsWidgetCloudNetwork::sltRetranslateUI);

    /* Update button states finally: */
    updateButtonStates();
}

void UIDetailsWidgetCloudNetwork::prepareThis()
{
    /* Prepare options widget layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
#ifdef VBOX_WS_MAC
        pLayout->setSpacing(10);
        pLayout->setContentsMargins(10, 10, 10, 10);
#endif

        /* Prepare network name label: */
        m_pLabelNetworkName = new QLabel(this);
        if (m_pLabelNetworkName)
        {
            m_pLabelNetworkName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelNetworkName, 0, 0);
        }
        /* Prepare network name editor: */
        m_pEditorNetworkName = new QLineEdit(this);
        if (m_pEditorNetworkName)
        {
            if (m_pLabelNetworkName)
                m_pLabelNetworkName->setBuddy(m_pEditorNetworkName);
            connect(m_pEditorNetworkName, &QLineEdit::textEdited,
                    this, &UIDetailsWidgetCloudNetwork::sltNetworkNameChanged);
            pLayout->addWidget(m_pEditorNetworkName, 0, 1, 1, 2);
        }

        /* Prepare cloud provider name label: */
        m_pLabelProviderName = new QLabel(this);
        if (m_pLabelProviderName)
        {
            m_pLabelProviderName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelProviderName, 1, 0);
        }
        /* Prepare cloud provider name combo: */
        m_pComboProviderName = new QComboBox(this);
        if (m_pComboProviderName)
        {
            if (m_pLabelProviderName)
                m_pLabelProviderName->setBuddy(m_pComboProviderName);
            connect(m_pComboProviderName, &QComboBox::currentIndexChanged,
                    this, &UIDetailsWidgetCloudNetwork::sltCloudProviderNameChanged);
            pLayout->addWidget(m_pComboProviderName, 1, 1, 1, 2);
        }

        /* Prepare cloud profile name label: */
        m_pLabelProfileName = new QLabel(this);
        if (m_pLabelProfileName)
        {
            m_pLabelProfileName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelProfileName, 2, 0);
        }
        /* Prepare cloud profile name combo: */
        m_pComboProfileName = new QComboBox(this);
        if (m_pComboProfileName)
        {
            if (m_pLabelProfileName)
                m_pLabelProfileName->setBuddy(m_pComboProfileName);
            connect(m_pComboProfileName, &QComboBox::currentIndexChanged,
                    this, &UIDetailsWidgetCloudNetwork::sltCloudProfileNameChanged);
            pLayout->addWidget(m_pComboProfileName, 2, 1, 1, 2);
        }

        /* Prepare network id label: */
        m_pLabelNetworkId = new QLabel(this);
        if (m_pLabelNetworkId)
        {
            m_pLabelNetworkId->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelNetworkId, 3, 0);
        }
        /* Prepare network id editor: */
        m_pEditorNetworkId = new QLineEdit(this);
        if (m_pEditorNetworkId)
        {
            if (m_pLabelNetworkId)
                m_pLabelNetworkId->setBuddy(m_pEditorNetworkId);
            connect(m_pEditorNetworkId, &QLineEdit::textChanged,
                    this, &UIDetailsWidgetCloudNetwork::sltNetworkIdChanged);
            pLayout->addWidget(m_pEditorNetworkId, 3, 1);
        }
        /* Prepare network id button: */
        m_pButtonNetworkId = new QIToolButton(this);
        if (m_pButtonNetworkId)
        {
            m_pButtonNetworkId->setIcon(UIIconPool::iconSet(":/subnet_16px.png"));
            connect(m_pButtonNetworkId, &QIToolButton::clicked,
                    this, &UIDetailsWidgetCloudNetwork::sltNetworkIdListRequested);
            pLayout->addWidget(m_pButtonNetworkId, 3, 2);
        }

        /* If parent embedded into stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Prepare button-box: */
            m_pButtonBoxOptions = new QIDialogButtonBox(this);
            if (m_pButtonBoxOptions)
            {
                m_pButtonBoxOptions->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
                connect(m_pButtonBoxOptions, &QIDialogButtonBox::clicked, this, &UIDetailsWidgetCloudNetwork::sltHandleButtonBoxClick);
                pLayout->addWidget(m_pButtonBoxOptions, 4, 0, 1, 2);
            }
        }
    }
}

void UIDetailsWidgetCloudNetwork::prepareProviders()
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pComboProviderName);

    /* Remember current item data to be able to restore it: */
    QString strOldData;
    if (m_pComboProviderName->currentIndex() != -1)
        strOldData = m_pComboProviderName->currentData().toString();

    /* Block signals while updating: */
    m_pComboProviderName->blockSignals(true);

    /* Clear combo initially: */
    m_pComboProviderName->clear();

    /* Add empty item: */
    m_pComboProviderName->addItem("--");

    /* Iterate through existing providers: */
    foreach (const CCloudProvider &comProvider, listCloudProviders())
    {
        /* Skip if we have nothing to populate (file missing?): */
        if (comProvider.isNull())
            continue;
        /* Acquire provider name: */
        QString strProviderName;
        if (!cloudProviderName(comProvider, strProviderName))
            continue;
        /* Acquire provider short name: */
        QString strProviderShortName;
        if (!cloudProviderShortName(comProvider, strProviderShortName))
            continue;

        /* Compose empty item, fill the data: */
        m_pComboProviderName->addItem(strProviderName);
        m_pComboProviderName->setItemData(m_pComboProviderName->count() - 1, strProviderShortName);
    }

    /* Set previous/default item if possible: */
    int iNewIndex = -1;
    if (   iNewIndex == -1
        && !strOldData.isNull())
        iNewIndex = m_pComboProviderName->findData(strOldData);
    if (   iNewIndex == -1
        && m_pComboProviderName->count() > 0)
        iNewIndex = 0;
    if (iNewIndex != -1)
        m_pComboProviderName->setCurrentIndex(iNewIndex);

    /* Unblock signals after update: */
    m_pComboProviderName->blockSignals(false);
}

void UIDetailsWidgetCloudNetwork::prepareProfiles()
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pComboProfileName);

    /* Remember current item data to be able to restore it: */
    QString strOldData;
    if (m_pComboProfileName->currentIndex() != -1)
        strOldData = m_pComboProfileName->currentData().toString();

    /* Block signals while updating: */
    m_pComboProfileName->blockSignals(true);

    /* Clear combo initially: */
    m_pComboProfileName->clear();

    /* Add empty item: */
    m_pComboProfileName->addItem("--");

    /* Acquire provider short name: */
    const QString strProviderShortName = m_pComboProviderName->currentData().toString();
    if (!strProviderShortName.isEmpty())
    {
        /* Acquire provider: */
        CCloudProvider comProvider = cloudProviderByShortName(strProviderShortName);
        if (comProvider.isNotNull())
        {
            /* Iterate through existing profiles: */
            foreach (const CCloudProfile &comProfile, listCloudProfiles(comProvider))
            {
                /* Skip if we have nothing to populate (wtf happened?): */
                if (comProfile.isNull())
                    continue;
                /* Acquire current profile name: */
                QString strProfileName;
                if (!cloudProfileName(comProfile, strProfileName))
                    continue;

                /* Compose item, fill the data: */
                m_pComboProfileName->addItem(strProfileName);
                m_pComboProfileName->setItemData(m_pComboProfileName->count() - 1, strProfileName);
            }

            /* Set previous/default item if possible: */
            int iNewIndex = -1;
            if (   iNewIndex == -1
                && !strOldData.isNull())
                iNewIndex = m_pComboProfileName->findData(strOldData);
            if (   iNewIndex == -1
                && m_pComboProfileName->count() > 0)
                iNewIndex = 0;
            if (iNewIndex != -1)
                m_pComboProfileName->setCurrentIndex(iNewIndex);
        }
    }

    /* Unblock signals after update: */
    m_pComboProfileName->blockSignals(false);
}

void UIDetailsWidgetCloudNetwork::loadData()
{
    /* Check whether network exists and enabled: */
    const bool fIsNetworkExists = m_newData.m_fExists;

    /* Update field availability: */
    m_pLabelNetworkName->setEnabled(fIsNetworkExists);
    m_pEditorNetworkName->setEnabled(fIsNetworkExists);
    m_pLabelProviderName->setEnabled(fIsNetworkExists);
    m_pComboProviderName->setEnabled(fIsNetworkExists);
    m_pLabelProfileName->setEnabled(fIsNetworkExists);
    m_pComboProfileName->setEnabled(fIsNetworkExists);
    m_pLabelNetworkId->setEnabled(fIsNetworkExists);
    m_pEditorNetworkId->setEnabled(fIsNetworkExists);
    m_pButtonNetworkId->setEnabled(fIsNetworkExists);

    /* Load fields: */
    m_pEditorNetworkName->setText(m_newData.m_strName);
    const int iProviderIndex = m_pComboProviderName->findData(m_newData.m_strProvider);
    m_pComboProviderName->setCurrentIndex(iProviderIndex == -1 ? 0 : iProviderIndex);
    const int iProfileIndex = m_pComboProfileName->findData(m_newData.m_strProfile);
    m_pComboProfileName->setCurrentIndex(iProfileIndex == -1 ? 0 : iProfileIndex);
    m_pEditorNetworkId->setText(m_newData.m_strId);
}
