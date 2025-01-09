/* $Id: UIMotherboardFeaturesEditor.h 107665 2025-01-09 15:48:07Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMotherboardFeaturesEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIMotherboardFeaturesEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIMotherboardFeaturesEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* Forward declarations: */
class QCheckBox;
class QGridLayout;
class QLabel;
class QPushButton;

/** UIEditor sub-class used as motherboard features editor. */
class SHARED_LIBRARY_STUFF UIMotherboardFeaturesEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about IO APIC change. */
    void sigChangedIoApic();
    /** Notifies listeners about UTC time change. */
    void sigChangedUtcTime();
    /** Notifies listeners about EFI change. */
    void sigChangedEfi();
    /** Notifies listeners about secure boot change. */
    void sigChangedSecureBoot();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIMotherboardFeaturesEditor(QWidget *pParent = 0);

    /** Defines whether 'enable IO APIC' feature in @a fOn. */
    void setEnableIoApic(bool fOn);
    /** Returns 'enable IO APIC' feature value. */
    bool isEnabledIoApic() const;

    /** Defines whether 'enable UTC time' feature in @a fOn. */
    void setEnableUtcTime(bool fOn);
    /** Returns 'enable UTC time' feature value. */
    bool isEnabledUtcTime() const;

    /** Defines whether 'enable EFI' feature in @a fOn. */
    void setEnableEfi(bool fOn);
    /** Returns 'enable EFI' feature value. */
    bool isEnabledEfi() const;

    /** Defines whether 'enable secure boot' feature in @a fOn. */
    void setEnableSecureBoot(bool fOn);
    /** Returns 'enable secure boot' feature value. */
    bool isEnabledSecureBoot() const;

    /** Returns 'reset to default secure boot keys' feature value. */
    bool isResetSecureBoot() const;

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

protected:

    /** Handles filter change. */
    virtual void handleFilterChange() RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** Handles 'enable EFI' feature being toggled. */
    void sltHandleEnableEfiToggling();
    /** Handles 'enable secure boot' feature being toggled. */
    void sltHandleEnableSecureBootToggling();

    /** Marks corresponding push button activated. */
    void sltResetSecureBoot();

private:

    /** Prepares all. */
    void prepare();
    /** Rebuilds layout. */
    void rebuildLayout();

    /** @name Values
     * @{ */
        /** Holds the 'enable IO APIC' feature value. */
        bool  m_fEnableIoApic;
        /** Holds the 'enable UTC time' feature value. */
        bool  m_fEnableUtcTime;
        /** Holds the 'enable EFI' feature value. */
        bool  m_fEnableEfi;
        /** Holds the 'enable secure boot' feature value. */
        bool  m_fEnableSecureBoot;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the main layout instance. */
        QGridLayout  *m_pLayout;
        /** Holds the label instance. */
        QLabel       *m_pLabel;
        /** Holds the 'enable IO APIC' check-box instance. */
        QCheckBox    *m_pCheckBoxEnableIoApic;
        /** Holds the 'enable UTC time' check-box instance. */
        QCheckBox    *m_pCheckBoxEnableUtcTime;
        /** Holds the 'enable EFI' check-box instance. */
        QCheckBox    *m_pCheckBoxEnableEfi;
        /** Holds the 'secure boot' check-box instance. */
        QCheckBox    *m_pCheckBoxEnableSecureBoot;
        /** Holds the 'secure boot' tool-button instance. */
        QPushButton  *m_pPushButtonResetSecureBoot;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIMotherboardFeaturesEditor_h */
