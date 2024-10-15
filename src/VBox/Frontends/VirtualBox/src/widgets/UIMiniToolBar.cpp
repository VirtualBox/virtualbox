/* $Id: UIMiniToolBar.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMiniToolBar class implementation.
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
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QStateMachine>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#ifdef VBOX_WS_NIX
# include <QWindowStateChangeEvent>
#endif

/* GUI includes: */
#include "UICommon.h"
#include "UIAnimationFramework.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIIconPool.h"
#include "UILoggingDefs.h"
#include "UIMiniToolBar.h"
#ifdef VBOX_WS_NIX
# include "UIExtraDataManager.h"
#endif

/* Other VBox includes: */
#include "iprt/assert.h"


/** QIToolBar reimplementation
  * providing UIMiniToolBar with mini-toolbar. */
class UIMiniToolBarPrivate : public QIToolBar
{
    Q_OBJECT;

signals:

    /** Notifies listeners about we are resized. */
    void sigResized();

    /** Notifies listeners about action triggered to toggle auto-hide. */
    void sigAutoHideToggled();
    /** Notifies listeners about action triggered to minimize. */
    void sigMinimizeAction();
    /** Notifies listeners about action triggered to exit. */
    void sigExitAction();
    /** Notifies listeners about action triggered to close. */
    void sigCloseAction();

public:

    /** Constructor. */
    UIMiniToolBarPrivate();

    /** Defines @a alignment. */
    void setAlignment(Qt::Alignment alignment);

    /** Returns whether we do auto-hide. */
    bool autoHide() const;
    /** Defines whether we do @a fAutoHide. */
    void setAutoHide(bool fAutoHide);

    /** Defines our @a strText. */
    void setText(const QString &strText);

    /** Adds our @a menus. */
    void addMenus(const QList<QMenu*> &menus);

protected:

    /** Show @a pEvent handler. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Polish @a pEvent handler. */
    virtual void polishEvent(QShowEvent *pEvent);
    /** Resize @a pEvent handler. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Paint @a pEvent handler. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE RT_FINAL;

private:

    /** Prepare routine. */
    void prepare();

    /** Rebuilds our shape. */
    void rebuildShape();

    /** Holds whether this widget was polished. */
    bool m_fPolished;
    /** Holds the alignment type. */
    Qt::Alignment m_alignment;
    /** Holds the shape. */
    QPainterPath m_shape;

    /** Holds the action to toggle auto-hide. */
    QAction *m_pAutoHideAction;
    /** Holds the name label. */
    QLabel *m_pLabel;
    /** Holds the action to trigger minimize. */
    QAction *m_pMinimizeAction;
    /** Holds the action to trigger exit. */
    QAction *m_pRestoreAction;
    /** Holds the action to trigger close. */
    QAction *m_pCloseAction;

    /** Holds the pointer to the place to insert menu. */
    QAction *m_pMenuInsertPosition;

    /** Holds the spacings. */
    QList<QWidget*> m_spacings;
    /** Holds the margins. */
    QList<QWidget*> m_margins;
};


/*********************************************************************************************************************************
*   Class UIMiniToolBarPrivate implementation.                                                                                   *
*********************************************************************************************************************************/

UIMiniToolBarPrivate::UIMiniToolBarPrivate()
    /* Variables: General stuff: */
    : m_fPolished(false)
    , m_alignment(Qt::AlignBottom)
    /* Variables: Contents stuff: */
    , m_pAutoHideAction(0)
    , m_pLabel(0)
    , m_pMinimizeAction(0)
    , m_pRestoreAction(0)
    , m_pCloseAction(0)
    /* Variables: Menu stuff: */
    , m_pMenuInsertPosition(0)
{
    /* Prepare: */
    prepare();
}

void UIMiniToolBarPrivate::setAlignment(Qt::Alignment alignment)
{
    /* Make sure alignment really changed: */
    if (m_alignment == alignment)
        return;

    /* Update alignment: */
    m_alignment = alignment;

    /* Rebuild shape: */
    rebuildShape();
}

bool UIMiniToolBarPrivate::autoHide() const
{
    /* Return auto-hide: */
    return !m_pAutoHideAction->isChecked();
}

void UIMiniToolBarPrivate::setAutoHide(bool fAutoHide)
{
    /* Make sure auto-hide really changed: */
    if (m_pAutoHideAction->isChecked() == !fAutoHide)
        return;

    /* Update auto-hide: */
    m_pAutoHideAction->setChecked(!fAutoHide);
}

void UIMiniToolBarPrivate::setText(const QString &strText)
{
    /* Make sure text really changed: */
    if (m_pLabel->text() == strText)
        return;

    /* Update text: */
    m_pLabel->setText(strText);

    /* Resize to sizehint: */
    resize(sizeHint());
}

