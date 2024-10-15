/* $Id: UIWizardNewVDVariantPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVDVariantPage class implementation.
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
#include "UIWizardNewVDVariantPage.h"
#include "UIWizardNewVD.h"
#include "QIRichTextLabel.h"

/* COM includes: */
#include "CMediumFormat.h"

UIWizardNewVDVariantPage::UIWizardNewVDVariantPage()
    : m_pDescriptionLabel(0)
    , m_pDynamicLabel(0)
    , m_pFixedLabel(0)
    , m_pSplitLabel(0)
    , m_pVariantWidget(0)
{
    prepare();
}

void UIWizardNewVDVariantPage::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    AssertReturnVoid(pMainLayout);

    m_pDescriptionLabel = new QIRichTextLabel(this);
    m_pDynamicLabel = new QIRichTextLabel(this);
    m_pFixedLabel = new QIRichTextLabel(this);
    m_pSplitLabel = new QIRichTextLabel(this);

    pMainLayout->addWidget(m_pDescriptionLabel);
    pMainLayout->addWidget(m_pDynamicLabel);
    pMainLayout->addWidget(m_pFixedLabel);
    pMainLayout->addWidget(m_pSplitLabel);

    m_pVariantWidget = new UIDiskVariantWidget(0);
    pMainLayout->addWidget(m_pVariantWidget);
    pMainLayout->addStretch();

    connect(m_pVariantWidget, &UIDiskVariantWidget::sigMediumVariantChanged,
            this, &UIWizardNewVDVariantPage::sltMediumVariantChanged);
    sltRetranslateUI();
}

void UIWizardNewVDVariantPage::sltRetranslateUI()
{
    setTitle(UIWizardNewVD::tr("Storage on physical hard disk"));

    if (m_pDescriptionLabel)
        m_pDescriptionLabel->setText(UIWizardNewVD::tr("Please choose whether the new virtual hard disk file should grow as it is used "
                                                       "(dynamically allocated) or if it should be created at its maximum size (fixed size)."));
    if (m_pDynamicLabel)
        m_pDynamicLabel->setText(UIWizardNewVD::tr("<p>A <b>dynamically allocated</b> hard disk file will only use space "
                                                   "on your physical hard disk as it fills up (up to a maximum <b>fixed size</b>), "
                                                   "although it will not shrink again automatically when space on it is freed.</p>"));
    if (m_pFixedLabel)
        m_pFixedLabel->setText(UIWizardNewVD::tr("<p>A <b>fixed size</b> hard disk file may take longer to create on some "
                                                 "systems but is often faster to use.</p>"));
    if (m_pSplitLabel)
        m_pSplitLabel->setText(UIWizardNewVD::tr("<p>You can also choose to <b>split</b> the hard disk file into several files "
                                                 "of up to two gigabytes each. This is mainly useful if you wish to store the "
                                                 "virtual machine on removable USB devices or old systems, some of which cannot "
                                                 "handle very large files."));
}

void UIWizardNewVDVariantPage::initializePage()
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturnVoid(pWizard && m_pVariantWidget);
    setWidgetVisibility(pWizard->mediumFormat());
    pWizard->setMediumVariant(m_pVariantWidget->mediumVariant());
    sltRetranslateUI();
}

bool UIWizardNewVDVariantPage::isComplete() const
{
    if (m_pVariantWidget && m_pVariantWidget->mediumVariant() != (qulonglong)KMediumVariant_Max)
        return true;
    return false;
}

void UIWizardNewVDVariantPage::setWidgetVisibility(const CMediumFormat &mediumFormat)
{
    AssertReturnVoid(m_pVariantWidget);
    m_pVariantWidget->updateMediumVariantWidgetsAfterFormatChange(mediumFormat);
    if (m_pDynamicLabel)
        m_pDynamicLabel->setHidden(!m_pVariantWidget->isCreateDynamicPossible());
    if (m_pFixedLabel)
        m_pFixedLabel->setHidden(!m_pVariantWidget->isCreateFixedPossible());
    if (m_pSplitLabel)
        m_pSplitLabel->setHidden(!m_pVariantWidget->isCreateSplitPossible());
}

void UIWizardNewVDVariantPage::sltMediumVariantChanged(qulonglong uVariant)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVD>());
    wizardWindow<UIWizardNewVD>()->setMediumVariant(uVariant);
}
