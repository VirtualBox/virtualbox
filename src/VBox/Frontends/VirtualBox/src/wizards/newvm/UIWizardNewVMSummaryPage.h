/* $Id: UIWizardNewVMSummaryPage.h 107022 2024-11-14 13:56:24Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMSummaryPage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMSummaryPage_h
#define FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMSummaryPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QIRichTextLabel;
class QITreeView;
class UIWizardNewVMSummaryModel;

class UIWizardNewVMSummaryPage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    UIWizardNewVMSummaryPage(const QString strHelpKeyword = QString());

private slots:

    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    void prepare();
    void createConnections();
    virtual void initializePage() RT_OVERRIDE RT_FINAL;
    virtual bool isComplete() const RT_OVERRIDE RT_FINAL;
    virtual bool validatePage() RT_OVERRIDE RT_FINAL;
    /** @name Widgets
     * @{ */
       QIRichTextLabel *m_pLabel;
       QITreeView *m_pTree;
    /** @} */
    UIWizardNewVMSummaryModel *m_pModel;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMSummaryPage_h */
