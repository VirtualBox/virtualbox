/* $Id: UIScaleFactorEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIScaleFactorEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIScaleFactorEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIScaleFactorEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* Forward declarations: */
class QComboBox;
class QGridLayout;
class QLabel;
class QSpinBox;
class QIAdvancedSlider;

/** UIEditor sub-class providing GUI with monitor scale factor editing functionality.
  * It includes a combo box to select a monitor, a slider, and a spinbox to display/modify values.
  * The first item in the combo box is used to change the scale factor of all monitors. */
class SHARED_LIBRARY_STUFF UIScaleFactorEditor : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIScaleFactorEditor(QWidget *pParent);

    /** Defines @a iMonitorCount. */
    void setMonitorCount(int iMonitorCount);
    /** Defines a list of guest-screen @a scaleFactors. */
    void setScaleFactors(const QList<double> &scaleFactors);

    /** Returns either a single global scale factor or a list of scale factor for each monitor. */
    QList<double> scaleFactors() const;

    /** Defines @a dDefaultScaleFactor. */
    void setDefaultScaleFactor(double dDefaultScaleFactor);

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** @name Internal slots handling respective widget's value update.
      * @{ */
        void sltScaleSpinBoxValueChanged(int iValue);
        void sltScaleSliderValueChanged(int iValue);
        void sltMonitorComboIndexChanged(int iIndex);
    /** @} */

private:

    /** Prepares all. */
    void prepare();
    /** Prepare min/max values of related widgets wrt. device pixel ratio(s). */
    void prepareScaleFactorMinMaxValues();

    /** Defines whether scale factor is @a fGlobal one. */
    void setIsGlobalScaleFactor(bool fGlobal);
    /** Defines @a iScaleFactor for certain @a iMonitorIndex. */
    void setScaleFactor(int iMonitorIndex, int iScaleFactor);
    /** Defines slider's @a iValue. */
    void setSliderValue(int iValue);
    /** Defines spinbox's @a iValue. */
    void setSpinBoxValue(int iValue);

    /** Sets the spinbox and slider to scale factor of currently selected monitor. */
    void updateValuesAfterMonitorChange();

    /** @name Member widgets.
      * @{ */
        QGridLayout      *m_pLayout;
        QLabel           *m_pLabel;
        QComboBox        *m_pMonitorComboBox;
        QIAdvancedSlider *m_pScaleSlider;
        QSpinBox         *m_pScaleSpinBox;
        QLabel           *m_pMinScaleLabel;
        QLabel           *m_pMaxScaleLabel;
    /** @} */

    /** Holds the per-monitor scale factors. The 0th item is for all monitors (global). */
    QList<double>  m_scaleFactors;
    /** Holds the default scale factor. */
    double         m_dDefaultScaleFactor;
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIScaleFactorEditor_h */
