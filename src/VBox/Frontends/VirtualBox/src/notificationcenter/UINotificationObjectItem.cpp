/* $Id: UINotificationObjectItem.cpp 108937 2025-03-25 11:11:46Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UINotificationObjectItem class implementation.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "QIToolButton.h"
#include "UIHelpBrowserDialog.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UINotificationObject.h"
#include "UINotificationObjectItem.h"
#include "UITranslationEventListener.h"


/*********************************************************************************************************************************
*   Class UINotificationObjectItem implementation.                                                                               *
*********************************************************************************************************************************/

UINotificationObjectItem::UINotificationObjectItem(QWidget *pParent, UINotificationObject *pObject /* = 0 */)
    : QWidget(pParent)
    , m_pObject(pObject)
    , m_pLayoutMain(0)
    , m_pLayoutUpper(0)
    , m_pLabelName(0)
    , m_pButtonHelp(0)
    , m_pButtonForget(0)
    , m_pButtonClose(0)
    , m_pLabelDetails(0)
    , m_fHovered(false)
    , m_fToggled(false)
{
    /* Make sure item is opaque. */
    setAutoFillBackground(true);

    /* Prepare main layout: */
    m_pLayoutMain = new QVBoxLayout(this);
    if (m_pLayoutMain)
    {
        /* Prepare upper layout: */
        m_pLayoutUpper = new QHBoxLayout;
        if (m_pLayoutUpper)
        {
            /* Prepare name label: */
            m_pLabelName = new QLabel(this);
            if (m_pLabelName)
            {
                m_pLabelName->setText(m_pObject->name());
                m_pLayoutUpper->addWidget(m_pLabelName);
            }

            /* Prepare help button: */
            if (!m_pObject->helpKeyword().isEmpty())
                m_pButtonHelp = new QIToolButton(this);
            if (m_pButtonHelp)
            {
                m_pButtonHelp->setIcon(UIIconPool::iconSet(":/help_16px.png"));
                m_pButtonHelp->setIconSize(QSize(10, 10));
                m_pButtonHelp->setProperty("helpkeyword", m_pObject->helpKeyword());
                connect(m_pButtonHelp, &QIToolButton::clicked,
                        this, &UINotificationObjectItem::sltHandleHelpRequest);

                m_pLayoutUpper->addWidget(m_pButtonHelp);
            }

            /* Prepare close button: */
            m_pButtonClose = new QIToolButton(this);
            if (m_pButtonClose)
            {
                m_pButtonClose->setIcon(UIIconPool::iconSet(":/close_16px.png"));
                m_pButtonClose->setIconSize(QSize(10, 10));
                connect(m_pButtonClose, &QIToolButton::clicked,
                        m_pObject, &UINotificationObject::close,
                        Qt::QueuedConnection);

                m_pLayoutUpper->addWidget(m_pButtonClose);
            }

            /* Add to layout: */
            m_pLayoutMain->addLayout(m_pLayoutUpper);
        }

        /* Prepare details label: */
        m_pLabelDetails = new QIRichTextLabel(this);
        if (m_pLabelDetails)
        {
            QFont myFont = m_pLabelDetails->font();
            myFont.setPointSize(myFont.pointSize() - 1);
            m_pLabelDetails->setBrowserFont(myFont);
            m_pLabelDetails->setVisible(false);
            int iHint = m_pLabelName->minimumSizeHint().width();
            if (m_pButtonHelp)
                iHint += m_pLayoutUpper->spacing() + m_pButtonHelp->minimumSizeHint().width();
            if (m_pButtonForget)
                iHint += m_pLayoutUpper->spacing() + m_pButtonForget->minimumSizeHint().width();
            if (m_pButtonClose)
                iHint += m_pLayoutUpper->spacing() + m_pButtonClose->minimumSizeHint().width();
            m_pLabelDetails->setMinimumTextWidth(iHint);
            m_pLabelDetails->setText(m_pObject->details());

            m_pLayoutMain->addWidget(m_pLabelDetails);
        }

        /* Prepare forget button: */
        if (!m_pObject->internalName().isEmpty())
        {
            m_pButtonForget = new QPushButton(this);
            if (m_pButtonForget)
            {
                QFont myFont = m_pButtonForget->font();
                myFont.setPointSize(myFont.pointSize() - 2);
                m_pButtonForget->setFont(myFont);
                m_pButtonForget->setIcon(UIIconPool::iconSet(":/close_popup_16px.png"));
                m_pButtonForget->setIconSize(QSize(10, 10));
                connect(m_pButtonForget, &QIToolButton::clicked,
                        m_pObject, &UINotificationObject::dismiss,
                        Qt::QueuedConnection);

                m_pLayoutMain->addWidget(m_pButtonForget);
            }
        }
    }

    /* Install translation listener: */
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UINotificationObjectItem::sltRetranslateUI);
    sltRetranslateUI();
}

