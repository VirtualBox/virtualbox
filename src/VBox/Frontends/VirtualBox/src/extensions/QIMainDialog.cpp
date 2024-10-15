/* $Id: QIMainDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIMainDialog class implementation.
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
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QSizeGrip>
#include <QUrl>

/* GUI includes: */
#include "QIMainDialog.h"
#include "UIDesktopWidgetWatchdog.h"
#include "VBoxUtils.h"

/* Other VBox includes: */
#include <iprt/assert.h>


QIMainDialog::QIMainDialog(QWidget *pParent /* = 0 */,
                           Qt::WindowFlags enmFlags /* = Qt::Dialog */,
                           bool fIsAutoCentering /* = true */)
    : QMainWindow(pParent, enmFlags)
    , m_fIsAutoCentering(fIsAutoCentering)
    , m_fPolished(false)
    , m_iResult(QDialog::Rejected)
    , m_fRejectByEscape(true)
{
    /* Install event-filter: */
    qApp->installEventFilter(this);
}

int QIMainDialog::exec(bool fApplicationModal /* = true */)
{
    /* Check for the recursive run: */
    AssertMsgReturn(!m_pEventLoop, ("QIMainDialog::exec() is called recursively!\n"), QDialog::Rejected);

    /* Reset the result code: */
    setResult(QDialog::Rejected);

    /* Should we delete ourself on close in theory? */
    const bool fOldDeleteOnClose = testAttribute(Qt::WA_DeleteOnClose);
    /* For the exec() time, set this attribute to 'false': */
    setAttribute(Qt::WA_DeleteOnClose, false);

    /* Which is the current window-modality? */
    const Qt::WindowModality oldModality = windowModality();
    /* For the exec() time, set this attribute to 'window-modal' or 'application-modal': */
    setWindowModality(!fApplicationModal ? Qt::WindowModal : Qt::ApplicationModal);

    /* Show ourself: */
    show();

    /* Create a local event-loop: */
    {
        QEventLoop eventLoop;
        m_pEventLoop = &eventLoop;

        /* Guard ourself for the case
         * we destroyed ourself in our event-loop: */
        QPointer<QIMainDialog> guard = this;

        /* Start the blocking event-loop: */
        eventLoop.exec();

        /* Are we still valid? */
        if (guard.isNull())
            return QDialog::Rejected;

        m_pEventLoop = 0;
    }

    /* Save the result code early (we can delete ourself on close): */
    const int iResultCode = result();

    /* Return old modality: */
    setWindowModality(oldModality);

    /* Reset attribute to previous value: */
    setAttribute(Qt::WA_DeleteOnClose, fOldDeleteOnClose);
    /* Delete ourself if we should do that on close: */
    if (fOldDeleteOnClose)
        delete this;

    /* Return the result code: */
    return iResultCode;
}

QPushButton *QIMainDialog::defaultButton() const
{
    return m_pDefaultButton;
}

void QIMainDialog::setDefaultButton(QPushButton *pButton)
{
    m_pDefaultButton = pButton;
}

bool QIMainDialog::isSizeGripEnabled() const
{
    return m_pSizeGrip;
}

void QIMainDialog::setSizeGripEnabled(bool fEnabled)
{
    /* Create if missed: */
    if (!m_pSizeGrip && fEnabled)
    {
        m_pSizeGrip = new QSizeGrip(this);
        m_pSizeGrip->resize(m_pSizeGrip->sizeHint());
        m_pSizeGrip->show();
    }
    /* Destroy if present: */
    else if (m_pSizeGrip && !fEnabled)
    {
        delete m_pSizeGrip;
        m_pSizeGrip = 0;
    }
}

void QIMainDialog::setVisible(bool fVisible)
{
    /* Call to base-class: */
    QMainWindow::setVisible(fVisible);

    /* Exit from the event-loop if there is any and
     * we are changing our state from visible to hidden. */
    if (m_pEventLoop && !fVisible)
        m_pEventLoop->exit();
}

