/* $Id: UIMediumDetailsWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumDetailsWidget class implementation.
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
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStackedLayout>
#include <QStyle>
#include <QTextEdit>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIFileDialog.h"
#include "QILabel.h"
#include "QILineEdit.h"
#include "QITabWidget.h"
#include "QIToolButton.h"
#include "UIConverter.h"
#include "UIIconPool.h"
#include "UIMediumDetailsWidget.h"
#include "UIMediumManager.h"
#include "UIMediumSizeEditor.h"
#include "UITranslator.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CSystemProperties.h"


UIMediumDetailsWidget::UIMediumDetailsWidget(UIMediumManagerWidget *pParent, EmbedTo enmEmbedding)
    : QWidget(pParent)
    , m_pParent(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_oldData(UIDataMedium())
    , m_newData(UIDataMedium())
    , m_pTabWidget(0)
    , m_pLabelType(0), m_pComboBoxType(0), m_pErrorPaneType(0)
    , m_pLabelLocation(0), m_pEditorLocation(0), m_pErrorPaneLocation(0), m_pButtonLocation(0)
    , m_pLabelDescription(0), m_pEditorDescription(0), m_pErrorPaneDescription(0)
    , m_pLabelSize(0), m_pEditorSize(0), m_pErrorPaneSize(0)
    , m_pButtonBox(0)
    , m_pProgressBar(0)
    , m_fValid(true)
    , m_pLayoutDetails(0)
{
    /* Prepare: */
    prepare();
}

void UIMediumDetailsWidget::setCurrentType(UIMediumDeviceType enmType)
{
    /* If known type was requested => raise corresponding container: */
    if (m_aContainers.contains(enmType))
        m_pLayoutDetails->setCurrentWidget(infoContainer(enmType));
}

void UIMediumDetailsWidget::setData(const UIDataMedium &data)
{
    /* Cache old/new data: */
    m_oldData = data;
    m_newData = m_oldData;

    /* Load options data: */
    loadDataForOptions();
    /* Load details data: */
    loadDataForDetails();
}

void UIMediumDetailsWidget::enableDisableMediumModificationWidgets(bool fMediumIsModifiable)
{
    if (m_pComboBoxType)
        m_pComboBoxType->setEnabled(fMediumIsModifiable);
    if (m_pEditorLocation)
        m_pEditorLocation->setEnabled(fMediumIsModifiable);
    if (m_pEditorSize)
        m_pEditorSize->setEnabled(fMediumIsModifiable);
    if (m_pEditorDescription)
        m_pEditorDescription->setEnabled(fMediumIsModifiable);
}

void UIMediumDetailsWidget::setOptionsEnabled(bool fEnabled)
{
    m_pTabWidget->widget(0)->setEnabled(fEnabled);
}

void UIMediumDetailsWidget::sltRetranslateUI()
{
    /* Translate tab-widget: */
    m_pTabWidget->setTabText(0, UIMediumManager::tr("&Attributes"));
    m_pTabWidget->setTabText(1, UIMediumManager::tr("&Information"));

    /* Translate 'Options' tab content. */

    /* Translate labels: */
    m_pLabelType->setText(UIMediumManager::tr("&Type:"));
    m_pLabelLocation->setText(UIMediumManager::tr("&Location:"));
    m_pLabelDescription->setText(UIMediumManager::tr("&Description:"));
    m_pLabelSize->setText(UIMediumManager::tr("&Size:"));

    /* Translate fields: */
    m_pComboBoxType->setToolTip(UIMediumManager::tr("Holds the type of this medium."));
    for (int i = 0; i < m_pComboBoxType->count(); ++i)
        m_pComboBoxType->setItemText(i, gpConverter->toString(m_pComboBoxType->itemData(i).value<KMediumType>()));
    m_pEditorLocation->setToolTip(UIMediumManager::tr("Holds the location of this medium."));
    m_pButtonLocation->setText(UIMediumManager::tr("Choose Medium Location"));
    m_pButtonLocation->setToolTip(UIMediumManager::tr("Choose Medium Location"));
    m_pEditorDescription->setToolTip(UIMediumManager::tr("Holds the description of this medium."));
    m_pEditorSize->setToolTip(UIMediumManager::tr("Holds the size of this medium."));

    /* Translate button-box: */
    if (m_pButtonBox)
    {
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setText(UIMediumManager::tr("Reset"));
        m_pButtonBox->button(QDialogButtonBox::Ok)->setText(UIMediumManager::tr("Apply"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setShortcut(Qt::Key_Escape);
        m_pButtonBox->button(QDialogButtonBox::Ok)->setShortcut(QString("Ctrl+Return"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setStatusTip(UIMediumManager::tr("Reset changes in current medium details"));
        m_pButtonBox->button(QDialogButtonBox::Ok)->setStatusTip(UIMediumManager::tr("Apply changes in current medium details"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->
            setToolTip(UIMediumManager::tr("Reset Changes (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Cancel)->shortcut().toString()));
        m_pButtonBox->button(QDialogButtonBox::Ok)->
            setToolTip(UIMediumManager::tr("Apply Changes (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Ok)->shortcut().toString()));
    }

    /* Translate 'Details' tab content. */

    /* Retranslate validation: */
    retranslateValidation();
}

void UIMediumDetailsWidget::sltTypeIndexChanged(int iIndex)
{
    m_newData.m_options.m_enmMediumType = m_pComboBoxType->itemData(iIndex).value<KMediumType>();
    revalidate(m_pErrorPaneType);
    updateButtonStates();
}

void UIMediumDetailsWidget::sltLocationPathChanged(const QString &strPath)
{
    m_newData.m_options.m_strLocation = strPath;
    revalidate(m_pErrorPaneLocation);
    updateButtonStates();
}

void UIMediumDetailsWidget::sltChooseLocationPath()
{
    /* Open file-save dialog to choose location for current medium: */
    const QString strFileName = QIFileDialog::getSaveFileName(m_pEditorLocation->text(),
                                                              QApplication::translate("UIMediumManager", "Current extension (*.%1)")
                                                                 .arg(QFileInfo(m_oldData.m_options.m_strLocation).suffix()),
                                                              this,
                                                              QApplication::translate("UIMediumManager", "Choose the location of this medium"),
                                                              0, true, true);
    if (!strFileName.isNull())
        m_pEditorLocation->setText(QDir::toNativeSeparators(strFileName));
}

void UIMediumDetailsWidget::sltDescriptionTextChanged()
{
    m_newData.m_options.m_strDescription = m_pEditorDescription->toPlainText();
    revalidate(m_pErrorPaneDescription);
    updateButtonStates();
}

void UIMediumDetailsWidget::sltSizeValueChanged(qulonglong uSize)
{
    m_newData.m_options.m_uLogicalSize = uSize;
    revalidate(m_pErrorPaneSize);
    updateButtonStates();
}

void UIMediumDetailsWidget::sltHandleButtonBoxClick(QAbstractButton *pButton)
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

void UIMediumDetailsWidget::prepare()
{
    /* Prepare this: */
    prepareThis();

    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMediumDetailsWidget::sltRetranslateUI);


    /* Update button states finally: */
    updateButtonStates();
}

void UIMediumDetailsWidget::prepareThis()
{
    /* Create layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    AssertPtrReturnVoid(pLayout);
    {
        /* Configure layout: */
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare tab-widget: */
        prepareTabWidget();
    }
}

void UIMediumDetailsWidget::prepareTabWidget()
{
    /* Create tab-widget: */
    m_pTabWidget = new QITabWidget;
    AssertPtrReturnVoid(m_pTabWidget);
    {
        /* Prepare 'Options' tab: */
        prepareTabOptions();
        /* Prepare 'Details' tab: */
        prepareTabDetails();

        /* Add into layout: */
        layout()->addWidget(m_pTabWidget);
    }
}

void UIMediumDetailsWidget::prepareTabOptions()
{
    /* Create 'Options' tab: */
    QWidget *pTabOptions = new QWidget;
    AssertPtrReturnVoid(pTabOptions);
    {
        /* Create 'Options' layout: */
        QGridLayout *pLayoutOptions = new QGridLayout(pTabOptions);
        AssertPtrReturnVoid(pLayoutOptions);
        {
#ifdef VBOX_WS_MAC
            /* Configure layout: */
            pLayoutOptions->setSpacing(10);
            pLayoutOptions->setContentsMargins(10, 10, 10, 10);
            // WORKAROUND:
            // Using adjusted vertical spacing because there are special widgets which
            // requires more care and attention, UIFilePathSelector and UIMediumSizeEditor.
            pLayoutOptions->setVerticalSpacing(6);
#endif

            /* Get the required icon metric: */
            const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);

            /* Create type label: */
            m_pLabelType = new QLabel;
            AssertPtrReturnVoid(m_pLabelType);
            {
                /* Configure label: */
                m_pLabelType->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                /* Add into layout: */
                pLayoutOptions->addWidget(m_pLabelType, 0, 0);
            }

            /* Create type layout: */
            QHBoxLayout *pLayoutType = new QHBoxLayout;
            AssertPtrReturnVoid(pLayoutType);
            {
                /* Configure layout: */
                pLayoutType->setContentsMargins(0, 0, 0, 0);

                /* Create type editor: */
                m_pComboBoxType = new QComboBox;
                AssertPtrReturnVoid(m_pComboBoxType);
                {
                    /* Configure editor: */
                    m_pLabelType->setBuddy(m_pComboBoxType);
                    m_pComboBoxType->setSizeAdjustPolicy(QComboBox::AdjustToContents);
                    m_pComboBoxType->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                    connect(m_pComboBoxType, &QComboBox::activated,
                            this, &UIMediumDetailsWidget::sltTypeIndexChanged);
                    /* Add into layout: */
                    pLayoutType->addWidget(m_pComboBoxType);
                }

                /* Add stretch: */
                pLayoutType->addStretch();

                /* Create type error pane: */
                m_pErrorPaneType = new QLabel;
                AssertPtrReturnVoid(m_pErrorPaneType);
                {
                    /* Configure label: */
                    m_pErrorPaneType->setAlignment(Qt::AlignCenter);
                    m_pErrorPaneType->setPixmap(UIIconPool::iconSet(":/status_error_16px.png")
                                                .pixmap(QSize(iIconMetric, iIconMetric)));
                    /* Add into layout: */
                    pLayoutType->addWidget(m_pErrorPaneType);
                }

                /* Add into layout: */
                pLayoutOptions->addLayout(pLayoutType, 0, 1);
            }

            /* Create location label: */
            m_pLabelLocation = new QLabel;
            AssertPtrReturnVoid(m_pLabelLocation);
            {
                /* Configure label: */
                m_pLabelLocation->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                /* Add into layout: */
                pLayoutOptions->addWidget(m_pLabelLocation, 1, 0);
            }

            /* Create location layout: */
            QHBoxLayout *pLayoutLocation = new QHBoxLayout;
            AssertPtrReturnVoid(pLayoutLocation);
            {
                /* Configure layout: */
                pLayoutLocation->setContentsMargins(0, 0, 0, 0);

                /* Create location editor: */
                m_pEditorLocation = new QLineEdit;
                AssertPtrReturnVoid(m_pEditorLocation);
                {
                    /* Configure editor: */
                    m_pLabelLocation->setBuddy(m_pEditorLocation);
                    m_pEditorLocation->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
                    connect(m_pEditorLocation, &QLineEdit::textChanged,
                            this, &UIMediumDetailsWidget::sltLocationPathChanged);
                    /* Add into layout: */
                    pLayoutLocation->addWidget(m_pEditorLocation);
                }

                /* Create location error pane: */
                m_pErrorPaneLocation = new QLabel;
                AssertPtrReturnVoid(m_pErrorPaneLocation);
                {
                    /* Configure label: */
                    m_pErrorPaneLocation->setAlignment(Qt::AlignCenter);
                    m_pErrorPaneLocation->setPixmap(UIIconPool::iconSet(":/status_error_16px.png")
                                                    .pixmap(QSize(iIconMetric, iIconMetric)));
                    /* Add into layout: */
                    pLayoutLocation->addWidget(m_pErrorPaneLocation);
                }

                /* Create location button: */
                m_pButtonLocation = new QIToolButton;
                AssertPtrReturnVoid(m_pButtonLocation);
                {
                    /* Configure editor: */
                    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
                    m_pButtonLocation->setIconSize(QSize(iIconMetric, iIconMetric));
                    m_pButtonLocation->setIcon(UIIconPool::iconSet(":/select_file_16px.png"));
                    m_pButtonLocation->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                    connect(m_pButtonLocation, &QIToolButton::clicked,
                            this, &UIMediumDetailsWidget::sltChooseLocationPath);
                    /* Add into layout: */
                    pLayoutLocation->addWidget(m_pButtonLocation);
                }

                /* Add into layout: */
                pLayoutOptions->addLayout(pLayoutLocation, 1, 1);
            }

            /* Create description label: */
            m_pLabelDescription = new QLabel;
            AssertPtrReturnVoid(m_pLabelDescription);
            {
                /* Configure label: */
                m_pLabelDescription->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                /* Add into layout: */
                pLayoutOptions->addWidget(m_pLabelDescription, 2, 0);
            }

            /* Create description layout: */
            QGridLayout *pLayoutDescription = new QGridLayout;
            AssertPtrReturnVoid(pLayoutDescription);
            {
                /* Configure layout: */
                pLayoutDescription->setContentsMargins(0, 0, 0, 0);

                /* Create description editor: */
                m_pEditorDescription = new QTextEdit;
                AssertPtrReturnVoid(m_pEditorDescription);
                {
                    /* Configure editor: */
                    m_pLabelDescription->setBuddy(m_pEditorDescription);
                    QFontMetrics fontMetrics = m_pEditorDescription->fontMetrics();
                    QTextDocument *pTextDocument = m_pEditorDescription->document();
                    const int iMinimumHeight = fontMetrics.lineSpacing() * 3
                                             + pTextDocument->documentMargin() * 2
                                             + m_pEditorDescription->frameWidth() * 2;
                    m_pEditorDescription->setMaximumHeight(iMinimumHeight);
                    connect(m_pEditorDescription, &QTextEdit::textChanged,
                            this, &UIMediumDetailsWidget::sltDescriptionTextChanged);
                    /* Add into layout: */
                    pLayoutDescription->addWidget(m_pEditorDescription, 0, 0, 2, 1);
                }

                /* Create description error pane: */
                m_pErrorPaneDescription = new QLabel;
                AssertPtrReturnVoid(m_pErrorPaneDescription);
                {
                    /* Configure label: */
                    m_pErrorPaneDescription->setAlignment(Qt::AlignCenter);
                    m_pErrorPaneDescription->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                    m_pErrorPaneDescription->setPixmap(UIIconPool::iconSet(":/status_error_16px.png")
                                                       .pixmap(QSize(iIconMetric, iIconMetric)));
                    /* Add into layout: */
                    pLayoutDescription->addWidget(m_pErrorPaneDescription, 0, 1, Qt::AlignCenter);
                }

                /* Add into layout: */
                pLayoutOptions->addLayout(pLayoutDescription, 2, 1, 2, 1);
            }

            /* Create size label: */
            m_pLabelSize = new QLabel;
            AssertPtrReturnVoid(m_pLabelSize);
            {
                /* Configure label: */
                m_pLabelSize->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                /* Add into layout: */
                pLayoutOptions->addWidget(m_pLabelSize, 4, 0);
            }

            /* Create size layout: */
            QGridLayout *pLayoutSize = new QGridLayout;
            AssertPtrReturnVoid(pLayoutSize);
            {
                /* Configure layout: */
                pLayoutSize->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
                // WORKAROUND:
                // Using adjusted vertical stretch because there is special widget
                // which requires more care and attention, UIMediumSizeEditor.
                pLayoutSize->setRowStretch(0, 3);
                pLayoutSize->setRowStretch(1, 2);
#endif

                /* Create size editor: */
                m_pEditorSize = new UIMediumSizeEditor(0 /* parent */);
                AssertPtrReturnVoid(m_pEditorSize);
                {
                    /* Configure editor: */
                    m_pLabelSize->setBuddy(m_pEditorSize);
                    m_pEditorSize->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
                    connect(m_pEditorSize, &UIMediumSizeEditor::sigSizeChanged,
                            this, &UIMediumDetailsWidget::sltSizeValueChanged);
                    /* Add into layout: */
                    pLayoutSize->addWidget(m_pEditorSize, 0, 0, 2, 1);
                }

                /* Create size error pane: */
                m_pErrorPaneSize = new QLabel;
                AssertPtrReturnVoid(m_pErrorPaneSize);
                {
                    /* Configure label: */
                    m_pErrorPaneSize->setAlignment(Qt::AlignCenter);
                    m_pErrorPaneSize->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                    m_pErrorPaneSize->setPixmap(UIIconPool::iconSet(":/status_error_16px.png")
                                                .pixmap(QSize(iIconMetric, iIconMetric)));
                    /* Add into layout: */
                    pLayoutSize->addWidget(m_pErrorPaneSize, 0, 1, Qt::AlignCenter);
                }

                /* Add into layout: */
                pLayoutOptions->addLayout(pLayoutSize, 4, 1, 2, 1);
            }

            /* Create stretch: */
            QSpacerItem *pSpacer2 = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            AssertPtrReturnVoid(pSpacer2);
            {
                /* Add into layout: */
                pLayoutOptions->addItem(pSpacer2, 6, 0, 1, 2);
            }

            /* If parent embedded into stack: */
            if (m_enmEmbedding == EmbedTo_Stack)
            {
                /* Create button-box: */
                m_pButtonBox = new QIDialogButtonBox;
                AssertPtrReturnVoid(m_pButtonBox);
                {
                    /* Configure button-box: */
                    m_pButtonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
                    connect(m_pButtonBox, &QIDialogButtonBox::clicked, this, &UIMediumDetailsWidget::sltHandleButtonBoxClick);

                    /* Create progress-bar: */
                    m_pProgressBar = new UIEnumerationProgressBar;
                    AssertPtrReturnVoid(m_pProgressBar);
                    {
                        /* Configure progress-bar: */
                        m_pProgressBar->hide();
                        /* Add progress-bar into button-box layout: */
                        m_pButtonBox->addExtraWidget(m_pProgressBar);
                        /* Notify parent it has progress-bar: */
                        m_pParent->setProgressBar(m_pProgressBar);
                    }
                }

                /* Add into layout: */
                pLayoutOptions->addWidget(m_pButtonBox, 7, 0, 1, 2);
            }
        }

        /* Add to tab-widget: */
        m_pTabWidget->addTab(pTabOptions, QString());
    }
}

void UIMediumDetailsWidget::prepareTabDetails()
{
    /* Create 'Details' tab: */
    QWidget *pTabDetails = new QWidget;
    AssertPtrReturnVoid(pTabDetails);
    {
        /* Create stacked layout: */
        m_pLayoutDetails = new QStackedLayout(pTabDetails);
        AssertPtrReturnVoid(m_pLayoutDetails);
        {
            /* Create information-containers: */
            for (int i = (int)UIMediumDeviceType_HardDisk; i < (int)UIMediumDeviceType_All; ++i)
            {
                const UIMediumDeviceType enmType = (UIMediumDeviceType)i;
                prepareInformationContainer(enmType, enmType == UIMediumDeviceType_HardDisk ? 5 : 2); /// @todo Remove hard-coded values.
            }
        }

        /* Add to tab-widget: */
        m_pTabWidget->addTab(pTabDetails, QString());
    }
}

void UIMediumDetailsWidget::prepareInformationContainer(UIMediumDeviceType enmType, int cFields)
{
    /* Create information-container: */
    m_aContainers[enmType] = new QWidget;
    QWidget *pContainer = infoContainer(enmType);
    AssertPtrReturnVoid(pContainer);
    {
        /* Create layout: */
        new QGridLayout(pContainer);
        QGridLayout *pLayout = qobject_cast<QGridLayout*>(pContainer->layout());
        AssertPtrReturnVoid(pLayout);
        {
            /* Configure layout: */
            pLayout->setVerticalSpacing(0);
            pLayout->setColumnStretch(1, 1);

            /* Create labels & fields: */
            int i = 0;
            for (; i < cFields; ++i)
            {
                /* Create label: */
                m_aLabels[enmType] << new QLabel;
                QLabel *pLabel = infoLabel(enmType, i);
                AssertPtrReturnVoid(pLabel);
                {
                    /* Configure label: */
                    pLabel->setMargin(2);
                    pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

                    /* Add into layout: */
                    pLayout->addWidget(pLabel, i, 0);
                }

                /* Create field: */
                m_aFields[enmType] << new QILabel;
                QILabel *pField = infoField(enmType, i);
                AssertPtrReturnVoid(pField);
                {
                    /* Configure field: */
                    pField->setMargin(2);
                    pField->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed));
                    pField->setFullSizeSelection(true);

                    /* Add into layout: */
                    pLayout->addWidget(pField, i, 1);
                }
            }

            /* Create stretch: */
            QSpacerItem *pSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            AssertPtrReturnVoid(pSpacer);
            {
                /* Add into layout: */
                pLayout->addItem(pSpacer, i, 0, 1, 2);
            }
        }

        /* Add into layout: */
        m_pLayoutDetails->addWidget(pContainer);
    }
}

void UIMediumDetailsWidget::loadDataForOptions()
{
    /* Clear type combo-box: */
    m_pLabelType->setEnabled(m_newData.m_fValid);
    m_pComboBoxType->setEnabled(m_newData.m_fValid);
    m_pComboBoxType->clear();
    if (m_newData.m_fValid)
    {
        /* Populate type combo-box: */
        switch (m_newData.m_enmDeviceType)
        {
            case UIMediumDeviceType_HardDisk:
            {
                /* No type changes for differencing disks: */
                if (m_oldData.m_enmVariant & KMediumVariant_Diff)
                    m_pComboBoxType->addItem(QString(), m_oldData.m_options.m_enmMediumType);
                else
                {
                    m_pComboBoxType->addItem(QString(), QVariant::fromValue(KMediumType_Normal));
                    m_pComboBoxType->addItem(QString(), QVariant::fromValue(KMediumType_Immutable));
                    if (!m_newData.m_fHasChildren)
                    {
                        m_pComboBoxType->addItem(QString(), QVariant::fromValue(KMediumType_Writethrough));
                        m_pComboBoxType->addItem(QString(), QVariant::fromValue(KMediumType_Shareable));
                    }
                    m_pComboBoxType->addItem(QString(), QVariant::fromValue(KMediumType_MultiAttach));
                }
                break;
            }
            case UIMediumDeviceType_DVD:
            {
                m_pComboBoxType->addItem(QString(), QVariant::fromValue(KMediumType_Readonly));
                break;
            }
            case UIMediumDeviceType_Floppy:
            {
                m_pComboBoxType->addItem(QString(), QVariant::fromValue(KMediumType_Writethrough));
                m_pComboBoxType->addItem(QString(), QVariant::fromValue(KMediumType_Readonly));
                break;
            }
            default:
                break;
        }
        /* Translate type combo-box: */
        for (int i = 0; i < m_pComboBoxType->count(); ++i)
        {
            m_pComboBoxType->setItemText(i, gpConverter->toString(m_pComboBoxType->itemData(i).value<KMediumType>()));
            m_pComboBoxType->setItemData(i, mediumTypeTip(m_pComboBoxType->itemData(i).value<KMediumType>()), Qt::ToolTipRole);
        }
    }

    /* Choose the item with required type to be the current one: */
    for (int i = 0; i < m_pComboBoxType->count(); ++i)
        if (m_pComboBoxType->itemData(i).value<KMediumType>() == m_newData.m_options.m_enmMediumType)
            m_pComboBoxType->setCurrentIndex(i);
    sltTypeIndexChanged(m_pComboBoxType->currentIndex());

    /* Load location: */
    m_pLabelLocation->setEnabled(m_newData.m_fValid);
    m_pEditorLocation->setEnabled(m_newData.m_fValid);
    m_pButtonLocation->setEnabled(m_newData.m_fValid);
    m_pEditorLocation->setText(m_newData.m_options.m_strLocation);

    /* Load description: */
    m_pLabelDescription->setEnabled(m_newData.m_fValid);
    m_pEditorDescription->setEnabled(m_newData.m_fValid);
    m_pEditorDescription->setPlainText(m_newData.m_options.m_strDescription);

    /* Load size: */
    const bool fEnableResize =    m_newData.m_fValid
                               && m_newData.m_enmDeviceType == UIMediumDeviceType_HardDisk
                               && !(m_newData.m_enmVariant & KMediumVariant_Fixed);
    m_pLabelSize->setEnabled(fEnableResize);
    m_pEditorSize->setEnabled(fEnableResize);
    m_pEditorSize->setMediumSize(m_newData.m_options.m_uLogicalSize);
    sltSizeValueChanged(m_pEditorSize->mediumSize());

    /* Revalidate: */
    revalidate();
}

void UIMediumDetailsWidget::loadDataForDetails()
{
    /* Get information-labels just to acquire their number: */
    const QList<QLabel*> aLabels = m_aLabels.value(m_newData.m_enmDeviceType, QList<QLabel*>());
    /* Get information-fields just to acquire their number: */
    const QList<QILabel*> aFields = m_aFields.value(m_newData.m_enmDeviceType, QList<QILabel*>());
    /* For each the label => update contents: */
    for (int i = 0; i < aLabels.size(); ++i)
        infoLabel(m_newData.m_enmDeviceType, i)->setText(m_newData.m_details.m_aLabels.value(i, QString()));
    /* For each the field => update contents: */
    for (int i = 0; i < aFields.size(); ++i)
    {
        infoField(m_newData.m_enmDeviceType, i)->setText(m_newData.m_details.m_aFields.value(i, QString()));
        infoField(m_newData.m_enmDeviceType, i)->setEnabled(!infoField(m_newData.m_enmDeviceType, i)->text().trimmed().isEmpty());
    }
}

void UIMediumDetailsWidget::revalidate(QWidget *pWidget /* = 0 */)
{
    /* Reset the result: */
    m_fValid = true;

    /* Validate 'Options' tab content: */
    if (!pWidget || pWidget == m_pErrorPaneType)
    {
        /* Always valid for now: */
        const bool fError = false;
        m_pErrorPaneType->setVisible(fError);
        if (fError)
            m_fValid = false;
    }
    if (!pWidget || pWidget == m_pErrorPaneLocation)
    {
        /* If medium is valid itself, details are valid only is location is set: */
        const bool fError = m_newData.m_fValid && m_newData.m_options.m_strLocation.isEmpty();
        m_pErrorPaneLocation->setVisible(fError);
        if (fError)
            m_fValid = false;
    }
    if (!pWidget || pWidget == m_pErrorPaneDescription)
    {
        /* Always valid for now: */
        const bool fError = false;
        m_pErrorPaneDescription->setVisible(fError);
        if (fError)
            m_fValid = false;
    }
    if (!pWidget || pWidget == m_pErrorPaneSize)
    {
        /* Always valid for now: */
        const bool fError = m_newData.m_options.m_uLogicalSize < m_oldData.m_options.m_uLogicalSize;
        m_pErrorPaneSize->setVisible(fError);
        if (fError)
            m_fValid = false;
    }

    /* Retranslate validation: */
    retranslateValidation(pWidget);
}

void UIMediumDetailsWidget::retranslateValidation(QWidget *pWidget /* = 0 */)
{
    /* Translate 'Interface' tab content: */
//    if (!pWidget || pWidget == m_pErrorPaneType)
//        m_pErrorPaneType->setToolTip(UIMediumManager::tr("Cannot change from type <b>%1</b> to <b>%2</b>.")
//                                     .arg(m_oldData.m_options.m_enmType).arg(m_newData.m_options.m_enmType));
    if (!pWidget || pWidget == m_pErrorPaneLocation)
        m_pErrorPaneLocation->setToolTip(UIMediumManager::tr("Location cannot be empty."));
//    if (!pWidget || pWidget == m_pErrorPaneDescription)
//        m_pErrorPaneDescription->setToolTip(UIMediumManager::tr("Cannot change medium description from <b>%1</b> to <b>%2</b>.")
//                                                                .arg(m_oldData.m_options.m_strDescription)
//                                                                .arg(m_newData.m_options.m_strDescription));
    if (!pWidget || pWidget == m_pErrorPaneSize)
        m_pErrorPaneSize->setToolTip(UIMediumManager::tr("Cannot change medium size from <b>%1</b> to <b>%2</b> as storage "
                                                         "shrinking is currently not implemented.")
                                                         .arg(UITranslator::formatSize(m_oldData.m_options.m_uLogicalSize))
                                                         .arg(UITranslator::formatSize(m_newData.m_options.m_uLogicalSize)));
}

void UIMediumDetailsWidget::updateButtonStates()
{
//    if (m_newData != m_oldData)
//    {
//        if (m_newData.m_options != m_oldData.m_options)
//        {
//            if (m_newData.m_options.m_enmType != m_oldData.m_options.m_enmType)
//                printf("Type: %d\n", (int)m_newData.m_options.m_enmType);
//            if (m_newData.m_options.m_uLogicalSize != m_oldData.m_options.m_uLogicalSize)
//                printf("Size: %llu vs %llu\n", m_newData.m_options.m_uLogicalSize, m_oldData.m_options.m_uLogicalSize);
//            if (m_newData.m_options.m_strLocation != m_oldData.m_options.m_strLocation)
//                printf("Location: %s\n", m_newData.m_options.m_strLocation.toUtf8().constData());
//            if (m_newData.m_options.m_strDescription != m_oldData.m_options.m_strDescription)
//                printf("Description: %s\n", m_newData.m_options.m_strDescription.toUtf8().constData());
//        }
//    }

    /* Update 'Apply' / 'Reset' button states: */
    if (m_pButtonBox)
    {
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setEnabled(m_oldData != m_newData);
        m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled((m_oldData != m_newData) && m_fValid);
    }

    /* Notify listeners as well: */
    emit sigRejectAllowed(m_oldData != m_newData);
    emit sigAcceptAllowed((m_oldData != m_newData) && m_fValid);
}

/* static */
QString UIMediumDetailsWidget::mediumTypeTip(KMediumType enmType)
{
    switch (enmType)
    {
        case KMediumType_Normal:
            return UIMediumManager::tr("This type of medium is attached directly or indirectly, preserved when taking "
                                       "snapshots.");
        case KMediumType_Immutable:
            return UIMediumManager::tr("This type of medium is attached indirectly, changes are wiped out the next time the "
                                       "virtual machine is started.");
        case KMediumType_Writethrough:
            return UIMediumManager::tr("This type of medium is attached directly, ignored when taking snapshots.");
        case KMediumType_Shareable:
            return UIMediumManager::tr("This type of medium is attached directly, allowed to be used concurrently by several "
                                       "machines.");
        case KMediumType_Readonly:
            return UIMediumManager::tr("This type of medium is attached directly, and can be used by several machines.");
        case KMediumType_MultiAttach:
            return UIMediumManager::tr("This type of medium is attached indirectly, so that one base medium can be used for "
                                       "several VMs which have their own differencing medium to store their modifications.");
        default:
            break;
    }
    AssertFailedReturn(QString());
}

QWidget *UIMediumDetailsWidget::infoContainer(UIMediumDeviceType enmType) const
{
    /* Return information-container for known medium type: */
    return m_aContainers.value(enmType, 0);
}

QLabel *UIMediumDetailsWidget::infoLabel(UIMediumDeviceType enmType, int iIndex) const
{
    /* Acquire list of labels: */
    const QList<QLabel*> aLabels = m_aLabels.value(enmType, QList<QLabel*>());

    /* Return label for known index: */
    return aLabels.value(iIndex, 0);
}

QILabel *UIMediumDetailsWidget::infoField(UIMediumDeviceType enmType, int iIndex) const
{
    /* Acquire list of fields: */
    const QList<QILabel*> aFields = m_aFields.value(enmType, QList<QILabel*>());

    /* Return label for known index: */
    return aFields.value(iIndex, 0);
}
