/* $Id: UIGuestProcessControlDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGuestProcessControlDialog class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_guestctrl_UIGuestProcessControlDialog_h
#define FEQT_INCLUDED_SRC_guestctrl_UIGuestProcessControlDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QString>

/* GUI includes: */
#include "QIManagerDialog.h"

/* COM includes: */
#include "CGuest.h"

/** QIManagerDialogFactory extension used as a factory for the Guest Control dialog. */
class UIGuestProcessControlDialogFactory : public QIManagerDialogFactory
{
public:

    /** Constructs dialog factory. */
    UIGuestProcessControlDialogFactory();

protected:

    /** Creates derived @a pDialog instance.
      * @param  pCenterWidget  Passes the widget to center wrt. pCenterWidget. */
    virtual void create(QIManagerDialog *&pDialog, QWidget *pCenterWidget) RT_OVERRIDE;
};

/** QIManagerDialog extension providing GUI with the dialog displaying guest control releated logs. */
class UIGuestProcessControlDialog : public QIManagerDialog
{
    Q_OBJECT;

public:

    /** Constructs Guest Control dialog. */
    UIGuestProcessControlDialog(QWidget *pCenterWidget);

protected:

    /** @name Prepare/cleanup cascade.
     * @{ */
        /** Configures all. */
        virtual void configure() RT_OVERRIDE;
        /** Configures central-widget. */
        virtual void configureCentralWidget() RT_OVERRIDE;
        /** Perform final preparations. */
        virtual void finalize() RT_OVERRIDE;
        /** Loads dialog setting from extradata. */
        virtual void loadSettings() RT_OVERRIDE;

        /** Saves dialog setting into extradata. */
        virtual void saveSettings() RT_OVERRIDE;
    /** @} */

    /** @name Functions related to geometry restoration.
     * @{ */
        /** Returns whether the window should be maximized when geometry being restored. */
        virtual bool shouldBeMaximized() const RT_OVERRIDE;
    /** @} */

private slots:

    void sltSetCloseButtonShortCut(QKeySequence shortcut);
    /** @name Event-handling stuff.
      * @{ */
        /** Handles translation event. */
        void sltRetranslateUI();
    /** @} */

private:

    CGuest   m_comGuest;
    QString  m_strMachineName;
};

#endif /* !FEQT_INCLUDED_SRC_guestctrl_UIGuestProcessControlDialog_h */
