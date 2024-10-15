/* $Id: UIWizardNewCloudVMPageProperties.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewCloudVMPageProperties class implementation.
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
#include <QHeaderView>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "UIFormEditorWidget.h"
#include "UINotificationCenter.h"
#include "UIWizardNewCloudVM.h"
#include "UIWizardNewCloudVMPageProperties.h"

/* Namespaces: */
using namespace UIWizardNewCloudVMProperties;


/*********************************************************************************************************************************
*   Namespace UIWizardNewCloudVMProperties implementation.                                                                       *
*********************************************************************************************************************************/

void UIWizardNewCloudVMProperties::refreshFormPropertiesTable(UIFormEditorWidget *pFormEditor,
                                                              const CVirtualSystemDescriptionForm &comForm)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pFormEditor);
    AssertReturnVoid(comForm.isNotNull());

    /* Make sure the properties table get the new description form: */
    pFormEditor->setVirtualSystemDescriptionForm(comForm);
}


/*********************************************************************************************************************************
*   Class UIWizardNewCloudVMPageProperties implementation.                                                                       *
*********************************************************************************************************************************/

UIWizardNewCloudVMPageProperties::UIWizardNewCloudVMPageProperties()
    : m_pLabel(0)
    , m_pFormEditor(0)
{
    /* Prepare main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        /* Prepare label: */
        m_pLabel = new QIRichTextLabel(this);
        if (m_pLabel)
            pLayoutMain->addWidget(m_pLabel);

        /* Prepare form editor widget: */
        m_pFormEditor = new UIFormEditorWidget(this);
        if (m_pFormEditor)
        {
            /* Make form-editor fit 8 sections in height by default: */
            const int iDefaultSectionHeight = m_pFormEditor->verticalHeader()
                                            ? m_pFormEditor->verticalHeader()->defaultSectionSize()
                                            : 0;
            if (iDefaultSectionHeight > 0)
                m_pFormEditor->setMinimumHeight(8 * iDefaultSectionHeight);

            /* Add into layout: */
            pLayoutMain->addWidget(m_pFormEditor);
        }
    }
}

UIWizardNewCloudVM *UIWizardNewCloudVMPageProperties::wizard() const
{
    return qobject_cast<UIWizardNewCloudVM*>(UINativeWizardPage::wizard());
}

void UIWizardNewCloudVMPageProperties::sltRetranslateUI()
{
    /* Translate page: */
    setTitle(UIWizardNewCloudVM::tr("Cloud Virtual Machine settings"));

    /* Translate description label: */
    if (m_pLabel)
        m_pLabel->setText(UIWizardNewCloudVM::tr("These are the the suggested settings of the cloud VM creation procedure, they "
                                                 "are influencing the resulting cloud VM instance.  You can change many of the "
                                                 "properties shown by double-clicking on the items and disable others using the "
                                                 "check boxes below."));

    /* Translate cloud VM properties table: */
    if (m_pFormEditor)
        m_pFormEditor->setWhatsThis(UIWizardNewCloudVM::tr("Lists all the cloud VM properties."));
}

void UIWizardNewCloudVMPageProperties::initializePage()
{
    /* Make sure form-editor knows notification-center: */
    m_pFormEditor->setNotificationCenter(wizard()->notificationCenter());
    /* Generate VSD form, asynchronously: */
    QMetaObject::invokeMethod(this, "sltInitShortWizardForm", Qt::QueuedConnection);
}

bool UIWizardNewCloudVMPageProperties::isComplete() const
{
    /* Initial result: */
    bool fResult = true;

    /* Check cloud settings: */
    fResult =    wizard()->client().isNotNull()
              && wizard()->vsd().isNotNull();

    /* Return result: */
    return fResult;
}

bool UIWizardNewCloudVMPageProperties::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Make sure table has own data committed: */
    m_pFormEditor->makeSureEditorDataCommitted();

    /* Check whether we have proper VSD form: */
    CVirtualSystemDescriptionForm comForm = wizard()->vsdForm();
    /* Give changed VSD back: */
    if (comForm.isNotNull())
    {
        comForm.GetVirtualSystemDescription();
        fResult = comForm.isOk();
        if (!fResult)
            UINotificationMessage::cannotAcquireVirtualSystemDescriptionFormParameter(comForm, wizard()->notificationCenter());
    }

    /* Try to create cloud VM: */
    if (fResult)
    {
        fResult = wizard()->createCloudVM();

        /* If the final step failed we could try
         * sugest user more valid form this time: */
        if (!fResult)
        {
            wizard()->setVSDForm(CVirtualSystemDescriptionForm());
            sltInitShortWizardForm();
        }
    }

    /* Return result: */
    return fResult;
}

void UIWizardNewCloudVMPageProperties::sltInitShortWizardForm()
{
    /* Create Virtual System Description Form: */
    if (wizard()->vsdForm().isNull())
        wizard()->createVSDForm();

    /* Refresh form properties table: */
    refreshFormPropertiesTable(m_pFormEditor, wizard()->vsdForm());
    emit completeChanged();
}
