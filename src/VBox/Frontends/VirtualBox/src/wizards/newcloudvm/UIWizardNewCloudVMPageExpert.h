/* $Id: UIWizardNewCloudVMPageExpert.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewCloudVMPageExpert class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newcloudvm_UIWizardNewCloudVMPageExpert_h
#define FEQT_INCLUDED_SRC_wizards_newcloudvm_UIWizardNewCloudVMPageExpert_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIWizardNewCloudVMPageSource.h"
#include "UIWizardNewCloudVMPageProperties.h"

/* Forward declarations: */
class QLabel;
class QIListWidget;
class UIWizardNewCloudVM;

/** UINativeWizardPage extension for Expert page of the New Cloud VM wizard,
  * based on UIWizardNewCloudVMPage1 & UIWizardNewCloudVMPage2 namespace functions. */
class UIWizardNewCloudVMPageExpert : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs expert page. */
    UIWizardNewCloudVMPageExpert();

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

    /** Handles change in instance list. */
    void sltHandleSourceImageChange();

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Updates properties table. */
    void updatePropertiesTable();

    /** Holds whether we want full wizard form or short one. */
    bool     m_fFullWizard;
    /** Holds the image ID. */
    QString  m_strSourceImageId;

    /** Holds the provider type label instance. */
    QLabel       *m_pProviderLabel;
    /** Holds the provider type combo-box instance. */
    QComboBox    *m_pProviderComboBox;
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

    /** Holds the option label instance. */
    QLabel             *m_pLabelOptions;
    /** Holds the Form Editor widget instance. */
    UIFormEditorWidget *m_pFormEditor;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newcloudvm_UIWizardNewCloudVMPageExpert_h */
