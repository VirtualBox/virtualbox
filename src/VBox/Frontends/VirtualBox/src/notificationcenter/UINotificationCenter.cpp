/* $Id: UINotificationCenter.cpp 108938 2025-03-25 11:16:36Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UINotificationCenter class implementation.
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
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QSignalTransition>
#include <QState>
#include <QStateMachine>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIToolButton.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UINotificationCenter.h"
#include "UINotificationObjectItem.h"
#include "UINotificationModel.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** QScrollArea extension to make notification scroll-area more versatile. */
class UINotificationScrollArea : public QScrollArea
{
    Q_OBJECT;

public:

    /** Creates notification scroll-area passing @a pParent to the base-class. */
    UINotificationScrollArea(QWidget *pParent = 0);

    /** Returns minimum size-hint. */
    virtual QSize minimumSizeHint() const RT_OVERRIDE RT_FINAL;

    /** Assigns scrollable @a pWidget.
      * @note  Keep in mind that's an override, but NOT a virtual method. */
    void setWidget(QWidget *pWidget);

protected:

    /** Preprocesses @a pEvent for registered @a pWatched object. */
    virtual bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE RT_FINAL;
};


/*********************************************************************************************************************************
*   Class UINotificationScrollArea implementation.                                                                               *
*********************************************************************************************************************************/

UINotificationScrollArea::UINotificationScrollArea(QWidget *pParent /* = 0 */)
    : QScrollArea(pParent)
{
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
}

QSize UINotificationScrollArea::minimumSizeHint() const
{
    /* So, here is the logic,
     * we are taking width from widget if it's present,
     * while keeping height calculated by the base-class. */
    const QSize msh = QScrollArea::minimumSizeHint();
    return widget() ? QSize(widget()->minimumSizeHint().width(), msh.height()) : msh;
}

void UINotificationScrollArea::setWidget(QWidget *pWidget)
{
    /* We'd like to listen for a new widget's events: */
    if (widget())
        widget()->removeEventFilter(this);
    pWidget->installEventFilter(this);

    /* Call to base-class: */
    QScrollArea::setWidget(pWidget);
}

bool UINotificationScrollArea::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* For listened widget: */
    if (pWatched == widget())
    {
        switch (pEvent->type())
        {
            /* We'd like to handle layout-request events: */
            case QEvent::LayoutRequest:
                updateGeometry();
                break;
            default:
                break;
        }
    }

    /* Call to base-class: */
    return QScrollArea::eventFilter(pWatched, pEvent);
}


/*********************************************************************************************************************************
*   Class UINotificationCenter implementation.                                                                                   *
*********************************************************************************************************************************/

/* static */
UINotificationCenter *UINotificationCenter::s_pInstance = 0;

/* static */
void UINotificationCenter::create(QWidget *pParent /* = 0 */)
{
    AssertReturnVoid(!s_pInstance);
    s_pInstance = new UINotificationCenter(pParent);
}

/* static */
void UINotificationCenter::destroy()
{
    AssertPtrReturnVoid(s_pInstance);
    delete s_pInstance;
    s_pInstance = 0;
}

/* static */
UINotificationCenter *UINotificationCenter::instance()
{
    return s_pInstance;
}

UINotificationCenter::UINotificationCenter(QWidget *pParent)
    : QWidget(pParent)
    , m_pModel(0)
    , m_enmAlignment(Qt::AlignTop)
    , m_enmOrder(Qt::AscendingOrder)
    , m_pLayoutMain(0)
    , m_pLayoutButtons(0)
    , m_pButtonOpen(0)
    , m_pButtonToggleSorting(0)
#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
    , m_pButtonKeepFinished(0)
#endif
    , m_pButtonRemoveFinished(0)
    , m_pLayoutItems(0)
    , m_pStateMachineSliding(0)
    , m_iAnimatedValue(0)
    , m_pTimerOpen(0)
    , m_fLastResult(false)
{
    prepare();
}

UINotificationCenter::~UINotificationCenter()
{
    cleanup();
}

