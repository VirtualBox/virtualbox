/* $Id: UISerialSettingsEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISerialSettingsEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UISerialSettingsEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UISerialSettingsEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* COM includes: */
#include "KPortMode.h"

/* Forward declarations: */
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

/** UIEditor sub-class used as a serial settings editor. */
class SHARED_LIBRARY_STUFF UISerialSettingsEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about port availability changed. */
    void sigPortAvailabilityChanged();

    /** Notifies listeners about standard port option changed. */
    void sigStandardPortOptionChanged();

    /** Notifies listeners about port IRQ changed. */
    void sigPortIRQChanged();
    /** Notifies listeners about port IO address changed. */
    void sigPortIOAddressChanged();

    /** Notifies listeners about mode changed. */
    void sigModeChanged();
    /** Notifies listeners about path changed. */
    void sigPathChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UISerialSettingsEditor(QWidget *pParent = 0);

    /** Defines whether port options @a fAvailable. */
    void setPortOptionsAvailable(bool fAvailable);
    /** Defines whether port @a fEnabled. */
    void setPortEnabled(bool fEnabled);
    /** Returns whether port enabled. */
    bool isPortEnabled() const;

    /** Defines whether port IRQ and IO address options @a fAvailable. */
    void setIRQAndIOAddressOptionsAvailable(bool fAvailable);
    /** Sets standard port on the basis of passed uIRQ and uIOAddress. */
    void setPortByIRQAndIOAddress(ulong uIRQ, ulong uIOAddress);
    /** Returns whether current port is standard one. */
    bool isPortStandardOne() const;
    /** Defines port @a uIRQ. */
    void setIRQ(ulong uIRQ);
    /** Returns port IRQ. */
    ulong irq() const;
    /** Defines port @a uIOAddress. */
    void setIOAddress(ulong uIOAddress);
    /** Returns port IO address. */
    ulong ioAddress() const;

    /** Defines whether host mode options @a fAvailable. */
    void setHostModeOptionsAvailable(bool fAvailable);
    /** Defines host @a enmMode. */
    void setHostMode(KPortMode enmMode);
    /** Returns host mode. */
    KPortMode hostMode() const;

    /** Defines whether pipe options @a fAvailable. */
    void setPipeOptionsAvailable(bool fAvailable);
    /** Defines whether server @a fEnabled. */
    void setServerEnabled(bool fEnabled);
    /** Returns whether server enabled. */
    bool isServerEnabled() const;

    /** Defines whether path options @a fAvailable. */
    void setPathOptionsAvailable(bool fAvailable);
    /** Defines @a strPath. */
    void setPath(const QString &strPath);
    /** Returns path. */
    QString path() const;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** Handles port availability being toggled to @a fOn. */
    void sltHandlePortAvailabilityToggled(bool fOn);

    /** Handles standard port @a strOption being activated. */
    void sltHandleStandardPortOptionActivated(const QString &strOption);

    /** Handles mode change to item with certain @a iIndex. */
    void sltHandleModeChange(int iIndex);

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares connections. */
    void prepareConnections();

    /** Populates combo. */
    void populateCombo();

    /** Holds the mode. */
    KPortMode  m_enmPortMode;

    /** @name Widgets
     * @{ */
        /** Holds the port check-box instance. */
        QCheckBox *m_pCheckBoxPort;
        /** Holds the port settings widget instance. */
        QWidget   *m_pWidgetPortSettings;
        /** Holds the number label instance. */
        QLabel    *m_pLabelNumber;
        /** Holds the number combo instance. */
        QComboBox *m_pComboNumber;
        /** Holds the IRQ label instance. */
        QLabel    *m_pLabelIRQ;
        /** Holds the IRQ editor instance. */
        QLineEdit *m_pLineEditIRQ;
        /** Holds the IO address label instance. */
        QLabel    *m_pLabelIOAddress;
        /** Holds the IO address editor instance. */
        QLineEdit *m_pLineEditIOAddress;
        /** Holds the mode label instance. */
        QLabel    *m_pLabelMode;
        /** Holds the mode combo instance. */
        QComboBox *m_pComboMode;
        /** Holds the pipe check-box instance. */
        QCheckBox *m_pCheckBoxPipe;
        /** Holds the path label instance. */
        QLabel    *m_pLabelPath;
        /** Holds the path editor instance. */
        QLineEdit *m_pEditorPath;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UISerialSettingsEditor_h */
