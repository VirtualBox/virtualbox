/* $Id: UIApplianceUnverifiedCertificateViewer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIApplianceUnverifiedCertificateViewer class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_importappliance_UIApplianceUnverifiedCertificateViewer_h
#define FEQT_INCLUDED_SRC_wizards_importappliance_UIApplianceUnverifiedCertificateViewer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIDialog.h"

/* Forward declarations: */
class QLabel;
class QTextBrowser;
class CCertificate;

/** QIDialog extension
  * asking for consent to continue with unverifiable certificate. */
class UIApplianceUnverifiedCertificateViewer : public QIDialog
{
    Q_OBJECT;

public:

    /** Constructs appliance @a comCertificate viewer for passed @a pParent. */
    UIApplianceUnverifiedCertificateViewer(QWidget *pParent, const CCertificate &comCertificate);

protected:

    /** Prepares all. */
    void prepare();

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Holds the certificate reference. */
    const CCertificate &m_comCertificate;

    /** Holds the text-label instance. */
    QLabel       *m_pTextLabel;
    /** Holds the text-browser instance. */
    QTextBrowser *m_pTextBrowser;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_importappliance_UIApplianceUnverifiedCertificateViewer_h */
