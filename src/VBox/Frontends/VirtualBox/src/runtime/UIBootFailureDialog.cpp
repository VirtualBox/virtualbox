/* $Id: UIBootFailureDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIBootTimeErrorDialog class implementation.
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
#include <QAction>
#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QVBoxLayout>
#include <QPushButton>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIRichTextLabel.h"
#include "QIToolButton.h"
#include "UIBootFailureDialog.h"
#include "UIConverter.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIFilePathSelector.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UIMediumTools.h"
#include "UIModalWindowManager.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CMediumAttachment.h"
#include "CStorageController.h"

UIBootFailureDialog::UIBootFailureDialog(QWidget *pParent)
    : QIMainDialog(pParent)
    , m_pParent(pParent)
    , m_pCentralWidget(0)
    , m_pMainLayout(0)
    , m_pButtonBox(0)
    , m_pCloseButton(0)
    , m_pResetButton(0)
    , m_pLabel(0)
    , m_pBootImageSelector(0)
    , m_pBootImageLabel(0)
    , m_pIconLabel(0)
    , m_pSuppressDialogCheckBox(0)
{
    configure();
}

UIBootFailureDialog::~UIBootFailureDialog()
{
    if (m_pSuppressDialogCheckBox && m_pSuppressDialogCheckBox->isChecked())
    {
        QStringList suppressedMessageList = gEDataManager->suppressedMessages();
        suppressedMessageList << gpConverter->toInternalString(UIExtraDataMetaDefs::DialogType_BootFailure);
        gEDataManager->setSuppressedMessages(suppressedMessageList);
    }
}

QString UIBootFailureDialog::bootMediumPath() const
{
    if (!m_pBootImageSelector)
        return QString();
    return m_pBootImageSelector->path();
}

void UIBootFailureDialog::sltRetranslateUI()
{
    if (m_pCloseButton)
    {
        m_pCloseButton->setText(tr("&Cancel"));
        m_pCloseButton->setToolTip(tr("Closes this dialog without resetting the guest or mounting a medium"));
    }
    if (m_pResetButton)
    {
        m_pResetButton->setText(tr("&Mount and Retry Boot"));
        m_pResetButton->setToolTip(tr("Mounts the selected ISO if any and reboots the vm"));
    }

    if (m_pLabel)
        m_pLabel->setText(tr("The virtual machine failed to boot. That might be caused by a missing operating system "
                             "or misconfigured boot order. Mounting an operating system install DVD might solve this problem. "
                             "Selecting an ISO file will attempt to mount it after the dialog is closed."));

    if (m_pBootImageLabel)
        m_pBootImageLabel->setText(tr("DVD:"));
    if (m_pSuppressDialogCheckBox)
    {
        m_pSuppressDialogCheckBox->setText(tr("Do not show this dialog again"));
        m_pSuppressDialogCheckBox->setToolTip(tr("When checked this dialog will not be shown again."));
    }
    if (m_pBootImageSelector)
        m_pBootImageSelector->setToolTip(tr("Holds the path of the ISO to be attached to machine as boot medium."));

}

void UIBootFailureDialog::configure()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/media_manager_32px.png", ":/media_manager_16px.png"));
#endif

    setTitle();
    prepareWidgets();
    prepareConnections();
}

void UIBootFailureDialog::prepareConnections()
{
    if (m_pCloseButton)
        connect(m_pCloseButton, &QPushButton::clicked, this, &UIBootFailureDialog::sltCancel);
    if (m_pResetButton)
        connect(m_pResetButton, &QPushButton::clicked, this, &UIBootFailureDialog::sltReset);
}

void UIBootFailureDialog::prepareWidgets()
{
    m_pCentralWidget = new QWidget;
    if (!m_pCentralWidget)
        return;
    setCentralWidget(m_pCentralWidget);

    m_pMainLayout = new QVBoxLayout;
    m_pCentralWidget->setLayout(m_pMainLayout);

    if (!m_pMainLayout || !menuBar())
        return;

    QHBoxLayout *pTopLayout = new QHBoxLayout;
    pTopLayout->setContentsMargins(0, 0, 0, 0);

    m_pIconLabel = new QLabel;
    if (m_pIconLabel)
    {
        m_pIconLabel->setPixmap(iconPixmap());
        m_pIconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        pTopLayout->addWidget(m_pIconLabel, Qt::AlignTop | Qt::AlignCenter);
    }

    m_pLabel = new QIRichTextLabel;
    if (m_pLabel)
        pTopLayout->addWidget(m_pLabel);

    QHBoxLayout *pSelectorLayout = new QHBoxLayout;
    pSelectorLayout->setContentsMargins(0, 0, 0, 0);
    m_pBootImageLabel = new QLabel;

    if (m_pBootImageLabel)
    {
        pSelectorLayout->addWidget(m_pBootImageLabel);
        m_pBootImageLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    }

    m_pBootImageSelector = new UIFilePathSelector;
    if (m_pBootImageSelector)
    {
        m_pBootImageSelector->setMode(UIFilePathSelector::Mode_File_Open);
        m_pBootImageSelector->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        m_pBootImageSelector->setFileDialogFilters("ISO Images(*.iso *.ISO)");
        m_pBootImageSelector->setResetEnabled(false);
        m_pBootImageSelector->setInitialPath(UIMediumTools::defaultFolderPathForType(UIMediumDeviceType_DVD));
        m_pBootImageSelector->setRecentMediaListType(UIMediumDeviceType_DVD);
        if (m_pBootImageLabel)
            m_pBootImageLabel->setBuddy(m_pBootImageSelector);
        pSelectorLayout->addWidget(m_pBootImageSelector);
        connect(m_pBootImageSelector, &UIFilePathSelector::pathChanged,
                this, &UIBootFailureDialog::sltFileSelectorPathChanged);
    }

    m_pMainLayout->addLayout(pTopLayout);
    m_pMainLayout->addLayout(pSelectorLayout);

    m_pSuppressDialogCheckBox = new QCheckBox;
    if (m_pSuppressDialogCheckBox)
        m_pMainLayout->addWidget(m_pSuppressDialogCheckBox);

    m_pButtonBox = new QIDialogButtonBox;
    if (m_pButtonBox)
    {
        m_pCloseButton = m_pButtonBox->addButton(QString(), QDialogButtonBox::RejectRole);
        m_pResetButton = m_pButtonBox->addButton(QString(), QDialogButtonBox::ActionRole);
        m_pCloseButton->setShortcut(Qt::Key_Escape);

        m_pMainLayout->addWidget(m_pButtonBox);
    }

    m_pMainLayout->addStretch();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIBootFailureDialog::sltRetranslateUI);
}

void UIBootFailureDialog::sltCancel()
{
    done(static_cast<int>(ReturnCode_Close));
}

void UIBootFailureDialog::sltReset()
{
    done(static_cast<int>(ReturnCode_Reset));
}

void UIBootFailureDialog::showEvent(QShowEvent *pEvent)
{
    if (m_pParent)
        gpDesktop->centerWidget(this, m_pParent, false);
    QIMainDialog::showEvent(pEvent);

}

void UIBootFailureDialog::setTitle()
{
}

void UIBootFailureDialog::sltFileSelectorPathChanged(const QString &strPath)
{
    Q_UNUSED(strPath);
    bool fISOValid = checkISOImage();
    if (m_pBootImageSelector)
        m_pBootImageSelector->mark(!fISOValid, tr("The path is invalid"), tr("The path is valid"));

    if (m_pResetButton)
        m_pResetButton->setEnabled(fISOValid);
}

QPixmap UIBootFailureDialog::iconPixmap()
{
    QIcon icon = UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_MessageBoxWarning);
    if (icon.isNull())
        return QPixmap();
    int iSize = QApplication::style()->pixelMetric(QStyle::PM_MessageBoxIconSize, 0, 0);
    return icon.pixmap(iSize, iSize);
}

bool UIBootFailureDialog::checkISOImage() const
{
    AssertReturn(m_pBootImageSelector, true);
    if (m_pBootImageSelector->path().isEmpty())
        return true;
    QFileInfo fileInfo(m_pBootImageSelector->path());
    if (!fileInfo.exists() || !fileInfo.isReadable())
        return false;
    return true;
}
