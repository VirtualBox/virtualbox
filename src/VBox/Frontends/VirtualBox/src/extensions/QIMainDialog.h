/* $Id: QIMainDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIMainDialog class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIMainDialog_h
#define FEQT_INCLUDED_SRC_extensions_QIMainDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>
#include <QMainWindow>
#include <QPointer>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QPushButton;
class QEventLoop;
class QSizeGrip;

/** QDialog analog based on QMainWindow. */
class SHARED_LIBRARY_STUFF QIMainDialog : public QMainWindow
{
    Q_OBJECT;

public:

    /** Constructs main-dialog passing @a pParent and @a enmFlags to the base-class.
      * @param  fIsAutoCentering  Brigs whether this dialog should be centered according it's parent. */
    QIMainDialog(QWidget *pParent = 0,
                 Qt::WindowFlags enmFlags = Qt::Dialog,
                 bool fIsAutoCentering = true);

    /** Returns the dialog's result code. */
    int result() const { return m_iResult; }

    /** Executes the dialog, launching local event-loop.
      * @param fApplicationModal defines whether this dialog should be modal to application or window. */
    int exec(bool fApplicationModal = true);

    /** Returns dialog's default button. */
    QPushButton *defaultButton() const;
    /** Defines dialog's default @a pButton. */
    void setDefaultButton(QPushButton *pButton);

    /** Returns whether size-grip was enabled for that dialog. */
    bool isSizeGripEnabled() const;
    /** Defines whether size-grip should be @a fEnabled for that dialog. */
    void setSizeGripEnabled(bool fEnabled);

public slots:

    /** Defines whether the dialog is @a fVisible. */
    virtual void setVisible(bool fVisible) RT_OVERRIDE;

protected:

    /** Preprocesses any Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;
    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    /** Handles first show @a pEvent. */
    virtual void polishEvent(QShowEvent *pEvent);

    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

    /** Handles key-press @a pEvent. */
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;

    /** Searches for dialog's default button. */
    QPushButton *searchDefaultButton() const;

    /** Sets reject-by-escape-key flag. */
    void setRejectByEscape(bool fRejectByEscape);

protected slots:

    /** Sets the modal dialog's result code to @a iResult. */
    void setResult(int iResult) { m_iResult = iResult; }

    /** Closes the modal dialog and sets its result code to @a iResult.
      * If this dialog is shown with exec(), done() causes the local
      * event-loop to finish, and exec() to return @a iResult. */
    virtual void done(int iResult);
    /** Hides the modal dialog and sets the result code to Accepted. */
    virtual void accept() { done(QDialog::Accepted); }
    /** Hides the modal dialog and sets the result code to Rejected. */
    virtual void reject() { done(QDialog::Rejected); }

private:

    /** Holds whether this dialog should be centered according it's parent. */
    const bool  m_fIsAutoCentering;
    /** Holds whether this dialog is polished. */
    bool        m_fPolished;

    /** Holds modal dialog's result code. */
    int                   m_iResult;
    /** Holds modal dialog's event-loop. */
    QPointer<QEventLoop>  m_pEventLoop;

    /** Holds dialog's default button. */
    QPointer<QPushButton>  m_pDefaultButton;
    /** Holds dialog's size-grip. */
    QPointer<QSizeGrip>    m_pSizeGrip;
    /** Holds reject by escape flag. When true pressing escape rejects the dialog. Default is true.*/
    bool m_fRejectByEscape;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIMainDialog_h */
