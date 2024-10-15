/* $Id: UISettingsWarningPane.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISettingsWarningPane class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_UISettingsWarningPane_h
#define FEQT_INCLUDED_SRC_settings_UISettingsWarningPane_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QHBoxLayout;
class QEvent;
class QLabel;
class QObject;
class QString;
class QTimer;
class QWidget;
class UISettingsPageValidator;

/** QWidget sub-class,
  * used a settings dialog warning-pane. */
class SHARED_LIBRARY_STUFF UISettingsWarningPane : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies about hover enter event.
      * @param  pValidator  Brings the validator reference. */
    void sigHoverEnter(UISettingsPageValidator *pValidator);
    /** Notifies about hover leave event.
      * @param  pValidator  Brings the validator reference. */
    void sigHoverLeave(UISettingsPageValidator *pValidator);

public:

    /** Constructs warning-pane passing @a pParent to the base-class. */
    UISettingsWarningPane(QWidget *pParent = 0);

    /** Defines current warning-label @a strText. */
    void setWarningLabelText(const QString &strText);
    /** Defines whether warning-label should be @a fVisible. */
    void setWarningLabelVisible(bool fVisible);

    /** Registers corresponding @a pValidator. */
    void registerValidator(UISettingsPageValidator *pValidator);

protected:

    /** Preprocesses Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles hover timer timeout. */
    void sltHandleHoverTimer();

private:

    /** Prepares all. */
    void prepare();

    /** Holds the icon layout instance. */
    QHBoxLayout *m_pIconLayout;
    /** Holds the text label instance. */
    QLabel      *m_pTextLabel;

    /** Holds the page validators list. */
    QList<UISettingsPageValidator*>  m_validators;
    /** Holds the page icons list. */
    QList<QLabel*>                   m_icons;
    /** Holds the icons hovered-states list. */
    QList<bool>                      m_hovered;

    /** Holds the hover timer instance. */
    QTimer *m_pHoverTimer;
    /** Holds the hovered icon-label position. */
    int     m_iHoveredIconLabelPosition;
};

#endif /* !FEQT_INCLUDED_SRC_settings_UISettingsWarningPane_h */
