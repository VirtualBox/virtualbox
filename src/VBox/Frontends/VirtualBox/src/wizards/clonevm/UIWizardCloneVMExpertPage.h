/* $Id: UIWizardCloneVMExpertPage.h 110043 2025-06-30 10:09:23Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardCloneVMExpertPage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_clonevm_UIWizardCloneVMExpertPage_h
#define FEQT_INCLUDED_SRC_wizards_clonevm_UIWizardCloneVMExpertPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Local includes: */
#include "UINativeWizardPage.h"
#include "UIWizardCloneVMEditors.h"

/* Forward declarations: */
class UICloneVMAdditionalOptionsEditor;
class UICloneVMCloneModeGroupBox;
class UICloneVMCloneTypeGroupBox;
class UICloneVMNamePathEditor;

/** Expert page of the Clone Virtual Machine wizard. */
class UIWizardCloneVMExpertPage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructor. */
    UIWizardCloneVMExpertPage(const QString &strOriginalName, const QString &strDefaultPath,
                              bool fAdditionalInfo, bool fShowChildsOption, const QString &strGroup);

private slots:

    void sltCloneNameChanged(const QString &strCloneName);
    void sltClonePathChanged(const QString &strClonePath);
    void sltMACAddressClonePolicyChanged(MACAddressClonePolicy enmMACAddressClonePolicy);
    void sltKeepDiskNamesToggled(bool fKeepDiskNames);
    void sltKeepHardwareUUIDsToggled(bool fKeepHardwareUUIDs);
    void sltCloneTypeChanged(bool fIsFullClone);
    void sltCloneModeChanged(KCloneMode enmCloneMode);
    /** Translation stuff. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepare stuff. */
    void initializePage() RT_OVERRIDE;
    void prepare(const QString &strOriginalName, const QString &strDefaultPath, bool fShowChildsOption);

    /** Validation stuff. */
    bool isComplete() const RT_OVERRIDE;
    bool validatePage() RT_OVERRIDE;
    void setCloneModeGroupBoxEnabled();

    UICloneVMNamePathEditor *m_pNamePathGroupBox;
    UICloneVMCloneTypeGroupBox *m_pCloneTypeGroupBox;
    UICloneVMCloneModeGroupBox *m_pCloneModeGroupBox;
    UICloneVMAdditionalOptionsEditor *m_pAdditionalOptionsGroupBox;
    QString m_strGroup;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_clonevm_UIWizardCloneVMExpertPage_h */
