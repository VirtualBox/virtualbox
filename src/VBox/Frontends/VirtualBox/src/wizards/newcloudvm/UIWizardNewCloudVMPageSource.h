/* $Id: UIWizardNewCloudVMPageSource.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewCloudVMPageSource class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newcloudvm_UIWizardNewCloudVMPageSource_h
#define FEQT_INCLUDED_SRC_wizards_newcloudvm_UIWizardNewCloudVMPageSource_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* COM includes: */
#include "CVirtualSystemDescription.h"

/* Forward declarations: */
class QComboBox;
class QGridLayout;
class QLabel;
class QIListWidget;
class QTabBar;
class QIRichTextLabel;
class QIToolButton;
class UINotificationCenter;
class UIWizardNewCloudVM;
class CCloudClient;
class CCloudProvider;

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

/** Namespace for source page of the New Cloud VM wizard. */
namespace UIWizardNewCloudVMSource
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
    /** Populates @a pList with source images.
      @param  pTabBar    Brings the tab-bar source images should be acquired for.
      @param  comClient  Brings the cloud client source images should be acquired from. */
    void populateSourceImages(QIListWidget *pList,
                              QTabBar *pTabBar,
                              UINotificationCenter *pCenter,
                              const CCloudClient &comClient);
    /** Populates @a comVSD with form property.
      * @param  pWizard     Brings the wizard used as parent for warnings inside.
      * @param  pTabBar     Brings the tab-bar property should gather according to.
      * @param  strImageId  Brings the image id which should be added as property. */
    void populateFormProperties(CVirtualSystemDescription comVSD,
                                UIWizardNewCloudVM *pWizard,
                                QTabBar *pTabBar,
                                const QString &strImageId);

    /** Returns current user data for @a pList specified. */
    QString currentListWidgetData(QIListWidget *pList);
}

/** UINativeWizardPage extension for source page of the New Cloud VM wizard,
  * based on UIWizardNewCloudVMSource namespace functions. */
class UIWizardNewCloudVMPageSource : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs source basic page. */
    UIWizardNewCloudVMPageSource();

protected:

    /** Returns wizard this page belongs to. */
    UIWizardNewCloudVM *wizard() const;

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

    /** Handles change in source tab-bar. */
    void sltHandleSourceTabBarChange();

    /** Handles change in image list. */
    void sltHandleSourceImageChange();

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Holds the image ID. */
    QString  m_strSourceImageId;

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

    /** Holds the source image label instance. */
    QLabel       *m_pSourceImageLabel;
    /** Holds the source tab-bar instance. */
    QTabBar      *m_pSourceTabBar;
    /** Holds the source image list instance. */
    QIListWidget *m_pSourceImageList;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newcloudvm_UIWizardNewCloudVMPageSource_h */
