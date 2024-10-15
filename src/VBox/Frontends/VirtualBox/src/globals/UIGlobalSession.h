/* $Id: UIGlobalSession.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSession class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIGlobalSession_h
#define FEQT_INCLUDED_SRC_globals_UIGlobalSession_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QReadWriteLock>

/* COM includes: */
#include "CHost.h"
#include "CVirtualBox.h"
#include "CVirtualBoxClient.h"

/* Forward declarations: */
class UIGuestOSTypeManager;

/** QObject subclass wrapping global COM session functionality. */
class SHARED_LIBRARY_STUFF UIGlobalSession : public QObject
{
    Q_OBJECT;

signals:

    /** @name General COM stuff.
     * @{ */
        /** Notifies listeners about the VBoxSVC availability change. */
        void sigVBoxSVCAvailabilityChange(bool fAvailable);
    /** @} */

public:

    /** Returns static UIGlobalSession instance. */
    static UIGlobalSession *instance() { return s_pInstance; }
    /** Creates static UIGlobalSession instance. */
    static void create();
    /** Destroys static UIGlobalSession instance. */
    static void destroy();

    /** Prepares all. */
    bool prepare();
    /** Cleanups all. */
    void cleanup();

    /** @name General COM stuff.
     * @{ */
        /** Try to acquire COM cleanup protection token for reading. */
        bool comTokenTryLockForRead() { return m_comCleanupProtectionToken.tryLockForRead(); }
        /** Unlock previously acquired COM cleanup protection token. */
        void comTokenUnlock() { return m_comCleanupProtectionToken.unlock(); }

        /** Returns the copy of VirtualBox client wrapper. */
        CVirtualBoxClient virtualBoxClient() const { return m_comVBoxClient; }
        /** Returns the copy of VirtualBox object wrapper. */
        CVirtualBox virtualBox() const { return m_comVBox; }
        /** Returns the copy of VirtualBox host-object wrapper. */
        CHost host() const { return m_comHost; }
        /** Returns the symbolic VirtualBox home-folder representation. */
        QString homeFolder() const { return m_strHomeFolder; }

        /** Returns the VBoxSVC availability value. */
        bool isVBoxSVCAvailable() const { return m_fVBoxSVCAvailable; }
    /** @} */

    /** @name Guest OS type stuff.
     * @{ */
        const UIGuestOSTypeManager &guestOSTypeManager();
    /** @} */

    /** @name Recording stuff.
     * @{ */
        /** Returns supported recording features flag. */
        int supportedRecordingFeatures() const;
    /** @} */

protected slots:

    /** @name General COM stuff.
     * @{ */
        /** Handles the VBoxSVC availability change. */
        void sltHandleVBoxSVCAvailabilityChange(bool fAvailable);
    /** @} */

private:

    /** Construcs global COM session object. */
    UIGlobalSession();
    /** Destrucs global COM session object. */
    virtual ~UIGlobalSession() RT_OVERRIDE RT_FINAL;

    /** @name General COM stuff.
     * @{ */
        /** Re-initializes COM wrappers and containers. */
        bool comWrappersReinit();
    /** @} */

    /** Holds the singleton UIGlobalSession instance. */
    static UIGlobalSession *s_pInstance;

    /** @name General COM stuff.
     * @{ */
        /** Holds the COM cleanup protection token. */
        QReadWriteLock  m_comCleanupProtectionToken;

        /** Holds the instance of VirtualBox client wrapper. */
        CVirtualBoxClient  m_comVBoxClient;
        /** Holds the copy of VirtualBox object wrapper. */
        CVirtualBox        m_comVBox;
        /** Holds the copy of VirtualBox host-object wrapper. */
        CHost              m_comHost;
        /** Holds the symbolic VirtualBox home-folder representation. */
        QString            m_strHomeFolder;

        /** Holds whether acquired COM wrappers are currently valid. */
        bool  m_fWrappersValid;
        /** Holds whether VBoxSVC is currently available. */
        bool  m_fVBoxSVCAvailable;
    /** @} */

    /** @name Guest OS type stuff.
     * @{ */
        /** Holds the guest OS type manager instance. */
        UIGuestOSTypeManager *m_pGuestOSTypeManager;
    /** @} */

#ifdef VBOX_WS_WIN
    /** @name ATL stuff.
     * @{ */
        /** Holds the ATL module instance (for use with UIGlobalSession shared library only).
          * @note  Required internally by ATL (constructor records instance in global variable). */
        ATL::CComModule  _Module;
    /** @} */
#endif
};

#define gpGlobalSession UIGlobalSession::instance()

#endif /* !FEQT_INCLUDED_SRC_globals_UIGlobalSession_h */
