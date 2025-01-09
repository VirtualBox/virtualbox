/* $Id: UITakeSnapshotDialog.cpp 107666 2025-01-09 15:52:29Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UITakeSnapshotDialog class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QWindow>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QILabel.h"
#include "VBoxUtils.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIShortcutPool.h"
#include "UITakeSnapshotDialog.h"
#include "UITranslationEventListener.h"

UITakeSnapshotDialog::UITakeSnapshotDialog(QWidget *pParent, ulong cImmutableMedia)
    : QIDialog(pParent)
    , m_cImmutableMedia(cImmutableMedia)
    , m_pLabelIcon(0)
    , m_pLabelName(0), m_pEditorName(0)
    , m_pLabelDescription(0), m_pEditorDescription(0)
    , m_pLabelInfo(0)
    , m_pButtonBox(0)
{
    prepare();
}

void UITakeSnapshotDialog::setIcon(const QIcon &icon)
{
    m_icon = icon;
    updatePixmap();
}

void UITakeSnapshotDialog::setName(const QString &strName)
{
    m_pEditorName->setText(strName);
}

QString UITakeSnapshotDialog::name() const
{
    return m_pEditorName->text();
}

QString UITakeSnapshotDialog::description() const
{
    return m_pEditorDescription->toPlainText();
}

bool UITakeSnapshotDialog::event(QEvent *pEvent)
{
    /* Handle know event types: */
    switch (pEvent->type())
    {
        case QEvent::Show:
        case QEvent::ScreenChangeInternal:
        {
            /* Update pixmap: */
            updatePixmap();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QIDialog::event(pEvent);
}

void UITakeSnapshotDialog::sltRetranslateUI()
{
    setWindowTitle(tr("Take Snapshot of Virtual Machine"));
    m_pLabelName->setText(tr("Snapshot &Name"));
    m_pEditorName->setToolTip(tr("Holds the snapshot name"));
    m_pLabelDescription->setText(tr("Snapshot &Description"));
    m_pEditorDescription->setToolTip(tr("Holds the snapshot description"));
    m_pLabelInfo->setText(tr("Warning: You are taking a snapshot of a running machine which has %n immutable image(s) "
                             "attached to it. As long as you are working from this snapshot the immutable image(s) "
                             "will not be reset to avoid loss of data.", "", m_cImmutableMedia));

    if (m_pButtonBox)
    {
        m_pButtonBox->button(QDialogButtonBox::Ok)->setText(tr("Ok"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
        m_pButtonBox->button(QDialogButtonBox::Help)->setText(tr("Help"));

        m_pButtonBox->button(QDialogButtonBox::Ok)->setStatusTip(tr("Take Snapshot and close the dialog"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setStatusTip(tr("Close dialog without taking a snapshot"));
        m_pButtonBox->button(QDialogButtonBox::Help)->setStatusTip(tr("Show dialog help"));

        m_pButtonBox->button(QDialogButtonBox::Help)->setShortcut(UIShortcutPool::standardSequence(QKeySequence::HelpContents));

        if (m_pButtonBox->button(QDialogButtonBox::Ok)->shortcut().toString().isEmpty())
            m_pButtonBox->button(QDialogButtonBox::Ok)->setToolTip(tr("Accept"));
        else
            m_pButtonBox->button(QDialogButtonBox::Ok)->setToolTip(tr("Accept (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Ok)->shortcut().toString()));

        if (m_pButtonBox->button(QDialogButtonBox::Cancel)->shortcut().toString().isEmpty())
            m_pButtonBox->button(QDialogButtonBox::Cancel)->setToolTip(tr("Cancel"));
        else
            m_pButtonBox->button(QDialogButtonBox::Cancel)->setToolTip(tr("Cancel (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Cancel)->shortcut().toString()));

        if (m_pButtonBox->button(QDialogButtonBox::Help)->shortcut().toString().isEmpty())
            m_pButtonBox->button(QDialogButtonBox::Help)->setToolTip(tr("Show Help"));
        else
            m_pButtonBox->button(QDialogButtonBox::Help)->setToolTip(tr("Show Help (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Help)->shortcut().toString()));
    }
}

void UITakeSnapshotDialog::sltHandleNameChanged(const QString &strName)
{
    /* Update button state depending on snapshot name value: */
    m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(!strName.trimmed().isEmpty());
}

void UITakeSnapshotDialog::prepare()
{
    /* Prepare contents: */
    prepareContents();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UITakeSnapshotDialog::sltRetranslateUI);

    /* Invent minimum size: */
    QSize minimumSize;
    const int iHostScreen = UIDesktopWidgetWatchdog::screenNumber(parentWidget());
    if (iHostScreen >= 0 && iHostScreen < UIDesktopWidgetWatchdog::screenCount())
    {
        /* On the basis of current host-screen geometry if possible: */
        const QRect screenGeometry = gpDesktop->screenGeometry(iHostScreen);
        if (screenGeometry.isValid())
            minimumSize = screenGeometry.size() / 4;
    }
    /* Fallback to default size if we failed: */
    if (minimumSize.isNull())
        minimumSize = QSize(800, 600);
    /* Resize to initial size: */
    setMinimumSize(minimumSize);
}

void UITakeSnapshotDialog::prepareContents()
{
    /* Create layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        /* Configure layout: */
#ifdef VBOX_WS_MAC
        pLayout->setSpacing(20);
        pLayout->setContentsMargins(40, 20, 40, 20);
#else
        pLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing) * 2);
#endif

        /* Create sub-layout: */
        QVBoxLayout *pSubLayout1 = new QVBoxLayout;
        if (pSubLayout1)
        {
            /* Create icon label: */
            m_pLabelIcon = new QLabel;
            if (m_pLabelIcon)
            {
                /* Configure label: */
                m_pLabelIcon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

                /* Add into layout: */
                pSubLayout1->addWidget(m_pLabelIcon);
            }

            /* Add stretch: */
            pSubLayout1->addStretch();

            /* Add into layout: */
            pLayout->addLayout(pSubLayout1, 0, 0, 2, 1);
        }

        /* Create sub-layout 2: */
        QVBoxLayout *pSubLayout2 = new QVBoxLayout;
        if (pSubLayout2)
        {
            /* Configure layout: */
#ifdef VBOX_WS_MAC
            pSubLayout2->setSpacing(5);
#else
            pSubLayout2->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif

            /* Create name label: */
            m_pLabelName = new QLabel;
            if (m_pLabelName)
            {
                /* Add into layout: */
                pSubLayout2->addWidget(m_pLabelName);
            }

            /* Create name editor: */
            m_pEditorName = new QLineEdit;
            if (m_pEditorName)
            {
                /* Configure editor: */
                m_pLabelName->setBuddy(m_pEditorName);
                connect(m_pEditorName, &QLineEdit::textChanged,
                        this, &UITakeSnapshotDialog::sltHandleNameChanged);

                /* Add into layout: */
                pSubLayout2->addWidget(m_pEditorName);
            }

            /* Add into layout: */
            pLayout->addLayout(pSubLayout2, 0, 1);
        }

        /* Create sub-layout 3: */
        QVBoxLayout *pSubLayout3 = new QVBoxLayout;
        if (pSubLayout3)
        {
            /* Configure layout: */
#ifdef VBOX_WS_MAC
            pSubLayout3->setSpacing(5);
#else
            pSubLayout3->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif

            /* Create description label: */
            m_pLabelDescription = new QLabel;
            if (m_pLabelDescription)
            {
                /* Add into layout: */
                pSubLayout3->addWidget(m_pLabelDescription);
            }

            /* Create description editor: */
            m_pEditorDescription = new QTextEdit;
            if (m_pEditorDescription)
            {
                /* Configure editor: */
                m_pLabelDescription->setBuddy(m_pEditorDescription);

                /* Add into layout: */
                pSubLayout3->addWidget(m_pEditorDescription);
            }

            /* Add into layout: */
            pLayout->addLayout(pSubLayout3, 1, 1);
        }

        /* Create information label: */
        m_pLabelInfo = new QILabel;
        if (m_pLabelInfo)
        {
            /* Configure label: */
            m_pLabelInfo->setWordWrap(true);
            m_pLabelInfo->useSizeHintForWidth(400);

            /* Hide if machine have no immutable attachments: */
            if (!m_cImmutableMedia)
                m_pLabelInfo->setHidden(true);

            /* Add into layout: */
            pLayout->addWidget(m_pLabelInfo, 2, 0, 1, 2);
        }

        /* Create button-box: */
        m_pButtonBox = new QIDialogButtonBox;
        if (m_pButtonBox)
        {
            /* Configure button-box: */
            m_pButtonBox->setStandardButtons(  QDialogButtonBox::Ok
                                             | QDialogButtonBox::Cancel
                                             | QDialogButtonBox::Help);
            connect(m_pButtonBox, &QIDialogButtonBox::accepted,
                    this, &UITakeSnapshotDialog::accept);
            connect(m_pButtonBox, &QIDialogButtonBox::rejected,
                    this, &UITakeSnapshotDialog::reject);
            connect(m_pButtonBox->button(QIDialogButtonBox::Help), &QPushButton::pressed,
                    m_pButtonBox, &QIDialogButtonBox::sltHandleHelpRequest);
            uiCommon().setHelpKeyword(m_pButtonBox->button(QIDialogButtonBox::Help), "snapshots");

            /* Add into layout: */
            pLayout->addWidget(m_pButtonBox, 3, 0, 1, 2);
        }
    }
}

void UITakeSnapshotDialog::updatePixmap()
{
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize);
    const qreal fDevicePixelRatio = windowHandle() ? windowHandle()->devicePixelRatio() : 1;
    m_pLabelIcon->setPixmap(m_icon.pixmap(QSize(iIconMetric, iIconMetric), fDevicePixelRatio));
}
