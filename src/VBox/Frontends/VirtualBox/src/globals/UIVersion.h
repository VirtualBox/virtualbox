/* $Id: UIVersion.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVersion class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIVersion_h
#define FEQT_INCLUDED_SRC_globals_UIVersion_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QString>

/* GUI includes: */
#include "UILibraryDefs.h"

/** Represents VirtualBox version wrapper. */
class UIVersion
{
public:

    /** Constructs default object. */
    UIVersion();
    /** Constructs object based on parsed @a strFullVersionInfo. */
    UIVersion(const QString &strFullVersionInfo);

    /** Returns whether this object is valid. */
    bool isValid() const;

    /** Returns whether this object is equal to @a other. */
    bool equal(const UIVersion &other) const;
    /** Checks whether this object is equal to @a other. */
    bool operator==(const UIVersion &other) const { return equal(other); }
    /** Checks whether this object is NOT equal to @a other. */
    bool operator!=(const UIVersion &other) const { return !equal(other); }

    /** Checks whether this object is less than @a other. */
    bool operator<(const UIVersion &other) const;
    /** Checks whether this object is less or equal than @a other. */
    bool operator<=(const UIVersion &other) const;
    /** Checks whether this object is greater than @a other. */
    bool operator>(const UIVersion &other) const;
    /** Checks whether this object is greater or equal than @a other. */
    bool operator>=(const UIVersion &other) const;

    /** Returns object string representation. */
    QString toString() const;

    /** Returns the object X value. */
    int x() const { return m_x; }
    /** Returns the object Y value. */
    int y() const { return m_y; }
    /** Returns the object Z value. */
    int z() const { return m_z; }
    /** Returns the object postfix. */
    QString postfix() const { return m_strPostfix; }

    /** Defines the object @a x value. */
    void setX(int x) { m_x = x; }
    /** Defines the object @a y value. */
    void setY(int y) { m_y = y; }
    /** Defines the object @a z value. */
    void setZ(int z) { m_z = z; }
    /** Defines the object @a strPostfix. */
    void setPostfix(const QString &strPostfix) { m_strPostfix = strPostfix; }

    /** Returns effective released version guessed or hardcoded for this one version.
      * This can be even the version itself. */
    UIVersion effectiveReleasedVersion() const;

private:

    /** Holds the object X value. */
    int  m_x;
    /** Holds the object Y value. */
    int  m_y;
    /** Holds the object Z value. */
    int  m_z;

    /** Holds the object postfix. */
    QString  m_strPostfix;
};

/** Represents VirtualBox version info interface. */
class SHARED_LIBRARY_STUFF UIVersionInfo
{
public:

    /** Returns Qt runtime version string. */
    static QString qtRTVersionString();
    /** Returns Qt runtime version. */
    static uint qtRTVersion();
    /** Returns Qt runtime major version. */
    static uint qtRTMajorVersion();
    /** Returns Qt runtime minor version. */
    static uint qtRTMinorVersion();
    /** Returns Qt runtime revision number. */
    static uint qtRTRevisionNumber();

    /** Returns Qt compiled version string. */
    static QString qtCTVersionString();
    /** Returns Qt compiled version. */
    static uint qtCTVersion();

    /** Returns VBox version string. */
    static QString vboxVersionString();
    /** Returns normalized VBox version string. */
    static QString vboxVersionStringNormalized();
    /** Returns whether VBox version string contains BETA word. */
    static bool isBeta();
    /** Returns whether BETA label should be shown. */
    static bool showBetaLabel();

    /** Returns whether branding is active. */
    static bool brandingIsActive(bool fForce = false);
    /** Returns value for certain branding @a strKey from custom.ini file. */
    static QString brandingGetKey(QString strKey);

private:

    /** Constructs version info: */
    UIVersionInfo();

    /** Holds the VBox branding config file path. */
    static QString  s_strBrandingConfigFilePath;
};

#endif /* !FEQT_INCLUDED_SRC_globals_UIVersion_h */