void UINotificationCenter::setParent(QWidget *pParent)
{
    /* Additionally hide if parent unset: */
    if (!pParent)
        setHidden(true);

    /* Uninstall filter from previous parent: */
    if (parent())
        parent()->removeEventFilter(this);

    /* Reparent: */
    QWidget::setParent(pParent);

    /* Install filter to new parent: */
    if (parent())
        parent()->installEventFilter(this);

    /* Show only if there is something to show: */
    if (parent())
        setHidden(m_pModel->ids().isEmpty());
}

void UINotificationCenter::invoke()
{
    /* Open if center isn't opened yet: */
    if (!m_pButtonOpen->isChecked())
        m_pButtonOpen->animateClick();
}

QUuid UINotificationCenter::append(UINotificationObject *pObject)
{
    /* Sanity check: */
    AssertPtrReturn(m_pModel, QUuid());
    AssertPtrReturn(pObject, QUuid());

    /* Is object critical? */
    const bool fCritical = pObject->isCritical();
    /* Is object progress? */
    const bool fProgress = pObject->inherits("UINotificationProgress");

    /* Handle object. Be aware it can be deleted during handling! */
    const QUuid uId = m_pModel->appendObject(pObject);

    /* If object is critical and center isn't opened yet: */
    if (!m_pButtonOpen->isChecked() && fCritical)
    {
        /* We should delay progresses for a bit: */
        const int iDelay = fProgress ? 2000 : 0;
        /* We should issue an open request: */
        AssertPtrReturn(m_pTimerOpen, uId);
        m_uOpenObjectId = uId;
        m_pTimerOpen->start(iDelay);
    }

    return uId;
}

void UINotificationCenter::revoke(const QUuid &uId)
{
    AssertReturnVoid(!uId.isNull());
    return m_pModel->revokeObject(uId);
}

bool UINotificationCenter::handleNow(UINotificationProgress *pProgress)
{
    /* Check for the recursive run: */
    AssertMsgReturn(!m_pEventLoop, ("UINotificationCenter::handleNow() is called recursively!\n"), false);

    /* Reset the result: */
    m_fLastResult = false;

    /* Guard progress for the case
     * it destroyed itself in his append call: */
    QPointer<UINotificationProgress> guardProgress = pProgress;
    connect(pProgress, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationCenter::sltHandleProgressFinished);
    append(pProgress);

    /* Is progress still valid? */
    if (guardProgress.isNull())
        return m_fLastResult;
    /* Is progress still running? */
    if (guardProgress->isDone())
        return m_fLastResult;

    /* Create a local event-loop: */
    QEventLoop eventLoop;
    m_pEventLoop = &eventLoop;

    /* Guard ourself for the case
     * we destroyed ourself in our event-loop: */
    QPointer<UINotificationCenter> guardThis = this;

    /* Start the blocking event-loop: */
    eventLoop.exec();

    /* Are we still valid? */
    if (guardThis.isNull())
        return false;

    /* Cleanup event-loop: */
    m_pEventLoop = 0;

    /* Return actual result: */
    return m_fLastResult;
}

bool UINotificationCenter::hasOperationsPending() const
{
    return m_pEventLoop;
}

void UINotificationCenter::abortOperations()
{
    m_pEventLoop->exit();
    emit sigOperationsAborted();
}

void UINotificationCenter::sltRetranslateUI()
{
    if (m_pButtonOpen)
        m_pButtonOpen->setToolTip(tr("Open notification center"));
    if (m_pButtonToggleSorting)
        m_pButtonToggleSorting->setToolTip(tr("Toggle ascending/descending order"));
#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
    if (m_pButtonKeepFinished)
        m_pButtonKeepFinished->setToolTip(tr("Keep finished progresses"));
#endif
    if (m_pButtonRemoveFinished)
        m_pButtonRemoveFinished->setToolTip(tr("Delete finished notifications"));
}

bool UINotificationCenter::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* For parent object only: */
    if (pObject == parent())
    {
        /* Handle required event types: */
        switch (pEvent->type())
        {
            case QEvent::Resize:
            {
                /* When parent being resized we want
                 * to adjust overlay accordingly. */
                adjustGeometry();
                break;
            }
            default:
                break;
        }
    }

    /* Call to base-class: */
    return QWidget::eventFilter(pObject, pEvent);
}

