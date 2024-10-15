/* $Id: UIFontScaleEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFontScaleEditor class declaration.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIFontScaleEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIFontScaleEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* Forward declarations: */
class QGridLayout;
class QLabel;
class QIAdvancedSlider;
class UIFontScaleFactorSpinBox;

/** UIEditor sub-class providing GUI with monitor scale factor editing functionality.
  * It includes a combo box to select a monitor, a slider, and a spinbox to display/modify values.
  * The first item in the combo box is used to change the scale factor of all monitors. */
class SHARED_LIBRARY_STUFF UIFontScaleEditor : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIFontScaleEditor(QWidget *pParent);

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

    void setFontScaleFactor(int iFontScaleFactor);
    int fontScaleFactor() const;

private slots:

    /** @name Internal slots handling respective widget's value update.
      * @{ */
        void sltScaleSpinBoxValueChanged(int iValue);
        void sltScaleSliderValueChanged(int iValue);
        void sltMonitorComboIndexChanged(int iIndex);
    /** @} */

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();
    /** Prepare min/max values of related widgets wrt. device pixel ratio(s). */
    void prepareScaleFactorMinMax();

    /** Defines slider's @a iValue. */
    void setSliderValue(int iValue);
    /** Defines spinbox's @a iValue. */
    void setSpinBoxValue(int iValue);

    /** @name Member widgets.
      * @{ */
        QGridLayout              *m_pLayout;
        QLabel                   *m_pLabel;
        QIAdvancedSlider         *m_pScaleSlider;
        UIFontScaleFactorSpinBox *m_pScaleSpinBox;
        QLabel                   *m_pMinScaleLabel;
        QLabel                   *m_pMaxScaleLabel;
    /** @} */

    /** Hold the factor by which we divided spinbox's @a range to set slider's range to make slider mouse move stop on ticks. */
    const int m_iSliderRangeDivisor;
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIFontScaleEditor_h */
