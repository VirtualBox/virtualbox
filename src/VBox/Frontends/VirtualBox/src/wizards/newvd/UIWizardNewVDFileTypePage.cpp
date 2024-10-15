/* $Id: UIWizardNewVDFileTypePage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVDFileTypePage class implementation.
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
#include <QVBoxLayout>

/* GUI includes: */
#include "UIWizardDiskEditors.h"
#include "UIWizardNewVDFileTypePage.h"
#include "UIWizardNewVD.h"
#include "QIRichTextLabel.h"

UIWizardNewVDFileTypePage::UIWizardNewVDFileTypePage(KDeviceType enmDeviceType)
    : m_pLabel(0)
    , m_pFormatButtonGroup(0)
{
    prepare(enmDeviceType);
}

void UIWizardNewVDFileTypePage::prepare(KDeviceType enmDeviceType)
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    m_pLabel = new QIRichTextLabel(this);
    pMainLayout->addWidget(m_pLabel);
    m_pFormatButtonGroup = new UIDiskFormatsGroupBox(false, enmDeviceType, 0);
    pMainLayout->addWidget(m_pFormatButtonGroup, false);

    pMainLayout->addStretch();
    connect(m_pFormatButtonGroup, &UIDiskFormatsGroupBox::sigMediumFormatChanged,
            this, &UIWizardNewVDFileTypePage::sltMediumFormatChanged);
    sltRetranslateUI();
}

void UIWizardNewVDFileTypePage::sltMediumFormatChanged()
{
    AssertReturnVoid(m_pFormatButtonGroup);
    wizardWindow<UIWizardNewVD>()->setMediumFormat(m_pFormatButtonGroup->mediumFormat());
    emit completeChanged();
}

void UIWizardNewVDFileTypePage::sltRetranslateUI()
{
    setTitle(UIWizardNewVD::tr("Virtual hard disk file type"));
    m_pLabel->setText(UIWizardNewVD::tr("Please choose the type of file that you would like to use "
                                        "for the new virtual hard disk. If you do not need to use it "
                                        "with other virtualization software you can leave this setting unchanged."));
}

void UIWizardNewVDFileTypePage::initializePage()
{
    AssertReturnVoid(wizardWindow<UIWizardNewVD>());
    sltRetranslateUI();
    if (m_pFormatButtonGroup)
        wizardWindow<UIWizardNewVD>()->setMediumFormat(m_pFormatButtonGroup->mediumFormat());
}

bool UIWizardNewVDFileTypePage::isComplete() const
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    if (pWizard && !pWizard->mediumFormat().isNull())
        return true;
    return false;
}
