/* $Id: UIMaximumGuestScreenSizeEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMaximumGuestScreenSizeEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIMaximumGuestScreenSizeEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIMaximumGuestScreenSizeEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"
#include "UIExtraDataDefs.h"

/* Forward declarations: */
class QGridLayout;
class QLabel;
class QSpinBox;
class QComboBox;


/** Maximum guest screen size value. */
struct SHARED_LIBRARY_STUFF UIMaximumGuestScreenSizeValue
{
    /** Constructs maximum guest screen size value. */
    UIMaximumGuestScreenSizeValue(MaximumGuestScreenSizePolicy enmPolicy = MaximumGuestScreenSizePolicy_Any,
                                  const QSize &resolution = QSize());

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIMaximumGuestScreenSizeValue &other) const;
    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIMaximumGuestScreenSizeValue &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIMaximumGuestScreenSizeValue &other) const { return !equal(other); }

    /** Holds the maximum guest-screen policy. */
    MaximumGuestScreenSizePolicy  m_enmPolicy;
    /** Holds the maximum guest-screen size. */
    QSize                         m_size;
};
Q_DECLARE_METATYPE(UIMaximumGuestScreenSizeValue);


/** UIEditor sub-class used as a maximum guest screen size editor. */
class SHARED_LIBRARY_STUFF UIMaximumGuestScreenSizeEditor : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIMaximumGuestScreenSizeEditor(QWidget *pParent = 0);

    /** Defines editor @a guiValue. */
    void setValue(const UIMaximumGuestScreenSizeValue &guiValue);
    /** Returns editor value. */
    UIMaximumGuestScreenSizeValue value() const;

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

private slots:

    /** Handles current policy index change. */
    void sltHandleCurrentPolicyIndexChanged();
    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();
    /** Populates combo. */
    void populateCombo();

    /** Holds the value to be selected. */
    UIMaximumGuestScreenSizeValue  m_guiValue;

    /** @name Widgets
     * @{ */
        /** Holds the main layout: */
        QGridLayout *m_pLayout;
        /** Holds the policy label instance. */
        QLabel      *m_pLabelPolicy;
        /** Holds the policy combo instance. */
        QComboBox   *m_pComboPolicy;
        /** Holds the max width label instance. */
        QLabel      *m_pLabelMaxWidth;
        /** Holds the max width spinbox instance. */
        QSpinBox    *m_pSpinboxMaxWidth;
        /** Holds the max height label instance. */
        QLabel      *m_pLabelMaxHeight;
        /** Holds the max height spinbox instance. */
        QSpinBox    *m_pSpinboxMaxHeight;
    /** @} */
};


#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIMaximumGuestScreenSizeEditor_h */
