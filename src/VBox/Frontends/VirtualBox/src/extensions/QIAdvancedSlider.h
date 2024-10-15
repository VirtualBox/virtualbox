/* $Id: QIAdvancedSlider.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIAdvancedSlider class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIAdvancedSlider_h
#define FEQT_INCLUDED_SRC_extensions_QIAdvancedSlider_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class UIPrivateSlider;

/** QWidget extension providing GUI with advanced QSlider functionality. */
class SHARED_LIBRARY_STUFF QIAdvancedSlider : public QWidget
{
    Q_OBJECT;
    Q_PROPERTY(int value READ value WRITE setValue);

signals:

    /** Notifies about value changed to @a iValue. */
    void valueChanged(int iValue);

    /** Notifies about slider moved to @a iValue. */
    void sliderMoved(int iValue);

    /** Notifies about slider pressed. */
    void sliderPressed();
    /** Notifies about slider released. */
    void sliderReleased();

public:

    /** Constructs advanced-slider passing @a pParent to the base-class. */
    QIAdvancedSlider(QWidget *pParent = 0);
    /** Constructs advanced-slider passing @a pParent to the base-class.
      * @param  enmOrientation  Brings the slider orientation. */
    QIAdvancedSlider(Qt::Orientation enmOrientation, QWidget *pParent = 0);

    /** Returns the slider value. */
    int value() const;

    /** Defines the slider range to be from @a iMin to @a iMax. */
    void setRange(int iMin, int iMax);

    /** Defines the slider @a iMaximum. */
    void setMaximum(int iMaximum);
    /** Returns the slider maximum. */
    int maximum() const;

    /** Defines the slider @a iMinimum. */
    void setMinimum(int iMinimum);
    /** Returns the slider minimum. */
    int minimum() const;

    /** Defines the slider @a iPageStep. */
    void setPageStep(int iPageStep);
    /** Returns the slider page step. */
    int pageStep() const;

    /** Defines the slider @a iSingleStep. */
    void setSingleStep(int val);
    /** Returns the slider single step. */
    int singelStep() const;

    /** Defines the slider @a iTickInterval. */
    void setTickInterval(int val);
    /** Returns the slider tick interval. */
    int tickInterval() const;

    /** Returns the slider orientation. */
    Qt::Orientation orientation() const;

    /** Defines whether snapping is @a fEnabled. */
    void setSnappingEnabled(bool fEnabled);
    /** Returns whether snapping is enabled. */
    bool isSnappingEnabled() const;

    /** Defines the optimal hint to be from @a iMin to @a iMax. */
    void setOptimalHint(int iMin, int iMax);
    /** Defines the warning hint to be from @a iMin to @a iMax. */
    void setWarningHint(int iMin, int iMax);
    /** Defines the error hint to be from @a iMin to @a iMax. */
    void setErrorHint(int iMin, int iMax);

    /** Defines slider @a strToolTip. */
    void setToolTip(const QString &strToolTip);

public slots:

    /** Defines the slider @a enmOrientation. */
    void setOrientation(Qt::Orientation enmOrientation);

    /** Defines current slider @a iValue. */
    void setValue(int iValue);

private slots:

    /** Handles the slider move to @a iValue. */
    void sltSliderMoved(int iValue);

private:

    /** Prepares all. */
    void prepare(Qt::Orientation fOrientation = Qt::Horizontal);

    /** Returns snapped value for passed @a iValue. */
    int snapValue(int iValue);

    /** Holds the private QSlider instance. */
    UIPrivateSlider *m_pSlider;
    /** Holds the whether slider snapping is enabled. */
    bool             m_fSnappingEnabled;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIAdvancedSlider_h */
