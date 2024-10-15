/* $Id: QILabelSeparator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QILabelSeparator class implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <QHBoxLayout>
#include <QLabel>

/* GUI includes: */
#include "QILabelSeparator.h"


QILabelSeparator::QILabelSeparator(QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    : QWidget(pParent, enmFlags)
    , m_pLabel(0)
{
    prepare();
}

QILabelSeparator::QILabelSeparator(const QString &strText, QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    : QWidget(pParent, enmFlags)
    , m_pLabel(0)
{
    prepare();
    setText(strText);
}

QString QILabelSeparator::text() const
{
    return m_pLabel->text();
}

void QILabelSeparator::setBuddy(QWidget *pBuddy)
{
    m_pLabel->setBuddy(pBuddy);
}

void QILabelSeparator::clear()
{
    m_pLabel->clear();
}

void QILabelSeparator::setText(const QString &strText)
{
    m_pLabel->setText(strText);
}

void QILabelSeparator::prepare()
{
    /* Create layout: */
    QHBoxLayout *pLayout = new QHBoxLayout(this);
    if (pLayout)
    {
        /* Configure layout: */
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Create label: */
        m_pLabel = new QLabel;
        if (m_pLabel)
        {
            /* Add into layout: */
            pLayout->addWidget(m_pLabel);
        }

        /* Create separator: */
        QFrame *pSeparator = new QFrame;
        {
            /* Configure separator: */
            pSeparator->setFrameShape(QFrame::HLine);
            pSeparator->setFrameShadow(QFrame::Sunken);
            pSeparator->setEnabled(false);
            pSeparator->setContentsMargins(0, 0, 0, 0);
            // pSeparator->setStyleSheet("QFrame {border: 1px outset black; }");
            pSeparator->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);

            /* Add into layout: */
            pLayout->addWidget(pSeparator, Qt::AlignBottom);
        }
    }
}
