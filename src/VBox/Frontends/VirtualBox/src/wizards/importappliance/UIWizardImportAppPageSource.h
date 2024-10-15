/* $Id: UIWizardImportAppPageSource.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardImportAppPageSource class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageSource_h
#define FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageSource_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QComboBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QStackedWidget;
class QIRichTextLabel;
class QIToolButton;
class UIEmptyFilePathSelector;
class UINotificationCenter;
class UIWizardImportApp;
class CAppliance;
class CVirtualSystemDescriptionForm;

/** Source combo data fields. */
enum
{
    SourceData_Name            = Qt::UserRole + 1,
    SourceData_ShortName       = Qt::UserRole + 2,
    SourceData_IsItCloudFormat = Qt::UserRole + 3
};

/** Profile combo data fields. */
enum
{
    ProfileData_Name = Qt::UserRole + 1
};

/** Namespace for Source page of the Import Appliance wizard. */
namespace UIWizardImportAppSource
{
    /** Populates sources. */
    void populateSources(QComboBox *pCombo,
                         UINotificationCenter *pCenter,
                         bool fImportFromOCIByDefault,
                         const QString &strSource);

    /** Returns current source of @a pCombo specified. */
    QString source(QComboBox *pCombo);
    /** Returns whether source under certain @a iIndex is cloud one. */
    bool isSourceCloudOne(QComboBox *pCombo, int iIndex = -1);

    /** Refresh stacked widget. */
    void refreshStackedWidget(QStackedWidget *pStackedWidget,
                              bool fIsFormatCloudOne);

    /** Refresh profile combo. */
    void refreshProfileCombo(QComboBox *pCombo,
                             UINotificationCenter *pCenter,
                             const QString &strSource,
                             const QString &strProfileName,
                             bool fIsSourceCloudOne);
    /** Refresh profile instances. */
    void refreshCloudProfileInstances(QListWidget *pListWidget,
                                      UINotificationCenter *pCenter,
                                      const QString &strSource,
                                      const QString &strProfileName,
                                      bool fIsSourceCloudOne);
    /** Refresh cloud stuff. */
    void refreshCloudStuff(CAppliance &comCloudAppliance,
                           CVirtualSystemDescriptionForm &comCloudVsdImportForm,
                           UIWizardImportApp *pWizard,
                           const QString &strMachineId,
                           const QString &strSource,
                           const QString &strProfileName,
                           bool fIsSourceCloudOne);

    /** Returns imported file path. */
    QString path(UIEmptyFilePathSelector *pFileSelector);

    /** Returns profile name. */
    QString profileName(QComboBox *pCombo);
    /** Returns machine ID. */
    QString machineId(QListWidget *pListWidget);

    /** Updates source combo tool-tips. */
    void updateSourceComboToolTip(QComboBox *pCombo);
}

/** UINativeWizardPage extension for Source page of the Import Appliance wizard,
  * based on UIWizardImportAppSource namespace functions. */
class UIWizardImportAppPageSource : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs Source page.
      * @param  fImportFromOCIByDefault  Brings whether we should propose import from OCI by default.
      * @param  strFileName              Brings appliance file name. */
    UIWizardImportAppPageSource(bool fImportFromOCIByDefault, const QString &strFileName);

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

    /** Handles source combo change. */
    void sltHandleSourceComboChange();

    /** Handles profile combo change. */
    void sltHandleProfileComboChange();
    /** Handles profile tool-button click. */
    void sltHandleProfileButtonClick();

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Update local stuff. */
    void updateLocalStuff();
    /** Updates cloud stuff. */
    void updateCloudStuff();

    /** Holds whether default source should be Import from OCI. */
    bool     m_fImportFromOCIByDefault;
    /** Handles the appliance file name. */
    QString  m_strFileName;

    /** Holds the cached source. */
    QString  m_strSource;
    /** Holds the cached profile name. */
    QString  m_strProfileName;

    /** Holds the main label instance. */
    QIRichTextLabel *m_pLabelMain;
    /** Holds the description label instance. */
    QIRichTextLabel *m_pLabelDescription;

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
};

#endif /* !FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageSource_h */
