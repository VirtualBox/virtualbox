/* $Id: UIExecutionCapEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIExecutionCapEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIExecutionCapEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIExecutionCapEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* Forward declarations: */
class QGridLayout;
class QLabel;
class QSpinBox;
class QIAdvancedSlider;

/** UIEditor sub-class used as a execution cap editor. */
class SHARED_LIBRARY_STUFF UIExecutionCapEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about value changed. */
    void sigValueChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIExecutionCapEditor(QWidget *pParent = 0);

    /** Returns the medium execution cap. */
    int medExecCap() const;

    /** Defines editor @a iValue. */
    void setValue(int iValue);
    /** Returns editor value. */
    int value() const;

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;
    /** Handles slider value changes. */
    void sltHandleSliderChange();
    /** Handles spin-box value changes. */
    void sltHandleSpinBoxChange();

private:

    /** Prepares all. */
    void prepare();

    /** @name Options
     * @{ */
        /** Holds the minimum execution cap. */
        uint  m_uMinExecCap;
        /** Holds the medium execution cap. */
        uint  m_uMedExecCap;
        /** Holds the maximum execution cap. */
        uint  m_uMaxExecCap;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the main layout instance. */
        QGridLayout      *m_pLayout;
        /** Holds the main label instance. */
        QLabel           *m_pLabelExecCap;
        /** Holds the slider instance. */
        QIAdvancedSlider *m_pSlider;
        /** Holds the spinbox instance. */
        QSpinBox         *m_pSpinBox;
        /** Holds the minimum label instance. */
        QLabel           *m_pLabelExecCapMin;
        /** Holds the maximum label instance. */
        QLabel           *m_pLabelExecCapMax;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIExecutionCapEditor_h */
