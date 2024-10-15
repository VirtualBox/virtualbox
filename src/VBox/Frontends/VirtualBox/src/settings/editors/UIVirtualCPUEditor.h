/* $Id: UIVirtualCPUEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualCPUEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIVirtualCPUEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIVirtualCPUEditor_h
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

/** UIEditor sub-class used as a virtual CPU editor. */
class SHARED_LIBRARY_STUFF UIVirtualCPUEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a iValue changed. */
    void sigValueChanged(int iValue);

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIVirtualCPUEditor(QWidget *pParent = 0);

    /** Returns the maximum virtual CPU count. */
    int maxVCPUCount() const;

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
        /** Holds the minimum virtual CPU count. */
        uint  m_uMinVCPUCount;
        /** Holds the maximum virtual CPU count. */
        uint  m_uMaxVCPUCount;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the main layout instance. */
        QGridLayout      *m_pLayout;
        /** Holds the main label instance. */
        QLabel           *m_pLabelVCPU;
        /** Holds the slider instance. */
        QIAdvancedSlider *m_pSlider;
        /** Holds the spinbox instance. */
        QSpinBox         *m_pSpinBox;
        /** Holds the minimum label instance. */
        QLabel           *m_pLabelVCPUMin;
        /** Holds the maximum label instance. */
        QLabel           *m_pLabelVCPUMax;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIVirtualCPUEditor_h */