void UIMiniToolBarPrivate::addMenus(const QList<QMenu*> &menus)
{
    /* For each of the passed menu items: */
    for (int i = 0; i < menus.size(); ++i)
    {
        /* Get corresponding menu-action: */
        QAction *pAction = menus[i]->menuAction();
        /* Insert it into corresponding place: */
        insertAction(m_pMenuInsertPosition, pAction);
        /* Configure corresponding tool-button: */
        if (QToolButton *pButton = qobject_cast<QToolButton*>(widgetForAction(pAction)))
        {
            pButton->setPopupMode(QToolButton::InstantPopup);
            pButton->setAutoRaise(true);
        }
        /* Add some spacing: */
        if (i != menus.size() - 1)
            m_spacings << widgetForAction(insertWidget(m_pMenuInsertPosition, new QWidget(this)));
    }

    /* Resize to sizehint: */
    resize(sizeHint());
}

void UIMiniToolBarPrivate::showEvent(QShowEvent *pEvent)
{
    /* Make sure we should polish dialog: */
    if (m_fPolished)
        return;

    /* Call to polish-event: */
    polishEvent(pEvent);

    /* Mark dialog as polished: */
    m_fPolished = true;
}

void UIMiniToolBarPrivate::polishEvent(QShowEvent*)
{
    /* Toolbar spacings: */
    foreach(QWidget *pSpacing, m_spacings)
        pSpacing->setMinimumWidth(5);

    /* Title spacings: */
    foreach(QWidget *pLableMargin, m_margins)
        pLableMargin->setMinimumWidth(15);

    /* Resize to sizehint: */
    resize(sizeHint());
}

void UIMiniToolBarPrivate::resizeEvent(QResizeEvent*)
{
    /* Rebuild shape: */
    rebuildShape();

    /* Notify listeners: */
    emit sigResized();
}

void UIMiniToolBarPrivate::paintEvent(QPaintEvent*)
{
    /* Prepare painter: */
    QPainter painter(this);

    /* Fill background: */
    if (!m_shape.isEmpty())
    {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipPath(m_shape);
    }
    QRect backgroundRect = rect();
    QColor backgroundColor = QApplication::palette().color(QPalette::Window);
    QLinearGradient headerGradient(backgroundRect.bottomLeft(), backgroundRect.topLeft());
    headerGradient.setColorAt(0, backgroundColor.darker(120));
    headerGradient.setColorAt(1, backgroundColor.darker(90));
    painter.fillRect(backgroundRect, headerGradient);
}

void UIMiniToolBarPrivate::prepare()
{
    /* Determine icon metric: */
    const QStyle *pStyle = QApplication::style();
    const int iIconMetric = pStyle->pixelMetric(QStyle::PM_SmallIconSize);

    /* Configure toolbar: */
    setIconSize(QSize(iIconMetric, iIconMetric));

    /* Left margin: */
#ifdef VBOX_WS_NIX
    if (uiCommon().isCompositingManagerRunning())
        m_spacings << widgetForAction(addWidget(new QWidget));
#else /* !VBOX_WS_NIX */
    m_spacings << widgetForAction(addWidget(new QWidget));
#endif /* !VBOX_WS_NIX */

    /* Prepare push-pin: */
    m_pAutoHideAction = new QAction(this);
    m_pAutoHideAction->setIcon(UIIconPool::iconSet(":/pin_16px.png"));
    m_pAutoHideAction->setToolTip(UIMiniToolBar::tr("Always show the toolbar"));
    m_pAutoHideAction->setCheckable(true);
    connect(m_pAutoHideAction, SIGNAL(toggled(bool)), this, SIGNAL(sigAutoHideToggled()));
    addAction(m_pAutoHideAction);

    /* Left menu margin: */
    m_spacings << widgetForAction(addWidget(new QWidget));

    /* Right menu margin: */
    m_pMenuInsertPosition = addWidget(new QWidget);
    m_spacings << widgetForAction(m_pMenuInsertPosition);

    /* Left label margin: */
    m_margins << widgetForAction(addWidget(new QWidget));

    /* Insert a label for VM Name: */
    m_pLabel = new QLabel;
    m_pLabel->setAlignment(Qt::AlignCenter);
    addWidget(m_pLabel);

    /* Right label margin: */
    m_margins << widgetForAction(addWidget(new QWidget));

    /* Minimize action: */
    m_pMinimizeAction = new QAction(this);
    m_pMinimizeAction->setIcon(UIIconPool::iconSet(":/minimize_16px.png"));
    m_pMinimizeAction->setToolTip(UIMiniToolBar::tr("Minimize Window"));
    connect(m_pMinimizeAction, SIGNAL(triggered()), this, SIGNAL(sigMinimizeAction()));
    addAction(m_pMinimizeAction);

    /* Exit action: */
    m_pRestoreAction = new QAction(this);
    m_pRestoreAction->setIcon(UIIconPool::iconSet(":/restore_16px.png"));
    m_pRestoreAction->setToolTip(UIMiniToolBar::tr("Exit Full Screen or Seamless Mode"));
    connect(m_pRestoreAction, SIGNAL(triggered()), this, SIGNAL(sigExitAction()));
    addAction(m_pRestoreAction);

    /* Close action: */
    m_pCloseAction = new QAction(this);
    m_pCloseAction->setIcon(UIIconPool::iconSet(":/close_16px.png"));
    m_pCloseAction->setToolTip(UIMiniToolBar::tr("Close VM"));
    connect(m_pCloseAction, SIGNAL(triggered()), this, SIGNAL(sigCloseAction()));
    addAction(m_pCloseAction);

    /* Right margin: */
#ifdef VBOX_WS_NIX
    if (uiCommon().isCompositingManagerRunning())
        m_spacings << widgetForAction(addWidget(new QWidget));
#else /* !VBOX_WS_NIX */
    m_spacings << widgetForAction(addWidget(new QWidget));
#endif /* !VBOX_WS_NIX */
}

void UIMiniToolBarPrivate::rebuildShape()
{
#ifdef VBOX_WS_NIX
    if (!uiCommon().isCompositingManagerRunning())
        return;
#endif /* VBOX_WS_NIX */

    /* Rebuild shape: */
    QPainterPath shape;
    switch (m_alignment)
    {
        case Qt::AlignTop:
        {
            shape.moveTo(0, 0);
            shape.lineTo(shape.currentPosition().x(), height() - 10);
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(20, 20)).translated(0, -10), 180, 90);
            shape.lineTo(width() - 10, shape.currentPosition().y());
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(20, 20)).translated(-10, -20), 270, 90);
            shape.lineTo(shape.currentPosition().x(), 0);
            shape.closeSubpath();
            break;
        }
        case Qt::AlignBottom:
        {
            shape.moveTo(0, height());
            shape.lineTo(shape.currentPosition().x(), 10);
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(20, 20)).translated(0, -10), 180, -90);
            shape.lineTo(width() - 10, shape.currentPosition().y());
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(20, 20)).translated(-10, 0), 90, -90);
            shape.lineTo(shape.currentPosition().x(), height());
            shape.closeSubpath();
            break;
        }
        default:
            break;
    }
    m_shape = shape;

    /* Update: */
    update();
}


/*********************************************************************************************************************************
*   Class UIMiniToolBar implementation.                                                                                          *
*********************************************************************************************************************************/

/* static */
Qt::WindowFlags UIMiniToolBar::defaultWindowFlags(GeometryType geometryType)
{
    /* Not everywhere: */
    Q_UNUSED(geometryType);

#ifdef VBOX_WS_NIX
    /* Depending on current WM: */
    switch (uiCommon().typeOfWindowManager())
    {
        // WORKAROUND:
        // By strange reason, frameless full-screen windows under certain WMs
        // do not respect the transient relationship between each other.
        // By nor less strange reason, frameless full-screen *tool* windows
        // respects such relationship, so we are doing what WM want.
        case X11WMType_GNOMEShell:
        case X11WMType_KWin:
        case X11WMType_Metacity:
        case X11WMType_Mutter:
        case X11WMType_Xfwm4:
            return geometryType == GeometryType_Full ?
                   Qt::Tool | Qt::FramelessWindowHint :
                   Qt::Window | Qt::FramelessWindowHint;
        default: break;
    }
#endif /* VBOX_WS_NIX */

    /* Frameless window by default: */
    return Qt::Window | Qt::FramelessWindowHint;
}

UIMiniToolBar::UIMiniToolBar(QWidget *pParent,
                             GeometryType geometryType,
                             Qt::Alignment alignment,
                             bool fAutoHide /* = true */,
                             int iWindowIndex /* = -1 */)
    : QWidget(0, defaultWindowFlags(geometryType))
    /* Variables: General stuff: */
    , m_pParent(pParent)
    , m_geometryType(geometryType)
    , m_alignment(alignment)
    , m_fAutoHide(fAutoHide)
    , m_iWindowIndex(iWindowIndex)
    /* Variables: Contents stuff: */
    , m_pArea(0)
    , m_pToolbar(0)
    /* Variables: Hover stuff: */
    , m_fHovered(false)
    , m_pHoverEnterTimer(0)
    , m_pHoverLeaveTimer(0)
    , m_pAnimation(0)
#ifdef VBOX_WS_NIX
    , m_fIsParentMinimized(false)
#endif
{
    /* Prepare: */
    prepare();
}

UIMiniToolBar::~UIMiniToolBar()
{
    /* Cleanup: */
    cleanup();
}

void UIMiniToolBar::setAlignment(Qt::Alignment alignment)
{
    /* Make sure toolbar created: */
    AssertPtrReturnVoid(m_pToolbar);

    /* Make sure alignment really changed: */
    if (m_alignment == alignment)
        return;

    /* Update alignment: */
    m_alignment = alignment;

    /* Adjust geometry: */
    adjustGeometry();

    /* Propagate to child to update shape: */
    m_pToolbar->setAlignment(m_alignment);
}

