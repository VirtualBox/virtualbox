/* $Id: UIUSBControllerEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUSBControllerEditor class declaration.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIUSBControllerEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIUSBControllerEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* COM includes: */
#include "KUSBControllerType.h"

/* Forward declarations: */
class QRadioButton;

/** UIEditor sub-class used as a USB controller editor. */
class SHARED_LIBRARY_STUFF UIUSBControllerEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about value change. */
    void sigValueChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIUSBControllerEditor(QWidget *pParent = 0);

    /** Defines editor @a enmValue. */
    void setValue(KUSBControllerType enmValue);
    /** Returns editor value. */
    KUSBControllerType value() const;

    /** Returns the vector of supported values. */
    QVector<KUSBControllerType> supportedValues() const { return m_supportedValues; }

protected:

    /** Handles filter change. */
    virtual void handleFilterChange() RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();

    /** Updates button set. */
    void updateButtonSet();

    /** Holds the value to be selected. */
    KUSBControllerType  m_enmValue;

    /** Holds the vector of supported values. */
    QVector<KUSBControllerType>  m_supportedValues;

    /** Holds the USB1 radio-button instance. */
    QRadioButton *m_pRadioButtonUSB1;
    /** Holds the USB2 radio-button instance. */
    QRadioButton *m_pRadioButtonUSB2;
    /** Holds the USB3 radio-button instance. */
    QRadioButton *m_pRadioButtonUSB3;
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIUSBControllerEditor_h */
