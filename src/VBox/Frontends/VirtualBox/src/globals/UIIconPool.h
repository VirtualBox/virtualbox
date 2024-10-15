/* $Id: UIIconPool.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIIconPool class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIIconPool_h
#define FEQT_INCLUDED_SRC_globals_UIIconPool_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QFileIconProvider>
#include <QIcon>
#include <QPixmap>
#include <QHash>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class CMachine;

/** Interface which provides GUI with static API
  * allowing to dynamically compose icons at runtime. */
class SHARED_LIBRARY_STUFF UIIconPool
{
public:

    /** Default icon types. */
    enum UIDefaultIconType
    {
        /* Message-box related stuff: */
        UIDefaultIconType_MessageBoxInformation,
        UIDefaultIconType_MessageBoxQuestion,
        UIDefaultIconType_MessageBoxWarning,
        UIDefaultIconType_MessageBoxCritical,
        /* Dialog related stuff: */
        UIDefaultIconType_DialogCancel,
        UIDefaultIconType_DialogHelp,
        UIDefaultIconType_ArrowBack,
        UIDefaultIconType_ArrowForward
    };

    /** Creates pixmap from passed pixmap @a strName. */
    static QPixmap pixmap(const QString &strName);

    /** Creates icon from passed pixmap names for
      * @a strNormal, @a strDisabled and @a strActive icon states. */
    static QIcon iconSet(const QString &strNormal,
                         const QString &strDisabled = QString(),
                         const QString &strActive = QString());

    /** Creates icon from passed pixmap names for
      * @a strNormal, @a strDisabled, @a strActive icon states and
      * their analogs for toggled-off case. Used for toggle actions. */
    static QIcon iconSetOnOff(const QString &strNormal, const QString strNormalOff,
                              const QString &strDisabled = QString(), const QString &strDisabledOff = QString(),
                              const QString &strActive = QString(), const QString &strActiveOff = QString());

    /** Creates icon from passed pixmap names for
      * @a strNormal, @a strDisabled, @a strActive icon states and
      * their analogs for small-icon case. Used for setting pages. */
    static QIcon iconSetFull(const QString &strNormal, const QString &strSmall,
                             const QString &strNormalDisabled = QString(), const QString &strSmallDisabled = QString(),
                             const QString &strNormalActive = QString(), const QString &strSmallActive = QString());

    /** Creates icon from passed pixmaps for
      * @a normal, @a disabled and @a active icon states. */
    static QIcon iconSet(const QPixmap &normal,
                         const QPixmap &disabled = QPixmap(),
                         const QPixmap &active = QPixmap());

    /** Creates icon of passed @a defaultIconType
      * based on passed @a pWidget style (if any) or application style (otherwise). */
    static QIcon defaultIcon(UIDefaultIconType defaultIconType, const QWidget *pWidget = 0);

    /** Joins two pixmaps horizontally with 2px space between them and returns the result. */
    static QPixmap joinPixmaps(const QPixmap &pixmap1, const QPixmap &pixmap2);

protected:

    /** Constructs icon-pool.
      * Doesn't mean to be used directly,
      * cause this class is a bunch of statics. */
    UIIconPool() {}

    /** Destructs icon-pool. */
    virtual ~UIIconPool() {}

private:

    /** Adds resource named @a strName to passed @a icon
      * for @a mode (QIcon::Normal by default) and @a state (QIcon::Off by default). */
    static void addName(QIcon &icon, const QString &strName,
                        QIcon::Mode mode = QIcon::Normal, QIcon::State state = QIcon::Off);
};

/** UIIconPool interface extension used as general GUI icon-pool.
  * Provides GUI with guest OS types pixmap cache. */
class SHARED_LIBRARY_STUFF UIIconPoolGeneral : public UIIconPool
{
public:

    /** Creates singleton instance. */
    static void create();
    /** Destroys singleton instance. */
    static void destroy();
    /** Returns singleton instance. */
    static UIIconPoolGeneral *instance();

