/* $Id: UIWizardCloneVMEditors.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardDiskEditors class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_editors_UIWizardCloneVMEditors_h
#define FEQT_INCLUDED_SRC_wizards_editors_UIWizardCloneVMEditors_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QGroupBox>

/* Other VBox includes: */
#include "KCloneMode.h"

/* Forward declarations: */
class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QGridLayout;
class QComboBox;
class QLabel;
class QRadioButton;
class QILineEdit;
class UIFilePathSelector;

/** MAC address policies. */
enum MACAddressClonePolicy
{
    MACAddressClonePolicy_KeepAllMACs,
    MACAddressClonePolicy_KeepNATMACs,
    MACAddressClonePolicy_StripAllMACs,
    MACAddressClonePolicy_MAX
};
Q_DECLARE_METATYPE(MACAddressClonePolicy);

class UICloneVMNamePathEditor : public QGroupBox
{
    Q_OBJECT;

signals:

    void sigCloneNameChanged(const QString &strCloneName);
    void sigClonePathChanged(const QString &strClonePath);

public:

    UICloneVMNamePathEditor(const QString &strOriginalName, const QString &strDefaultPath, QWidget *pParent = 0);

    void setFirstColumnWidth(int iWidth);
    int firstColumnWidth() const;

    void setLayoutContentsMargins(int iLeft, int iTop, int iRight, int iBottom);

    QString cloneName() const;
    void setCloneName(const QString &strName);

    QString clonePath() const;
    void setClonePath(const QString &strPath);

    bool isComplete(const QString &strMachineGroup);

private slots:

    void sltRetranslateUI();

private:

    void prepare();

    QGridLayout *m_pContainerLayout;
    QILineEdit  *m_pNameLineEdit;
    UIFilePathSelector  *m_pPathSelector;
    QLabel      *m_pNameLabel;
    QLabel      *m_pPathLabel;

    QString      m_strOriginalName;
    QString      m_strDefaultPath;
};


class UICloneVMAdditionalOptionsEditor : public QGroupBox
{
    Q_OBJECT;

signals:

    void sigMACAddressClonePolicyChanged(MACAddressClonePolicy enmMACAddressClonePolicy);
    void sigKeepDiskNamesToggled(bool fKeepDiskNames);
    void sigKeepHardwareUUIDsToggled(bool fKeepHardwareUUIDs);

public:

    UICloneVMAdditionalOptionsEditor(QWidget *pParent = 0);

    bool isComplete();

    void setLayoutContentsMargins(int iLeft, int iTop, int iRight, int iBottom);

    MACAddressClonePolicy macAddressClonePolicy() const;
    void setMACAddressClonePolicy(MACAddressClonePolicy enmMACAddressClonePolicy);

    void setFirstColumnWidth(int iWidth);
    int firstColumnWidth() const;

    bool keepHardwareUUIDs() const;
    bool keepDiskNames() const;

private slots:

    void sltMACAddressClonePolicyChanged();
    void sltRetranslateUI();

private:

    void prepare();
    void populateMACAddressClonePolicies();
    void updateMACAddressClonePolicyComboToolTip();

    QGridLayout *m_pContainerLayout;
    QLabel *m_pMACComboBoxLabel;
    QComboBox *m_pMACComboBox;
    QLabel *m_pAdditionalOptionsLabel;
    QCheckBox   *m_pKeepDiskNamesCheckBox;
    QCheckBox   *m_pKeepHWUUIDsCheckBox;
};

class UICloneVMCloneTypeGroupBox : public QGroupBox
{
    Q_OBJECT;

signals:

    void sigFullCloneSelected(bool fSelected);

public:

    UICloneVMCloneTypeGroupBox(QWidget *pParent = 0);
    bool isFullClone() const;

private slots:

    void sltButtonClicked(QAbstractButton *);
    void sltRetranslateUI();

private:

    void prepare();

    QButtonGroup *m_pButtonGroup;
    QRadioButton *m_pFullCloneRadio;
    QRadioButton *m_pLinkedCloneRadio;
};


class UICloneVMCloneModeGroupBox : public QGroupBox
{
    Q_OBJECT;

signals:

    void sigCloneModeChanged(KCloneMode enmCloneMode);

public:

    UICloneVMCloneModeGroupBox(bool fShowChildsOption, QWidget *pParent = 0);
    KCloneMode cloneMode() const;

private slots:

    void sltButtonClicked();
    void sltRetranslateUI();

private:

    void prepare();

    bool m_fShowChildsOption;
    QRadioButton *m_pMachineRadio;
    QRadioButton *m_pMachineAndChildsRadio;
    QRadioButton *m_pAllRadio;
};



#endif /* !FEQT_INCLUDED_SRC_wizards_editors_UIWizardCloneVMEditors_h */
