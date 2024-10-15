/* $Id: UIWizardNewVMExpertPage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMExpertPage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMExpertPage_h
#define FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMExpertPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSet>

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QButtonGroup;
class QCheckBox;
class QGridLayout;
class QGroupBox;
class QRadioButton;
class QIToolButton;
class UIActionPool;
class UIAdditionalUnattendedOptions;
class UIDiskFormatsComboBox;
class UIDiskVariantWidget;
class UIMediumSizeAndPathGroupBox;
class UIGAInstallationGroupBox;
class UIMediaComboBox;
class UINameAndSystemEditor;
class UINewVMHardwareContainer;
class UIToolBox;
class UIUserNamePasswordGroupBox;

/** Expert page of the New Virtual Machine wizard. */
class UIWizardNewVMExpertPage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    UIWizardNewVMExpertPage(UIActionPool *pActionPool);
    void setISOFilePath(const QString &strISOFilePath);

private slots:

    void sltNameChanged(const QString &strNewName);
    void sltPathChanged(const QString &strNewPath);
    void sltOsTypeChanged();
    void sltMediaComboBoxIndexChanged();
    void sltGetWithFileOpenDialog();
    void sltISOPathChanged(const QString &strPath);
    void sltGAISOPathChanged(const QString &strPath);
    void sltOSFamilyTypeChanged(const QString &strGuestOSFamilyType);
    void sltInstallGACheckBoxToggle(bool fEnabled);
    void sltSkipUnattendedCheckBoxChecked(bool fSkip);
    void sltMediumFormatChanged();
    void sltMediumSizeChanged(qulonglong uSize);
    void sltMediumPathChanged(const QString &strPath);
    void sltMediumVariantChanged(qulonglong uVariant);
    void sltSelectedDiskSourceChanged();
    void sltMediumLocationButtonClicked();
    void sltMemorySizeChanged(int iValue);
    void sltCPUCountChanged(int iCount);
    void sltEFIEnabledChanged(bool fEnabled);
    void sltPasswordChanged(const QString &strPassword);
    void sltUserNameChanged(const QString &strUserName);
    void sltHostnameDomainNameChanged(const QString &strHostnameDomainName, bool fIsComplete);
    void sltProductKeyChanged(const QString &strProductKey);
    void sltStartHeadlessChanged(bool fStartHeadless);
    void sltSelectedEditionChanged(ulong uEditionIndex);
    /** Translation stuff. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    enum ExpertToolboxItems
    {
        ExpertToolboxItems_NameAndOSType,
        ExpertToolboxItems_Unattended,
        ExpertToolboxItems_Hardware,
        ExpertToolboxItems_Disk
    };

    /** Prepare stuff. */
    void createConnections();
    void initializePage() RT_OVERRIDE;
    void initializeWidgets();
    /** Set the values of the widget if they depend on OS
      * type like recommended RAM size. The widgets whose values are
      * are explicitly modified are exempt from this. */
    void setOSTypeDependedValues();
    void cleanupPage();

    /** Validation stuff. */
    bool isComplete() const RT_OVERRIDE;
    bool validatePage() RT_OVERRIDE;

    bool isProductKeyWidgetEnabled() const;
    void disableEnableUnattendedRelatedWidgets(bool fEnabled);
    void markWidgets() const;
    QWidget *createUnattendedWidgets();
    QWidget *createNewDiskWidgets();
    QWidget *createDiskWidgets();
    QWidget *createNameOSTypeWidgets();
    void updateVirtualMediumPathFromMachinePathName();
    void updateDiskWidgetsAfterMediumFormatChange();
    void updateHostnameDomainNameFromMachineName();
    void setEnableNewDiskWidgets(bool fEnable);
    void setSkipCheckBoxEnable();
    bool isUnattendedEnabled() const;
    bool isUnattendedInstallSupported() const;
    void setEnableDiskSelectionWidgets(bool fEnabled);

    /** @name Variables
      * @{ */
        UIToolBox  *m_pToolBox;
        QGroupBox *m_pDiskFormatVariantGroupBox;
        UIDiskVariantWidget *m_pDiskVariantWidget;
        UIDiskFormatsComboBox *m_pFormatComboBox;
        UIMediumSizeAndPathGroupBox *m_pSizeAndLocationGroup;
        UINameAndSystemEditor *m_pNameAndSystemEditor;
        QCheckBox *m_pSkipUnattendedCheckBox;
        QGridLayout *m_pNameAndSystemLayout;
        UINewVMHardwareContainer *m_pHardwareWidgetContainer;
        UIAdditionalUnattendedOptions *m_pAdditionalOptionsContainer;
        UIGAInstallationGroupBox *m_pGAInstallationISOContainer;
        UIUserNamePasswordGroupBox *m_pUserNamePasswordGroupBox;
        QButtonGroup *m_pDiskSourceButtonGroup;
        QRadioButton *m_pDiskEmpty;
        QRadioButton *m_pDiskNew;
        QRadioButton *m_pDiskExisting;
        UIMediaComboBox *m_pDiskSelector;
        QIToolButton *m_pDiskSelectionButton;
        QSet<QString> m_userModifiedParameters;
        bool m_fRecommendedNoDisk;
        qulonglong m_uMediumSizeMin;
        qulonglong m_uMediumSizeMax;
        UIActionPool *m_pActionPool;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMExpertPage_h */
