/* $Id: UIWizardExportAppPageSettings.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardExportAppPageSettings class implementation.
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
#include <QStackedWidget>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "UIApplianceExportEditorWidget.h"
#include "UIFormEditorWidget.h"
#include "UINotificationCenter.h"
#include "UIWizardExportApp.h"
#include "UIWizardExportAppPageSettings.h"

/* COM includes: */
#include "CMachine.h"
#include "CVirtualSystemDescriptionForm.h"

/* Namespaces: */
using namespace UIWizardExportAppSettings;


/*********************************************************************************************************************************
*   Class UIWizardExportAppSettings implementation.                                                                              *
*********************************************************************************************************************************/

void UIWizardExportAppSettings::refreshStackedWidget(QStackedWidget *pStackedWidget,
                                                     bool fIsFormatCloudOne)
{
    /* Update stack appearance according to chosen format: */
    pStackedWidget->setCurrentIndex((int)fIsFormatCloudOne);
}

void UIWizardExportAppSettings::refreshApplianceSettingsWidget(UIApplianceExportEditorWidget *pApplianceWidget,
                                                               const CAppliance &comAppliance,
                                                               bool fIsFormatCloudOne)
{
    /* Nothing for cloud case? */
    if (fIsFormatCloudOne)
        return;

    /* Sanity check: */
    AssertReturnVoid(comAppliance.isNotNull());

    /* Make sure the settings widget get the new appliance: */
    pApplianceWidget->setAppliance(comAppliance);
}

void UIWizardExportAppSettings::refreshFormPropertiesTable(UIFormEditorWidget *pFormEditor,
                                                           const CVirtualSystemDescriptionForm &comVsdForm,
                                                           bool fIsFormatCloudOne)
{
    /* Nothing for local case? */
    if (!fIsFormatCloudOne)
        return;

    /* Sanity check: */
    AssertReturnVoid(comVsdForm.isNotNull());

    /* Make sure the properties table get the new description form: */
    pFormEditor->setVirtualSystemDescriptionForm(comVsdForm);
}


/*********************************************************************************************************************************
*   Class UIWizardExportAppPageSettings implementation.                                                                          *
*********************************************************************************************************************************/

UIWizardExportAppPageSettings::UIWizardExportAppPageSettings()
    : m_pLabel(0)
    , m_pSettingsWidget2(0)
    , m_pApplianceWidget(0)
    , m_pFormEditor(0)
    , m_fLaunching(false)
{
    /* Create main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Create label: */
        m_pLabel = new QIRichTextLabel(this);
        if (m_pLabel)
            pMainLayout->addWidget(m_pLabel);

        /* Create settings widget 2: */
        m_pSettingsWidget2 = new QStackedWidget(this);
        if (m_pSettingsWidget2)
        {
            /* Create appliance widget container: */
            QWidget *pApplianceWidgetCnt = new QWidget(this);
            if (pApplianceWidgetCnt)
            {
                /* Create appliance widget layout: */
                QVBoxLayout *pApplianceWidgetLayout = new QVBoxLayout(pApplianceWidgetCnt);
                if (pApplianceWidgetLayout)
                {
                    pApplianceWidgetLayout->setContentsMargins(0, 0, 0, 0);

                    /* Create appliance widget: */
                    m_pApplianceWidget = new UIApplianceExportEditorWidget(pApplianceWidgetCnt);
                    if (m_pApplianceWidget)
                    {
                        m_pApplianceWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
                        pApplianceWidgetLayout->addWidget(m_pApplianceWidget);
                    }
                }

                /* Add into layout: */
                m_pSettingsWidget2->addWidget(pApplianceWidgetCnt);
            }

            /* Create form editor container: */
            QWidget *pFormEditorCnt = new QWidget(this);
            if (pFormEditorCnt)
            {
                /* Create form editor layout: */
                QVBoxLayout *pFormEditorLayout = new QVBoxLayout(pFormEditorCnt);
                if (pFormEditorLayout)
                {
                    pFormEditorLayout->setContentsMargins(0, 0, 0, 0);

                    /* Create form editor widget: */
                    m_pFormEditor = new UIFormEditorWidget(pFormEditorCnt);
                    if (m_pFormEditor)
                        pFormEditorLayout->addWidget(m_pFormEditor);
                }

                /* Add into layout: */
                m_pSettingsWidget2->addWidget(pFormEditorCnt);
            }

            /* Add into layout: */
            pMainLayout->addWidget(m_pSettingsWidget2);
        }
    }
}