bool UINotificationCenter::event(QEvent *pEvent)
{
    /* Handle required event types: */
    switch (pEvent->type())
    {
        /* When we are being asked to update layout
         * we want to adjust overlay accordingly. */
        case QEvent::LayoutRequest:
        {
            adjustGeometry();
            break;
        }
        /* When we are being resized or moved we want
         * to adjust transparency mask accordingly. */
        case QEvent::Move:
        case QEvent::Resize:
        {
            adjustMask();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QWidget::event(pEvent);
}

void UINotificationCenter::paintEvent(QPaintEvent *pEvent)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pEvent);

    /* Prepare painter: */
    QPainter painter(this);

    /* Limit painting with incoming rectangle: */
    painter.setClipRect(pEvent->rect());

    /* Paint background: */
    paintBackground(&painter);
    paintFrame(&painter);
}

void UINotificationCenter::sltHandleAlignmentChange()
{
    /* Update alignment: */
    m_enmAlignment = gEDataManager->notificationCenterAlignment();

    /* Re-insert to layout: */
    m_pLayoutMain->removeItem(m_pLayoutButtons);
    m_pLayoutMain->insertLayout(m_enmAlignment == Qt::AlignTop ? 0 : -1, m_pLayoutButtons);

    /* Adjust mask to make sure button visible, layout should be finalized already: */
    QCoreApplication::sendPostedEvents(0, QEvent::LayoutRequest);
    adjustMask();
}

void UINotificationCenter::sltIssueOrderChange()
{
    const Qt::SortOrder enmSortOrder = m_pButtonToggleSorting->isChecked()
                                     ? Qt::AscendingOrder
                                     : Qt::DescendingOrder;
    gEDataManager->setNotificationCenterOrder(enmSortOrder);
}

void UINotificationCenter::sltHandleOrderChange()
{
    /* Save new order: */
    m_enmOrder = gEDataManager->notificationCenterOrder();

    /* Cleanup items first: */
    cleanupItems();

    /* Populate model contents again: */
    foreach (const QUuid &uId, m_pModel->ids())
    {
        UINotificationObjectItem *pItem = UINotificationItem::create(this, m_pModel->objectById(uId));
        m_items[uId] = pItem;
        m_pLayoutItems->insertWidget(m_enmOrder == Qt::AscendingOrder ? -1 : 0, pItem);
    }

    /* Hide and slide away if there are no notifications to show: */
    setHidden(m_pModel->ids().isEmpty());
    if (m_pModel->ids().isEmpty() && m_pButtonOpen->isChecked())
        m_pButtonOpen->toggle();
}

void UINotificationCenter::sltHandleOpenButtonToggled(bool fToggled)
{
    if (fToggled)
        emit sigOpen();
    else
        emit sigClose();
}

#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
void UINotificationCenter::sltHandleKeepButtonToggled(bool fToggled)
{
    gEDataManager->setKeepSuccessfullNotificationProgresses(fToggled);
}
#endif /* VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON */

void UINotificationCenter::sltHandleRemoveFinishedButtonClicked()
{
    m_pModel->revokeFinishedObjects();
}

void UINotificationCenter::sltHandleOpenButtonContextMenuRequested(const QPoint &)
{
    /* Create menu: */
    QMenu menu(m_pButtonOpen);

    /* Create action: */
    QAction action(  m_enmAlignment == Qt::AlignTop
                   ? tr("Align Bottom")
                   : tr("Align Top"),
                   m_pButtonOpen);
    menu.addAction(&action);

    /* Execute menu, check if any (single) action is clicked: */
    QAction *pAction = menu.exec(m_pButtonOpen->mapToGlobal(QPoint(m_pButtonOpen->width(), 0)));
    if (pAction)
    {
        const Qt::Alignment enmAlignment = m_enmAlignment == Qt::AlignTop
                                         ? Qt::AlignBottom
                                         : Qt::AlignTop;
        gEDataManager->setNotificationCenterAlignment(enmAlignment);
    }
}

void UINotificationCenter::sltHandleOpenTimerTimeout()
{
    /* Make sure it's invoked by corresponding timer only: */
    QTimer *pTimer = qobject_cast<QTimer*>(sender());
    AssertPtrReturnVoid(pTimer);
    AssertReturnVoid(pTimer == m_pTimerOpen);

    /* Stop corresponding timer: */
    m_pTimerOpen->stop();

    /* Check whether we really closed: */
    if (m_pButtonOpen->isChecked())
        return;

    /* Check whether message with particular ID exists: */
    if (!m_pModel->hasObject(m_uOpenObjectId))
        return;

    /* Toggle open button: */
    m_pButtonOpen->animateClick();
}

void UINotificationCenter::sltHandleModelItemAdded(const QUuid &uId)
{
    /* Add corresponding model item representation: */
    AssertReturnVoid(!m_items.contains(uId));
    UINotificationObjectItem *pItem = UINotificationItem::create(this, m_pModel->objectById(uId));
    m_items[uId] = pItem;
    m_pLayoutItems->insertWidget(m_enmOrder == Qt::AscendingOrder ? -1 : 0, pItem);

    /* Show if there are notifications to show: */
    setHidden(m_pModel->ids().isEmpty());
}

void UINotificationCenter::sltHandleModelItemRemoved(const QUuid &uId)
{
    /* Remove corresponding model item representation if present: */
    if (m_items.contains(uId))
        delete m_items.take(uId);

    /* Hide and slide away if there are no notifications to show: */
    setHidden(m_pModel->ids().isEmpty());
    if (m_pModel->ids().isEmpty() && m_pButtonOpen->isChecked())
        m_pButtonOpen->toggle();
}

void UINotificationCenter::sltHandleProgressFinished()
{
    /* Acquire the sender: */
    UINotificationProgress *pProgress = qobject_cast<UINotificationProgress*>(sender());
    AssertPtrReturnVoid(pProgress);

    /* Set the result: */
    m_fLastResult = pProgress->error().isNull();

    /* Break the loop if exists: */
    if (m_pEventLoop)
        m_pEventLoop->exit();
}

void UINotificationCenter::prepare()
{
    /* Make sure notification-center and it's children always have cursor,
     * even if parent (like VM view) set it to Qt::BlankCursor intentionally. */
    setCursor(Qt::ArrowCursor);

    /* Hide initially: */
    setHidden(true);

    /* Listen for parent events: */
    if (parent())
        parent()->installEventFilter(this);

    /* Prepare the rest of stuff: */
    prepareModel();
    prepareWidgets();
    prepareStateMachineSliding();
    prepareOpenTimer();

    /* Prepare alignment: */
    m_enmAlignment = gEDataManager->notificationCenterAlignment();
    connect(gEDataManager, &UIExtraDataManager::sigNotificationCenterAlignmentChange,
            this, &UINotificationCenter::sltHandleAlignmentChange);
    sltHandleAlignmentChange();
    /* Prepare order: */
    m_enmOrder = gEDataManager->notificationCenterOrder();
    connect(gEDataManager, &UIExtraDataManager::sigNotificationCenterOrderChange,
            this, &UINotificationCenter::sltHandleOrderChange);
    sltHandleOrderChange();

    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UINotificationCenter::sltRetranslateUI);
}

void UINotificationCenter::prepareModel()
{
    m_pModel = new UINotificationModel(this);
    if (m_pModel)
    {
        connect(m_pModel, &UINotificationModel::sigItemAdded,
                this, &UINotificationCenter::sltHandleModelItemAdded);
        connect(m_pModel, &UINotificationModel::sigItemRemoved,
                this, &UINotificationCenter::sltHandleModelItemRemoved);
    }
}

