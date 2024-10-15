/* $Id: UIDisplayScreenFeaturesEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDisplayScreenFeaturesEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIDisplayScreenFeaturesEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIDisplayScreenFeaturesEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* Forward declarations: */
class QCheckBox;
class QGridLayout;
class QLabel;

/** UIEditor sub-class used as machine display screen features editor. */
class SHARED_LIBRARY_STUFF UIDisplayScreenFeaturesEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about 'enable 3D acceleration' feature status changed. */
    void sig3DAccelerationFeatureStatusChange();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIDisplayScreenFeaturesEditor(QWidget *pParent = 0);

    /** Defines whether 'enable 3D acceleration' feature in @a fOn. */
    void setEnable3DAcceleration(bool fOn);
    /** Returns 'enable 3D acceleration' feature value. */
    bool isEnabled3DAcceleration() const;

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
        /** Holds the 'enable 3D acceleration' feature value. */
        bool  m_fEnable3DAcceleration;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the main layout instance. */
        QGridLayout *m_pLayout;
        /** Holds the label instance. */
        QLabel      *m_pLabel;
        /** Holds the 'enable 3D acceleration' check-box instance. */
        QCheckBox   *m_pCheckBoxEnable3DAcceleration;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIDisplayScreenFeaturesEditor_h */
