/* $Id: UIWizardCloneVMTypePage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardCloneVMTypePage class implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include "UIWizardCloneVMTypePage.h"
#include "UIWizardCloneVM.h"
#include "QIRichTextLabel.h"
#include "UIWizardCloneVMEditors.h"

UIWizardCloneVMTypePage::UIWizardCloneVMTypePage(bool fAdditionalInfo)
    : m_pLabel(0)
    , m_fAdditionalInfo(fAdditionalInfo)
    , m_pCloneTypeGroupBox(0)
{
    prepare();
}

void UIWizardCloneVMTypePage::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    AssertReturnVoid(pMainLayout);
    m_pLabel = new QIRichTextLabel(this);
    if (m_pLabel)
        pMainLayout->addWidget(m_pLabel);

    m_pCloneTypeGroupBox = new UICloneVMCloneTypeGroupBox;
    if (m_pCloneTypeGroupBox)
    {
        m_pCloneTypeGroupBox->setFlat(true);
        pMainLayout->addWidget(m_pCloneTypeGroupBox);
        connect(m_pCloneTypeGroupBox, &UICloneVMCloneTypeGroupBox::sigFullCloneSelected,
                this, &UIWizardCloneVMTypePage::sltCloneTypeChanged);
    }

    pMainLayout->addStretch();
}

void UIWizardCloneVMTypePage::sltCloneTypeChanged(bool fIsFullClone)
{
    UIWizardCloneVM *pWizard = wizardWindow<UIWizardCloneVM>();
    AssertReturnVoid(pWizard);
    m_userModifiedParameters << "LinkedClone";
    pWizard->setLinkedClone(!fIsFullClone);
    /* Show/hide 3rd page according to linked clone toggle: */
    pWizard->setCloneModePageVisible(fIsFullClone);
}

void UIWizardCloneVMTypePage::sltRetranslateUI()
{
    /* Translate page: */
    setTitle(UIWizardCloneVM::tr("Clone type"));

    /* Translate widgets: */
    QString strLabel = UIWizardCloneVM::tr("<p>Please choose the type of clone you wish to create.</p>"
                                           "<p>If you choose <b>Full Clone</b>, "
                                           "an exact copy (including all virtual hard disk files) "
                                           "of the original virtual machine will be created.</p>"
                                           "<p>If you choose <b>Linked Clone</b>, "
                                           "a new machine will be created, but the virtual hard disk files "
                                           "will be tied to the virtual hard disk files of original machine "
                                           "and you will not be able to move the new virtual machine "
                                           "to a different computer without moving the original as well.</p>");
    if (m_fAdditionalInfo)
        strLabel += UIWizardCloneVM::tr("<p>If you create a <b>Linked Clone</b> then a new snapshot will be created "
                                        "in the original virtual machine as part of the cloning process.</p>");
    if (m_pLabel)
        m_pLabel->setText(strLabel);
}

void UIWizardCloneVMTypePage::initializePage()
{
    AssertReturnVoid(wizardWindow<UIWizardCloneVM>());
    sltRetranslateUI();
    if (m_pCloneTypeGroupBox && !m_userModifiedParameters.contains("LinkedClone"))
        wizardWindow<UIWizardCloneVM>()->setLinkedClone(!m_pCloneTypeGroupBox->isFullClone());
}

bool UIWizardCloneVMTypePage::validatePage()
{
    UIWizardCloneVM *pWizard = wizardWindow<UIWizardCloneVM>();
    AssertReturn(pWizard, false);

    /* This page could be final: */
    if (!pWizard->isCloneModePageVisible())
    {
        /* Initial result: */
        bool fResult = true;

        /* Trying to clone VM: */
        fResult = pWizard->cloneVM();

        /* Return result: */
        return fResult;
    }
    else
        return true;
}
