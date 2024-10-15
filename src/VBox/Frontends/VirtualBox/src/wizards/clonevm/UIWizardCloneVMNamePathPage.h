/* $Id: UIWizardCloneVMNamePathPage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardCloneVMNamePathPage class declaration.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_wizards_clonevm_UIWizardCloneVMNamePathPage_h
#define FEQT_INCLUDED_SRC_wizards_clonevm_UIWizardCloneVMNamePathPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSet>

/* Local includes: */
#include "UINativeWizardPage.h"
#include "UIWizardCloneVMEditors.h"

/* Forward declarations: */
class UICloneVMAdditionalOptionsEditor;
class UICloneVMNamePathEditor;
class QIRichTextLabel;

namespace UIWizardCloneVMNamePathCommon
{
    QString composeCloneFilePath(const QString &strCloneName, const QString &strGroup, const QString &strFolderPath);
}

class UIWizardCloneVMNamePathPage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    UIWizardCloneVMNamePathPage(const QString &strOriginalName, const QString &strDefaultPath, const QString &strGroup);

private slots:

    void sltCloneNameChanged(const QString &strCloneName);
    void sltClonePathChanged(const QString &strClonePath);
    void sltMACAddressClonePolicyChanged(MACAddressClonePolicy enmMACAddressClonePolicy);
    void sltKeepDiskNamesToggled(bool fKeepDiskNames);
    void sltKeepHardwareUUIDsToggled(bool fKeepHardwareUUIDs);
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    void initializePage() RT_OVERRIDE;
    void prepare(const QString &strDefaultClonePath);
    /** Validation stuff */
    bool isComplete() const RT_OVERRIDE;

    QIRichTextLabel *m_pMainLabel;
    UICloneVMNamePathEditor *m_pNamePathEditor;
    UICloneVMAdditionalOptionsEditor *m_pAdditionalOptionsEditor;
    QString      m_strOriginalName;
    QString      m_strGroup;
    QSet<QString> m_userModifiedParameters;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_clonevm_UIWizardCloneVMNamePathPage_h */
