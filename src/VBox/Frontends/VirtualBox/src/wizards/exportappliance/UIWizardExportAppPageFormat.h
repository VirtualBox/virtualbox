/* $Id: UIWizardExportAppPageFormat.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardExportAppPageFormat class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportAppPageFormat_h
#define FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportAppPageFormat_h
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
class QLabel;
class QStackedWidget;
class QIRichTextLabel;
class QIToolButton;
class UIEmptyFilePathSelector;
class UINotificationCenter;

/** Format combo data fields. */
enum
{
    FormatData_Name            = Qt::UserRole + 1,
    FormatData_ShortName       = Qt::UserRole + 2,
    FormatData_IsItCloudFormat = Qt::UserRole + 3
};

/** Profile combo data fields. */
enum
{
    ProfileData_Name = Qt::UserRole + 1
};

/** Namespace for Format page of the Export Appliance wizard. */
namespace UIWizardExportAppFormat
{
    /** Populates formats. */
    void populateFormats(QComboBox *pCombo, UINotificationCenter *pCenter, bool fExportToOCIByDefault);
    /** Populates MAC address policies. */
    void populateMACAddressPolicies(QComboBox *pCombo);

    /** Returns current format of @a pCombo specified. */
    QString format(QComboBox *pCombo);
    /** Returns whether format under certain @a iIndex is cloud one. */
    bool isFormatCloudOne(QComboBox *pCombo, int iIndex = -1);

    /** Refresh stacked widget. */
    void refreshStackedWidget(QStackedWidget *pStackedWidget,
                              bool fIsFormatCloudOne);

    /** Refresh file selector name. */
    void refreshFileSelectorName(QString &strFileSelectorName,
                                 const QStringList &machineNames,
                                 const QString &strDefaultApplianceName,
                                 bool fIsFormatCloudOne);
    /** Refresh file selector extension. */
    void refreshFileSelectorExtension(QString &strFileSelectorExt,
                                      UIEmptyFilePathSelector *pFileSelector,
                                      bool fIsFormatCloudOne);
    /** Refresh file selector path. */
    void refreshFileSelectorPath(UIEmptyFilePathSelector *pFileSelector,
                                 const QString &strFileSelectorName,
                                 const QString &strFileSelectorExt,
                                 bool fIsFormatCloudOne);
    /** Refresh Manifest check-box access. */
    void refreshManifestCheckBoxAccess(QCheckBox *pCheckBox,
                                       bool fIsFormatCloudOne);
    /** Refresh Include ISOs check-box access. */
    void refreshIncludeISOsCheckBoxAccess(QCheckBox *pCheckBox,
                                          bool fIsFormatCloudOne);
    /** Refresh local stuff. */
    void refreshLocalStuff(CAppliance &comLocalAppliance,
                           UIWizardExportApp *pWizard,
                           const QList<QUuid> &machineIDs,
                           const QString &strUri);

    /** Refresh profile combo. */
    void refreshProfileCombo(QComboBox *pCombo,
                             UINotificationCenter *pCenter,
                             const QString &strFormat,
                             bool fIsFormatCloudOne);
    /** Refresh cloud profile. */
    void refreshCloudProfile(CCloudProfile &comCloudProfile,
                             UINotificationCenter *pCenter,
                             const QString &strShortProviderName,
                             const QString &strProfileName,
                             bool fIsFormatCloudOne);
    /** Refresh cloud export mode. */
    void refreshCloudExportMode(const QMap<CloudExportMode, QAbstractButton*> &radios,
                                bool fIsFormatCloudOne);
    /** Refresh cloud stuff. */
    void refreshCloudStuff(CAppliance &comCloudAppliance,
                           CCloudClient &comCloudClient,
                           CVirtualSystemDescription &comCloudVsd,
                           CVirtualSystemDescriptionForm &comCloudVsdExportForm,
                           UIWizardExportApp *pWizard,
                           const CCloudProfile &comCloudProfile,
                           const QList<QUuid> &machineIDs,
                           const QString &strUri,
                           const CloudExportMode enmCloudExportMode);

    /** Returns current profile name of @a pCombo specified. */
    QString profileName(QComboBox *pCombo);
    /** Returns current cloud export mode chosen in @a radioButtons specified. */
    CloudExportMode cloudExportMode(const QMap<CloudExportMode, QAbstractButton*> &radioButtons);

    /** Updates format combo tool-tips. */
    void updateFormatComboToolTip(QComboBox *pCombo);
    /** Updates MAC address export policy combo tool-tips. */
    void updateMACAddressExportPolicyComboToolTip(QComboBox *pCombo);
}

/** UINativeWizardPage extension for Format page of the Export Appliance wizard,
  * based on UIWizardExportAppFormat namespace functions. */
class UIWizardExportAppPageFormat : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs Format page. */
    UIWizardExportAppPageFormat(bool fExportToOCIByDefault);

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

    /** Handles change in format combo-box. */
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

    /** Holds whether default format should be Export to OCI. */
    bool  m_fExportToOCIByDefault;

    /** Holds the default appliance name. */
    QString  m_strDefaultApplianceName;
    /** Holds the file selector name. */
    QString  m_strFileSelectorName;
    /** Holds the file selector ext. */
    QString  m_strFileSelectorExt;

    /** Holds the Cloud Profile object instance. */
    CCloudProfile  m_comCloudProfile;

    /** Holds the format label instance. */
    QIRichTextLabel *m_pLabelFormat;
    /** Holds the settings label instance. */
    QIRichTextLabel *m_pLabelSettings;

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
};

#endif /* !FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportAppPageFormat_h */