void UIMiniToolBar::setAutoHide(bool fAutoHide, bool fPropagateToChild /* = true */)
{
    /* Make sure toolbar created: */
    AssertPtrReturnVoid(m_pToolbar);

    /* Make sure auto-hide really changed: */
    if (m_fAutoHide == fAutoHide)
        return;

    /* Update auto-hide: */
    m_fAutoHide = fAutoHide;

    /* Adjust geometry: */
    adjustGeometry();

    /* Propagate to child to update action if necessary: */
    if (fPropagateToChild)
        m_pToolbar->setAutoHide(m_fAutoHide);
}

void UIMiniToolBar::setText(const QString &strText)
{
    /* Make sure toolbar created: */
    AssertPtrReturnVoid(m_pToolbar);

    /* Propagate to child: */
    m_pToolbar->setText(strText);
}

void UIMiniToolBar::addMenus(const QList<QMenu*> &menus)
{
    /* Make sure toolbar created: */
    AssertPtrReturnVoid(m_pToolbar);

    /* Propagate to child: */
    m_pToolbar->addMenus(menus);
}

void UIMiniToolBar::adjustGeometry()
{
    /* Resize toolbar to minimum size: */
    m_pToolbar->resize(m_pToolbar->sizeHint());

    /* Calculate toolbar position: */
    int iX = 0, iY = 0;
    iX = width() / 2 - m_pToolbar->width() / 2;
    switch (m_alignment)
    {
        case Qt::AlignTop:    iY = 0; break;
        case Qt::AlignBottom: iY = height() - m_pToolbar->height(); break;
        default: break;
    }

    /* Update auto-hide animation: */
    m_shownToolbarPosition = QPoint(iX, iY);
    switch (m_alignment)
    {
        case Qt::AlignTop:    m_hiddenToolbarPosition = m_shownToolbarPosition - QPoint(0, m_pToolbar->height() - 3); break;
        case Qt::AlignBottom: m_hiddenToolbarPosition = m_shownToolbarPosition + QPoint(0, m_pToolbar->height() - 3); break;
    }
    m_pAnimation->update();

    /* Update toolbar geometry if known: */
    if (property("AnimationState").toString() == "Final")
        m_pToolbar->move(m_shownToolbarPosition);
    else
        m_pToolbar->move(m_hiddenToolbarPosition);

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /* Adjust window mask: */
    setMask(m_pToolbar->geometry());
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
}

bool UIMiniToolBar::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Detect if we have window activation stolen: */
    if (pWatched == this && pEvent->type() == QEvent::WindowActivate)
    {
#if   defined(VBOX_WS_WIN)
        /* Just call the method asynchronously, after possible popups opened: */
        QTimer::singleShot(0, this, SLOT(sltCheckWindowActivationSanity()));
#elif defined(VBOX_WS_NIX)
        // WORKAROUND:
        // Under certain WMs we can receive stolen activation event too early,
        // returning activation to initial source immediately makes no sense.
        // In fact, Qt is not become aware of actual window activation later,
        // so we are going to check for window activation in let's say 100ms.
        QTimer::singleShot(100, this, SLOT(sltCheckWindowActivationSanity()));
#endif /* VBOX_WS_NIX */
    }

    /* If that's parent window event: */
    if (pWatched == m_pParent)
    {
        switch (pEvent->type())
        {
            case QEvent::Hide:
            {
                /* Skip if parent or we are minimized: */
                if (   isParentMinimized()
                    || isMinimized())
                    break;

                /* Asynchronously call for sltHide(): */
                LogRel2(("GUI: UIMiniToolBar::eventFilter: Parent hide event\n"));
                QMetaObject::invokeMethod(this, "sltHide", Qt::QueuedConnection);
                break;
            }
            case QEvent::Show:
            {
                /* Skip if parent or we are minimized: */
                if (   isParentMinimized()
                    || isMinimized())
                    break;

                /* Asynchronously call for sltShow(): */
                LogRel2(("GUI: UIMiniToolBar::eventFilter: Parent show event\n"));
                QMetaObject::invokeMethod(this, "sltShow", Qt::QueuedConnection);
                break;
            }
            case QEvent::Move:
            {
                // WORKAROUND:
                // In certain cases there can be that parent is moving outside of
                // full-screen geometry. That for example can happen if virtual
                // desktop being changed. We should ignore Move event in such case.
                /* Skip if parent is outside of full-screen geometry: */
                QMoveEvent *pMoveEvent = static_cast<QMoveEvent*>(pEvent);
                if (!gpDesktop->screenGeometry(m_pParent).contains(pMoveEvent->pos()))
                    break;
                /* Skip if parent or we are invisible: */
                if (   !m_pParent->isVisible()
                    || !isVisible())
                    break;
                /* Skip if parent or we are minimized: */
                if (   isParentMinimized()
                    || isMinimized())
                    break;

                /* Asynchronously call for sltShow(): */
                LogRel2(("GUI: UIMiniToolBar::eventFilter: Parent move event\n"));
                QMetaObject::invokeMethod(this, "sltShow", Qt::QueuedConnection);
                break;
            }
            case QEvent::Resize:
            {
                /* Skip if parent or we are invisible: */
                if (   !m_pParent->isVisible()
                    || !isVisible())
                    break;
                /* Skip if parent or we are minimized: */
                if (   isParentMinimized()
                    || isMinimized())
                    break;

                /* Asynchronously call for sltShow(): */
                LogRel2(("GUI: UIMiniToolBar::eventFilter: Parent resize event\n"));
                QMetaObject::invokeMethod(this, "sltShow", Qt::QueuedConnection);
                break;
            }
#ifdef VBOX_WS_NIX
            case QEvent::WindowStateChange:
            {
                /* Watch for parent window state changes: */
                QWindowStateChangeEvent *pChangeEvent = static_cast<QWindowStateChangeEvent*>(pEvent);
                LogRel2(("GUI: UIMiniToolBar::eventFilter: Parent window state changed from %d to %d\n",
                         (int)pChangeEvent->oldState(), (int)m_pParent->windowState()));

                if (   m_pParent->windowState() & Qt::WindowMinimized
                    && !m_fIsParentMinimized)
                {
                    /* Mark parent window minimized, isMinimized() is not enough due to Qt5vsX11 fight: */
                    LogRel2(("GUI: UIMiniToolBar::eventFilter: Parent window is minimized\n"));
                    m_fIsParentMinimized = true;
                }
                else
                if (m_fIsParentMinimized)
                {
                    switch (m_geometryType)
                    {
                        case GeometryType_Available:
                        {
                            if (   m_pParent->windowState() == Qt::WindowMaximized
                                && pChangeEvent->oldState() == Qt::WindowNoState)
                            {
                                /* Mark parent window non-minimized, isMinimized() is not enough due to Qt5vsX11 fight: */
                                LogRel2(("GUI: UIMiniToolBar::eventFilter: Parent window is maximized\n"));
                                m_fIsParentMinimized = false;
                            }
                            break;
                        }
                        case GeometryType_Full:
                        {
                            if (   m_pParent->windowState() == Qt::WindowFullScreen
                                && pChangeEvent->oldState() == Qt::WindowNoState)
                            {
                                /* Mark parent window non-minimized, isMinimized() is not enough due to Qt5vsX11 fight: */
                                LogRel2(("GUI: UIMiniToolBar::eventFilter: Parent window is full-screen\n"));
                                m_fIsParentMinimized = false;
                            }
                            break;
                        }
                    }
                }
                break;
            }
#endif /* VBOX_WS_NIX */
            default:
                break;
        }
    }

    /* Call to base-class: */
    return QWidget::eventFilter(pWatched, pEvent);
}

void UIMiniToolBar::resizeEvent(QResizeEvent*)
{
    /* Adjust geometry: */
    adjustGeometry();
}

void UIMiniToolBar::enterEvent(QEnterEvent*)
{
    /* Stop the hover-leave timer if necessary: */
    if (m_pHoverLeaveTimer && m_pHoverLeaveTimer->isActive())
        m_pHoverLeaveTimer->stop();

    /* Start the hover-enter timer: */
    if (m_pHoverEnterTimer)
        m_pHoverEnterTimer->start();
}

void UIMiniToolBar::leaveEvent(QEvent*)
{
    // WORKAROUND:
    // No idea why, but GUI receives mouse leave event
    // when the mouse cursor is on the border of screen
    // even if underlying widget is on the border of
    // screen as well, we should detect and ignore that.
    // Besides that, this is a good way to keep the
    // tool-bar visible when the mouse moving through
    // the desktop strut till the real screen border.
    const QPoint cursorPosition = QCursor::pos();
    if (   cursorPosition.y() <= y() + 1
        || cursorPosition.y() >= y() + height() - 1)
        return;

    /* Stop the hover-enter timer if necessary: */
    if (m_pHoverEnterTimer && m_pHoverEnterTimer->isActive())
        m_pHoverEnterTimer->stop();

    /* Start the hover-leave timer: */
    if (m_fAutoHide && m_pHoverLeaveTimer)
        m_pHoverLeaveTimer->start();
}

void UIMiniToolBar::sltHandleToolbarResize()
{
    /* Adjust geometry: */
    adjustGeometry();
}

void UIMiniToolBar::sltAutoHideToggled()
{
    /* Propagate from child: */
    setAutoHide(m_pToolbar->autoHide(), false);
    emit sigAutoHideToggled(m_pToolbar->autoHide());
}

void UIMiniToolBar::sltHoverEnter()
{
    /* Mark as 'hovered' if necessary: */
    if (!m_fHovered)
    {
        m_fHovered = true;
        emit sigHoverEnter();
    }
}

