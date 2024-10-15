/* $Id: UISettingsPageValidator.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: UISettingsPageValidator class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_UISettingsPageValidator_h
#define FEQT_INCLUDED_SRC_settings_UISettingsPageValidator_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QPixmap>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QString;
class UISettingsPage;

/** QObject sub-class,
  * used as settings-page validator. */
class SHARED_LIBRARY_STUFF UISettingsPageValidator : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about validity change for @a pValidator. */
    void sigValidityChanged(UISettingsPageValidator *pValidator);

    /** Asks listener to show warning icon. */
    void sigShowWarningIcon();
    /** Asks listener to hide warning icon. */
    void sigHideWarningIcon();

public:

    /** Constructs page validator for a certain @a pPage,
      * passing @a pParent to the base-class. */
    UISettingsPageValidator(QObject *pParent, UISettingsPage *pPage);

    /** Returns page. */
    UISettingsPage *page() const { return m_pPage; }

    /** Returns warning pixmap. */
    QPixmap warningPixmap() const;
    /** Returns internal name. */
    QString internalName() const;

    /** Defines title @a strPrefix. */
    void setTitlePrefix(const QString &strPrefix);

    /** Returns whether validator is valid. */
    bool isValid() const { return m_fIsValid; }
    /** Defines whether validator @a fIsValid. */
    void setValid(bool fIsValid) { m_fIsValid = fIsValid; }

    /** Returns last message. */
    QString lastMessage() const { return m_strLastMessage; }
    /** Defines @a strLastMessage. */
    void setLastMessage(const QString &strLastMessage);

    /** Invalidates validator, notifying listener(s). */
    void invalidate();

    /** Revalidate validator. */
    void revalidate();

private:

    /** Holds the validated page. */
    UISettingsPage *m_pPage;

    /** Holds the title prefix. */
    QString  m_strPrefix;

    /** Holds whether the page is valid. */
    bool  m_fIsValid;

    /** Holds the last message. */
    QString  m_strLastMessage;
};

#endif /* !FEQT_INCLUDED_SRC_settings_UISettingsPageValidator_h */
