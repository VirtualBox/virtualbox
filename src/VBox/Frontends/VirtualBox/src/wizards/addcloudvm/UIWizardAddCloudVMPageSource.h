/* $Id: UIWizardAddCloudVMPageSource.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardAddCloudVMPageSource class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_addcloudvm_UIWizardAddCloudVMPageSource_h
#define FEQT_INCLUDED_SRC_wizards_addcloudvm_UIWizardAddCloudVMPageSource_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* COM includes: */
#include "CCloudClient.h"

/* Forward declarations: */
class QComboBox;
class QGridLayout;
class QLabel;
class QIListWidget;
class QIRichTextLabel;
class QIToolButton;
class UINotificationCenter;
class UIWizardAddCloudVM;

/** Provider combo data fields. */
enum
{
    ProviderData_Name      = Qt::UserRole + 1,
    ProviderData_ShortName = Qt::UserRole + 2
};

/** Profile combo data fields. */
enum
{
    ProfileData_Name = Qt::UserRole + 1
};

/** Namespace for source page of the Add Cloud VM wizard. */
namespace UIWizardAddCloudVMSource
{
    /** Populates @a pCombo with known providers. */
    void populateProviders(QComboBox *pCombo, UINotificationCenter *pCenter);
    /** Populates @a pCombo with known profiles.
      * @param  strProviderShortName  Brings the short name of provider profiles related to.
      * @param  strProfileName        Brings the name of profile to be chosen by default. */
    void populateProfiles(QComboBox *pCombo,
                          UINotificationCenter *pCenter,
                          const QString &strProviderShortName,
                          const QString &strProfileName);
    /** Populates @a pList with profile instances available in @a comClient. */
    void populateProfileInstances(QIListWidget *pList, UINotificationCenter *pCenter, const CCloudClient &comClient);

    /** Returns current user data for @a pList specified. */
    QStringList currentListWidgetData(QIListWidget *pList);
}

/** UINativeWizardPage extension for source page of the Add Cloud VM wizard,
  * based on UIWizardAddCloudVMSource namespace functions. */
class UIWizardAddCloudVMPageSource : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs source page. */
    UIWizardAddCloudVMPageSource();

protected:

    /** Returns wizard this page belongs to. */
    UIWizardAddCloudVM *wizard() const;

    /** Performs page initialization. */
    virtual void initializePage() RT_OVERRIDE RT_FINAL;

    /** Returns whether page is complete. */
    virtual bool isComplete() const RT_OVERRIDE RT_FINAL;

    /** Performs page validation. */
    virtual bool validatePage() RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles change in provider combo-box. */
    void sltHandleProviderComboChange();

    /** Handles change in profile combo-box. */
    void sltHandleProfileComboChange();
    /** Handles profile tool-button click. */
    void sltHandleProfileButtonClick();

    /** Handles change in instance list. */
    void sltHandleSourceInstanceChange();

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Holds the main label instance. */
    QIRichTextLabel *m_pLabelMain;

    /** Holds the provider layout instance. */
    QGridLayout *m_pProviderLayout;
    /** Holds the provider type label instance. */
    QLabel      *m_pProviderLabel;
    /** Holds the provider type combo-box instance. */
    QComboBox   *m_pProviderComboBox;

    /** Holds the description label instance. */
    QIRichTextLabel *m_pLabelDescription;

    /** Holds the options layout instance. */
    QGridLayout  *m_pOptionsLayout;
    /** Holds the profile label instance. */
    QLabel       *m_pProfileLabel;
    /** Holds the profile combo-box instance. */
    QComboBox    *m_pProfileComboBox;
    /** Holds the profile management tool-button instance. */
    QIToolButton *m_pProfileToolButton;
    /** Holds the source instance label instance. */
    QLabel       *m_pSourceInstanceLabel;
    /** Holds the source instance list instance. */
    QIListWidget *m_pSourceInstanceList;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_addcloudvm_UIWizardAddCloudVMPageSource_h */
