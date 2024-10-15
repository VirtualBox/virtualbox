/* $Id: UIWelcomePane.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWelcomePane class implementation.
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
#include <QApplication>
#include <QButtonGroup>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QUrl>

/* GUI includes */
#include "QIRichTextLabel.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UITranslationEventListener.h"
#include "UIWelcomePane.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/*********************************************************************************************************************************
*   Class UIWelcomePane implementation.                                                                                          *
*********************************************************************************************************************************/

UIWelcomePane::UIWelcomePane(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pLabelGreetings(0)
    , m_pLabelMode(0)
    , m_pLabelIcon(0)
{
    prepare();
}

bool UIWelcomePane::event(QEvent *pEvent)
{
    /* Handle known event types: */
    switch (pEvent->type())
    {
        case QEvent::Show:
        case QEvent::ScreenChangeInternal:
        {
            /* Update pixmap: */
            updateTextLabels();
            updatePixmap();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QWidget::event(pEvent);
}

void UIWelcomePane::sltRetranslateUI()
{
    /* Translate greetings text: */
    if (m_pLabelGreetings)
        m_pLabelGreetings->setText(tr("<h3>Welcome to VirtualBox!</h3>"
                                      "<p>The left part of application window contains global tools and "
                                      "lists all virtual machines and virtual machine groups on your computer. "
                                      "You can import, add and create new VMs using corresponding toolbar buttons. "
                                      "You can popup a tools of currently selected element using corresponding element "
                                      "button.</p>"
                                      "<p>You can press the <b>%1</b> key to get instant help, or visit "
                                      "<a href=https://www.virtualbox.org>www.virtualbox.org</a> "
                                      "for more information and latest news.</p>")
                                      .arg(QKeySequence(QKeySequence::HelpContents).toString(QKeySequence::NativeText)));

    /* Translate experience mode stuff: */
    if (m_pLabelMode)
        m_pLabelMode->setText(tr("<h3>Please choose Experience Mode!</h3>"
                                 "By default, the VirtualBox GUI is hiding some options, tools and wizards. "
                                 "<p>The <b>Basic Mode</b> is intended for those users who are not interested in advanced "
                                 "functionality and prefer a simpler, cleaner interface.</p>"
                                 "<p>The <b>Expert Mode</b> is intended for experienced users who wish to utilize all "
                                 "VirtualBox functionality.</p>"
                                 "<p>You can choose whether you are a beginner or experienced user by selecting required "
                                 "option at the right. This choice can always be changed in Global Preferences or Machine "
                                 "Settings windows.</p>"));
    if (m_buttons.contains(false))
        m_buttons.value(false)->setText(tr("Basic Mode"));
    if (m_buttons.contains(true))
        m_buttons.value(true)->setText(tr("Expert Mode"));
}

void UIWelcomePane::sltHandleLinkActivated(const QUrl &urlLink)
{
    uiCommon().openURL(urlLink.toString());
}

void UIWelcomePane::sltHandleButtonClicked(QAbstractButton *pButton)
{
    /* Make sure one of buttons was really pressed: */
    AssertReturnVoid(m_buttons.contains(pButton));

    /* Hide everything related to experience mode: */
    if (m_pLabelMode)
        m_pLabelMode->hide();
    if (m_buttons.contains(false))
        m_buttons.value(false)->hide();
    if (m_buttons.contains(true))
        m_buttons.value(true)->hide();

    /* Check which button was pressed actually and save the value: */
    const bool fExpertMode = m_buttons.key(pButton, false);
    gEDataManager->setSettingsInExpertMode(fExpertMode);
}

void UIWelcomePane::prepare()
{
    /* Prepare default welcome icon: */
    m_icon = UIIconPool::iconSet(":/tools_banner_global_200px.png");

    /* Prepare main layout: */
    QGridLayout *pMainLayout = new QGridLayout(this);
    if (pMainLayout)
    {
        const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 2;
        const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin);
        const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin);
        const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) / 2;
#ifdef VBOX_WS_MAC
        const int iSpacing = 20;
#else
        const int iSpacing = qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing);
#endif
        pMainLayout->setContentsMargins(iL, iT, iR, iB);
        pMainLayout->setSpacing(iSpacing);
        pMainLayout->setRowStretch(2, 1);

        /* Prepare greetings label: */
        m_pLabelGreetings = new QIRichTextLabel(this);
        if (m_pLabelGreetings)
        {
            m_pLabelGreetings->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
            connect(m_pLabelGreetings, &QIRichTextLabel::sigLinkClicked, this, &UIWelcomePane::sltHandleLinkActivated);
            pMainLayout->addWidget(m_pLabelGreetings, 0, 0);
        }

        /* Prepare icon label: */
        m_pLabelIcon = new QLabel(this);
        if (m_pLabelIcon)
        {
            m_pLabelIcon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            pMainLayout->addWidget(m_pLabelIcon, 0, 1);
        }

        /* This block for the case if experienced mode is NOT defined yet: */
        if (gEDataManager->extraDataString(UIExtraDataDefs::GUI_Settings_ExpertMode).isNull())
        {
            /* Prepare experience mode label: */
            m_pLabelMode = new QIRichTextLabel(this);
            if (m_pLabelMode)
            {
                m_pLabelMode->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
                pMainLayout->addWidget(m_pLabelMode, 1, 0);
            }

            /* Prepare button layout: */
            QVBoxLayout *pLayoutButton = new QVBoxLayout;
            if (pLayoutButton)
            {
                pLayoutButton->setSpacing(iSpacing / 2);

                /* Prepare button group: */
                QButtonGroup *pButtonGroup = new QButtonGroup(this);
                if (pButtonGroup)
                {
                    /* Prepare Basic button ('false' means 'not Expert'): */
                    m_buttons[false] = new QPushButton(this);
                    QAbstractButton *pButtonBasic = m_buttons.value(false);
                    if (pButtonBasic)
                    {
                        pButtonGroup->addButton(pButtonBasic);
                        pLayoutButton->addWidget(pButtonBasic);
                    }

                    /* Prepare Expert button ('true' means 'is Expert'): */
                    m_buttons[true] = new QPushButton(this);
                    QAbstractButton *pButtonExpert = m_buttons[true];
                    if (pButtonExpert)
                    {
                        pButtonGroup->addButton(pButtonExpert);
                        pLayoutButton->addWidget(pButtonExpert);
                    }

                    connect(pButtonGroup, &QButtonGroup::buttonClicked,
                            this, &UIWelcomePane::sltHandleButtonClicked);
                }

                pLayoutButton->addStretch();
                pMainLayout->addLayout(pLayoutButton, 1, 1);
            }
        }
    }

    /* Assign Help keyword: */
    uiCommon().setHelpKeyword(this, "intro-starting");

    /* Translate finally: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIWelcomePane::sltRetranslateUI);

    /* Update stuff: */
    updateTextLabels();
    updatePixmap();
}

void UIWelcomePane::updateTextLabels()
{
    /* For all the text-labels: */
    QList<QIRichTextLabel*> labels = findChildren<QIRichTextLabel*>();
    if (!labels.isEmpty())
    {
        /* Make sure their minimum width is around 20% of the screen width: */
        const QSize screenGeometry = gpDesktop->screenGeometry(this).size();
        foreach (QIRichTextLabel *pLabel, labels)
        {
            pLabel->setMinimumTextWidth(screenGeometry.width() * .2);
            pLabel->resize(pLabel->minimumSizeHint());
        }
    }
}

void UIWelcomePane::updatePixmap()
{
    /* Assign corresponding icon: */
    if (!m_icon.isNull() && m_pLabelIcon)
    {
        /* Check which size goes as the default one: */
        const QList<QSize> sizes = m_icon.availableSizes();
        const QSize defaultSize = sizes.isEmpty() ? QSize(200, 200) : sizes.first();
        /* Acquire device-pixel ratio: */
        const qreal fDevicePixelRatio = gpDesktop->devicePixelRatio(this);
        m_pLabelIcon->setPixmap(m_icon.pixmap(defaultSize, fDevicePixelRatio));
    }
}
