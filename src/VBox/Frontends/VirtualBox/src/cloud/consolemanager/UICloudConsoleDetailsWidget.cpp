/* $Id: UICloudConsoleDetailsWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudConsoleDetailsWidget class implementation.
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
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedLayout>
#include <QStyle>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "UICloudConsoleDetailsWidget.h"
#include "UICloudConsoleManager.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UICloudConsoleDetailsWidget::UICloudConsoleDetailsWidget(EmbedTo enmEmbedding, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_pStackedLayout(0)
    , m_pLabelApplicationName(0)
    , m_pEditorApplicationName(0)
    , m_pLabelApplicationPath(0)
    , m_pEditorApplicationPath(0)
    , m_pLabelApplicationArgument(0)
    , m_pEditorApplicationArgument(0)
    , m_pLabelProfileName(0)
    , m_pEditorProfileName(0)
    , m_pLabelProfileArgument(0)
    , m_pEditorProfileArgument(0)
    , m_pButtonBox(0)
{
    prepare();
}

void UICloudConsoleDetailsWidget::setApplicationData(const UIDataCloudConsoleApplication &data)
{
    /* Clear all data first: */
    clearData();

    /* Cache old/new data: */
    m_oldApplicationData = data;
    m_newApplicationData = m_oldApplicationData;

    /* Switch to proper stack: */
    m_pStackedLayout->setCurrentIndex(0);

    /* Load data: */
    loadData();
}

void UICloudConsoleDetailsWidget::setProfileData(const UIDataCloudConsoleProfile &data)
{
    /* Clear all data first: */
    clearData();

    /* Cache old/new data: */
    m_oldProfileData = data;
    m_newProfileData = m_oldProfileData;

    /* Switch to proper stack: */
    m_pStackedLayout->setCurrentIndex(1);

    /* Load data: */
    loadData();
}

void UICloudConsoleDetailsWidget::clearData()
{
    /* Clear widgets: */
    m_pEditorApplicationName->setText(QString());
    m_pEditorApplicationPath->setText(QString());
    m_pEditorApplicationArgument->setText(QString());
    m_pEditorProfileName->setText(QString());
    m_pEditorProfileArgument->setText(QString());

    /* Clear data: */
    m_oldApplicationData = UIDataCloudConsoleApplication();
    m_newApplicationData = m_oldApplicationData;
    m_oldProfileData = UIDataCloudConsoleProfile();
    m_newProfileData = m_oldProfileData;
}

void UICloudConsoleDetailsWidget::sltRetranslateUI()
{
    /* Translate editor labels: */
    m_pLabelApplicationName->setText(UICloudConsoleManager::tr("Name:"));
    m_pLabelApplicationPath->setText(UICloudConsoleManager::tr("Path:"));
    m_pLabelApplicationArgument->setText(UICloudConsoleManager::tr("Argument:"));
    m_pLabelProfileName->setText(UICloudConsoleManager::tr("Name:"));
    m_pLabelProfileArgument->setText(UICloudConsoleManager::tr("Argument:"));

    /* Translate editor placeholders: */
    m_pEditorApplicationName->setPlaceholderText(UICloudConsoleManager::tr("Enter a name for this console application..."));
    m_pEditorApplicationPath->setPlaceholderText(UICloudConsoleManager::tr("Enter a path for this console application..."));
    m_pEditorApplicationArgument->setPlaceholderText(UICloudConsoleManager::tr("Enter an argument for this console application..."));
    m_pEditorProfileName->setPlaceholderText(UICloudConsoleManager::tr("Enter a name for this console profile..."));
    m_pEditorProfileArgument->setPlaceholderText(UICloudConsoleManager::tr("Enter an argument for this console profile..."));

    /* Translate buttons: */
    if (m_pButtonBox)
    {
        /* 'Reset' button: */
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setText(UICloudConsoleManager::tr("Reset"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setStatusTip(UICloudConsoleManager::tr("Reset changes in current console details"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setShortcut(Qt::Key_Escape);
        m_pButtonBox->button(QDialogButtonBox::Cancel)->
            setToolTip(UICloudConsoleManager::tr("Reset Changes (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Cancel)->shortcut().toString()));
        /* 'Apply' button: */
        m_pButtonBox->button(QDialogButtonBox::Ok)->setText(UICloudConsoleManager::tr("Apply"));
        m_pButtonBox->button(QDialogButtonBox::Ok)->setStatusTip(UICloudConsoleManager::tr("Apply changes in current console details"));
        m_pButtonBox->button(QDialogButtonBox::Ok)->setShortcut(QString("Ctrl+Return"));
        m_pButtonBox->button(QDialogButtonBox::Ok)->
            setToolTip(UICloudConsoleManager::tr("Apply Changes (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Ok)->shortcut().toString()));
    }

    /* Retranslate validation: */
    retranslateValidation();
}

void UICloudConsoleDetailsWidget::sltApplicationNameChanged(const QString &strName)
{
    /* Push changes back: */
    m_newApplicationData.m_strName = strName;

    /* Revalidate: */
    revalidate(m_pEditorApplicationName);
    /* Update button states: */
    updateButtonStates();
}

void UICloudConsoleDetailsWidget::sltApplicationPathChanged(const QString &strPath)
{
    /* Push changes back: */
    m_newApplicationData.m_strPath = strPath;

    /* Revalidate: */
    revalidate(m_pEditorApplicationPath);
    /* Update button states: */
    updateButtonStates();
}

void UICloudConsoleDetailsWidget::sltApplicationArgumentChanged(const QString &strArgument)
{
    /* Push changes back: */
    m_newApplicationData.m_strArgument = strArgument;

    /* Revalidate: */
    revalidate(m_pEditorApplicationArgument);
    /* Update button states: */
    updateButtonStates();
}

void UICloudConsoleDetailsWidget::sltProfileNameChanged(const QString &strName)
{
    /* Push changes back: */
    m_newProfileData.m_strName = strName;

    /* Revalidate: */
    revalidate(m_pEditorProfileName);
    /* Update button states: */
    updateButtonStates();
}

void UICloudConsoleDetailsWidget::sltProfileArgumentChanged(const QString &strArgument)
{
    /* Push changes back: */
    m_newProfileData.m_strArgument = strArgument;

    /* Revalidate: */
    revalidate(m_pEditorProfileArgument);
    /* Update button states: */
    updateButtonStates();
}

void UICloudConsoleDetailsWidget::sltHandleButtonBoxClick(QAbstractButton *pButton)
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

void UICloudConsoleDetailsWidget::prepare()
{
    /* Prepare widgets: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloudConsoleDetailsWidget::sltRetranslateUI);

    /* Update button states finally: */
    updateButtonStates();
}

void UICloudConsoleDetailsWidget::prepareWidgets()
{
    /* Create main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        pMainLayout->setContentsMargins(0, 0, 0, 0);

        /* Create stacked layout: */
        m_pStackedLayout = new QStackedLayout;
        if (m_pStackedLayout)
        {
            /* Add into layout: */
            pMainLayout->addLayout(m_pStackedLayout);

            /* Create application widget: */
            QWidget *pWidgetApplication = new QWidget;
            if (pWidgetApplication)
            {
                /* Create application layout: */
                QGridLayout *pLayoutApplication = new QGridLayout(pWidgetApplication);
                if (pLayoutApplication)
                {
                    pLayoutApplication->setContentsMargins(0, 0, 0, 0);
                    pLayoutApplication->setRowStretch(3, 1);

                    if (m_enmEmbedding == EmbedTo_Dialog)
                    {
                        pLayoutApplication->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
                        pLayoutApplication->setSpacing(10);
#else
                        pLayoutApplication->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif
                    }
                    else
                    {
#ifdef VBOX_WS_MAC
                        pLayoutApplication->setContentsMargins(13, 0, 13, 13);
                        pLayoutApplication->setSpacing(10);
#else
                        const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) * 1.5;
                        const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin) * 1.5;
                        const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin) * 1.5;
                        const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) * 1.5;
                        pLayoutApplication->setContentsMargins(iL, iT, iR, iB);
#endif
                    }

                    /* Create name editor: */
                    m_pEditorApplicationName = new QLineEdit(pWidgetApplication);
                    if (m_pEditorApplicationName)
                    {
                        connect(m_pEditorApplicationName, &QLineEdit::textChanged,
                                this, &UICloudConsoleDetailsWidget::sltApplicationNameChanged);

                        /* Add into layout: */
                        pLayoutApplication->addWidget(m_pEditorApplicationName, 0, 1);
                    }
                    /* Create name label: */
                    m_pLabelApplicationName = new QLabel(pWidgetApplication);
                    if (m_pLabelApplicationName)
                    {
                        m_pLabelApplicationName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                        m_pLabelApplicationName->setBuddy(m_pEditorApplicationName);

                        /* Add into layout: */
                        pLayoutApplication->addWidget(m_pLabelApplicationName, 0, 0);
                    }

                    /* Create path editor: */
                    m_pEditorApplicationPath = new QLineEdit(pWidgetApplication);
                    if (m_pEditorApplicationPath)
                    {
                        connect(m_pEditorApplicationPath, &QLineEdit::textChanged,
                                this, &UICloudConsoleDetailsWidget::sltApplicationPathChanged);

                        /* Add into layout: */
                        pLayoutApplication->addWidget(m_pEditorApplicationPath, 1, 1);
                    }
                    /* Create path label: */
                    m_pLabelApplicationPath = new QLabel(pWidgetApplication);
                    if (m_pLabelApplicationPath)
                    {
                        m_pLabelApplicationPath->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                        m_pLabelApplicationPath->setBuddy(m_pEditorApplicationPath);

                        /* Add into layout: */
                        pLayoutApplication->addWidget(m_pLabelApplicationPath, 1, 0);
                    }

                    /* Create argument editor: */
                    m_pEditorApplicationArgument = new QLineEdit(pWidgetApplication);
                    if (m_pEditorApplicationArgument)
                    {
                        connect(m_pEditorApplicationArgument, &QLineEdit::textChanged,
                                this, &UICloudConsoleDetailsWidget::sltApplicationArgumentChanged);

                        /* Add into layout: */
                        pLayoutApplication->addWidget(m_pEditorApplicationArgument, 2, 1);
                    }
                    /* Create argument label: */
                    m_pLabelApplicationArgument = new QLabel(pWidgetApplication);
                    if (m_pLabelApplicationArgument)
                    {
                        m_pLabelApplicationArgument->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                        m_pLabelApplicationArgument->setBuddy(m_pEditorApplicationArgument);

                        /* Add into layout: */
                        pLayoutApplication->addWidget(m_pLabelApplicationArgument, 2, 0);
                    }
                }

                /* Add into layout: */
                m_pStackedLayout->addWidget(pWidgetApplication);
            }

            /* Create profile widget: */
            QWidget *pWidgetProfile = new QWidget;
            if (pWidgetProfile)
            {
                /* Create profile layout: */
                QGridLayout *pLayoutProfile = new QGridLayout(pWidgetProfile);
                if (pLayoutProfile)
                {
                    pLayoutProfile->setContentsMargins(0, 0, 0, 0);
                    pLayoutProfile->setRowStretch(2, 1);

                    if (m_enmEmbedding == EmbedTo_Dialog)
                    {
                        pLayoutProfile->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
                        pLayoutProfile->setSpacing(10);
#else
                        pLayoutProfile->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif
                    }
                    else
                    {
#ifdef VBOX_WS_MAC
                        pLayoutProfile->setContentsMargins(13, 0, 13, 13);
                        pLayoutProfile->setSpacing(10);
#else
                        const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) * 1.5;
                        const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin) * 1.5;
                        const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin) * 1.5;
                        const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) * 1.5;
                        pLayoutProfile->setContentsMargins(iL, iT, iR, iB);
#endif
                    }

                    /* Create name editor: */
                    m_pEditorProfileName = new QLineEdit(pWidgetProfile);
                    if (m_pEditorProfileName)
                    {
                        connect(m_pEditorProfileName, &QLineEdit::textChanged,
                                this, &UICloudConsoleDetailsWidget::sltProfileNameChanged);

                        /* Add into layout: */
                        pLayoutProfile->addWidget(m_pEditorProfileName, 0, 1);
                    }
                    /* Create name label: */
                    m_pLabelProfileName = new QLabel(pWidgetProfile);
                    if (m_pLabelProfileName)
                    {
                        m_pLabelProfileName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                        m_pLabelProfileName->setBuddy(m_pEditorProfileName);

                        /* Add into layout: */
                        pLayoutProfile->addWidget(m_pLabelProfileName, 0, 0);
                    }

                    /* Create argument editor: */
                    m_pEditorProfileArgument = new QLineEdit(pWidgetProfile);
                    if (m_pEditorProfileArgument)
                    {
                        connect(m_pEditorProfileArgument, &QLineEdit::textChanged,
                                this, &UICloudConsoleDetailsWidget::sltProfileArgumentChanged);

                        /* Add into layout: */
                        pLayoutProfile->addWidget(m_pEditorProfileArgument, 1, 1);
                    }
                    /* Create name label: */
                    m_pLabelProfileArgument = new QLabel(pWidgetProfile);
                    if (m_pLabelProfileArgument)
                    {
                        m_pLabelProfileArgument->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                        m_pLabelProfileArgument->setBuddy(m_pEditorProfileArgument);

                        /* Add into layout: */
                        pLayoutProfile->addWidget(m_pLabelProfileArgument, 1, 0);
                    }
                }

                /* Add into layout: */
                m_pStackedLayout->addWidget(pWidgetProfile);
            }
        }

        /* If parent embedded into stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Create button-box: */
            m_pButtonBox = new QIDialogButtonBox;
            if  (m_pButtonBox)
            {
                m_pButtonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
                connect(m_pButtonBox, &QIDialogButtonBox::clicked, this, &UICloudConsoleDetailsWidget::sltHandleButtonBoxClick);

                /* Add into layout: */
                pMainLayout->addWidget(m_pButtonBox);
            }
        }
    }
}

void UICloudConsoleDetailsWidget::loadData()
{
    /* If application pane is selected: */
    if (m_pStackedLayout->currentIndex() == 0)
    {
        m_pEditorApplicationName->setText(m_oldApplicationData.m_strName);
        m_pEditorApplicationPath->setText(m_oldApplicationData.m_strPath);
        m_pEditorApplicationArgument->setText(m_oldApplicationData.m_strArgument);
    }
    /* If profile pane is selected: */
    else
    {
        m_pEditorProfileName->setText(m_oldProfileData.m_strName);
        m_pEditorProfileArgument->setText(m_oldProfileData.m_strArgument);
    }
}

void UICloudConsoleDetailsWidget::revalidate(QWidget *pWidget /* = 0 */)
{
    /// @todo validate application/profile details!

    /* Retranslate validation: */
    retranslateValidation(pWidget);
}

void UICloudConsoleDetailsWidget::retranslateValidation(QWidget *pWidget /* = 0 */)
{
    Q_UNUSED(pWidget);

    /// @todo translate vaidation errors!
}

void UICloudConsoleDetailsWidget::updateButtonStates()
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

    /* If application pane is selected: */
    if (m_pStackedLayout->currentIndex() == 0)
    {
        /* Update 'Apply' / 'Reset' button states: */
        if (m_pButtonBox)
        {
            m_pButtonBox->button(QDialogButtonBox::Cancel)->setEnabled(m_oldApplicationData != m_newApplicationData);
            m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(m_oldApplicationData != m_newApplicationData);
        }

        /* Notify listeners as well: */
        emit sigDataChanged(m_oldApplicationData != m_newApplicationData);
    }
    /* If profile pane is selected: */
    else
    {
        /* Update 'Apply' / 'Reset' button states: */
        if (m_pButtonBox)
        {
            m_pButtonBox->button(QDialogButtonBox::Cancel)->setEnabled(m_oldProfileData != m_newProfileData);
            m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(m_oldProfileData != m_newProfileData);
        }

        /* Notify listeners as well: */
        emit sigDataChanged(m_oldProfileData != m_newProfileData);
    }
}