void UINotificationCenter::prepareWidgets()
{
    /* Prepare main layout: */
    m_pLayoutMain = new QVBoxLayout(this);
    if (m_pLayoutMain)
    {
        /* Create container scroll-area: */
        UINotificationScrollArea *pScrollAreaContainer = new UINotificationScrollArea(this);
        if (pScrollAreaContainer)
        {
            /* Prepare container widget: */
            QWidget *pWidgetContainer = new QWidget(pScrollAreaContainer);
            if (pWidgetContainer)
            {
                /* Prepare container layout: */
                QVBoxLayout *pLayoutContainer = new QVBoxLayout(pWidgetContainer);
                if (pLayoutContainer)
                {
                    pLayoutContainer->setContentsMargins(0, 0, 0, 0);

                    /* Prepare items layout: */
                    m_pLayoutItems = new QVBoxLayout;
                    if (m_pLayoutItems)
                        pLayoutContainer->addLayout(m_pLayoutItems);

                    pLayoutContainer->addStretch();
                }

                /* Add to scroll-area: */
                pScrollAreaContainer->setWidget(pWidgetContainer);
            }

            /* Configure container scroll-area: */
            pScrollAreaContainer->setWidgetResizable(true);
            pScrollAreaContainer->setFrameShape(QFrame::NoFrame);
            pScrollAreaContainer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
            pScrollAreaContainer->viewport()->setAutoFillBackground(false);
            pScrollAreaContainer->widget()->setAutoFillBackground(false);

            /* Add to layout: */
            m_pLayoutMain->addWidget(pScrollAreaContainer);
        }

        /* Prepare buttons layout: */
        m_pLayoutButtons = new QHBoxLayout;
        if (m_pLayoutButtons)
        {
            m_pLayoutButtons->setContentsMargins(0, 0, 0, 0);

            /* Prepare open button: */
            m_pButtonOpen = new QIToolButton(this);
            if (m_pButtonOpen)
            {
                m_pButtonOpen->setIcon(UIIconPool::iconSet(":/notification_center_16px.png"));
                m_pButtonOpen->setCheckable(true);
                m_pButtonOpen->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(m_pButtonOpen, &QIToolButton::toggled,
                        this, &UINotificationCenter::sltHandleOpenButtonToggled);
                connect(m_pButtonOpen, &QIToolButton::customContextMenuRequested,
                        this, &UINotificationCenter::sltHandleOpenButtonContextMenuRequested);
                m_pLayoutButtons->addWidget(m_pButtonOpen);
            }

            /* Add stretch: */
            m_pLayoutButtons->addStretch(1);

            /* Prepare toggle-sorting button: */
            m_pButtonToggleSorting = new QIToolButton(this);
            if (m_pButtonToggleSorting)
            {
                m_pButtonToggleSorting->setIcon(UIIconPool::iconSet(":/notification_center_sort_16px.png"));
                m_pButtonToggleSorting->setCheckable(true);
                m_pButtonToggleSorting->setChecked(gEDataManager->notificationCenterOrder() == Qt::AscendingOrder);
                connect(m_pButtonToggleSorting, &QIToolButton::toggled, this, &UINotificationCenter::sltIssueOrderChange);
                m_pLayoutButtons->addWidget(m_pButtonToggleSorting);
            }

#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
            /* Prepare keep-finished button: */
            m_pButtonKeepFinished = new QIToolButton(this);
            if (m_pButtonKeepFinished)
            {
                m_pButtonKeepFinished->setIcon(UIIconPool::iconSet(":/notification_center_hold_progress_16px.png"));
                m_pButtonKeepFinished->setCheckable(true);
                m_pButtonKeepFinished->setChecked(gEDataManager->keepSuccessfullNotificationProgresses());
                connect(m_pButtonKeepFinished, &QIToolButton::toggled, this, &UINotificationCenter::sltHandleKeepButtonToggled);
                m_pLayoutButtons->addWidget(m_pButtonKeepFinished);
            }
#endif /* VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON */

            /* Prepare remove-finished button: */
            m_pButtonRemoveFinished = new QIToolButton(this);
            if (m_pButtonRemoveFinished)
            {
                m_pButtonRemoveFinished->setIcon(UIIconPool::iconSet(":/notification_center_delete_progress_16px.png"));
                connect(m_pButtonRemoveFinished, &QIToolButton::clicked, this, &UINotificationCenter::sltHandleRemoveFinishedButtonClicked);
                m_pLayoutButtons->addWidget(m_pButtonRemoveFinished);
            }

            /* Add to layout: */
            m_pLayoutMain->insertLayout(m_enmAlignment == Qt::AlignTop ? 0 : -1, m_pLayoutButtons);
        }
    }
}