void UIMiniToolBar::sltHoverLeave()
{
    /* Mark as 'unhovered' if necessary: */
    if (m_fHovered)
    {
        m_fHovered = false;
        if (m_fAutoHide)
            emit sigHoverLeave();
    }
}

void UIMiniToolBar::sltCheckWindowActivationSanity()
{
    /* Do nothing if parent window is already active: */
    if (   m_pParent
        && QGuiApplication::focusWindow() == m_pParent->windowHandle())
        return;

    /* We can't touch window activation if have modal or popup
     * window opened, otherwise internal Qt state get flawed: */
    if (   QApplication::activeModalWidget()
        || QApplication::activePopupWidget())
    {
        /* But we should recheck the state in let's say 300ms: */
        QTimer::singleShot(300, this, SLOT(sltCheckWindowActivationSanity()));
        return;
    }

    /* Notify listener about we have stole window activation: */
    emit sigNotifyAboutWindowActivationStolen();
}

void UIMiniToolBar::sltHide()
{
    LogRel(("GUI: Hide mini-toolbar for window #%d\n", m_iWindowIndex));

#if defined(VBOX_WS_MAC)

    // Nothing

#elif defined(VBOX_WS_WIN)

    /* Reset window state to NONE and hide it: */
    setWindowState(Qt::WindowNoState);
    hide();

#elif defined(VBOX_WS_NIX)

    /* Just hide window: */
    hide();

#else

# warning "port me"

#endif
}

void UIMiniToolBar::sltShow()
{
    LogRel(("GUI: Show mini-toolbar for window #%d\n", m_iWindowIndex));

    /* Update transience: */
    sltAdjustTransience();

#if defined(VBOX_WS_MAC)

    // Nothing

#elif defined(VBOX_WS_WIN)

    // WORKAROUND:
    // If the host-screen is changed => we should
    // reset window state to NONE first because
    // we need an expose on showFullScreen call.
    if (m_geometryType == GeometryType_Full)
        setWindowState(Qt::WindowNoState);

    /* Adjust window: */
    sltAdjust();
    /* Show window in necessary mode: */
    switch (m_geometryType)
    {
        case GeometryType_Available:
        {
            /* Show normal: */
            show();
            break;
        }
        case GeometryType_Full:
        {
            /* Show full-screen: */
            showFullScreen();
            break;
        }
    }

#elif defined(VBOX_WS_NIX)

    /* Show window in necessary mode: */
    switch (m_geometryType)
    {
        case GeometryType_Available:
        {
            /* Adjust window: */
            sltAdjust();
            /* Show maximized: */
            if (!isMaximized())
                showMaximized();
            break;
        }
        case GeometryType_Full:
        {
            /* Show full-screen: */
            showFullScreen();
            /* Adjust window: */
            sltAdjust();
            break;
        }
    }

#else

# warning "port me"

#endif

    /* Simulate toolbar auto-hiding: */
    simulateToolbarAutoHiding();
}

