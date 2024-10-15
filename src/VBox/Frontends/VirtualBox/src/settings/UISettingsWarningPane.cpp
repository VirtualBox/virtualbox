/* $Id: UISettingsWarningPane.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISettingsWarningPane class implementation.
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

/* Qt includes: */
#include <QHBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QTimer>

/* GUI includes: */
#include "UISettingsPageValidator.h"
#include "UISettingsWarningPane.h"

/* Other VBox includes: */
#include <iprt/assert.h>


UISettingsWarningPane::UISettingsWarningPane(QWidget *pParent)
    : QWidget(pParent)
    , m_pIconLayout(0)
    , m_pTextLabel(0)
    , m_pHoverTimer(0)
    , m_iHoveredIconLabelPosition(-1)
{
    prepare();
}

void UISettingsWarningPane::setWarningLabelText(const QString &strText)
{
    m_pTextLabel->setText(strText);
}

void UISettingsWarningPane::setWarningLabelVisible(bool fVisible)
{
    m_pTextLabel->setVisible(fVisible);
}

void UISettingsWarningPane::registerValidator(UISettingsPageValidator *pValidator)
{
    /* Make sure validator exists: */
    AssertPtrReturnVoid(pValidator);

    /* Make sure validator is not registered yet: */
    if (m_validators.contains(pValidator))
    {
        AssertMsgFailed(("Validator is registered already!\n"));
        return;
    }

    /* Register validator: */
    m_validators << pValidator;

    /* Create icon-label for newly registered validator: */
    QLabel *pIconLabel = new QLabel;
    {
        /* Configure icon-label: */
        pIconLabel->setMouseTracking(true);
        pIconLabel->installEventFilter(this);
        pIconLabel->setPixmap(pValidator->warningPixmap());
        connect(pValidator, &UISettingsPageValidator::sigShowWarningIcon, pIconLabel, &QLabel::show);
        connect(pValidator, &UISettingsPageValidator::sigHideWarningIcon, pIconLabel, &QLabel::hide);

        /* Add icon-label into list: */
        m_icons << pIconLabel;
        /* Add icon-label into layout: */
        m_pIconLayout->addWidget(pIconLabel);
    }

    /* Mark icon as 'unhovered': */
    m_hovered << false;
}

bool UISettingsWarningPane::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Depending on event-type: */
    switch (pEvent->type())
    {
        /* One of icons hovered: */
        case QEvent::MouseMove:
        {
            /* Cast object to label: */
            if (QLabel *pIconLabel = qobject_cast<QLabel*>(pObject))
            {
                /* Search for the corresponding icon: */
                if (m_icons.contains(pIconLabel))
                {
                    /* Mark icon-label hovered if not yet: */
                    int iIconLabelPosition = m_icons.indexOf(pIconLabel);
                    if (!m_hovered[iIconLabelPosition])
                    {
                        m_hovered[iIconLabelPosition] = true;
                        m_iHoveredIconLabelPosition = iIconLabelPosition;
                        m_pHoverTimer->start();
                    }
                }
            }
            break;
        }

        /* One of icons unhovered: */
        case QEvent::Leave:
        {
            /* Cast object to label: */
            if (QLabel *pIconLabel = qobject_cast<QLabel*>(pObject))
            {
                /* Search for the corresponding icon: */
                if (m_icons.contains(pIconLabel))
                {
                    /* Mark icon-label unhovered if not yet: */
                    int iIconLabelPosition = m_icons.indexOf(pIconLabel);
                    if (m_hovered[iIconLabelPosition])
                    {
                        m_hovered[iIconLabelPosition] = false;
                        if (m_pHoverTimer->isActive())
                        {
                            m_pHoverTimer->stop();
                            m_iHoveredIconLabelPosition = -1;
                        }
                        else
                            emit sigHoverLeave(m_validators[iIconLabelPosition]);
                    }
                }
            }
            break;
        }

        /* Default case: */
        default:
            break;
    }

    /* Call to base-class: */
    return QWidget::eventFilter(pObject, pEvent);
}

void UISettingsWarningPane::sltHandleHoverTimer()
{
    /* Notify listeners about hovering: */
    if (m_iHoveredIconLabelPosition >= 0 && m_iHoveredIconLabelPosition < m_validators.size())
        emit sigHoverEnter(m_validators[m_iHoveredIconLabelPosition]);
}

void UISettingsWarningPane::prepare()
{
    /* Create main-layout: */
    QHBoxLayout *pMainLayout = new QHBoxLayout(this);
    {
        /* Configure layout: */
        pMainLayout->setContentsMargins(0, 0, 0, 0);

        /* Add left stretch: */
        pMainLayout->addStretch();

        /* Create text-label: */
        m_pTextLabel = new QLabel;
        {
            m_pTextLabel->setVisible(false);
            pMainLayout->addWidget(m_pTextLabel);
        }

        /* Create layout: */
        m_pIconLayout = new QHBoxLayout;
        {
            /* Configure layout: */
            m_pIconLayout->setContentsMargins(0, 0, 0, 0);

            /* Add into layout: */
            pMainLayout->addLayout(m_pIconLayout);
        }

        /* Create hover-timer: */
        m_pHoverTimer = new QTimer(this);
        {
            /* Configure timer: */
            m_pHoverTimer->setInterval(200);
            m_pHoverTimer->setSingleShot(true);
            connect(m_pHoverTimer, &QTimer::timeout, this, &UISettingsWarningPane::sltHandleHoverTimer);
        }

        /* Add right stretch: */
        pMainLayout->addStretch();
    }
}
