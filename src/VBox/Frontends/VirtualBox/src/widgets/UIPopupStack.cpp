/* $Id: UIPopupStack.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupStack class implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <QEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QScrollArea>
#include <QStatusBar>
#include <QVBoxLayout>

/* GUI includes: */
#include "UIDesktopWidgetWatchdog.h"
#include "UIPopupStack.h"
#include "UIPopupStackViewport.h"


UIPopupStack::UIPopupStack(const QString &strID, UIPopupStackOrientation enmOrientation)
    : m_strID(strID)
    , m_enmOrientation(enmOrientation)
    , m_pScrollArea(0)
    , m_pScrollViewport(0)
    , m_iParentMenuBarHeight(0)
    , m_iParentStatusBarHeight(0)
{
    /* Prepare: */
    prepare();
}

bool UIPopupStack::exists(const QString &strID) const
{
    /* Redirect question to viewport: */
    return m_pScrollViewport->exists(strID);
}

void UIPopupStack::createPopupPane(const QString &strID,
                                   const QString &strMessage, const QString &strDetails,
                                   const QMap<int, QString> &buttonDescriptions)
{
    /* Redirect request to viewport: */
    m_pScrollViewport->createPopupPane(strID,
                                       strMessage, strDetails,
                                       buttonDescriptions);

    /* Propagate size: */
    propagateSize();
}

void UIPopupStack::updatePopupPane(const QString &strID,
                                   const QString &strMessage, const QString &strDetails)
{
    /* Redirect request to viewport: */
    m_pScrollViewport->updatePopupPane(strID,
                                       strMessage, strDetails);
}

void UIPopupStack::recallPopupPane(const QString &strID)
{
    /* Redirect request to viewport: */
    m_pScrollViewport->recallPopupPane(strID);
}

void UIPopupStack::setOrientation(UIPopupStackOrientation enmOrientation)
{
    /* Make sure orientation has changed: */
    if (m_enmOrientation == enmOrientation)
        return;

    /* Update orientation: */
    m_enmOrientation = enmOrientation;
    sltAdjustGeometry();
}

void UIPopupStack::setParent(QWidget *pParent)
{
    /* Call to base-class: */
    QWidget::setParent(pParent);
    /* Recalculate parent menu-bar height: */
    m_iParentMenuBarHeight = parentMenuBarHeight(pParent);
    /* Recalculate parent status-bar height: */
    m_iParentStatusBarHeight = parentStatusBarHeight(pParent);
}

void UIPopupStack::setParent(QWidget *pParent, Qt::WindowFlags enmFlags)
{
    /* Call to base-class: */
    QWidget::setParent(pParent, enmFlags);
    /* Recalculate parent menu-bar height: */
    m_iParentMenuBarHeight = parentMenuBarHeight(pParent);
    /* Recalculate parent status-bar height: */
    m_iParentStatusBarHeight = parentStatusBarHeight(pParent);
}

bool UIPopupStack::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Call to base-class if that is not parent event: */
    if (!parent() || pWatched != parent())
        return QWidget::eventFilter(pWatched, pEvent);

    /* Handle parent geometry events: */
    switch (pEvent->type())
    {
        case QEvent::Resize:
        {
            /* Propagate size: */
            propagateSize();
            /* Adjust geometry: */
            sltAdjustGeometry();
            break;
        }
        case QEvent::Move:
        {
            /* Adjust geometry: */
            sltAdjustGeometry();
            break;
        }
        default:
            break; /* Shuts up MSC. */
    }

    /* Call to base-class: */
    return QWidget::eventFilter(pWatched, pEvent);
}

void UIPopupStack::showEvent(QShowEvent*)
{
    /* Propagate size: */
    propagateSize();
    /* Adjust geometry: */
    sltAdjustGeometry();
}

void UIPopupStack::sltAdjustGeometry()
{
    /* Make sure parent is currently set: */
    if (!parent())
        return;

    /* Read parent geometry: */
    QRect geo(parentWidget()->geometry());
    if (!parentWidget()->isWindow())
        geo.moveTo(parentWidget()->mapToGlobal(QPoint(0, 0)));

    /* Determine size: */
    int iWidth = parentWidget()->width();
    int iHeight = parentWidget()->height();
    /* Subtract menu-bar and status-bar heights: */
    iHeight -= (m_iParentMenuBarHeight + m_iParentStatusBarHeight);
    /* Check if minimum height is even less than current: */
    if (m_pScrollViewport)
    {
        /* Get minimum viewport height: */
        int iMinimumHeight = m_pScrollViewport->minimumSizeHint().height();
        /* Subtract layout margins: */
        int iLeft, iTop, iRight, iBottom;
        m_pMainLayout->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
        iMinimumHeight += (iTop + iBottom);
        /* Compare minimum and current height: */
        iHeight = qMin(iHeight, iMinimumHeight);
    }

    /* Determine origin: */
    int iX = 0;
    int iY = 0;
    /* Shift for top-level window: */
    if (isWindow())
    {
        iX += geo.x();
        iY += geo.y();
    }
    switch (m_enmOrientation)
    {
        case UIPopupStackOrientation_Top:
        {
            /* Just add menu-bar height: */
            iY += m_iParentMenuBarHeight;
            break;
        }
        case UIPopupStackOrientation_Bottom:
        {
            /* Shift to bottom: */
            iY += (geo.height() - iHeight);
            /* And subtract status-bar height: */
            iY -= m_iParentStatusBarHeight;
            break;
        }
    }

    /* Adjust geometry: */
    UIDesktopWidgetWatchdog::setTopLevelGeometry(this, iX, iY, iWidth, iHeight);
}