    /** Returns icon defined for a passed @a comMachine. */
    QIcon userMachineIcon(const CMachine &comMachine) const;
    /** Returns pixmap of a passed @a size defined for a passed @a comMachine. */
    QPixmap userMachinePixmap(const CMachine &comMachine, const QSize &size) const;
    /** Returns pixmap defined for a passed @a comMachine.
      * In case if non-null @a pLogicalSize pointer provided, it will be updated properly. */
    QPixmap userMachinePixmapDefault(const CMachine &comMachine, QSize *pLogicalSize = 0) const;

    /** Returns icon corresponding to passed @a strOSTypeID. */
    QIcon guestOSTypeIcon(const QString &strOSTypeID) const;
    /** Returns pixmap corresponding to passed @a strOSTypeID and @a size. */
    QPixmap guestOSTypePixmap(const QString &strOSTypeID, const QSize &size) const;
    /** Returns pixmap corresponding to passed @a strOSTypeID.
      * In case if non-null @a pLogicalSize pointer provided, it will be updated properly. */
    QPixmap guestOSTypePixmapDefault(const QString &strOSTypeID, QSize *pLogicalSize = 0) const;

    /** Returns default system icon of certain @a enmType. */
    QIcon defaultSystemIcon(QFileIconProvider::IconType enmType) { return m_fileIconProvider.icon(enmType); }
    /** Returns file icon fetched from passed file @a info. */
    QIcon defaultFileIcon(const QFileInfo &info) { return m_fileIconProvider.icon(info); }

    /** Returns cached default warning pixmap. */
    QPixmap warningIcon() const { return m_pixWarning; }
    /** Returns cached default error pixmap. */
    QPixmap errorIcon() const { return m_pixError; }

private:

    /** Creates icon from passed pixmap names for
      * @a strNormal, @a strDisabled and @a strActive icon states. Utilizes addNameAndOverlay to overlay pixmaps. */
    static QIcon overlayedIconSet(const QString &strGuestOSTypeId,
                                  const QString &strNormal,
                                  const QString &strDisabled = QString(),
                                  const QString &strActive = QString());
    /* Just like UIIconPool::addName add resources to icon but additionally overlays corresponding text wrt. strGuestOSTypeId. */
    static void addNameAndOverlay(QIcon &icon, const QString &strName, const QString &strGuestOSTypeId,
                        QIcon::Mode mode = QIcon::Normal, QIcon::State state = QIcon::Off);

    /** Constructs general icon-pool. */
    UIIconPoolGeneral();
    /** Destructs general icon-pool. */
    virtual ~UIIconPoolGeneral() /* override final */;
    /** Overlay text @p strArch on top of @p pixmap. */
    static void overlayArchitectureTextOnPixmap(const QString &strArch, QPixmap &pixmap);
    /** Returns the architecture text we overlay on guest OS type id icon.*/
    static QString determineOSArchString(const QString &strOSTypeId);
    /** Holds the singleton instance. */
    static UIIconPoolGeneral *s_pInstance;

    /** Holds the global file icon provider instance. */
    QFileIconProvider  m_fileIconProvider;

    /** Guest OS type icon-names cache. */
    QHash<QString, QString>        m_guestOSTypeIconNames;
    /** Guest OS type icons cache. */
    mutable QHash<QString, QIcon>  m_guestOSTypeIcons;

    /** Holds the warning pixmap. */
    QPixmap  m_pixWarning;
    /** Holds the error pixmap. */
    QPixmap  m_pixError;

    /** Allows for shortcut access. */
    friend UIIconPoolGeneral &generalIconPool();
};

/** Singleton UIIconPoolGeneral 'official' name. */
inline UIIconPoolGeneral &generalIconPool() { return *UIIconPoolGeneral::instance(); }

#endif /* !FEQT_INCLUDED_SRC_globals_UIIconPool_h */
