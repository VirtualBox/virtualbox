/* $Id: UIWizardCloneVMModePage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardCloneVMModePage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_clonevm_UIWizardCloneVMModePage_h
#define FEQT_INCLUDED_SRC_wizards_clonevm_UIWizardCloneVMModePage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSet>

/* GUI includes: */
#include "UINativeWizardPage.h"

/* COM includes: */
#include "KCloneMode.h"

/* Forward declaration: */
class QIRichTextLabel;
class UICloneVMCloneModeGroupBox;

/** 3rd page of the Clone Virtual Machine wizard (basic extension). */
class UIWizardCloneVMModePage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructor. */
    UIWizardCloneVMModePage(bool fShowChildsOption);

private slots:

    void sltCloneModeChanged(KCloneMode enmCloneMode);
    /** Translation stuff. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepare stuff. */
    void initializePage() RT_OVERRIDE;
    void prepare();

    /** Validation stuff. */
    bool validatePage() RT_OVERRIDE;

    /** Widgets. */
    QIRichTextLabel *m_pLabel;
    UICloneVMCloneModeGroupBox *m_pCloneModeGroupBox;

    bool m_fShowChildsOption;
    QSet<QString> m_userModifiedParameters;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_clonevm_UIWizardCloneVMModePage_h */
