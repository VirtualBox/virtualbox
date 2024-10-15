/* $Id: UIWizardNewVMUnattendedPage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMUnattendedPage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMUnattendedPage_h
#define FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMUnattendedPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSet>

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QIRichTextLabel;
class UIAdditionalUnattendedOptions;
class UIGAInstallationGroupBox;
class UIUserNamePasswordGroupBox;
class UIWindowsISOImageSelector;

namespace UIWizardNewVMUnattendedCommon
{
    /** Returns false if ISO path selector is non empty but has invalid file path. */
    bool checkGAISOFile(const QString &strPatho);
}

class UIWizardNewVMUnattendedPage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    UIWizardNewVMUnattendedPage();

private slots:

    void sltInstallGACheckBoxToggle(bool fChecked);
    void sltGAISOPathChanged(const QString &strPath);
    void sltPasswordChanged(const QString &strPassword);
    void sltUserNameChanged(const QString &strUserName);
    void sltHostnameDomainNameChanged(const QString &strHostnameDomainName, bool fIsComplete);
    void sltProductKeyChanged(const QString &strProductKey);
    void sltStartHeadlessChanged(bool fStartHeadless);
    void sltSelectedWindowsImageChanged(ulong uImageIndex);
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    void prepare();
    void createConnections();
    void initializePage() RT_OVERRIDE;
    bool isComplete() const RT_OVERRIDE;
    /** Returns true if we show the widgets for guest os product key. */
    bool isProductKeyWidgetEnabled() const;
    void markWidgets() const;

    /** @name Widgets
      * @{ */
        QIRichTextLabel *m_pLabel;
        UIAdditionalUnattendedOptions *m_pAdditionalOptionsContainer;
        UIGAInstallationGroupBox *m_pGAInstallationISOContainer;
        UIUserNamePasswordGroupBox *m_pUserNamePasswordGroupBox;
    /** @} */
    QSet<QString> m_userModifiedParameters;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMUnattendedPage_h */
