/* $Id: UIErrorPane.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIErrorPane class implementation.
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

/* Qt includes: */
#include <QTextBrowser>
#include <QVBoxLayout>

/* GUI includes: */
#include "UIErrorPane.h"


UIErrorPane::UIErrorPane(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pBrowserDetails(0)
{
    prepare();
}

void UIErrorPane::setErrorDetails(const QString &strDetails)
{
    /* Redirect to details browser: */
    m_pBrowserDetails->setText(strDetails);
}

void UIErrorPane::prepare()
{
    /* Prepare main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        pMainLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare details browser: */
        m_pBrowserDetails = new QTextBrowser;
        if (m_pBrowserDetails)
        {
            m_pBrowserDetails->setFocusPolicy(Qt::StrongFocus);
            m_pBrowserDetails->document()->setDefaultStyleSheet("a { text-decoration: none; }");

            /* Add into layout: */
            pMainLayout->addWidget(m_pBrowserDetails);
        }
    }
}
