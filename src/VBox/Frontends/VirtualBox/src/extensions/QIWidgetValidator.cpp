/* $Id: QIWidgetValidator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIWidgetValidator class implementation.
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

/* GUI includes: */
#include "QIWidgetValidator.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/*********************************************************************************************************************************
*   Class QObjectValidator implementation.                                                                                       *
*********************************************************************************************************************************/

QObjectValidator::QObjectValidator(QValidator *pValidator, QObject *pParent /* = 0 */)
    : QObject(pParent)
    , m_pValidator(pValidator)
    , m_enmState(QValidator::Invalid)
{
    prepare();
}

void QObjectValidator::sltValidate(QString strInput /* = QString() */)
{
    /* Make sure validator assigned: */
    AssertPtrReturnVoid(m_pValidator);

    /* Validate: */
    int iPosition = 0;
    const QValidator::State enmState = m_pValidator->validate(strInput, iPosition);

    /* If validity state changed: */
    if (m_enmState != enmState)
    {
        /* Update last validity state: */
        m_enmState = enmState;

        /* Notifies listener(s) about validity change: */
        emit sigValidityChange(m_enmState);
    }
}

void QObjectValidator::prepare()
{
    /* Make sure validator assigned: */
    AssertPtrReturnVoid(m_pValidator);

    /* Register validator as child: */
    m_pValidator->setParent(this);

    /* Validate: */
    sltValidate();
}


/*********************************************************************************************************************************
*   Class QObjectValidatorGroup implementation.                                                                                  *
*********************************************************************************************************************************/

QObjectValidatorGroup::QObjectValidatorGroup(QObject *pParent)
    : QObject(pParent)
    , m_fResult(false)
{
}

void QObjectValidatorGroup::addObjectValidator(QObjectValidator *pObjectValidator)
{
    /* Make sure object-validator passed: */
    AssertPtrReturnVoid(pObjectValidator);

    /* Register object-validator as child: */
    pObjectValidator->setParent(this);

    /* Insert object-validator to internal map: */
    m_group.insert(pObjectValidator, toResult(pObjectValidator->state()));

    /* Attach object-validator to group: */
    connect(pObjectValidator, &QObjectValidator::sigValidityChange,
            this, &QObjectValidatorGroup::sltValidate);
}

void QObjectValidatorGroup::sltValidate(QValidator::State enmState)
{
    /* Determine sender object-validator: */
    QObjectValidator *pObjectValidatorSender = qobject_cast<QObjectValidator*>(sender());
    /* Make sure that is one of our senders: */
    AssertReturnVoid(pObjectValidatorSender && m_group.contains(pObjectValidatorSender));

    /* Update internal map: */
    m_group[pObjectValidatorSender] = toResult(enmState);

    /* Enumerate all the registered object-validators: */
    bool fResult = true;
    foreach (QObjectValidator *pObjectValidator, m_group.keys())
        if (!toResult(pObjectValidator->state()))
        {
            fResult = false;
            break;
        }

    /* If validity state changed: */
    if (m_fResult != fResult)
    {
        /* Update last validity state: */
        m_fResult = fResult;

        /* Notifies listener(s) about validity change: */
        emit sigValidityChange(m_fResult);
    }
}

/* static */
bool QObjectValidatorGroup::toResult(QValidator::State enmState)
{
    return enmState == QValidator::Acceptable;
}


/*********************************************************************************************************************************
*   Class QIULongValidator implementation.                                                                                       *
*********************************************************************************************************************************/

QValidator::State QIULongValidator::validate(QString &strInput, int &iPosition) const
{
    Q_UNUSED(iPosition);

    /* Get the stripped string: */
    QString strStripped = strInput.trimmed();

    /* 'Intermediate' for empty string or started from '0x': */
    if (strStripped.isEmpty() ||
        strStripped.toUpper() == QString("0x").toUpper())
        return Intermediate;

    /* Convert to ulong: */
    bool fOk;
    ulong uEntered = strInput.toULong(&fOk, 0);

    /* 'Invalid' if failed to convert: */
    if (!fOk)
        return Invalid;

    /* 'Acceptable' if fits the bounds: */
    if (uEntered >= m_uBottom && uEntered <= m_uTop)
        return Acceptable;

    /* 'Invalid' if more than top, 'Intermediate' if less than bottom: */
    return uEntered > m_uTop ? Invalid : Intermediate;
}
