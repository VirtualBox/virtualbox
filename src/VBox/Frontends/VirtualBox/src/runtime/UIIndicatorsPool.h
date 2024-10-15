/* $Id: UIIndicatorsPool.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIIndicatorsPool class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIIndicatorsPool_h
#define FEQT_INCLUDED_SRC_runtime_UIIndicatorsPool_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>
#include <QMap>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* COM includes: */
#include "KDeviceActivity.h"

/* Forward declarations: */
class QTimer;
class QHBoxLayout;
class QIStatusBarIndicator;
class UIMachine;

/** QWidget extension
  * providing Runtime UI with status-bar indicators. */
class UIIndicatorsPool : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies about context menu request.
      * @param indicatorType reflects which type of indicator it is,
      * @param position      reflects contex-menu position. */
    void sigContextMenuRequest(IndicatorType indicatorType, const QPoint &position);

public:

    /** Constructs indicator-pool passing @a pParent to the base-class.
      * @param  pMachine  Brings the machine UI reference. */
    UIIndicatorsPool(UIMachine *pMachine, QWidget *pParent = 0);
    /** Destructs indicator-pool. */
    virtual ~UIIndicatorsPool() RT_OVERRIDE;

    /** Updates appearance for passed @a indicatorType. */
    void updateAppearance(IndicatorType indicatorType);

    /** Defines whether indicator-states auto-update is @a fEnabled. */
    void setAutoUpdateIndicatorStates(bool fEnabled);

    /** Returns global screen position corresponding to @a indicatorPosition inside indicator of @a enmIndicatorType. */
    QPoint mapIndicatorPositionToGlobal(IndicatorType enmIndicatorType, const QPoint &indicatorPosition);

private slots:

    /** Handles configuration change. */
    void sltHandleConfigurationChange(const QUuid &uMachineID);

    /** Handles indicator-states auto-update. */
    void sltAutoUpdateIndicatorStates();

    /** Handles context-menu request. */
    void sltContextMenuRequest(QIStatusBarIndicator *pIndicator, QContextMenuEvent *pEvent);

private:

    /** Prepare routine. */
    void prepare();
    /** Prepare connections routine: */
    void prepareConnections();
    /** Prepare contents routine. */
    void prepareContents();
    /** Prepare update-timer routine: */
    void prepareUpdateTimer();

    /** Update pool routine. */
    void updatePool();

    /** Cleanup update-timer routine: */
    void cleanupUpdateTimer();
    /** Cleanup contents routine. */
    void cleanupContents();
    /** Cleanup routine. */
    void cleanup();

    /** Context-menu event handler. */
    void contextMenuEvent(QContextMenuEvent *pEvent) RT_OVERRIDE;

    /** Returns position for passed @a indicatorType. */
    int indicatorPosition(IndicatorType indicatorType) const;

    /** Updates passed @a pIndicator with current @a state value. */
    void updateIndicatorStateForDevice(QIStatusBarIndicator *pIndicator, KDeviceActivity state);

    /** Holds the machine UI reference. */
    UIMachine *m_pMachine;

    /** Holds whether status-bar is enabled. */
    bool  m_fEnabled;

    /** Holds the cached restrictions. */
    QList<IndicatorType>  m_restrictions;
    /** Holds the cached order. */
    QList<IndicatorType>  m_order;

    /** Holds cached indicator instances. */
    QMap<IndicatorType, QIStatusBarIndicator*>  m_pool;

    /** Holds the main-layout instance. */
    QHBoxLayout *m_pMainLayout;

    /** Holds the auto-update timer instance. */
    QTimer *m_pTimerAutoUpdate;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIIndicatorsPool_h */
