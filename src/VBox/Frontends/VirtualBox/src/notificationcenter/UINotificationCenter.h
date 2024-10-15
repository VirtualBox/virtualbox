/* $Id: UINotificationCenter.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINotificationCenter class declaration.
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

#ifndef FEQT_INCLUDED_SRC_notificationcenter_UINotificationCenter_h
#define FEQT_INCLUDED_SRC_notificationcenter_UINotificationCenter_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QEventLoop>
#include <QPointer>
#include <QMap>
#include <QUuid>
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UINotificationObjects.h"

/* Forward declarations: */
class QHBoxLayout;
class QPainter;
class QStateMachine;
class QTimer;
class QVBoxLayout;
class QIToolButton;
class UINotificationModel;
class UINotificationObject;
class UINotificationObjectItem;

/** QWidget-based notification-center overlay. */
class SHARED_LIBRARY_STUFF UINotificationCenter : public QWidget
{
    Q_OBJECT;
    Q_PROPERTY(int animatedValue READ animatedValue WRITE setAnimatedValue);

signals:

    /** Requests sliding state-machine to open overlay. */
    void sigOpen();
    /** Requests sliding state-machine to close overlay. */
    void sigClose();

    /** Notifies listener about all operations aborted. */
    void sigOperationsAborted();

public:

    /** Creates notification-center for passed @a pParent. */
    static void create(QWidget *pParent = 0);
    /** Destroys notification-center. */
    static void destroy();
    /** Returns notification-center singleton instance. */
    static UINotificationCenter *instance();

    /** Constructs notification-center passing @a pParent to the base-class. */
    UINotificationCenter(QWidget *pParent);
    /** Destructs notification-center. */
    virtual ~UINotificationCenter() RT_OVERRIDE RT_FINAL;

    /** Defines notification-center @a pParent. */
    void setParent(QWidget *pParent);

    /** Invokes notification-center. */
    void invoke();

    /** Appends a notification @a pObject to intenal model. */
    QUuid append(UINotificationObject *pObject);
    /** Revokes a notification object referenced by @a uId from intenal model. */
    void revoke(const QUuid &uId);

    /** Immediately and synchronously handles passed notification @a pProgress.
      * @note It's a blocking call finished by sltHandleProgressFinished(). */
    bool handleNow(UINotificationProgress *pProgress);
    /** Returns whether center has blocking operation. */
    bool hasOperationsPending() const;
    /** Aborts blocking operations being performed. */
    void abortOperations();

protected:

    /** Preprocesses any Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles alignment changes. */
    void sltHandleAlignmentChange();

    /** Issues order changes. */
    void sltIssueOrderChange();
    /** Handles order changes. */
    void sltHandleOrderChange();

    /** Issues request to make open button @a fToggled. */
    void sltHandleOpenButtonToggled(bool fToggled);
#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
    /** Toggles notification-progresses keep approach. */
    void sltHandleKeepButtonToggled(bool fToggled);
#endif
    /** Removes finished notifications. */
    void sltHandleRemoveFinishedButtonClicked();

    /** Invokes open button context menu at specified @a position. */
    void sltHandleOpenButtonContextMenuRequested(const QPoint &position);

    /** Handles open-timer timeout. */
    void sltHandleOpenTimerTimeout();

    /** Handles signal about model item with specified @a uId was added. */
    void sltHandleModelItemAdded(const QUuid &uId);
    /** Handles signal about model item with specified @a uId was removed. */
    void sltHandleModelItemRemoved(const QUuid &uId);

    /** Handles immediate progress being finished.
      * @note Breaks blocking handleNow() call. */
    void sltHandleProgressFinished();

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares everything. */
    void prepare();
    /** Prepares model. */
    void prepareModel();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares sliding state-machine. */
    void prepareStateMachineSliding();
    /** Prepares open-timer. */
    void prepareOpenTimer();
    /** Cleanups model. */
    void cleanupModel();
    /** Cleanups items. */
    void cleanupItems();
    /** Cleanups everything. */
    void cleanup();

    /** Paints background using pre-configured @a pPainter. */
    void paintBackground(QPainter *pPainter);
    /** Paints frame using pre-configured @a pPainter. */
    void paintFrame(QPainter *pPainter);

    /** Defines animated @a iValue. */
    void setAnimatedValue(int iValue);
    /** Returns animated value. */
    int animatedValue() const;

    /** Adjusts geometry. */
    void adjustGeometry();
    /** Adjusts mask. */
    void adjustMask();

    /** Holds the notification-center singleton instance. */
    static UINotificationCenter *s_pInstance;

    /** Holds the model instance. */
    UINotificationModel *m_pModel;

    /** Holds the alignment. */
    Qt::Alignment  m_enmAlignment;
    /** Holds the order. */
    Qt::SortOrder  m_enmOrder;

    /** Holds the main layout instance. */
    QVBoxLayout  *m_pLayoutMain;
    /** Holds the buttons layout instance. */
    QHBoxLayout  *m_pLayoutButtons;
    /** Holds the open button instance. */
    QIToolButton *m_pButtonOpen;
    /** Holds the toggle-sorting button instance. */
    QIToolButton *m_pButtonToggleSorting;
#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
    /** Holds the keep-finished button instance. */
    QIToolButton *m_pButtonKeepFinished;
#endif
    /** Holds the remove-finished button instance. */
    QIToolButton *m_pButtonRemoveFinished;
    /** Holds the items layout instance. */
    QVBoxLayout  *m_pLayoutItems;

    /** Holds the map of item instances. */
    QMap<QUuid, UINotificationObjectItem*>  m_items;

    /** Holds the sliding state-machine instance. */
    QStateMachine *m_pStateMachineSliding;
    /** Holds the sliding animation current value. */
    int            m_iAnimatedValue;

    /** Holds the open-timer instance. */
    QTimer *m_pTimerOpen;
    /** Holds the open-object ID. */
    QUuid   m_uOpenObjectId;

    /** Holds the separate event-loop instance.
      * @note  This event-loop is only used when the center
      *        handles progress directly via handleNow(). */
    QPointer<QEventLoop>  m_pEventLoop;
    /** Holds the last handleNow() result. */
    bool                  m_fLastResult;
};

/** Singleton notification-center 'official' name. */
#define gpNotificationCenter UINotificationCenter::instance()

/** QObject subclass receiving notification value and storing is as a property. */
class SHARED_LIBRARY_STUFF UINotificationReceiver : public QObject
{
    Q_OBJECT;

public slots:

    /** Defines received property by @a value. */
    void setReceiverProperty(const QVariant &value);
};

#endif /* !FEQT_INCLUDED_SRC_notificationcenter_UINotificationCenter_h */
