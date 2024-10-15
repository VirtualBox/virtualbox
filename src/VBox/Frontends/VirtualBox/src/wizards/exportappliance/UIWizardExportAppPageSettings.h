/* $Id: UIWizardExportAppPageSettings.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardExportAppPageSettings class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportAppPageSettings_h
#define FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportAppPageSettings_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QList>

/* GUI includes: */
#include "UINativeWizardPage.h"

/* COM includes: */
#include "CAppliance.h"

/* Forward declarations: */
class QStackedWidget;
class QIRichTextLabel;
class UIApplianceExportEditorWidget;
class UIFormEditorWidget;
class UIWizardExportApp;

/** Namespace for Settings page of the Export Appliance wizard. */
namespace UIWizardExportAppSettings
{
    /** Refresh stacked widget. */
    void refreshStackedWidget(QStackedWidget *pStackedWidget,
                              bool fIsFormatCloudOne);

    /** Refreshes appliance settings widget. */
    void refreshApplianceSettingsWidget(UIApplianceExportEditorWidget *pApplianceWidget,
                                        const CAppliance &comAppliance,
                                        bool fIsFormatCloudOne);
    /** Refreshes form properties table. */
    void refreshFormPropertiesTable(UIFormEditorWidget *pFormEditor,
                                    const CVirtualSystemDescriptionForm &comVsdForm,
                                    bool fIsFormatCloudOne);
}

/** UINativeWizardPage extension for Settings page of the Export Appliance wizard,
  * based on UIWizardExportAppSettings namespace functions. */
class UIWizardExportAppPageSettings : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs Settings page. */
    UIWizardExportAppPageSettings();

protected:

    /** Returns wizard this page belongs to. */
    UIWizardExportApp *wizard() const;

    /** Performs page initialization. */
    virtual void initializePage() RT_OVERRIDE RT_FINAL;

    /** Performs page validation. */
    virtual bool validatePage() RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Holds the label instance. */
    QIRichTextLabel *m_pLabel;

    /** Holds the settings widget 2 instance. */
    QStackedWidget *m_pSettingsWidget2;

    /** Holds the appliance widget reference. */
    UIApplianceExportEditorWidget *m_pApplianceWidget;
    /** Holds the Form Editor widget instance. */
    UIFormEditorWidget            *m_pFormEditor;

    /** Holds whether cloud exporting is at launching stage. */
    bool  m_fLaunching;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportAppPageSettings_h */
