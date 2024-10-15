/* $Id: UIMiniToolbarSettingsEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMiniToolbarSettingsEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIMiniToolbarSettingsEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIMiniToolbarSettingsEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* Forward declarations: */
class QCheckBox;
class QGridLayout;
class QLabel;

/** UIEditor sub-class used as mini-toolbar editor. */
class SHARED_LIBRARY_STUFF UIMiniToolbarSettingsEditor : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIMiniToolbarSettingsEditor(QWidget *pParent = 0);

    /** Defines whether 'show mini-toolbar' feature in @a fOn. */
    void setShowMiniToolbar(bool fOn);
    /** Returns 'show mini-toolbar' feature value. */
    bool showMiniToolbar() const;

    /** Defines whether 'mini-toolbar at top' feature in @a fOn. */
    void setMiniToolbarAtTop(bool fOn);
    /** Returns 'mini-toolbar at top' feature value. */
    bool miniToolbarAtTop() const;

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

    /** @name Values
     * @{ */
        /** Holds the 'show mini-toolbar' feature value. */
        bool  m_fShowMiniToolbar;
        /** Holds the 'mini-toolbar at top' feature value. */
        bool  m_fMiniToolbarAtTop;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the main layout instance. */
        QGridLayout *m_pLayout;
        /** Holds the label instance. */
        QLabel      *m_pLabel;
        /** Holds the 'show mini-toolbar' check-box instance. */
        QCheckBox   *m_pCheckBoxShowMiniToolBar;
        /** Holds the 'mini-toolbar alignment' check-box instance. */
        QCheckBox   *m_pCheckBoxMiniToolBarAtTop;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIMiniToolbarSettingsEditor_h */
