/* $Id: UINativeWizardPage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINativeWizardPage class declaration.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_wizards_UINativeWizardPage_h
#define FEQT_INCLUDED_SRC_wizards_UINativeWizardPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class UINativeWizard;

/** QWidget extension with advanced functionality emulating QWizardPage behavior. */
class SHARED_LIBRARY_STUFF UINativeWizardPage : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies about page validity changes. */
    void completeChanged();

public:

    /** Constructs wizard page. */
    UINativeWizardPage();

    /** Redirects the translation call to actual handler. */
    void retranslate() { sltRetranslateUI(); }

    /** Defines page @a strTitle. */
    void setTitle(const QString &strTitle);
    /** Returns page title. */
    QString title() const;

    /** Handles the page initialization. */
    virtual void initializePage() {}
    /** Tests the page for completeness, enables the Next button if Ok.
      * @returns whether all conditions to go to next page are satisfied. */
    virtual bool isComplete() const { return true; }
    /** Tests the page for validity, tranfers to the Next page is Ok.
      * @returns whether page state to go to next page is bearable. */
    virtual bool validatePage() { return true; }

protected slots:

    virtual void sltRetranslateUI() = 0;

protected:

    /** Returns wizard this page belongs to. */
    UINativeWizard *wizard() const;

    template<typename T>
        T *wizardWindow() const
    {
        return   parentWidget() && parentWidget()->window()
            ? qobject_cast<T*>(parentWidget()->window())
            : 0;
    }


    /** Holds the page title. */
    QString  m_strTitle;
};


#endif /* !FEQT_INCLUDED_SRC_wizards_UINativeWizardPage_h */
