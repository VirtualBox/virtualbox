/* $Id: QIDialogContainer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIDialogContainer class implementation.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIDialogContainer.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


QIDialogContainer::QIDialogContainer(QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    : QDialog(pParent, enmFlags)
    , m_pLayout(0)
    , m_pWidget(0)
    , m_pProgressLabel(0)
    , m_pProgressBar(0)
    , m_pButtonBox(0)
{
    prepare();
}

void QIDialogContainer::setWidget(QWidget *pWidget)
{
    delete m_pWidget;
    m_pWidget = pWidget;
    if (m_pWidget)
        m_pLayout->addWidget(m_pWidget, 0, 0);
}

void QIDialogContainer::setProgressBarHidden(bool fHidden)
{
    AssertPtrReturnVoid(m_pProgressLabel);
    AssertPtrReturnVoid(m_pProgressBar);
    m_pProgressLabel->setHidden(fHidden);
    m_pProgressBar->setHidden(fHidden);
}

void QIDialogContainer::setOkButtonEnabled(bool fEnabled)
{
    AssertPtrReturnVoid(m_pButtonBox);
    AssertPtrReturnVoid(m_pButtonBox->button(QDialogButtonBox::Ok));
    m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(fEnabled);
}

void QIDialogContainer::sltRetranslateUI()
{
    m_pProgressLabel->setText(tr("Loading"));
}

void QIDialogContainer::prepare()
{
    /* Prepare layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        /* Prepare dialog button-box: */
        m_pButtonBox = new QIDialogButtonBox(this);
        if (m_pButtonBox)
        {
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Ok);
            connect(m_pButtonBox, &QIDialogButtonBox::accepted,
                    this, &QDialog::accept);
            connect(m_pButtonBox, &QIDialogButtonBox::rejected,
                    this, &QDialog::reject);

            /* Prepare progress-layout: */
            QHBoxLayout *pHLayout = new QHBoxLayout;
            if (pHLayout)
            {
                pHLayout->setContentsMargins(0, 0, 0, 0);

                /* Prepare progress-label: */
                m_pProgressLabel = new QLabel(this);
                if (m_pProgressLabel)
                {
                    m_pProgressLabel->setHidden(true);

                    /* Add into layout: */
                    pHLayout->addWidget(m_pProgressLabel);
                }
                /* Prepare progress-bar: */
                m_pProgressBar = new QProgressBar(this);
                if (m_pProgressBar)
                {
                    m_pProgressBar->setHidden(true);
                    m_pProgressBar->setTextVisible(false);
                    m_pProgressBar->setMinimum(0);
                    m_pProgressBar->setMaximum(0);

                    /* Add into layout: */
                    pHLayout->addWidget(m_pProgressBar);
                }

                /* Add into button-box: */
                m_pButtonBox->addExtraLayout(pHLayout);
            }

            /* Add into layout: */
            m_pLayout->addWidget(m_pButtonBox, 1, 0);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &QIDialogContainer::sltRetranslateUI);
}