bool UINotificationObjectItem::event(QEvent *pEvent)
{
    /* Handle required event types: */
    switch (pEvent->type())
    {
        case QEvent::Enter:
        case QEvent::MouseMove:
        {
            m_fHovered = true;
            update();
            break;
        }
        case QEvent::Leave:
        {
            m_fHovered = false;
            update();
            break;
        }
        case QEvent::MouseButtonRelease:
        {
            m_fToggled = !m_fToggled;
            m_pLabelDetails->setVisible(m_fToggled);
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QWidget::event(pEvent);
}

void UINotificationObjectItem::paintEvent(QPaintEvent *pPaintEvent)
{
    /* Prepare painter: */
    QPainter painter(this);
    painter.setClipRect(pPaintEvent->rect());
    /* Acquire palette: */
    const bool fActive = isActiveWindow();
    QPalette pal = QApplication::palette();

    /* Gather suitable colors: */
    QColor color = pal.color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window);
    QColor color1;
    QColor color2;
    if (color.black() > 128)
    {
        color1 = color.lighter(110);
        color2 = color.lighter(105);
    }
    else
    {
        color1 = color.darker(105);
        color2 = color.darker(110);
    }
    /* Prepare background gradient: */
    QLinearGradient grad(QPointF(0, 0), QPointF(width(), height()));
    {
        grad.setColorAt(0, color1);
        grad.setColorAt(1, color2);
    }
    /* Fill background: */
    painter.fillRect(rect(), grad);

    /* If item is hovered: */
    if (m_fHovered)
    {
        /* Gather suitable color: */
        QColor color3 = pal.color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Highlight);
        /* Override painter pen: */
        painter.setPen(color3);
        /* Draw frame: */
        painter.drawRect(rect());
    }
}

void UINotificationObjectItem::sltRetranslateUI()
{
    if (m_pButtonClose)
        m_pButtonClose->setToolTip(QApplication::translate("UIMessageCenter", "Close"));
    if (m_pButtonForget)
        m_pButtonForget->setText(QApplication::translate("UIMessageCenter", "Don't show again"));
}

void UINotificationObjectItem::sltHandleHelpRequest()
{
    UIHelpBrowserDialog::findManualFileAndShow("helpkeyword");
}


/*********************************************************************************************************************************
*   Class UINotificationProgressItem implementation.                                                                             *
*********************************************************************************************************************************/

UINotificationProgressItem::UINotificationProgressItem(QWidget *pParent, UINotificationProgress *pProgress /* = 0 */)
    : UINotificationObjectItem(pParent, pProgress)
    , m_pProgressBar(0)
{
    /* Main layout was prepared in base-class: */
    if (m_pLayoutMain)
    {
        /* Name label was prepared in base-class: */
        if (m_pLabelName)
            m_pLabelName->setText(progress()->name());
        /* Details label was prepared in base-class: */
        if (m_pLabelDetails)
        {
            const int iHint = m_pLabelName->minimumSizeHint().width()
                            + m_pLayoutUpper->spacing()
                            + m_pButtonClose->minimumSizeHint().width();
            m_pLabelDetails->setMinimumTextWidth(iHint);
            updateDetails();
        }

        /* Prepare progress-bar: */
        m_pProgressBar = new QProgressBar(this);
        if (m_pProgressBar)
        {
            m_pProgressBar->setMinimum(0);
            m_pProgressBar->setMaximum(100);
            m_pProgressBar->setValue(progress()->percent());

            m_pLayoutMain->addWidget(m_pProgressBar);
        }
    }

    /* Prepare progress connections: */
    connect(progress(), &UINotificationProgress::sigProgressStarted,
            this, &UINotificationProgressItem::sltHandleProgressStarted);
    connect(progress(), &UINotificationProgress::sigProgressChange,
            this, &UINotificationProgressItem::sltHandleProgressChange);
    connect(progress(), &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressItem::sltHandleProgressFinished);
}

void UINotificationProgressItem::sltHandleProgressStarted()
{
    /* Init close-button and progress-bar states: */
    if (m_pButtonClose)
        m_pButtonClose->setEnabled(progress()->isCancelable());
    if (m_pProgressBar)
        m_pProgressBar->setValue(0);
    /* Update details with fetched stuff if any: */
    if (m_pLabelDetails)
        updateDetails();
}

void UINotificationProgressItem::sltHandleProgressChange(ulong uPercent)
{
    /* Update close-button and progress-bar states: */
    if (m_pButtonClose)
        m_pButtonClose->setEnabled(progress()->isCancelable());
    if (m_pProgressBar)
        m_pProgressBar->setValue(uPercent);
}

void UINotificationProgressItem::sltHandleProgressFinished()
{
    /* Finalize close-button and progress-bar states: */
    if (m_pButtonClose)
        m_pButtonClose->setEnabled(true);
    if (m_pProgressBar)
        m_pProgressBar->setValue(100);
    /* Update details with error text if any: */
    if (m_pLabelDetails)
        updateDetails();
}

UINotificationProgress *UINotificationProgressItem::progress() const
{
    return qobject_cast<UINotificationProgress*>(m_pObject);
}