void UINotificationCenter::prepareStateMachineSliding()
{
    /* Create sliding animation state-machine: */
    m_pStateMachineSliding = new QStateMachine(this);
    if (m_pStateMachineSliding)
    {
        /* Create 'closed' state: */
        QState *pStateClosed = new QState(m_pStateMachineSliding);
        /* Create 'opened' state: */
        QState *pStateOpened = new QState(m_pStateMachineSliding);

        /* Configure 'closed' state: */
        if (pStateClosed)
        {
            /* When we entering closed state => we assigning animatedValue to 0: */
            pStateClosed->assignProperty(this, "animatedValue", 0);

            /* Add state transitions: */
            QSignalTransition *pClosedToOpened = pStateClosed->addTransition(this, SIGNAL(sigOpen()), pStateOpened);
            if (pClosedToOpened)
            {
                /* Create forward animation: */
                QPropertyAnimation *pAnimationForward = new QPropertyAnimation(this, "animatedValue", this);
                if (pAnimationForward)
                {
                    pAnimationForward->setEasingCurve(QEasingCurve::InCubic);
                    pAnimationForward->setDuration(300);
                    pAnimationForward->setStartValue(0);
                    pAnimationForward->setEndValue(100);

                    /* Add to transition: */
                    pClosedToOpened->addAnimation(pAnimationForward);
                }
            }
        }

        /* Configure 'opened' state: */
        if (pStateOpened)
        {
            /* When we entering opened state => we assigning animatedValue to 100: */
            pStateOpened->assignProperty(this, "animatedValue", 100);

            /* Add state transitions: */
            QSignalTransition *pOpenedToClosed = pStateOpened->addTransition(this, SIGNAL(sigClose()), pStateClosed);
            if (pOpenedToClosed)
            {
                /* Create backward animation: */
                QPropertyAnimation *pAnimationBackward = new QPropertyAnimation(this, "animatedValue", this);
                if (pAnimationBackward)
                {
                    pAnimationBackward->setEasingCurve(QEasingCurve::InCubic);
                    pAnimationBackward->setDuration(300);
                    pAnimationBackward->setStartValue(100);
                    pAnimationBackward->setEndValue(0);

                    /* Add to transition: */
                    pOpenedToClosed->addAnimation(pAnimationBackward);
                }
            }
        }

        /* Initial state is 'closed': */
        m_pStateMachineSliding->setInitialState(pStateClosed);
        /* Start state-machine: */
        m_pStateMachineSliding->start();
    }
}

void UINotificationCenter::prepareOpenTimer()
{
    m_pTimerOpen = new QTimer(this);
    if (m_pTimerOpen)
        connect(m_pTimerOpen, &QTimer::timeout,
                this, &UINotificationCenter::sltHandleOpenTimerTimeout);
}

void UINotificationCenter::cleanupModel()
{
    delete m_pModel;
    m_pModel = 0;
}

void UINotificationCenter::cleanupItems()
{
    qDeleteAll(m_items);
    m_items.clear();
}

void UINotificationCenter::cleanup()
{
    cleanupModel();
    cleanupItems();
}

void UINotificationCenter::paintBackground(QPainter *pPainter)
{
    /* Acquire palette: */
    const bool fActive = parentWidget() && parentWidget()->isActiveWindow();
    const QPalette pal = QApplication::palette();

    /* Gather suitable color: */
    QColor backgroundColor = pal.color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window).darker(120);
    backgroundColor.setAlpha((double)animatedValue() / 100 * 220);

    /* Acquire pixel metric: */
    const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4;

    /* Adjust rectangle: */
    QRect rectAdjusted = rect();
    rectAdjusted.adjust(iMetric, iMetric, 0, -iMetric);

    /* Paint background: */
    pPainter->fillRect(rectAdjusted, backgroundColor);
}

