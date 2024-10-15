/* $Id: UICloudMachineSettingsDialogPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudMachineSettingsDialogPage class implementation.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include "UICloudMachineSettingsDialog.h"
#include "UICloudMachineSettingsDialogPage.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"

UICloudMachineSettingsDialogPage::UICloudMachineSettingsDialogPage(QWidget *pParent,
                                                                   bool fFullScale /* = true */)
    : QWidget(pParent)
    , m_pParent(qobject_cast<UICloudMachineSettingsDialog*>(pParent))
    , m_fFullScale(fFullScale)
{
    prepare();
}

void UICloudMachineSettingsDialogPage::setForm(const CForm &comForm)
{
    m_comForm = comForm;
    updateEditor();
}

void UICloudMachineSettingsDialogPage::setFilter(const QString &strFilter)
{
    m_strFilter = strFilter;
    updateEditor();
}

void UICloudMachineSettingsDialogPage::makeSureDataCommitted()
{
    AssertPtrReturnVoid(m_pFormEditor.data());
    m_pFormEditor->makeSureEditorDataCommitted();
}

void UICloudMachineSettingsDialogPage::sltRetranslateUI()
{
    AssertPtrReturnVoid(m_pFormEditor.data());
    m_pFormEditor->setWhatsThis(UICloudMachineSettingsDialog::tr("Contains a list of cloud machine settings."));
}

void UICloudMachineSettingsDialogPage::prepare()
{
    /* Prepare layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare form editor widget: */
        m_pFormEditor = new UIFormEditorWidget(this, m_pParent ? m_pParent->notificationCenter() : 0);
        if (m_pFormEditor)
        {
            /* Make form-editor fit 12 sections in height by default: */
            const int iDefaultSectionHeight = m_pFormEditor->verticalHeader()
                                            ? m_pFormEditor->verticalHeader()->defaultSectionSize()
                                            : 0;
            if (iDefaultSectionHeight > 0)
            {
                const int iProposedHeight = iDefaultSectionHeight * (m_fFullScale ? 12 : 6);
                const int iProposedWidth = iProposedHeight * 1.66;
                m_pFormEditor->setMinimumSize(iProposedWidth, iProposedHeight);
            }

            /* Add into layout: */
            pLayout->addWidget(m_pFormEditor);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloudMachineSettingsDialogPage::sltRetranslateUI);
}

void UICloudMachineSettingsDialogPage::updateEditor()
{
    /* Make sure editor present: */
    AssertPtrReturnVoid(m_pFormEditor.data());

    /* Make sure form isn't null: */
    if (m_comForm.isNotNull())
    {
        /* Acquire initial values: */
        const QVector<CFormValue> initialValues = m_comForm.GetValues();

        /* If filter null: */
        if (m_strFilter.isNull())
        {
            /* Push initial values to editor: */
            m_pFormEditor->setFormValues(initialValues);
        }
        /* If filter present: */
        else
        {
            /* Acquire group fields: */
            const QVector<QString> groupFields = m_comForm.GetFieldGroup(m_strFilter);
            /* Filter out unrelated values: */
            QVector<CFormValue> filteredValues;
            foreach (const CFormValue &comValue, initialValues)
                if (groupFields.contains(comValue.GetLabel()))
                    filteredValues << comValue;
            /* Push filtered values to editor: */
            m_pFormEditor->setFormValues(filteredValues);
        }
    }

    /* Revalidate: */
    emit sigValidChanged(m_comForm.isNotNull());
}
