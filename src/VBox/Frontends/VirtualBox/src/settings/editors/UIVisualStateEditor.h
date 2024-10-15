/* $Id: UIVisualStateEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVisualStateEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIVisualStateEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIVisualStateEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUuid>

/* GUI includes: */
#include "UIEditor.h"
#include "UIExtraDataDefs.h"

/* Forward declarations: */
class QComboBox;
class QGridLayout;
class QLabel;

/** UIEditor sub-class used as a visual state editor. */
class SHARED_LIBRARY_STUFF UIVisualStateEditor : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIVisualStateEditor(QWidget *pParent = 0);

    /** Defines editor @a uMachineId. */
    void setMachineId(const QUuid &uMachineId);

    /** Defines editor @a enmValue. */
    void setValue(UIVisualStateType enmValue);
    /** Returns editor value. */
    UIVisualStateType value() const;

    /** Returns the vector of supported values. */
    QVector<UIVisualStateType> supportedValues() const { return m_supportedValues; }

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();
    /** Populates combo. */
    void populateCombo();

    /** Holds the machine id. */
    QUuid  m_uMachineId;

    /** Holds the value to be selected. */
    UIVisualStateType  m_enmValue;

    /** Holds the vector of supported values. */
    QVector<UIVisualStateType>  m_supportedValues;

    /** Holds the main layout instance. */
    QGridLayout *m_pLayout;
    /** Holds the label instance. */
    QLabel      *m_pLabel;
    /** Holds the combo instance. */
    QComboBox   *m_pCombo;
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIVisualStateEditor_h */
