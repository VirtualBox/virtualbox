/* $Id: UIWizardNewVDFileTypePage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVDFileTypePage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvd_UIWizardNewVDFileTypePage_h
#define FEQT_INCLUDED_SRC_wizards_newvd_UIWizardNewVDFileTypePage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* COM includes:*/
#include "KDeviceType.h"

/* Forward declarations: */
class QIRichTextLabel;
class UIDiskFormatsGroupBox;

/** 1st page of the New Virtual Hard Drive wizard (basic extension). */
class SHARED_LIBRARY_STUFF UIWizardNewVDFileTypePage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructor. */
    UIWizardNewVDFileTypePage(KDeviceType enmDeviceType);

private slots:

    void sltMediumFormatChanged();
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    void prepare(KDeviceType enmDeviceType);
    void initializePage() RT_OVERRIDE RT_FINAL;

    /** Validation stuff. */
    bool isComplete() const RT_OVERRIDE RT_FINAL;

    QIRichTextLabel *m_pLabel;
    UIDiskFormatsGroupBox *m_pFormatButtonGroup;
};


#endif /* !FEQT_INCLUDED_SRC_wizards_newvd_UIWizardNewVDFileTypePage_h */
