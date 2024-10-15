/* $Id: UIWizardNewVDVariantPage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVDVariantPage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvd_UIWizardNewVDVariantPage_h
#define FEQT_INCLUDED_SRC_wizards_newvd_UIWizardNewVDVariantPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class CMediumFormat;
class QIRichTextLabel;
class UIDiskVariantWidget;

class SHARED_LIBRARY_STUFF UIWizardNewVDVariantPage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    UIWizardNewVDVariantPage();

private slots:

    void sltMediumVariantChanged(qulonglong uVariant);
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    void initializePage() RT_OVERRIDE RT_FINAL;
    bool isComplete() const RT_OVERRIDE RT_FINAL;
    void prepare();
    void setWidgetVisibility(const CMediumFormat &mediumFormat);

    QIRichTextLabel *m_pDescriptionLabel;
    QIRichTextLabel *m_pDynamicLabel;
    QIRichTextLabel *m_pFixedLabel;
    QIRichTextLabel *m_pSplitLabel;
    UIDiskVariantWidget *m_pVariantWidget;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newvd_UIWizardNewVDVariantPage_h */
