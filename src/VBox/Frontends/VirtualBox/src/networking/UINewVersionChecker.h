/* $Id: UINewVersionChecker.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINewVersionChecker class declaration.
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

#ifndef FEQT_INCLUDED_SRC_networking_UINewVersionChecker_h
#define FEQT_INCLUDED_SRC_networking_UINewVersionChecker_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UINetworkCustomer.h"

/** UINetworkCustomer extension for new version check. */
class SHARED_LIBRARY_STUFF UINewVersionChecker : public UINetworkCustomer
{
    Q_OBJECT;

signals:

    /** Notifies listeners about progress failed with @a strError. */
    void sigProgressFailed(const QString &strError);
    /** Notifies listeners about progress canceled. */
    void sigProgressCanceled();
    /** Notifies listeners about progress finished. */
    void sigProgressFinished();

public:

    /** Constructs new version checker.
      * @param  fForcedCall  Brings whether this customer has forced privelegies. */
    UINewVersionChecker(bool fForcedCall);

    /** Returns whether this customer has forced privelegies. */
    bool isItForcedCall() const { return m_fForcedCall; }
    /** Returns url. */
    QUrl url() const { return m_url; }

public slots:

    /** Starts new version check. */
    void start();
    /** Cancels new version check. */
    void cancel();

protected:

    /** Handles network reply progress for @a iReceived amount of bytes among @a iTotal. */
    virtual void processNetworkReplyProgress(qint64 iReceived, qint64 iTotal) RT_OVERRIDE RT_FINAL;
    /** Handles network reply failed with specified @a strError. */
    virtual void processNetworkReplyFailed(const QString &strError) RT_OVERRIDE RT_FINAL;
    /** Handles network reply canceling for a passed @a pReply. */
    virtual void processNetworkReplyCanceled(UINetworkReply *pReply) RT_OVERRIDE RT_FINAL;
    /** Handles network reply finishing for a passed @a pReply. */
    virtual void processNetworkReplyFinished(UINetworkReply *pReply) RT_OVERRIDE RT_FINAL;

private:

    /** Generates platform information. */
    static QString platformInfo();

    /** Holds whether this customer has forced privelegies. */
    bool  m_fForcedCall;
    /** Holds the new version checker URL. */
    QUrl  m_url;
};

#endif /* !FEQT_INCLUDED_SRC_networking_UINewVersionChecker_h */
