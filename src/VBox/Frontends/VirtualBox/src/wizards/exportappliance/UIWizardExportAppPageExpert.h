/* $Id: UIWizardExportAppPageExpert.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardExportAppPageExpert class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportAppPageExpert_h
#define FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportAppPageExpert_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"
#include "UIWizardExportApp.h"

/* COM includes: */
#include "CAppliance.h"
#include "CCloudClient.h"
#include "CCloudProfile.h"
#include "CVirtualSystemDescription.h"
#include "CVirtualSystemDescriptionForm.h"

/* Forward declarations: */
class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QIListWidget;
class QStackedWidget;
class QIToolButton;
class UIApplianceExportEditorWidget;
class UIEmptyFilePathSelector;
class UIFormEditorWidget;
class UIToolBox;
class UIWizardExportApp;

/** UINativeWizardPage extension for expert page of the Export Appliance wizard,
  * based on UIWizardExportAppVMs, UIWizardExportAppFormat & UIWizardExportAppSettings namespace functions. */
class UIWizardExportAppPageExpert : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs expert page.
      * @param  selectedVMNames  Brings the list of selected VM names. */
    UIWizardExportAppPageExpert(const QStringList &selectedVMNames, bool fExportToOCIByDefault);

protected:

    /** Returns wizard this page belongs to. */
    UIWizardExportApp *wizard() const;

    /** Performs page initialization. */
    virtual void initializePage() RT_OVERRIDE RT_FINAL;

    /** Returns whether page is complete. */
    virtual bool isComplete() const RT_OVERRIDE RT_FINAL;

    /** Performs page validation. */
    virtual bool validatePage() RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles VM selector index change. */
    void sltHandleVMItemSelectionChanged();

    /** Handles format combo change. */
    void sltHandleFormatComboChange();

    /** Handles change in file-name selector. */
    void sltHandleFileSelectorChange();

    /** Handles change in MAC address export policy combo-box. */
    void sltHandleMACAddressExportPolicyComboChange();

    /** Handles change in manifest check-box. */
    void sltHandleManifestCheckBoxChange();

    /** Handles change in include ISOs check-box. */
    void sltHandleIncludeISOsCheckBoxChange();

    /** Handles change in profile combo-box. */
    void sltHandleProfileComboChange();

    /** Handles cloud export radio-button clicked. */
    void sltHandleRadioButtonToggled(QAbstractButton *pButton, bool fToggled);

    /** Handles profile tool-button click. */
    void sltHandleProfileButtonClick();

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Update local stuff. */
    void updateLocalStuff();
    /** Updates cloud stuff. */
    void updateCloudStuff();

    /** Holds the list of selected VM names. */
    const QStringList  m_selectedVMNames;
    /** Holds whether default format should be Export to OCI. */
    bool               m_fExportToOCIByDefault;

    /** Holds the default appliance name. */
    QString  m_strDefaultApplianceName;
    /** Holds the file selector name. */
    QString  m_strFileSelectorName;
    /** Holds the file selector ext. */
    QString  m_strFileSelectorExt;

    /** Holds the Cloud Profile object reference. */
    CCloudProfile  m_comCloudProfile;


    /** Holds the tool-box instance. */
    UIToolBox *m_pToolBox;


    /** Holds the VM selector instance. */
    QIListWidget *m_pVMSelector;


    /** Holds the format layout. */
    QGridLayout *m_pFormatLayout;
    /** Holds the format combo-box label instance. */
    QLabel      *m_pFormatComboBoxLabel;
    /** Holds the format combo-box instance. */
    QComboBox   *m_pFormatComboBox;

    /** Holds the settings widget 1 instance. */
    QStackedWidget *m_pSettingsWidget1;

    /** Holds the settings layout 1. */
    QGridLayout             *m_pSettingsLayout1;
    /** Holds the file selector label instance. */
    QLabel                  *m_pFileSelectorLabel;
    /** Holds the file selector instance. */
    UIEmptyFilePathSelector *m_pFileSelector;
    /** Holds the MAC address policy combo-box label instance. */
    QLabel                  *m_pMACComboBoxLabel;
    /** Holds the MAC address policy check-box instance. */
    QComboBox               *m_pMACComboBox;
    /** Holds the additional label instance. */
    QLabel                  *m_pAdditionalLabel;
    /** Holds the manifest check-box instance. */
    QCheckBox               *m_pManifestCheckbox;
    /** Holds the include ISOs check-box instance. */
    QCheckBox               *m_pIncludeISOsCheckbox;

    /** Holds the settings layout 2. */
    QGridLayout   *m_pSettingsLayout2;
    /** Holds the profile label instance. */
    QLabel        *m_pProfileLabel;
    /** Holds the profile combo-box instance. */
    QComboBox     *m_pProfileComboBox;
    /** Holds the profile management tool-button instance. */
    QIToolButton  *m_pProfileToolButton;

    /** Holds the export mode label instance. */
    QLabel                                  *m_pExportModeLabel;
    /** Holds the export mode button group instance. */
    QButtonGroup                            *m_pExportModeButtonGroup;
    /** Holds the map of export mode button instances. */
    QMap<CloudExportMode, QAbstractButton*>  m_exportModeButtons;


    /** Holds the settings widget 2 instance. */
    QStackedWidget *m_pSettingsWidget2;

    /** Holds the appliance widget reference. */
    UIApplianceExportEditorWidget *m_pApplianceWidget;
    /** Holds the Form Editor widget instance. */
    UIFormEditorWidget            *m_pFormEditor;

    /** Holds whether cloud exporting is at launching stage. */
    bool  m_fLaunching;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportAppPageExpert_h */
