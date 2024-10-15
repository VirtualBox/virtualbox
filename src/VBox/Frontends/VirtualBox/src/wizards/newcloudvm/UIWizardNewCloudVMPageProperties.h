/* $Id: UIWizardNewCloudVMPageProperties.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewCloudVMPageProperties class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newcloudvm_UIWizardNewCloudVMPageProperties_h
#define FEQT_INCLUDED_SRC_wizards_newcloudvm_UIWizardNewCloudVMPageProperties_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QIRichTextLabel;
class UIFormEditorWidget;
class UIWizardNewCloudVM;
class CVirtualSystemDescriptionForm;

/** Namespace for properties page of the New Cloud VM wizard. */
namespace UIWizardNewCloudVMProperties
{
    /** Refreshes @a pFormEditor on the basis of comForm specified. */
    void refreshFormPropertiesTable(UIFormEditorWidget *pFormEditor, const CVirtualSystemDescriptionForm &comForm);
}

/** UINativeWizardPage extension for properties page of the New Cloud VM wizard,
  * based on UIWizardNewCloudVMProperties namespace functions. */
class UIWizardNewCloudVMPageProperties : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs properties basic page. */
    UIWizardNewCloudVMPageProperties();

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

    /** Initializes short wizard form. */
    void sltInitShortWizardForm();

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Holds the label instance. */
    QIRichTextLabel *m_pLabel;

    /** Holds the Form Editor widget instance. */
    UIFormEditorWidget *m_pFormEditor;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newcloudvm_UIWizardNewCloudVMPageProperties_h */
