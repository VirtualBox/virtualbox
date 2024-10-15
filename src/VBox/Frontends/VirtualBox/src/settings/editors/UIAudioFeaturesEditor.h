/* $Id: UIAudioFeaturesEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIAudioFeaturesEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIAudioFeaturesEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIAudioFeaturesEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* Forward declarations: */
class QCheckBox;
class QGridLayout;
class QLabel;

/** UIEditor sub-class used as audio features editor. */
class SHARED_LIBRARY_STUFF UIAudioFeaturesEditor : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIAudioFeaturesEditor(QWidget *pParent = 0);

    /** Defines whether 'enable output' feature in @a fOn. */
    void setEnableOutput(bool fOn);
    /** Returns 'enable output' feature value. */
    bool outputEnabled() const;

    /** Defines whether 'enable input' feature in @a fOn. */
    void setEnableInput(bool fOn);
    /** Returns 'enable input' feature value. */
    bool inputEnabled() const;

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE;

private:

    /** Prepares all. */
    void prepare();

    /** @name Values
     * @{ */
        /** Holds the 'enable output' feature value. */
        bool  m_fEnableOutput;
        /** Holds the 'enable input' feature value. */
        bool  m_fEnableInput;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the main layout instance. */
        QGridLayout *m_pLayout;
        /** Holds the label instance. */
        QLabel      *m_pLabel;
        /** Holds the 'enable output' check-box instance. */
        QCheckBox   *m_pCheckBoxEnableOutput;
        /** Holds the 'enable input' check-box instance. */
        QCheckBox   *m_pCheckBoxEnableInput;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIAudioFeaturesEditor_h */
