/* $Id: QIWidgetValidator.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIWidgetValidator class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIWidgetValidator_h
#define FEQT_INCLUDED_SRC_extensions_QIWidgetValidator_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QValidator>

/* GUI includes: */
#include "UILibraryDefs.h"


/** QObject sub-class,
  * providing passed QObject with validation routine. */
class SHARED_LIBRARY_STUFF QObjectValidator : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listener(s) about validity changed to @a enmState. */
    void sigValidityChange(QValidator::State enmState);

public:

    /** Constructs object validator passing @a pParent to the base-class.
      * @param  pValidator  Brings the validator passed on to the OObject
      *                     children and used to perform validation itself. */
    QObjectValidator(QValidator *pValidator, QObject *pParent = 0);

    /** Returns last validation state. */
    QValidator::State state() const { return m_enmState; }

public slots:

    /** Performs validation: */
    void sltValidate(QString strInput = QString());

private:

    /** Prepare routine. */
    void prepare();

    /** Holds the validator reference. */
    QValidator *m_pValidator;

    /** Holds the validation state. */
    QValidator::State m_enmState;
};


/** QObject sub-class,
  * which can group various QObjectValidator instances to operate on. */
class SHARED_LIBRARY_STUFF QObjectValidatorGroup : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listener(s) about validity changed to @a fValid. */
    void sigValidityChange(bool fValid);

public:

    /** Constructs validation group passing @a pParent to the base-class. */
    QObjectValidatorGroup(QObject *pParent);

    /** Adds @a pObjectValidator.
      * @note The ownership of @a pObjectValidator is transferred to the group,
      *       and it's the group's responsibility to delete it. */
    void addObjectValidator(QObjectValidator *pObjectValidator);

    /** Returns last validation result. */
    bool result() const { return m_fResult; }

private slots:

    /** Performs validation for a passed @a enmState. */
    void sltValidate(QValidator::State enmState);

private:

    /** Converts QValidator::State to bool result. */
    static bool toResult(QValidator::State enmState);

    /** Holds object-validators and their states. */
    QMap<QObjectValidator*, bool> m_group;

    /** Holds validation result. */
    bool m_fResult;
};


/** QValidator extension,
  * for long number validations. */
class SHARED_LIBRARY_STUFF QIULongValidator : public QValidator
{
public:

    /** Constructs long validator passing @a pParent to the base-class. */
    QIULongValidator(QObject *pParent)
        : QValidator(pParent)
        , m_uBottom(0), m_uTop(ULONG_MAX)
    {}

    /** Constructs long validator passing @a pParent to the base-class.
      * @param  uMinimum  Holds the minimum valid border.
      * @param  uMaximum  Holds the maximum valid border. */
    QIULongValidator(ulong uMinimum, ulong uMaximum,
                     QObject *pParent)
        : QValidator(pParent)
        , m_uBottom(uMinimum), m_uTop(uMaximum)
    {}

    /** Destructs long validator. */
    virtual ~QIULongValidator() {}

    /** Performs validation for @a strInput at @a iPosition. */
    State validate(QString &strInput, int &iPosition) const RT_OVERRIDE RT_FINAL;

    /** Defines @a uBottom. */
    void setBottom(ulong uBottom) { setRange(uBottom, m_uTop); }
    /** Defines @a uTop. */
    void setTop(ulong uTop) { setRange(m_uBottom, uTop); }
    /** Defines range based on passed @a uBottom and @a uTop. */
    void setRange(ulong uBottom, ulong uTop) { m_uBottom = uBottom; m_uTop = uTop; }
    /** Returns bottom. */
    ulong bottom() const { return m_uBottom; }
    /** Returns top. */
    ulong top() const { return m_uTop; }

private:

    /** Holds the bottom. */
    ulong m_uBottom;
    /** Holds the top. */
    ulong m_uTop;
};


#endif /* !FEQT_INCLUDED_SRC_extensions_QIWidgetValidator_h */
