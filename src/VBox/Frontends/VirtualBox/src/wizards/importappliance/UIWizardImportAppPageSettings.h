/* $Id: UIWizardImportAppPageSettings.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardImportAppPageSettings class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageSettings_h
#define FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageSettings_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"
#include "UIWizardImportApp.h"

/* Forward declarations: */
class QCheckBox;
class QComboBox;
class QLabel;
class QStackedWidget;
class QIRichTextLabel;
class UIApplianceImportEditorWidget;
class UIFilePathSelector;
class UIFormEditorWidget;

/** Certificate text template types. */
enum kCertText
{
    kCertText_Uninitialized = 0,
    kCertText_Unsigned,
    kCertText_IssuedTrusted,
    kCertText_IssuedExpired,
    kCertText_IssuedUnverified,
    kCertText_SelfSignedTrusted,
    kCertText_SelfSignedExpired,
    kCertText_SelfSignedUnverified
};

/** Namespace for Settings page of the Import Appliance wizard. */
namespace UIWizardImportAppSettings
{
    /** Refresh stacked widget. */
    void refreshStackedWidget(QStackedWidget *pStackedWidget,
                              bool fIsSourceCloudOne);

    /** Refreshes appliance widget. */
    void refreshApplianceWidget(UIApplianceImportEditorWidget *pApplianceWidget,
                                const CAppliance &comAppliance,
                                bool fIsSourceCloudOne);
    /** Refresh MAC address import policies. */
    void refreshMACAddressImportPolicies(QComboBox *pCombo,
                                         bool fIsSourceCloudOne);

    /** Refreshes form properties table. */
    void refreshFormPropertiesTable(UIFormEditorWidget *pFormEditor,
                                    const CVirtualSystemDescriptionForm &comForm,
                                    bool fIsSourceCloudOne);

    /** Returns MAC address import policy. */
    MACAddressImportPolicy macAddressImportPolicy(QComboBox *pCombo);
    /** Returns whether hard disks should be imported as VDIs. */
    bool isImportHDsAsVDI(QCheckBox *pCheckBox);

    /** Translates MAC import policy combo. */
    void retranslateMACImportPolicyCombo(QComboBox *pCombo);
    /** Translates certificate label. */
    void retranslateCertificateLabel(QLabel *pLabel, const kCertText &enmType, const QString &strSignedBy);

    /** Updates MAC import policy combo tool-tips. */
    void updateMACImportPolicyComboToolTip(QComboBox *pCombo);
}

/** UINativeWizardPage extension for Settings page of the Import Appliance wizard,
  * based on UIWizardImportAppSettings namespace functions. */
class UIWizardImportAppPageSettings : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs Settings page.
      * @param  strFileName  Brings appliance file name. */
    UIWizardImportAppPageSettings(const QString &strFileName);

protected:

    /** Returns wizard this page belongs to. */
    UIWizardImportApp *wizard() const;

    /** Performs page initialization. */
    virtual void initializePage() RT_OVERRIDE RT_FINAL;

    /** Performs page validation. */
    virtual bool validatePage() RT_OVERRIDE RT_FINAL;

private slots:

    /** Inits page async way. */
    void sltAsyncInit();

    /** Handles import path editor change. */
    void sltHandleImportPathEditorChange();
    /** Handles MAC address import policy combo change. */
    void sltHandleMACImportPolicyComboChange();
    /** Handles import HDs as VDI check-box change. */
    void sltHandleImportHDsAsVDICheckBoxChange();
    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Handles appliance certificate. */
    void handleApplianceCertificate();

    /** Handles the appliance file name. */
    QString  m_strFileName;

    /** Holds the description label instance. */
    QIRichTextLabel *m_pLabelDescription;

    /** Holds the settings widget 2 instance. */
    QStackedWidget *m_pSettingsWidget2;

    /** Holds the appliance widget instance. */
    UIApplianceImportEditorWidget *m_pApplianceWidget;
    /** Holds the import file-path label instance. */
    QLabel                        *m_pLabelImportFilePath;
    /** Holds the import file-path editor instance. */
    UIFilePathSelector            *m_pEditorImportFilePath;
    /** Holds the MAC address label instance. */
    QLabel                        *m_pLabelMACImportPolicy;
    /** Holds the MAC address combo instance. */
    QComboBox                     *m_pComboMACImportPolicy;
    /** Holds the additional options label instance. */
    QLabel                        *m_pLabelAdditionalOptions;
    /** Holds the 'import HDs as VDI' checkbox instance. */
    QCheckBox                     *m_pCheckboxImportHDsAsVDI;
    /** Holds the signature/certificate info label instance. */
    QLabel                        *m_pCertLabel;

    /** Holds the certificate text template type. */
    kCertText  m_enmCertText;

    /** Holds the "signed by" information. */
    QString  m_strSignedBy;

    /** Holds the Form Editor widget instance. */
    UIFormEditorWidget *m_pFormEditor;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageSettings_h */