void UINotificationProgressItem::updateDetails()
{
    AssertPtrReturnVoid(m_pLabelDetails);
    const QString strDetails = progress()->details();
    const QString strError = progress()->error();
    const QString strFullDetails = strError.isNull()
                                 ? strDetails
                                 : QString("%1<br>%2").arg(strDetails, strError);
    m_pLabelDetails->setText(strFullDetails);
    if (!strError.isEmpty())
    {
        m_fToggled = true;
        m_pLabelDetails->setVisible(m_fToggled);
    }
}


#ifdef VBOX_GUI_WITH_NETWORK_MANAGER


/*********************************************************************************************************************************
*   Class UINotificationDownloaderItem implementation.                                                                           *
*********************************************************************************************************************************/

UINotificationDownloaderItem::UINotificationDownloaderItem(QWidget *pParent, UINotificationDownloader *pDownloader /* = 0 */)
    : UINotificationObjectItem(pParent, pDownloader)
    , m_pProgressBar(0)
{
    /* Main layout was prepared in base-class: */
    if (m_pLayoutMain)
    {
        /* Name label was prepared in base-class: */
        if (m_pLabelName)
            m_pLabelName->setText(downloader()->name());
        /* Details label was prepared in base-class: */
        if (m_pLabelDetails)
        {
            const int iHint = m_pLabelName->minimumSizeHint().width()
                            + m_pLayoutUpper->spacing()
                            + m_pButtonClose->minimumSizeHint().width();
            m_pLabelDetails->setMinimumTextWidth(iHint);
            updateDetails();
        }

        /* Prepare progress-bar: */
        m_pProgressBar = new QProgressBar(this);
        if (m_pProgressBar)
        {
            m_pProgressBar->setMinimum(0);
            m_pProgressBar->setMaximum(100);
            m_pProgressBar->setValue(downloader()->percent());

            m_pLayoutMain->addWidget(m_pProgressBar);
        }
    }

    /* Prepare downloader connections: */
    connect(downloader(), &UINotificationDownloader::sigProgressStarted,
            this, &UINotificationDownloaderItem::sltHandleProgressStarted);
    connect(downloader(), &UINotificationDownloader::sigProgressChange,
            this, &UINotificationDownloaderItem::sltHandleProgressChange);
    connect(downloader(), &UINotificationDownloader::sigProgressFailed,
            this, &UINotificationDownloaderItem::sltHandleProgressFinished);
    connect(downloader(), &UINotificationDownloader::sigProgressCanceled,
            this, &UINotificationDownloaderItem::sltHandleProgressFinished);
    connect(downloader(), &UINotificationDownloader::sigProgressFinished,
            this, &UINotificationDownloaderItem::sltHandleProgressFinished);
}

void UINotificationDownloaderItem::sltHandleProgressStarted()
{
    /* Init progress-bar state: */
    if (m_pProgressBar)
        m_pProgressBar->setValue(0);
    /* Update details with fetched stuff if any: */
    if (m_pLabelDetails)
        updateDetails();
}

void UINotificationDownloaderItem::sltHandleProgressChange(ulong uPercent)
{
    /* Update progress-bar state: */
    if (m_pProgressBar)
        m_pProgressBar->setValue(uPercent);
}

void UINotificationDownloaderItem::sltHandleProgressFinished()
{
    /* Finalize progress-bar state: */
    if (m_pProgressBar)
        m_pProgressBar->setValue(100);
    /* Update details with error text if any: */
    if (m_pLabelDetails)
        updateDetails();
}

UINotificationDownloader *UINotificationDownloaderItem::downloader() const
{
    return qobject_cast<UINotificationDownloader*>(m_pObject);
}

void UINotificationDownloaderItem::updateDetails()
{
    AssertPtrReturnVoid(m_pLabelDetails);
    const QString strDetails = downloader()->details();
    const QString strError = downloader()->error();
    const QString strFullDetails = strError.isNull()
                                 ? strDetails
                                 : QString("%1<br>%2").arg(strDetails, strError);
    m_pLabelDetails->setText(strFullDetails);
    if (!strError.isEmpty())
    {
        m_fToggled = true;
        m_pLabelDetails->setVisible(m_fToggled);
    }
}

#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */


/*********************************************************************************************************************************
*   Namespace UINotificationProgressItem implementation.                                                                         *
*********************************************************************************************************************************/

UINotificationObjectItem *UINotificationItem::create(QWidget *pParent, UINotificationObject *pObject)
{
    /* Handle known types: */
    if (pObject->inherits("UINotificationProgress"))
        return new UINotificationProgressItem(pParent, static_cast<UINotificationProgress*>(pObject));
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    else if (pObject->inherits("UINotificationDownloader"))
        return new UINotificationDownloaderItem(pParent, static_cast<UINotificationDownloader*>(pObject));
#endif
    /* Handle defaults: */
    return new UINotificationObjectItem(pParent, pObject);
}
