/* $Id: UIWizardNewVMEditors.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMEditors class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_editors_UIWizardNewVMEditors_h
#define FEQT_INCLUDED_SRC_wizards_editors_UIWizardNewVMEditors_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>
#include <QGroupBox>

/* Forward declarations: */
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QILineEdit;
class UIBaseMemoryEditor;
class UIFilePathSelector;
class UIHostnameDomainNameEditor;
class UIPasswordLineEdit;
class UIUserNamePasswordEditor;
class UIVirtualCPUEditor;

class UIUserNamePasswordGroupBox : public QGroupBox
{
    Q_OBJECT;

signals:

    void sigUserNameChanged(const QString &strUserName);
    void sigPasswordChanged(const QString &strPassword);

public:

    UIUserNamePasswordGroupBox(QWidget *pParent = 0);

    /** @name Wrappers for UIUserNamePasswordEditor
      * @{ */
        QString userName() const;
        void setUserName(const QString &strUserName);

        QString password() const;
        void setPassword(const QString &strPassword);
        bool isComplete();
        void setLabelsVisible(bool fVisible);
    /** @} */

private slots:

    void sltRetranslateUI();

private:

    void prepare();

    UIUserNamePasswordEditor *m_pUserNamePasswordEditor;
};


class UIGAInstallationGroupBox : public QGroupBox
{
    Q_OBJECT;

signals:

    void sigPathChanged(const QString &strPath);

public:

    UIGAInstallationGroupBox(QWidget *pParent = 0);

    /** @name Wrappers for UIFilePathSelector
      * @{ */
        QString path() const;
        void setPath(const QString &strPath, bool fRefreshText = true);
        void mark();
        bool isComplete() const;
    /** @} */

private slots:

    void sltToggleWidgetsEnabled(bool fEnabled);
    void sltRetranslateUI();

private:

    void prepare();

    QLabel *m_pGAISOPathLabel;
    UIFilePathSelector *m_pGAISOFilePathSelector;
};

class UIAdditionalUnattendedOptions : public QGroupBox
{
    Q_OBJECT;

signals:

    void sigHostnameDomainNameChanged(const QString &strHostnameDomainName, bool fIsComplete);
    void sigProductKeyChanged(const QString &strProductKey);
    void sigStartHeadlessChanged(bool fChecked);

public:

    UIAdditionalUnattendedOptions(QWidget *pParent = 0);

    /** @name Wrappers for UIFilePathSelector
      * @{ */
        QString hostname() const;
        void setHostname(const QString &strHostname);
        QString domainName() const;
        void setDomainName(const QString &strDomain);
        QString hostnameDomainName() const;
        bool isComplete() const;
        bool isHostnameComplete() const;
        void mark();
        void disableEnableProductKeyWidgets(bool fEnabled);
    /** @} */

private slots:

    void sltRetranslateUI();

private:

    void prepare();

    UIHostnameDomainNameEditor *m_pHostnameDomainNameEditor;
};


class UINewVMHardwareContainer : public QWidget
{
    Q_OBJECT;

signals:

    void sigMemorySizeChanged(int iSize);
    void sigCPUCountChanged(int iCount);
    void sigEFIEnabledChanged(bool fEnabled);

public:

    UINewVMHardwareContainer(QWidget *pParent = 0);

    /** @name Wrappers for UIFilePathSelector
      * @{ */
        void setMemorySize(int iSize);
        void setCPUCount(int iCount);
        void setEFIEnabled(bool fEnabled);
    /** @} */

private slots:

    void sltRetranslateUI();

private:

    void prepare();

    /** Updates minimum layout hint. */
    void updateMinimumLayoutHint();

    UIBaseMemoryEditor *m_pBaseMemoryEditor;
    UIVirtualCPUEditor *m_pVirtualCPUEditor;
    QCheckBox *m_pEFICheckBox;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_editors_UIWizardNewVMEditors_h */
