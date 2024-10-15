/* $Id: QIDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIDialog class declaration.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIDialog_h
#define FEQT_INCLUDED_SRC_extensions_QIDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>
#include <QPointer>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QEventLoop;

/** QDialog extension providing the GUI with
  * the advanced capabilities like delayed show. */
class SHARED_LIBRARY_STUFF QIDialog : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs the dialog passing @a pParent and @a enmFlags to the base-class. */
    QIDialog(QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());

    /** Defines whether the dialog is @a fVisible. */
    void setVisible(bool fVisible) RT_OVERRIDE;

public slots:

    /** Shows the dialog as a modal one, blocking until the user closes it.
      * @param  fShow              Brings whether the dialog should be shown instantly.
      * @param  fApplicationModal  Brings whether the dialog should be application-modal. */
    virtual int execute(bool fShow = true, bool fApplicationModal = false);

    /** Shows the dialog as a modal one, blocking until the user closes it. */
    virtual int exec() RT_OVERRIDE { return execute(); }

    /** Closes the dialog and sets its result code to iResult. */
    virtual void done(int iResult) RT_OVERRIDE;

protected:

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    /** Handles show @a pEvent sent for the first time. */
    virtual void polishEvent(QShowEvent *pEvent);

private:

    /** Holds whether the dialog is polished. */
    bool m_fPolished;

    /** Holds the separate event-loop instance.
      * @note  This event-loop is only used when the dialog being executed via the execute()
      *        functionality, allowing for the delayed show and advanced modality flag. */
    QPointer<QEventLoop> m_pEventLoop;
};

/** Safe pointer to the QIDialog class. */
typedef QPointer<QIDialog> UISafePointerDialog;

#endif /* !FEQT_INCLUDED_SRC_extensions_QIDialog_h */
