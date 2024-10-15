/* $Id: QIDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIDialog class implementation.
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

/* Qt includes: */
#include <QEventLoop>

/* GUI includes: */
#include "QIDialog.h"
#include "UIDesktopWidgetWatchdog.h"

/* Other VBox includes: */
#include "iprt/assert.h"


QIDialog::QIDialog(QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    : QDialog(pParent, enmFlags)
    , m_fPolished(false)
{
    /* Do not count that window as important for application,
     * it will NOT be taken into account when other top-level windows will be closed: */
    setAttribute(Qt::WA_QuitOnClose, false);
}

void QIDialog::setVisible(bool fVisible)
{
    /* Call to base-class: */
    QDialog::setVisible(fVisible);

    /* Exit from the event-loop if there is any and
     * we are changing our state from visible to hidden. */
    if (m_pEventLoop && !fVisible)
        m_pEventLoop->exit();
}

int QIDialog::execute(bool fShow /* = true */, bool fApplicationModal /* = false */)
{
    /* Check for the recursive run: */
    AssertMsgReturn(!m_pEventLoop, ("QIDialog::execute() is called recursively!\n"), QDialog::Rejected);

    /* Reset the result-code: */
    setResult(QDialog::Rejected);

    /* Should we delete ourself on close in theory? */
    const bool fOldDeleteOnClose = testAttribute(Qt::WA_DeleteOnClose);
    /* For the exec() time, set this attribute to 'false': */
    setAttribute(Qt::WA_DeleteOnClose, false);

    /* Which is the current window-modality? */
    const Qt::WindowModality oldModality = windowModality();
    /* For the exec() time, set this attribute to 'window-modal' or 'application-modal': */
    setWindowModality(!fApplicationModal ? Qt::WindowModal : Qt::ApplicationModal);

    /* Show ourself if requested: */
    if (fShow)
        show();

    /* Create a local event-loop: */
    {
        QEventLoop eventLoop;
        m_pEventLoop = &eventLoop;

        /* Guard ourself for the case
         * we destroyed ourself in our event-loop: */
        QPointer<QIDialog> guard = this;

        /* Start the blocking event-loop: */
        eventLoop.exec();

        /* Are we still valid? */
        if (guard.isNull())
            return QDialog::Rejected;

        m_pEventLoop = 0;
    }

    /* Save the result-code early (we can delete ourself on close): */
    const int iResultCode = result();

    /* Return old modality: */
    setWindowModality(oldModality);

    /* Reset attribute to previous value: */
    setAttribute(Qt::WA_DeleteOnClose, fOldDeleteOnClose);
    /* Delete ourself if we should do that on close: */
    if (fOldDeleteOnClose)
        delete this;

    /* Return the result-code: */
    return iResultCode;
}

void QIDialog::done(int iResult)
{
    /* Call to base-class: */
    QDialog::done(iResult);

    /* Make sure event-loop exited even if no dialog visibility changed, s.a. QIDialog::setVisible above.
     * That is necessary to exit event-loop if dialog was executed with fShow == false. */
    if (m_pEventLoop && m_pEventLoop->isRunning() && !QDialog::isVisible())
        m_pEventLoop->exit();
}

void QIDialog::showEvent(QShowEvent *pEvent)
{
    /* Make sure we should polish dialog: */
    if (m_fPolished)
        return;

    /* Call to polish-event: */
    polishEvent(pEvent);

    /* Mark dialog as polished: */
    m_fPolished = true;
}

void QIDialog::polishEvent(QShowEvent *)
{
    /* Make sure layout is polished: */
    adjustSize();
#ifdef VBOX_WS_MAC
    /* And dialog have fixed size: */
    setFixedSize(size());
#endif /* VBOX_WS_MAC */

    /* Explicit centering according to parent if any: */
    if (parentWidget())
        gpDesktop->centerWidget(this, parentWidget(), false);
}