void UINotificationCenter::paintFrame(QPainter *pPainter)
{
    /* Acquire palette: */
    const bool fActive = parentWidget() && parentWidget()->isActiveWindow();
    QPalette pal = QApplication::palette();

    /* Gather suitable colors: */
    QColor color1 = pal.color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window).lighter(110);
    color1.setAlpha(0);
    QColor color2 = pal.color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window).darker(200);

    /* Acquire pixel metric: */
    const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4;

    /* Top-left corner: */
    QRadialGradient grad1(QPointF(iMetric, iMetric), iMetric);
    {
        grad1.setColorAt(0, color2);
        grad1.setColorAt(1, color1);
    }
    /* Bottom-left corner: */
    QRadialGradient grad2(QPointF(iMetric, height() - iMetric), iMetric);
    {
        grad2.setColorAt(0, color2);
        grad2.setColorAt(1, color1);
    }

    /* Top line: */
    QLinearGradient grad3(QPointF(iMetric, 0), QPointF(iMetric, iMetric));
    {
        grad3.setColorAt(0, color1);
        grad3.setColorAt(1, color2);
    }
    /* Bottom line: */
    QLinearGradient grad4(QPointF(iMetric, height()), QPointF(iMetric, height() - iMetric));
    {
        grad4.setColorAt(0, color1);
        grad4.setColorAt(1, color2);
    }
    /* Left line: */
    QLinearGradient grad5(QPointF(0, height() - iMetric), QPointF(iMetric, height() - iMetric));
    {
        grad5.setColorAt(0, color1);
        grad5.setColorAt(1, color2);
    }

    /* Paint shape/shadow: */
    pPainter->fillRect(QRect(0,       0,                  iMetric,           iMetric),                grad1);
    pPainter->fillRect(QRect(0,       height() - iMetric, iMetric,           iMetric),                grad2);
    pPainter->fillRect(QRect(iMetric, 0,                  width() - iMetric, iMetric),                grad3);
    pPainter->fillRect(QRect(iMetric, height() - iMetric, width() - iMetric, iMetric),                grad4);
    pPainter->fillRect(QRect(0,       iMetric,            iMetric,           height() - iMetric * 2), grad5);
}

void UINotificationCenter::setAnimatedValue(int iValue)
{
    /* Store recent value: */
    m_iAnimatedValue = iValue;

    // WORKAROUND:
    // Hide items if they are masked anyway.
    // This actually shouldn't be necessary but
    // *is* required to avoid painting artifacts.
    foreach (QWidget *pItem, m_items.values())
        pItem->setVisible(animatedValue());

    /* Adjust geometry: */
    adjustGeometry();
}

int UINotificationCenter::animatedValue() const
{
    return m_iAnimatedValue;
}

void UINotificationCenter::adjustGeometry()
{
    /* Make sure parent exists: */
    QWidget *pParent = parentWidget();
    if (!pParent)
        return;
    /* Acquire parent width and height: */
    const int iParentWidth = pParent->width();
    const int iParentHeight = pParent->height();

    /* Acquire minimum width (includes margins by default): */
    int iMinimumWidth = minimumSizeHint().width();
    /* Acquire minimum button width (including margins manually): */
    int iL, iT, iR, iB;
    m_pLayoutMain->getContentsMargins(&iL, &iT, &iR, &iB);
    const int iMinimumButtonWidth = m_pButtonOpen->minimumSizeHint().width() + iL + iR;

    /* Make sure we have some default width if there is no contents: */
    iMinimumWidth = qMax(iMinimumWidth, 200);

    /* Move and resize notification-center finally: */
    move(iParentWidth - (iMinimumButtonWidth + (double)animatedValue() / 100 * (iMinimumWidth - iMinimumButtonWidth)), 0);
    resize(iMinimumWidth, iParentHeight);
}

void UINotificationCenter::adjustMask()
{
    /* We do include open-button mask only if center is opened or animated to be: */
    QRegion region;
    if (!animatedValue())
        region += QRect(m_pButtonOpen->mapToParent(QPoint(0, 0)), m_pButtonOpen->size());
    setMask(region);
}


/*********************************************************************************************************************************
*   Class UINotificationReceiver implementation.                                                                                 *
*********************************************************************************************************************************/

void UINotificationReceiver::setReceiverProperty(const QVariant &value)
{
    setProperty("received_value", value);
}


#include "UINotificationCenter.moc"
