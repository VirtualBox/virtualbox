/* $Id: UICloudProfileDetailsWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudProfileDetailsWidget class implementation.
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
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QITableWidget.h"
#include "UICommon.h"
#include "UICloudProfileDetailsWidget.h"
#include "UICloudProfileManager.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UICloudProfileDetailsWidget::UICloudProfileDetailsWidget(EmbedTo enmEmbedding, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_pLabelName(0)
    , m_pEditorName(0)
    , m_pLabelTableWidget(0)
    , m_pTableWidget(0)
    , m_pButtonBox(0)
{
    prepare();
}

void UICloudProfileDetailsWidget::setData(const UIDataCloudProfile &data)
{
    /* Cache old/new data: */
    m_oldData = data;
    m_newData = m_oldData;

    /* Load data: */
    loadData();

    /* Translate linked widgets: */
    retranslateEditor();
    retranslateButtons();
}

void UICloudProfileDetailsWidget::sltRetranslateUI()
{
    /// @todo add description tool-tips

    /* Translate name-editor label: */
    m_pLabelName->setText(UICloudProfileManager::tr("Name:"));
    /* Translate name-editor: */
    retranslateEditor();

    /* Translate table-widget label: */
    m_pLabelTableWidget->setText(UICloudProfileManager::tr("Properties:"));
    /* Translate table-widget: */
    m_pTableWidget->setWhatsThis(UICloudProfileManager::tr("Contains cloud profile settings"));

    /* Translate buttons: */
    retranslateButtons();

    /* Retranslate validation: */
    retranslateValidation();

    /* Update table tool-tips: */
    updateTableToolTips();
}

void UICloudProfileDetailsWidget::retranslateEditor()
{
    /* Translate placeholders: */
    m_pEditorName->setPlaceholderText(  m_oldData.m_strName.isNull()
                                      ? UICloudProfileManager::tr("Enter a name for the new profile...")
                                      : UICloudProfileManager::tr("Enter a name for this profile..."));
}