bool QIMainDialog::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Skip for inactive window: */
    if (!isActiveWindow())
        return QMainWindow::eventFilter(pObject, pEvent);

    /* Skip for children of other than this one window: */
    if (qobject_cast<QWidget*>(pObject) &&
        qobject_cast<QWidget*>(pObject)->window() != this)
        return QMainWindow::eventFilter(pObject, pEvent);

    /* Depending on event-type: */
    switch (pEvent->type())
    {
        /* Auto-default-button focus-in processor used to move the "default"
         * button property into the currently focused button. */
        case QEvent::FocusIn:
        {
            if (qobject_cast<QPushButton*>(pObject) &&
                (pObject->parent() == centralWidget() ||
                 qobject_cast<QDialogButtonBox*>(pObject->parent())))
            {
                qobject_cast<QPushButton*>(pObject)->setDefault(pObject != m_pDefaultButton);
                if (m_pDefaultButton)
                    m_pDefaultButton->setDefault(pObject == m_pDefaultButton);
            }
            break;
        }
        /* Auto-default-button focus-out processor used to remove the "default"
         * button property from the previously focused button. */
        case QEvent::FocusOut:
        {
            if (qobject_cast<QPushButton*>(pObject) &&
                (pObject->parent() == centralWidget() ||
                 qobject_cast<QDialogButtonBox*>(pObject->parent())))
            {
                if (m_pDefaultButton)
                    m_pDefaultButton->setDefault(pObject != m_pDefaultButton);
                qobject_cast<QPushButton*>(pObject)->setDefault(pObject == m_pDefaultButton);
            }
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QMainWindow::eventFilter(pObject, pEvent);
}

bool QIMainDialog::event(QEvent *pEvent)
{
    /* Depending on event-type: */
    switch (pEvent->type())
    {
        case QEvent::Polish:
        {
            /* Initially search for the default-button: */
            m_pDefaultButton = searchDefaultButton();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QMainWindow::event(pEvent);
}

void QIMainDialog::showEvent(QShowEvent *pEvent)
{
    /* Polish dialog if necessary: */
    if (!m_fPolished)
    {
        polishEvent(pEvent);
        m_fPolished = true;
    }

    /* Call to base-class: */
    QMainWindow::showEvent(pEvent);
}

void QIMainDialog::polishEvent(QShowEvent *)
{
    /* Explicit centering according to our parent: */
    if (m_fIsAutoCentering)
        gpDesktop->centerWidget(this, parentWidget(), false);
}

void QIMainDialog::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QMainWindow::resizeEvent(pEvent);

    /* Adjust the size-grip location for the current resize event: */
    if (m_pSizeGrip)
    {
        if (isRightToLeft())
            m_pSizeGrip->move(rect().bottomLeft() - m_pSizeGrip->rect().bottomLeft());
        else
            m_pSizeGrip->move(rect().bottomRight() - m_pSizeGrip->rect().bottomRight());
    }
}

void QIMainDialog::keyPressEvent(QKeyEvent *pEvent)
{
    /* Make sure that we only proceed if no
     * popup or other modal widgets are open. */
    if (qApp->activePopupWidget() ||
        (qApp->activeModalWidget() && qApp->activeModalWidget() != this))
    {
        /* Call to base-class: */
        return QMainWindow::keyPressEvent(pEvent);
    }

    /* Special handling for some keys: */
    switch (pEvent->key())
    {
        /* Special handling for Escape key: */
        case Qt::Key_Escape:
        {
            if (pEvent->modifiers() == Qt::NoModifier && m_fRejectByEscape)
            {
                setResult(QDialog::Rejected);
                close();
                return;
            }
            break;
        }
#ifdef VBOX_WS_MAC
        /* Special handling for Period key: */
        case Qt::Key_Period:
        {
            if (pEvent->modifiers() == Qt::ControlModifier)
            {
                setResult(QDialog::Rejected);
                close();
                return;
            }
            break;
        }
#endif /* VBOX_WS_MAC */
        /* Special handling for Return/Enter key: */
        case Qt::Key_Return:
        case Qt::Key_Enter:
        {
            if (((pEvent->modifiers() == Qt::NoModifier) && (pEvent->key() == Qt::Key_Return)) ||
                ((pEvent->modifiers() & Qt::KeypadModifier) && (pEvent->key() == Qt::Key_Enter)))
            {
                if (QPushButton *pCurrentDefault = searchDefaultButton())
                {
                    pCurrentDefault->animateClick();
                    return;
                }
            }
            break;
        }
        /* Default handling for others: */
        default: break;
    }

    /* Call to base-class: */
    QMainWindow::keyPressEvent(pEvent);
}

QPushButton *QIMainDialog::searchDefaultButton() const
{
    /* Search for the first default-button in the dialog: */
    QList<QPushButton*> list = findChildren<QPushButton*>();
    foreach (QPushButton *pButton, list)
        if (pButton->isDefault() &&
            (pButton->parent() == centralWidget() ||
             qobject_cast<QDialogButtonBox*>(pButton->parent())))
            return pButton;
    return 0;
}

void QIMainDialog::done(int iResult)
{
    /* Set the final result: */
    setResult(iResult);
    /* Hide: */
    hide();
}

void QIMainDialog::setRejectByEscape(bool fRejectByEscape)
{
    m_fRejectByEscape = fRejectByEscape;
}