void UIMiniToolBar::sltAdjust()
{
    LogRel(("GUI: Adjust mini-toolbar for window #%d\n", m_iWindowIndex));

    /* Get corresponding host-screen: */
    const int iHostScreenCount = UIDesktopWidgetWatchdog::screenCount();
    int iHostScreen = UIDesktopWidgetWatchdog::screenNumber(m_pParent);
    // WORKAROUND:
    // When switching host-screen count, especially in complex cases where RDP client is "replacing" host-screen(s) with own virtual-screen(s),
    // Qt could behave quite arbitrary and laggy, and due to racing there could be a situation when QDesktopWidget::screenNumber() returns -1
    // as a host-screen number where the parent window is currently located. We should handle this situation anyway, so let's assume the parent
    // window is located on primary (0) host-screen if it's present or ignore this request at all.
    if (iHostScreen < 0 || iHostScreen >= iHostScreenCount)
    {
        if (iHostScreenCount > 0)
        {
            LogRel(("GUI:  Mini-toolbar parent window #%d is located on invalid host-screen #%d. Fallback to primary.\n", m_iWindowIndex, iHostScreen));
            iHostScreen = 0;
        }
        else
        {
            LogRel(("GUI:  Mini-toolbar parent window #%d is located on invalid host-screen #%d. Ignore request.\n", m_iWindowIndex, iHostScreen));
            return;
        }
    }

    /* Get corresponding working area: */
    QRect workingArea;
    switch (m_geometryType)
    {
        case GeometryType_Available: workingArea = gpDesktop->availableGeometry(iHostScreen); break;
        case GeometryType_Full:      workingArea = gpDesktop->screenGeometry(iHostScreen); break;
    }
    Q_UNUSED(workingArea);

#if defined(VBOX_WS_MAC)

    // Nothing

#elif defined(VBOX_WS_WIN)

    switch (m_geometryType)
    {
        case GeometryType_Available:
        {
            /* Set appropriate window size: */
            const QSize newSize = workingArea.size();
            LogRel(("GUI:  Resize mini-toolbar for window #%d to %dx%d\n",
                     m_iWindowIndex, newSize.width(), newSize.height()));
            resize(newSize);

            /* Move window onto required screen: */
            const QPoint newPosition = workingArea.topLeft();
            LogRel(("GUI:  Move mini-toolbar for window #%d to %dx%d\n",
                     m_iWindowIndex, newPosition.x(), newPosition.y()));
            move(newPosition);

            break;
        }
        case GeometryType_Full:
        {
            /* Map window onto required screen: */
            LogRel(("GUI:  Map mini-toolbar for window #%d to screen %d of %d\n",
                     m_iWindowIndex, iHostScreen, qApp->screens().size()));
            windowHandle()->setScreen(qApp->screens().at(iHostScreen));

            /* Set appropriate window size: */
            const QSize newSize = workingArea.size();
            LogRel(("GUI:  Resize mini-toolbar for window #%d to %dx%d\n",
                     m_iWindowIndex, newSize.width(), newSize.height()));
            resize(newSize);

            break;
        }
    }

#elif defined(VBOX_WS_NIX)

    switch (m_geometryType)
    {
        case GeometryType_Available:
        {
            /* Make sure we are located on corresponding host-screen: */
            if (   UIDesktopWidgetWatchdog::screenCount() > 1
                && (x() != workingArea.x() || y() != workingArea.y()))
            {
                // WORKAROUND:
                // With Qt5 on KDE we can't just move the window onto desired host-screen if
                // window is maximized. So we have to show it normal first of all:
                if (isVisible() && isMaximized())
                    showNormal();

                // WORKAROUND:
                // With Qt5 on X11 we can't just move the window onto desired host-screen if
                // window size is more than the available geometry (working area) of that
                // host-screen. So we are resizing it to a smaller size first of all:
                const QSize newSize = workingArea.size() * .9;
                LogRel(("GUI:  Resize mini-toolbar for window #%d to smaller size %dx%d\n",
                        m_iWindowIndex, newSize.width(), newSize.height()));
                resize(newSize);

                /* Move window onto required screen: */
                const QPoint newPosition = workingArea.topLeft();
                LogRel(("GUI:  Move mini-toolbar for window #%d to %dx%d\n",
                        m_iWindowIndex, newPosition.x(), newPosition.y()));
                move(newPosition);
            }

            break;
        }
        case GeometryType_Full:
        {
            /* Determine whether we should use the native full-screen mode: */
            const bool fUseNativeFullScreen =    NativeWindowSubsystem::X11SupportsFullScreenMonitorsProtocol()
                                              && !gEDataManager->legacyFullscreenModeRequested();
            if (fUseNativeFullScreen)
            {
                /* Tell recent window managers which host-screen this window should be mapped to: */
                NativeWindowSubsystem::X11SetFullScreenMonitor(this, iHostScreen);
            }

            /* Set appropriate window size: */
            const QSize newSize = workingArea.size();
            LogRel(("GUI:  Resize mini-toolbar for window #%d to %dx%d\n",
                    m_iWindowIndex, newSize.width(), newSize.height()));
            resize(newSize);

            /* Move window onto required screen: */
            const QPoint newPosition = workingArea.topLeft();
            LogRel(("GUI:  Move mini-toolbar for window #%d to %dx%d\n",
                    m_iWindowIndex, newPosition.x(), newPosition.y()));
            move(newPosition);

            /* Re-apply the full-screen state lost on above move(): */
            setWindowState(Qt::WindowFullScreen);

            break;
        }
    }

#else

# warning "port me"

#endif
}

void UIMiniToolBar::sltAdjustTransience()
{
    // WORKAROUND:
    // Make sure win id is generated,
    // else Qt5 can crash otherwise.
    winId();
    m_pParent->winId();

    /* Add the transience dependency: */
    windowHandle()->setTransientParent(m_pParent->windowHandle());
}

