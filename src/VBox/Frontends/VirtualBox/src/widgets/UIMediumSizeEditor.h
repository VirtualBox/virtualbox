/* $Id: UIMediumSizeEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumSizeEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIMediumSizeEditor_h
#define FEQT_INCLUDED_SRC_widgets_UIMediumSizeEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QRegularExpression>
#include <QSlider>
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;
class QString;
class QWidget;
class QILineEdit;

/** QSlider sub-class used as logarithmic-scaled medium size editor. */
class UIMediumSizeSlider : public QSlider
{
    Q_OBJECT;

signals:

    /** Notifies about scaled @a uValue change. */
    void sigScaledValueChanged(qulonglong uValue);

public:

    /** Constructs medium size slider passing @a pParent to the base-class.
      * @param  uSizeMax  Brings the maximum value to adjust slider scaling accordingly. */
    UIMediumSizeSlider(qulonglong uSizeMax, QWidget *pParent = 0);

    /** Defines scaled @a uMinimum. */
    void setScaledMinimum(qulonglong uMinimum);
    /** Defines scaled @a uMaximum. */
    void setScaledMaximum(qulonglong uMaximum);
    /** Defines scaled @a uValue. */
    void setScaledValue(qulonglong uValue);

    /** Returns scaled value in readable format. */
    QString scaledValueToString();

private slots:

    /** Handles the signal about @a iValue change. */
    void sltValueChanged(int iValue);

private:

    /** Calculates slider scale according to passed @a uMaximumMediumSize. */
    static int calculateSliderScale(qulonglong uMaximumMediumSize);
    /** Returns log2 for passed @a uValue. */
    static int log2i(qulonglong uValue);

    /** Converts passed bytes @a uValue to slides scaled value using @a iSliderScale. */
    static int sizeMBToSlider(qulonglong uValue, int iSliderScale);
    /** Converts passed slider @a uValue to bytes unscaled value using @a iSliderScale. */
    static qulonglong sliderToSizeMB(int uValue, int iSliderScale);

    /** Holds the slider scale. */
    const int  m_iSliderScale;

    /** Holds the scaled minimum. */
    qulonglong  m_uScaledMinimum;
    /** Holds the scaled maximum. */
    qulonglong  m_uScaledMaximum;
    /** Holds the scaled value. */
    qulonglong  m_uScaledValue;
};

/** Medium size editor widget. */
class SHARED_LIBRARY_STUFF UIMediumSizeEditor : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about medium size changed. */
    void sigSizeChanged(qulonglong uSize);

public:

    /* Holds the block size. We force m_uSize to be multiple of this number. */
    static const qulonglong  s_uSectorSize;

    /** Constructs medium size editor passing @a pParent to the base-class. */
    UIMediumSizeEditor(QWidget *pParent = 0, qulonglong uMinimumSize = _4M);

    /** Returns the medium size. */
    qulonglong mediumSize() const { return m_uSize; }
    /** Sets the initial medium size as the widget is created. */
    void setMediumSize(qulonglong uSize);

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

    /** Handles size slider change. */
    void sltSizeSliderChanged(qulonglong uValue);
    /** Handles size editor text edit finished signal. */
    void sltSizeEditorTextChanged();

private:

    /** Prepares all. */
    void prepare();

    /** Updates slider/editor tool-tips. */
    void updateSizeToolTips(qulonglong uSize);
    /** Checks if the uSize is divisible by s_uSectorSize */
    qulonglong checkSectorSizeAlignment(qulonglong uSize);
    /** Ensures there is only proper size-suffix available. */
    QString ensureSizeSuffix(const QString &strSizeString);

    /** Holds the minimum medium size. */
    const qulonglong  m_uSizeMin;
    /** Holds the maximum medium size. */
    const qulonglong  m_uSizeMax;
    /** Holds the current medium size. */
    qulonglong        m_uSize;
    /** Holds the size suffix. */
    QString           m_strSizeSuffix;

    /** Holds the size slider. */
    UIMediumSizeSlider *m_pSlider;
    /** Holds the minimum size label. */
    QLabel             *m_pLabelMinSize;
    /** Holds the maximum size label. */
    QLabel             *m_pLabelMaxSize;
    /** Holds the size editor. */
    QILineEdit         *m_pEditor;

    /* A regular expression used to remove any character from a QString which is neither a digit nor decimal separator. */
    QRegularExpression m_regExNonDigitOrSeparator;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIMediumSizeEditor_h */