void UIPopupStack::sltPopupPaneRemoved(QString)
{
    /* Move focus to the parent: */
    if (parentWidget())
        parentWidget()->setFocus();
}

void UIPopupStack::sltPopupPanesRemoved()
{
    /* Ask popup-center to remove us: */
    emit sigRemove(m_strID);
}

void UIPopupStack::prepare()
{
    /* Configure background: */
    setAutoFillBackground(false);
#if defined(VBOX_WS_WIN) || defined (VBOX_WS_MAC)
    /* Using Qt API to enable translucent background for the Win/Mac host: */
    setAttribute(Qt::WA_TranslucentBackground);
#endif

#ifdef VBOX_WS_MAC
    /* Do not hide popup-stack: */
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif

    /* Prepare content: */
    prepareContent();
}

void UIPopupStack::prepareContent()
{
    /* Create main-layout: */
    m_pMainLayout = new QVBoxLayout(this);
    {
        /* Configure main-layout: */
        m_pMainLayout->setContentsMargins(0, 0, 0, 0);
        /* Create scroll-area: */
        m_pScrollArea = new QScrollArea;
        {
            /* Configure scroll-area: */
            m_pScrollArea->setCursor(Qt::ArrowCursor);
            m_pScrollArea->setWidgetResizable(true);
            m_pScrollArea->setFrameStyle(QFrame::NoFrame | QFrame::Plain);
            m_pScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            //m_pScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            QPalette pal = m_pScrollArea->palette();
            m_pScrollArea->setPalette(pal);
            /* Create scroll-viewport: */
            m_pScrollViewport = new UIPopupStackViewport;
            {
                /* Configure scroll-viewport: */
                m_pScrollViewport->setCursor(Qt::ArrowCursor);
                /* Connect scroll-viewport: */
                connect(this, &UIPopupStack::sigProposeStackViewportSize,
                        m_pScrollViewport, &UIPopupStackViewport::sltHandleProposalForSize);
                connect(m_pScrollViewport, &UIPopupStackViewport::sigSizeHintChanged,
                        this, &UIPopupStack::sltAdjustGeometry);
                connect(m_pScrollViewport, &UIPopupStackViewport::sigPopupPaneDone,
                        this, &UIPopupStack::sigPopupPaneDone);
                connect(m_pScrollViewport, &UIPopupStackViewport::sigPopupPaneRemoved,
                        this, &UIPopupStack::sltPopupPaneRemoved);
                connect(m_pScrollViewport, &UIPopupStackViewport::sigPopupPanesRemoved,
                        this, &UIPopupStack::sltPopupPanesRemoved);
            }
            /* Assign scroll-viewport to scroll-area: */
            m_pScrollArea->setWidget(m_pScrollViewport);
        }
        /* Add scroll-area to layout: */
        m_pMainLayout->addWidget(m_pScrollArea);
    }
}

void UIPopupStack::propagateSize()
{
    /* Make sure parent is currently set: */
    if (!parent())
        return;

    /* Get parent size: */
    QSize newSize = parentWidget()->size();
    /* Subtract left/right layout margins: */
    if (m_pMainLayout)
    {
        int iLeft, iTop, iRight, iBottom;
        m_pMainLayout->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
        newSize.setWidth(newSize.width() - (iLeft + iRight));
        newSize.setHeight(newSize.height() - (iTop + iBottom));
    }
    /* Subtract scroll-area frame-width: */
    if (m_pScrollArea)
    {
        newSize.setWidth(newSize.width() - (2 * m_pScrollArea->frameWidth()));
        newSize.setHeight(newSize.height() - (2 * m_pScrollArea->frameWidth()));
    }
    newSize.setHeight(newSize.height() - (m_iParentMenuBarHeight + m_iParentStatusBarHeight));

    /* Propose resulting size to viewport: */
    emit sigProposeStackViewportSize(newSize);
}

/* static */
int UIPopupStack::parentMenuBarHeight(QWidget *pParent)
{
    /* Menu-bar can exist only on QMainWindow sub-class: */
    if (pParent)
    {
        if (QMainWindow *pMainWindow = qobject_cast<QMainWindow*>(pParent))
        {
            /* Search for existing menu-bar child: */
            if (QMenuBar *pMenuBar = pMainWindow->findChild<QMenuBar*>())
                return pMenuBar->height();
        }
    }
    /* Zero by default: */
    return 0;
}

/* static */
int UIPopupStack::parentStatusBarHeight(QWidget *pParent)
{
    /* Status-bar can exist only on QMainWindow sub-class: */
    if (pParent)
    {
        if (QMainWindow *pMainWindow = qobject_cast<QMainWindow*>(pParent))
        {
            /* Search for existing status-bar child: */
            if (QStatusBar *pStatusBar = pMainWindow->findChild<QStatusBar*>())
                if (pStatusBar->isVisible())
                    return pStatusBar->height();

        }
    }
    /* Zero by default: */
    return 0;
}
