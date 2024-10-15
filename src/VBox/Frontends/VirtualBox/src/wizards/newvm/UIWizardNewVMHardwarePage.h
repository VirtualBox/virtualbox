/* $Id: UIWizardNewVMHardwarePage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMHardwarePage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMHardwarePage_h
#define FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMHardwarePage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSet>

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QIRichTextLabel;
class UINewVMHardwareContainer;

class UIWizardNewVMHardwarePage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    UIWizardNewVMHardwarePage();

private slots:

    void sltMemorySizeChanged(int iValue);
    void sltCPUCountChanged(int iCount);
    void sltEFIEnabledChanged(bool fEnabled);
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepare stuff. */
    void prepare();
    void createConnections();
    virtual void initializePage() RT_OVERRIDE RT_FINAL;
    virtual bool isComplete() const RT_OVERRIDE RT_FINAL;

    /** @name Widgets
      * @{ */
        QIRichTextLabel    *m_pLabel;
        UINewVMHardwareContainer *m_pHardwareWidgetContainer;
    /** @} */
    /** This set is used to decide if we have to set wizard's parameters
      * some default values or not. When user modifies a value through a widget we
      * no longer touch user set value during page initilization. see initializePage. */
    QSet<QString> m_userModifiedParameters;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMHardwarePage_h */