void UICloudProfileDetailsWidget::retranslateButtons()
{
    /* Translate button-box: */
    if (m_pButtonBox)
    {
        /* Common: 'Reset' button: */
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setText(UICloudProfileManager::tr("Reset"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setStatusTip(UICloudProfileManager::tr("Reset changes in current profile details"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setShortcut(Qt::Key_Escape);
        m_pButtonBox->button(QDialogButtonBox::Cancel)->
            setToolTip(UICloudProfileManager::tr("Reset Changes (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Cancel)->shortcut().toString()));

        if (m_oldData.m_strName.isNull())
        {
            /* Provider: 'Add' button: */
            m_pButtonBox->button(QDialogButtonBox::Ok)->setText(UICloudProfileManager::tr("Add"));
            m_pButtonBox->button(QDialogButtonBox::Ok)->setStatusTip(UICloudProfileManager::tr("Add a new profile with following name"));
            m_pButtonBox->button(QDialogButtonBox::Ok)->setShortcut(QString("Ctrl+Return"));
            m_pButtonBox->button(QDialogButtonBox::Ok)->
                setToolTip(UICloudProfileManager::tr("Add Profile (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Ok)->shortcut().toString()));
        }
        else
        {
            /* Profile: 'Apply' button: */
            m_pButtonBox->button(QDialogButtonBox::Ok)->setText(UICloudProfileManager::tr("Apply"));
            m_pButtonBox->button(QDialogButtonBox::Ok)->setStatusTip(UICloudProfileManager::tr("Apply changes in current profile details"));
            m_pButtonBox->button(QDialogButtonBox::Ok)->setShortcut(QString("Ctrl+Return"));
            m_pButtonBox->button(QDialogButtonBox::Ok)->
                setToolTip(UICloudProfileManager::tr("Apply Changes (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Ok)->shortcut().toString()));
        }
    }
}

void UICloudProfileDetailsWidget::sltNameChanged(const QString &strName)
{
    /* Push changes back: */
    m_newData.m_strName = strName;

    /* Revalidate: */
    revalidate(m_pEditorName);
    /* Update button states: */
    updateButtonStates();
}

void UICloudProfileDetailsWidget::sltTableChanged(QTableWidgetItem *pItem)
{
    /* Make sure item is valid: */
    AssertPtrReturnVoid(pItem);
    const int iRow = pItem->row();
    AssertReturnVoid(iRow >= 0);

    /* Skip if one of items isn't yet created.
     * This can happen when 1st is already while 2nd isn't yet. */
    QTableWidgetItem *pItemK = m_pTableWidget->item(iRow, 0);
    QTableWidgetItem *pItemV = m_pTableWidget->item(iRow, 1);
    if (!pItemK || !pItemV)
        return;

    /* Push changes back: */
    const QString strKey = pItemK->text();
    const QString strValue = pItemV->text();
    m_newData.m_data[strKey] = qMakePair(strValue, m_newData.m_data.value(strKey).second);

    /* Revalidate: */
    revalidate(m_pTableWidget);
    /* Update button states: */
    updateButtonStates();
}

void UICloudProfileDetailsWidget::sltHandleButtonBoxClick(QAbstractButton *pButton)
{
    /* Make sure button-box exists: */
    AssertPtrReturnVoid(m_pButtonBox);

    /* Disable buttons first of all: */
    m_pButtonBox->button(QDialogButtonBox::Cancel)->setEnabled(false);
    m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    /* Compare with known buttons: */
    if (pButton == m_pButtonBox->button(QDialogButtonBox::Cancel))
        emit sigDataChangeRejected();
    else
    if (pButton == m_pButtonBox->button(QDialogButtonBox::Ok))
        emit sigDataChangeAccepted();
}

void UICloudProfileDetailsWidget::prepare()
{
    /* Prepare widgets: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UICloudProfileDetailsWidget::sltRetranslateUI);

    /* Update button states finally: */
    updateButtonStates();
    uiCommon().setHelpKeyword(this, "ovf-cloud-profile-manager");
}

void UICloudProfileDetailsWidget::prepareWidgets()
{
    /* Create layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        if (m_enmEmbedding == EmbedTo_Dialog)
        {
            pLayout->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
            pLayout->setSpacing(10);
#else
            pLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif
        }
        else
        {
#ifdef VBOX_WS_MAC
            pLayout->setContentsMargins(13, 0, 13, 13);
            pLayout->setSpacing(10);
#else
            const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) * 1.5;
            const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin) * 1.5;
            const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin) * 1.5;
            const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) * 1.5;
            pLayout->setContentsMargins(iL, iT, iR, iB);
#endif
        }

        /* Create name editor: */
        m_pEditorName = new QLineEdit;
        if (m_pEditorName)
        {
            connect(m_pEditorName, &QLineEdit::textChanged, this, &UICloudProfileDetailsWidget::sltNameChanged);

            /* Add into layout: */
            pLayout->addWidget(m_pEditorName, 0, 1);
        }
        /* Create name label: */
        m_pLabelName = new QLabel;
        if (m_pLabelName)
        {
            m_pLabelName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLabelName->setBuddy(m_pEditorName);

            /* Add into layout: */
            pLayout->addWidget(m_pLabelName, 0, 0);
        }

        /* Create tab-widget: */
        m_pTableWidget = new QITableWidget;
        if (m_pTableWidget)
        {
            m_pTableWidget->setAlternatingRowColors(true);
            m_pTableWidget->horizontalHeader()->setVisible(false);
            m_pTableWidget->verticalHeader()->setVisible(false);
            m_pTableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            connect(m_pTableWidget, &QITableWidget::itemChanged, this, &UICloudProfileDetailsWidget::sltTableChanged);

            /* Add into layout: */
            pLayout->addWidget(m_pTableWidget, 1, 1);
        }
        /* Create tab-widget label: */
        m_pLabelTableWidget = new QLabel;
        if (m_pLabelTableWidget)
        {
            m_pLabelTableWidget->setAlignment(Qt::AlignRight | Qt::AlignTop);
            m_pLabelTableWidget->setBuddy(m_pTableWidget);

            /* Add into layout: */
            pLayout->addWidget(m_pLabelTableWidget, 1, 0);
        }

        /* If parent embedded into stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Create button-box: */
            m_pButtonBox = new QIDialogButtonBox;
            if  (m_pButtonBox)
            {
                m_pButtonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
                connect(m_pButtonBox, &QIDialogButtonBox::clicked, this, &UICloudProfileDetailsWidget::sltHandleButtonBoxClick);

                /* Add into layout: */
                pLayout->addWidget(m_pButtonBox, 2, 0, 1, 2);
            }
        }
    }
}

void UICloudProfileDetailsWidget::loadData()
{
    /* Clear table initially: */
    m_pTableWidget->clear();

    /* Fill name editor: */
    m_pEditorName->setText(m_oldData.m_strName);

    /* Configure table: */
    m_pTableWidget->setRowCount(m_oldData.m_data.keys().size());
    m_pTableWidget->setColumnCount(2);

    /* Push acquired keys/values to data fields: */
    for (int i = 0; i < m_pTableWidget->rowCount(); ++i)
    {
        /* Gather values: */
        const QString strKey = m_oldData.m_data.keys().at(i);
        const QString strValue = m_oldData.m_data.value(strKey).first;
        const QString strToolTip = m_oldData.m_data.value(strKey).second;

        /* Create key item: */
        QITableWidgetItem *pItemK = new QITableWidgetItem(strKey);
        if (pItemK)
        {
            /* Non-editable for sure, but non-selectable? */
            pItemK->setFlags(pItemK->flags() & ~Qt::ItemIsEditable);
            /* Use non-translated description as tool-tip: */
            pItemK->setData(Qt::UserRole, strToolTip);

            /* Insert into table: */
            m_pTableWidget->setItem(i, 0, pItemK);
        }

        /* Create value item: */
        QITableWidgetItem *pItemV = new QITableWidgetItem(strValue);
        if (pItemV)
        {
            /* Use the value as tool-tip, there can be quite long values: */
            pItemV->setToolTip(strValue);

            /* Insert into table: */
            m_pTableWidget->setItem(i, 1, pItemV);
        }
    }

    /* Update table tooltips: */
    updateTableToolTips();
    /* Adjust table contents: */
    adjustTableContents();
}

void UICloudProfileDetailsWidget::revalidate(QWidget *pWidget /* = 0 */)
{
    /// @todo validate profile settings table!

    /* Retranslate validation: */
    retranslateValidation(pWidget);
}

void UICloudProfileDetailsWidget::retranslateValidation(QWidget *pWidget /* = 0 */)
{
    Q_UNUSED(pWidget);

    /// @todo translate vaidation errors!
}

void UICloudProfileDetailsWidget::updateTableToolTips()
{
    /* Iterate through all the key items: */
    for (int i = 0; i < m_pTableWidget->rowCount(); ++i)
    {
        /* Acquire current key item: */
        QTableWidgetItem *pItemK = m_pTableWidget->item(i, 0);
        if (pItemK)
        {
            const QString strToolTip = pItemK->data(Qt::UserRole).toString();
            pItemK->setToolTip(UICloudProfileManager::tr(strToolTip.toUtf8().constData()));
        }
    }
}

void UICloudProfileDetailsWidget::adjustTableContents()
{
    /* Disable last column stretching temporary: */
    m_pTableWidget->horizontalHeader()->setStretchLastSection(false);

    /* Resize both columns to contents: */
    m_pTableWidget->resizeColumnsToContents();
    /* Then acquire full available width: */
    const int iFullWidth = m_pTableWidget->viewport()->width();
    /* First column should not be less than it's minimum size, last gets the rest: */
    const int iMinimumWidth0 = qMin(m_pTableWidget->horizontalHeader()->sectionSize(0), iFullWidth / 2);
    m_pTableWidget->horizontalHeader()->resizeSection(0, iMinimumWidth0);

    /* Enable last column stretching again: */
    m_pTableWidget->horizontalHeader()->setStretchLastSection(true);
}

void UICloudProfileDetailsWidget::updateButtonStates()
{
#if 0
    if (m_oldData != m_newData)
    {
        printf("Old data:\n");
        foreach (const QString &strKey, m_oldData.m_data.keys())
        {
            const QString strValue = m_oldData.m_data.value(strKey).first;
            const QString strDecription = m_oldData.m_data.value(strKey).second;
            printf(" %s: %s, %s\n", strKey.toUtf8().constData(), strValue.toUtf8().constData(), strDecription.toUtf8().constData());
        }
        printf("New data:\n");
        foreach (const QString &strKey, m_newData.m_data.keys())
        {
            const QString strValue = m_newData.m_data.value(strKey).first;
            const QString strDecription = m_newData.m_data.value(strKey).second;
            printf(" %s: %s, %s\n", strKey.toUtf8().constData(), strValue.toUtf8().constData(), strDecription.toUtf8().constData());
        }
        printf("\n");
    }
#endif

    /* Update 'Apply' / 'Reset' button states: */
    if (m_pButtonBox)
    {
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setEnabled(m_oldData != m_newData);
        m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(m_oldData != m_newData);
    }

    /* Notify listeners as well: */
    emit sigDataChanged(m_oldData != m_newData);
}
