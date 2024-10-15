/* $Id: UISlidingToolBar.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISlidingToolBar class implementation.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include <QCloseEvent>
#include <QHBoxLayout>

/* GUI includes: */
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UISlidingToolBar.h"
#include "UIAnimationFramework.h"
#include "UIMachineWindow.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif


UISlidingToolBar::UISlidingToolBar(QWidget *pParentWidget, QWidget *pIndentWidget, QWidget *pChildWidget, Position enmPosition)
    : QWidget(pParentWidget, Qt::Tool | Qt::FramelessWindowHint)
    , m_enmPosition(enmPosition)
    , m_parentRect(pParentWidget ? pParentWidget->geometry() : QRect())
    , m_indentRect(pIndentWidget ? pIndentWidget->geometry() : QRect())
    , m_pAnimation(0)
    , m_fExpanded(false)
    , m_pMainLayout(0)
    , m_pArea(0)
    , m_pWidget(pChildWidget)
{
    /* Prepare: */
    prepare();
}

#ifdef VBOX_WS_MAC
bool UISlidingToolBar::event(QEvent *pEvent)
{
    /* Depending on event-type: */
    switch (pEvent->type())
    {
        case QEvent::Resize:
        case QEvent::WindowActivate:
        {
            // WORKAROUND:
            // By some strange reason
            // cocoa resets NSWindow::setHasShadow option
            // for frameless windows on every window resize/activation.
            // So we have to make sure window still has no shadows.
            darwinSetWindowHasShadow(this, false);
            break;
        }
        default:
            break;
    }
    /* Call to base-class: */
    return QWidget::event(pEvent);
}
#endif /* VBOX_WS_MAC */

void UISlidingToolBar::showEvent(QShowEvent *)
{
    /* If window isn't expanded: */
    if (!m_fExpanded)
    {
        /* Start expand animation: */
        emit sigShown();
    }
}

void UISlidingToolBar::closeEvent(QCloseEvent *pEvent)
{
    /* If window isn't expanded: */
    if (!m_fExpanded)
    {
        /* Ignore close-event: */
        pEvent->ignore();
        return;
    }

    /* If animation state is Final: */
    const QString strAnimationState = property("AnimationState").toString();
    bool fAnimationComplete = strAnimationState == "Final";
    if (fAnimationComplete)
    {
        /* Ignore close-event: */
        pEvent->ignore();
        /* And start collapse animation: */
        emit sigCollapse();
    }
}

void UISlidingToolBar::sltParentGeometryChanged(const QRect &parentRect)
{
    /* Update rectangle: */
    m_parentRect = parentRect;
    /* Adjust geometry: */
    adjustGeometry();
    /* Update animation: */
    updateAnimation();
}

void UISlidingToolBar::prepare()
{
    /* Tell the application we are not that important: */
    setAttribute(Qt::WA_QuitOnClose, false);
    /* Delete window when closed: */
    setAttribute(Qt::WA_DeleteOnClose);

#if   defined(VBOX_WS_MAC) || defined(VBOX_WS_WIN)
    /* Make sure we have no background
     * until the first one paint-event: */
    setAttribute(Qt::WA_NoSystemBackground);
    /* Use Qt API to enable translucency: */
    setAttribute(Qt::WA_TranslucentBackground);
#elif defined(VBOX_WS_NIX)
    if (uiCommon().isCompositingManagerRunning())
    {
        /* Use Qt API to enable translucency: */
        setAttribute(Qt::WA_TranslucentBackground);
    }
#endif /* VBOX_WS_NIX */

    /* Prepare contents: */
    prepareContents();
    /* Prepare geometry: */
    prepareGeometry();
    /* Prepare animation: */
    prepareAnimation();
}

void UISlidingToolBar::prepareContents()
{
    /* Create main-layout: */
    m_pMainLayout = new QHBoxLayout(this);
    if (m_pMainLayout)
    {
        /* Configure main-layout: */
        m_pMainLayout->setContentsMargins(0, 0, 0, 0);
        m_pMainLayout->setSpacing(0);
        /* Create area: */
        m_pArea = new QWidget;
        if (m_pArea)
        {
            /* Configure area: */
            m_pArea->setAcceptDrops(true);
            m_pArea->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            QPalette pal1 = m_pArea->palette();
            pal1.setColor(QPalette::Window, QColor(Qt::transparent));
            m_pArea->setPalette(pal1);
            /* Make sure valid child-widget passed: */
            if (m_pWidget)
            {
                /* Configure child-widget: */
                QPalette pal2 = m_pWidget->palette();
                pal2.setColor(QPalette::Window, QApplication::palette().color(QPalette::Window));
                m_pWidget->setPalette(pal2);
                /* Using abstract (old-style) connection here(!) since the base classes can be different: */
                connect(m_pWidget, SIGNAL(sigCancelClicked()), this, SLOT(close()));
                /* Add child-widget into area: */
                m_pWidget->setParent(m_pArea);
            }
            /* Add area into main-layout: */
            m_pMainLayout->addWidget(m_pArea);
        }
    }
}