void UIMiniToolBar::prepare()
{
    /* Install event-filters: */
    installEventFilter(this);
    m_pParent->installEventFilter(this);

#if   defined(VBOX_WS_WIN)
    /* No background until first paint-event: */
    setAttribute(Qt::WA_NoSystemBackground);
    /* Enable translucency through Qt API: */
    setAttribute(Qt::WA_TranslucentBackground);
#elif defined(VBOX_WS_NIX)
    /* Enable translucency through Qt API if supported: */
    if (uiCommon().isCompositingManagerRunning())
        setAttribute(Qt::WA_TranslucentBackground);
#endif /* VBOX_WS_NIX */

    /* Make sure we have no focus: */
    setFocusPolicy(Qt::NoFocus);

    /* Prepare area: */
    m_pArea = new QWidget;
    {
        /* Allow any area size: */
        m_pArea->setMinimumSize(QSize(1, 1));
        /* Configure own background: */
        QPalette pal = m_pArea->palette();
        pal.setColor(QPalette::Window, QColor(Qt::transparent));
        m_pArea->setPalette(pal);
        /* Layout area according parent-widget: */
        QVBoxLayout *pMainLayout = new QVBoxLayout(this);
        pMainLayout->setContentsMargins(0, 0, 0, 0);
        pMainLayout->addWidget(m_pArea);
        /* Make sure we have no focus: */
        m_pArea->setFocusPolicy(Qt::NoFocus);
    }

    /* Prepare mini-toolbar: */
    m_pToolbar = new UIMiniToolBarPrivate;
    {
        /* Make sure we have no focus: */
        m_pToolbar->setFocusPolicy(Qt::NoFocus);
        /* Propagate known options to child: */
        m_pToolbar->setAutoHide(m_fAutoHide);
        m_pToolbar->setAlignment(m_alignment);
        /* Configure own background: */
        QPalette pal = m_pToolbar->palette();
        pal.setColor(QPalette::Window, QApplication::palette().color(QPalette::Window));
        m_pToolbar->setPalette(pal);
        /* Configure child connections: */
        connect(m_pToolbar, &UIMiniToolBarPrivate::sigResized, this, &UIMiniToolBar::sltHandleToolbarResize);
        connect(m_pToolbar, &UIMiniToolBarPrivate::sigAutoHideToggled, this, &UIMiniToolBar::sltAutoHideToggled);
        connect(m_pToolbar, &UIMiniToolBarPrivate::sigMinimizeAction, this, &UIMiniToolBar::sigMinimizeAction);
        connect(m_pToolbar, &UIMiniToolBarPrivate::sigExitAction, this, &UIMiniToolBar::sigExitAction);
        connect(m_pToolbar, &UIMiniToolBarPrivate::sigCloseAction, this, &UIMiniToolBar::sigCloseAction);
        /* Add child to area: */
        m_pToolbar->setParent(m_pArea);
        /* Make sure we have no focus: */
        m_pToolbar->setFocusPolicy(Qt::NoFocus);
    }

    /* Prepare hover-enter/leave timers: */
    m_pHoverEnterTimer = new QTimer(this);
    {
        m_pHoverEnterTimer->setSingleShot(true);
        m_pHoverEnterTimer->setInterval(500);
        connect(m_pHoverEnterTimer, &QTimer::timeout, this, &UIMiniToolBar::sltHoverEnter);
    }
    m_pHoverLeaveTimer = new QTimer(this);
    {
        m_pHoverLeaveTimer->setSingleShot(true);
        m_pHoverLeaveTimer->setInterval(500);
        connect(m_pHoverLeaveTimer, &QTimer::timeout, this, &UIMiniToolBar::sltHoverLeave);
    }

    /* Install 'auto-hide' animation to 'toolbarPosition' property: */
    m_pAnimation = UIAnimation::installPropertyAnimation(this,
                                                         "toolbarPosition",
                                                         "hiddenToolbarPosition", "shownToolbarPosition",
                                                         SIGNAL(sigHoverEnter()), SIGNAL(sigHoverLeave()),
                                                         true);

    /* Adjust geometry first time: */
    adjustGeometry();

#ifdef VBOX_WS_NIX
    /* Hide mini-toolbar from taskbar and pager: */
    NativeWindowSubsystem::X11SetSkipTaskBarFlag(this);
    NativeWindowSubsystem::X11SetSkipPagerFlag(this);
#endif
}

void UIMiniToolBar::cleanup()
{
    /* Stop hover-enter/leave timers: */
    if (m_pHoverEnterTimer && m_pHoverEnterTimer->isActive())
        m_pHoverEnterTimer->stop();
    if (m_pHoverLeaveTimer && m_pHoverLeaveTimer->isActive())
        m_pHoverLeaveTimer->stop();

    /* Destroy animation before toolbar: */
    delete m_pAnimation;
    m_pAnimation = 0;

    /* Destroy toolbar after animation: */
    delete m_pToolbar;
    m_pToolbar = 0;
}

void UIMiniToolBar::simulateToolbarAutoHiding()
{
    /* This simulation helps user to notice
     * toolbar location, so it will be used only
     * 1. if toolbar unhovered and
     * 2. auto-hide feature enabled: */
    if (m_fHovered || !m_fAutoHide)
        return;

    /* Simulate hover-leave event: */
    m_fHovered = true;
    m_pHoverLeaveTimer->start();
}

void UIMiniToolBar::setToolbarPosition(QPoint point)
{
    /* Update position: */
    AssertPtrReturnVoid(m_pToolbar);
    m_pToolbar->move(point);

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /* Update window mask: */
    setMask(m_pToolbar->geometry());
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
}

QPoint UIMiniToolBar::toolbarPosition() const
{
    /* Return position: */
    AssertPtrReturn(m_pToolbar, QPoint());
    return m_pToolbar->pos();
}

bool UIMiniToolBar::isParentMinimized() const
{
#ifdef VBOX_WS_NIX
    return m_fIsParentMinimized;
#else
    return m_pParent->isMinimized();
#endif
}

#include "UIMiniToolBar.moc"
