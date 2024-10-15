/* $Id: UIWizardImportAppPageExpert.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardImportAppPageExpert class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageExpert_h
#define FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageExpert_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QStackedWidget;
class QIToolButton;
class UIApplianceImportEditorWidget;
class UIEmptyFilePathSelector;
class UIFilePathSelector;
class UIFormEditorWidget;
class UIToolBox;
class UIWizardImportApp;

/** UINativeWizardPage extension for expert page of the Import Appliance wizard,
  * based on UIWizardImportAppSource & UIWizardImportAppSettings namespace functions. */
class UIWizardImportAppPageExpert : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs expert page.
      * @param  fImportFromOCIByDefault  Brings whether we should propose import from OCI by default.
      * @param  strFileName              Brings appliance file name. */
    UIWizardImportAppPageExpert(bool fImportFromOCIByDefault, const QString &strFileName);

protected:

    /** Returns wizard this page belongs to. */
    UIWizardImportApp *wizard() const;

    /** Performs page initialization. */
    virtual void initializePage() RT_OVERRIDE RT_FINAL;

    /** Returns whether page is complete. */
    virtual bool isComplete() const RT_OVERRIDE RT_FINAL;

    /** Performs page validation. */
    virtual bool validatePage() RT_OVERRIDE RT_FINAL;

private slots:

    /** Inits page async way. */
    void sltAsyncInit();

    /** Handles source combo change. */
    void sltHandleSourceComboChange();

    /** Handles imported file selector change. */
    void sltHandleImportedFileSelectorChange();
    /** Handles profile combo change. */
    void sltHandleProfileComboChange();
    /** Handles profile tool-button click. */
    void sltHandleProfileButtonClick();
    /** Handles instance list change. */
    void sltHandleInstanceListChange();

    /** Handles import path editor change. */
    void sltHandleImportPathEditorChange();
    /** Handles MAC address import policy combo change. */
    void sltHandleMACImportPolicyComboChange();
    /** Handles import HDs as VDI check-box change. */
    void sltHandleImportHDsAsVDICheckBoxChange();
    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Holds whether default source should be Import from OCI. */
    bool     m_fImportFromOCIByDefault;
    /** Handles the appliance file name. */
    QString  m_strFileName;

    /** Holds the cached source. */
    QString  m_strSource;
    /** Holds the cached profile name. */
    QString  m_strProfileName;

    /** Holds the tool-box instance. */
    UIToolBox *m_pToolBox;

    /** Holds the source layout instance. */
    QGridLayout *m_pSourceLayout;
    /** Holds the source type label instance. */
    QLabel      *m_pSourceLabel;
    /** Holds the source type combo-box instance. */
    QComboBox   *m_pSourceComboBox;

    /** Holds the settings widget 1 instance. */
    QStackedWidget *m_pSettingsWidget1;

    /** Holds the local container layout instance. */
    QGridLayout             *m_pLocalContainerLayout;
    /** Holds the file label instance. */
    QLabel                  *m_pFileLabel;
    /** Holds the file selector instance. */
    UIEmptyFilePathSelector *m_pFileSelector;

    /** Holds the cloud container layout instance. */
    QGridLayout  *m_pCloudContainerLayout;
    /** Holds the profile label instance. */
    QLabel       *m_pProfileLabel;
    /** Holds the profile combo-box instance. */
    QComboBox    *m_pProfileComboBox;
    /** Holds the profile management tool-button instance. */
    QIToolButton *m_pProfileToolButton;
    /** Holds the profile instance label instance. */
    QLabel       *m_pProfileInstanceLabel;
    /** Holds the profile instance list instance. */
    QListWidget  *m_pProfileInstanceList;

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

    /** Holds the Form Editor widget instance. */
    UIFormEditorWidget *m_pFormEditor;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageExpert_h */
