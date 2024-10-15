/* $Id: UIWizardNewVMNameOSTypePage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMNameOSTypePage class declaration.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMNameOSTypePage_h
#define FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMNameOSTypePage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSet>

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QCheckBox;
class QGridLayout;
class QIRichTextLabel;
class UINameAndSystemEditor;
class UIWizardNewVM;

namespace UIWizardNewVMNameOSTypeCommon
{
    bool guessOSTypeFromName(UINameAndSystemEditor *pNameAndSystemEditor, QString strNewName);
    bool guessOSTypeDetectedOSTypeString(UINameAndSystemEditor *pNameAndSystemEditor, QString strDetectedOSType);
    bool createMachineFolder(UINameAndSystemEditor *pNameAndSystemEditor, UIWizardNewVM *pWizard);

    /** Removes a previously created folder (if exists) before creating a new one.
     *  used during page cleanup and new folder creation. Called upon page Next/Back and
     *  wizard cancel */
    bool cleanupMachineFolder(UIWizardNewVM *pWizard, bool fWizardCancel = false);
    void composeMachineFilePath(UINameAndSystemEditor *pNameAndSystemEditor, UIWizardNewVM *pWizard);
    /** Return false if ISO path is not empty but points to an missing or unreadable file. */
    bool checkISOFile(UINameAndSystemEditor *pNameAndSystemEditor);
}

/** 1st page of the New Virtual Machine wizard (basic extension). */
class UIWizardNewVMNameOSTypePage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructor. */
    UIWizardNewVMNameOSTypePage();
    void setISOFilePath(const QString &strISOFilePath);

protected:

    virtual bool isComplete() const RT_OVERRIDE RT_FINAL;
    /** Validation stuff. */
    virtual bool validatePage() RT_OVERRIDE;

private slots:

    void sltNameChanged(const QString &strNewText);
    void sltPathChanged(const QString &strNewPath);
    void sltOsTypeChanged();
    void sltISOPathChanged(const QString &strPath);
    void sltGuestOSFamilyChanged(const QString &strGuestOSFamilyId);
    void sltSkipUnattendedInstallChanged(bool fSkip);
    void sltSelectedEditionChanged(ulong uEditionIndex);
    /** Translation stuff. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepare stuff. */
    void prepare();
    void createConnections();
    void initializePage() RT_OVERRIDE;
    QWidget *createNameOSTypeWidgets();
    void markWidgets() const;
    void setSkipCheckBoxEnable();
    bool isUnattendedEnabled() const;
    bool isUnattendedInstallSupported() const;
    void setEditionSelectorEnabled();
    void updateInfoLabel();

    /** @name Widgets
     * @{ */
        QGridLayout           *m_pNameAndSystemLayout;
        UINameAndSystemEditor *m_pNameAndSystemEditor;
        QCheckBox             *m_pSkipUnattendedCheckBox;
        QIRichTextLabel       *m_pNameOSTypeLabel;
        QIRichTextLabel       *m_pInfoLabel;
    /** @} */
    QSet<QString> m_userModifiedParameters;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMNameOSTypePage_h */
