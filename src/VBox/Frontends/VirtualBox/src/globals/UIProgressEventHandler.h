/* $Id: UIProgressEventHandler.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIProgressEventHandler class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIProgressEventHandler_h
#define FEQT_INCLUDED_SRC_globals_UIProgressEventHandler_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
 #include "UIMainEventListener.h"

/* COM includes: */
# include "CEventListener.h"
# include "CEventSource.h"
# include "CProgress.h"


/** Private QObject extension
  * providing UIExtraDataManager with the CVirtualBox event-source. */
class SHARED_LIBRARY_STUFF UIProgressEventHandler : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about @a iPercent change for progress with @a uProgressId. */
    void sigProgressPercentageChange(const QUuid &uProgressId, const int iPercent);
    /** Notifies about task complete for progress with @a uProgressId. */
    void sigProgressTaskComplete(const QUuid &uProgressId);
    /** Notifies about handling has finished. */
    void sigHandlingFinished();

public:

    /** Constructs event proxy object on the basis of passed @a pParent. */
    UIProgressEventHandler(QObject *pParent, const CProgress &comProgress);
    /** Destructs event proxy object. */
    virtual ~UIProgressEventHandler() RT_OVERRIDE;

protected:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares listener. */
        void prepareListener();
        /** Prepares connections. */
        void prepareConnections();

        /** Cleanups connections. */
        void cleanupConnections();
        /** Cleanups listener. */
        void cleanupListener();
        /** Cleanups all. */
        void cleanup();
    /** @} */

private:

    /** Holds the progress wrapper. */
    CProgress  m_comProgress;

    /** Holds the Qt event listener instance. */
    ComObjPtr<UIMainEventListenerImpl>  m_pQtListener;
    /** Holds the COM event listener instance. */
    CEventListener                      m_comEventListener;
};


#endif /* !FEQT_INCLUDED_SRC_globals_UIProgressEventHandler_h */