void UISlidingToolBar::prepareGeometry()
{
    /* Prepare geometry based on parent and sub-window size-hints,
     * But move sub-window to initial position: */
    const QSize sh = m_pWidget->sizeHint();
    switch (m_enmPosition)
    {
        case Position_Top:
        {
            UIDesktopWidgetWatchdog::setTopLevelGeometry(this, m_parentRect.x(), m_parentRect.y()                         + m_indentRect.height(),
                                                  qMax(m_parentRect.width(), sh.width()), sh.height());
            m_pWidget->setGeometry(0, -sh.height(), qMax(width(), sh.width()), sh.height());
            break;
        }
        case Position_Bottom:
        {
            UIDesktopWidgetWatchdog::setTopLevelGeometry(this, m_parentRect.x(), m_parentRect.y() + m_parentRect.height() - m_indentRect.height() - sh.height(),
                                                  qMax(m_parentRect.width(), sh.width()), sh.height());
            m_pWidget->setGeometry(0,  sh.height(), qMax(width(), sh.width()), sh.height());
            break;
        }
    }

#ifdef VBOX_WS_NIX
    if (!uiCommon().isCompositingManagerRunning())
    {
        /* Use Xshape otherwise: */
        setMask(m_pWidget->geometry());
    }
#endif

#ifdef VBOX_WS_WIN
    /* Raise tool-window for proper z-order. */
    raise();
#endif

    /* Activate window after it was shown: */
    connect(this, &UISlidingToolBar::sigShown,
            this, &UISlidingToolBar::sltActivateWindow, Qt::QueuedConnection);
    /* Update window geometry after parent geometry changed: */
    /* Leave this in the old connection syntax for now: */
    connect(parent(), SIGNAL(sigGeometryChange(const QRect&)),
            this, SLOT(sltParentGeometryChanged(const QRect&)));
}

void UISlidingToolBar::prepareAnimation()
{
    /* Prepare sub-window geometry animation itself: */
    connect(this, SIGNAL(sigShown()), this, SIGNAL(sigExpand()), Qt::QueuedConnection);
    m_pAnimation = UIAnimation::installPropertyAnimation(this,
                                                         "widgetGeometry",
                                                         "startWidgetGeometry", "finalWidgetGeometry",
                                                         SIGNAL(sigExpand()), SIGNAL(sigCollapse()));
    connect(m_pAnimation, &UIAnimation::sigStateEnteredStart, this, &UISlidingToolBar::sltMarkAsCollapsed);
    connect(m_pAnimation, &UIAnimation::sigStateEnteredFinal, this, &UISlidingToolBar::sltMarkAsExpanded);
    /* Update geometry animation: */
    updateAnimation();
}

void UISlidingToolBar::adjustGeometry()
{
    /* Adjust geometry based on parent and sub-window size-hints: */
    const QSize sh = m_pWidget->sizeHint();
    switch (m_enmPosition)
    {
        case Position_Top:
        {
            UIDesktopWidgetWatchdog::setTopLevelGeometry(this, m_parentRect.x(), m_parentRect.y()                         + m_indentRect.height(),
                                                  qMax(m_parentRect.width(), sh.width()), sh.height());
            break;
        }
        case Position_Bottom:
        {
            UIDesktopWidgetWatchdog::setTopLevelGeometry(this, m_parentRect.x(), m_parentRect.y() + m_parentRect.height() - m_indentRect.height() - sh.height(),
                                                  qMax(m_parentRect.width(), sh.width()), sh.height());
            break;
        }
    }
    /* And move sub-window to corresponding position: */
    m_pWidget->setGeometry(0, 0, qMax(width(), sh.width()), sh.height());

#ifdef VBOX_WS_NIX
    if (!uiCommon().isCompositingManagerRunning())
    {
        /* Use Xshape otherwise: */
        setMask(m_pWidget->geometry());
    }
#endif

#ifdef VBOX_WS_WIN
    /* Raise tool-window for proper z-order. */
    raise();
#endif
}

void UISlidingToolBar::updateAnimation()
{
    /* Skip if no animation created: */
    if (!m_pAnimation)
        return;

    /* Recalculate sub-window geometry animation boundaries based on size-hint: */
    const QSize sh = m_pWidget->sizeHint();
    switch (m_enmPosition)
    {
        case Position_Top:    m_startWidgetGeometry = QRect(0, -sh.height(), qMax(width(), sh.width()), sh.height()); break;
        case Position_Bottom: m_startWidgetGeometry = QRect(0,  sh.height(), qMax(width(), sh.width()), sh.height()); break;
    }
    m_finalWidgetGeometry = QRect(0, 0, qMax(width(), sh.width()), sh.height());
    m_pAnimation->update();
}

void UISlidingToolBar::setWidgetGeometry(const QRect &rect)
{
    /* Apply sub-window geometry: */
    m_pWidget->setGeometry(rect);

#ifdef VBOX_WS_NIX
    if (!uiCommon().isCompositingManagerRunning())
    {
        /* Use Xshape otherwise: */
        setMask(m_pWidget->geometry());
    }
#endif
}

QRect UISlidingToolBar::widgetGeometry() const
{
    /* Return sub-window geometry: */
    return m_pWidget->geometry();
}