UIWizardExportApp *UIWizardExportAppPageSettings::wizard() const
{
    return qobject_cast<UIWizardExportApp*>(UINativeWizardPage::wizard());
}

void UIWizardExportAppPageSettings::sltRetranslateUI()
{
    /* Translate page: */
    setTitle(UIWizardExportApp::tr("Appliance settings"));

    /* Translate label: */
    if (wizard()->isFormatCloudOne())
        m_pLabel->setText(UIWizardExportApp::tr("This is the descriptive information which will be used to determine settings "
                                                "for a cloud storage your VM being exported to.  You can change it by double "
                                                "clicking on individual lines."));
    else
        m_pLabel->setText(UIWizardExportApp::tr("This is the descriptive information which will be added to the virtual "
                                                "appliance.  You can change it by double clicking on individual lines."));
}

void UIWizardExportAppPageSettings::initializePage()
{
    /* Make sure form-editor knows notification-center: */
    m_pFormEditor->setNotificationCenter(wizard()->notificationCenter());
    /* Translate page: */
    sltRetranslateUI();

    /* Refresh settings widget state: */
    refreshStackedWidget(m_pSettingsWidget2, wizard()->isFormatCloudOne());
    /* Refresh corresponding widgets: */
    refreshApplianceSettingsWidget(m_pApplianceWidget, wizard()->localAppliance(), wizard()->isFormatCloudOne());
    refreshFormPropertiesTable(m_pFormEditor, wizard()->vsdExportForm(), wizard()->isFormatCloudOne());

    /* Choose initially focused widget: */
    if (wizard()->isFormatCloudOne())
        m_pFormEditor->setFocus();
    else
        m_pApplianceWidget->setFocus();
}

bool UIWizardExportAppPageSettings::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Check whether there was cloud target selected: */
    if (wizard()->isFormatCloudOne())
    {
        /* Make sure table has own data committed: */
        m_pFormEditor->makeSureEditorDataCommitted();

        /* Init VSD form: */
        CVirtualSystemDescriptionForm comForm;
        /* Check whether we have proper VSD form: */
        if (!m_fLaunching)
        {
            /* We are going to upload image: */
            comForm = wizard()->vsdExportForm();
            fResult = comForm.isNotNull();
        }
        else
        {
            /* We are going to launch VM: */
            comForm = wizard()->vsdLaunchForm();
            fResult = comForm.isNotNull();
        }
        /* Give changed VSD back: */
        if (fResult)
        {
            comForm.GetVirtualSystemDescription();
            fResult = comForm.isOk();
            if (!fResult)
                UINotificationMessage::cannotAcquireVirtualSystemDescriptionFormParameter(comForm, wizard()->notificationCenter());
        }

        /* Final stage? */
        if (fResult)
        {
            if (!m_fLaunching)
            {
                /* For modes other than AskThenExport, try to export appliance first: */
                if (wizard()->cloudExportMode() != CloudExportMode_AskThenExport)
                    fResult = wizard()->exportAppliance();

                /* For modes other than DoNotAsk, switch from uploading image to launching VM: */
                if (   fResult
                    && wizard()->cloudExportMode() != CloudExportMode_DoNotAsk)
                {
                    /* Invert flags: */
                    fResult = false;
                    m_fLaunching = true;

                    /* Disable wizard buttons: */
                    wizard()->disableButtons();

                    /* Refresh corresponding widgets: */
                    wizard()->createVsdLaunchForm();
                    refreshFormPropertiesTable(m_pFormEditor, wizard()->vsdLaunchForm(), wizard()->isFormatCloudOne());
                }
            }
            else
            {
                /* For AskThenExport mode, try to export appliance in the end: */
                if (wizard()->cloudExportMode() == CloudExportMode_AskThenExport)
                    fResult = wizard()->exportAppliance();

                /* Try to create cloud VM: */
                if (fResult)
                    fResult = wizard()->createCloudVM();
            }
        }
    }
    /* Otherwise if there was local target selected: */
    else
    {
        /* Prepare export: */
        m_pApplianceWidget->prepareExport();

        /* Try to export appliance: */
        if (fResult)
            fResult = wizard()->exportAppliance();
    }

    /* Return result: */
    return fResult;
}
