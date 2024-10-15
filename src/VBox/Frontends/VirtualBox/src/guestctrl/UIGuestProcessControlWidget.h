/* $Id: UIGuestProcessControlWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGuestProcessControlWidget class declaration.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_guestctrl_UIGuestProcessControlWidget_h
#define FEQT_INCLUDED_SRC_guestctrl_UIGuestProcessControlWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* COM includes: */
#include "CGuest.h"
#include "CEventListener.h"

/* GUI includes: */
#include "QIManagerDialog.h"
#include "UIMainEventListener.h"

/* Forward declarations: */
class QVBoxLayout;
class QIToolBar;
class UIGuestControlTreeWidget;

/** QWidget extension
  * providing GUI with guest session information and control tab in session-information window. */
class UIGuestProcessControlWidget : public QWidget
{
    Q_OBJECT;

public:

    UIGuestProcessControlWidget(EmbedTo enmEmbedding, const CGuest &comGuest, QWidget *pParent,
                                QString strMachineName = QString(), bool fShowToolbar = false);
    ~UIGuestProcessControlWidget();
    /** When true we delete the corresponding tree item as soon as the guest session/process is unregistered. */
    static const bool  s_fDeleteAfterUnregister;

private slots:

    void sltRetranslateUI();
    void sltGuestSessionsUpdated();
    void sltGuestSessionRegistered(CGuestSession guestSession);
    void sltGuestSessionUnregistered(CGuestSession guestSession);
    void sltTreeItemUpdated();
    void sltCloseSessionOrProcess();
    void sltShowProperties();
    void sltCleanupListener();

private:

    void prepareObjects();
    void prepareConnections();
    void prepareToolBar();
    void prepareListener();
    void initGuestSessionTree();
    void updateTreeWidget();
    void addGuestSession(CGuestSession guestSession);

    CGuest                    m_comGuest;
    QVBoxLayout              *m_pMainLayout;
    UIGuestControlTreeWidget *m_pTreeWidget;
    const EmbedTo             m_enmEmbedding;
    QIToolBar                *m_pToolBar;

    /** Holds the Qt event listener instance. */
    ComObjPtr<UIMainEventListenerImpl> m_pQtListener;

    /** Holds the COM event listener instance. */
    CEventListener  m_comEventListener;
    const bool      m_fShowToolbar;
    QString         m_strMachineName;
};

#endif /* !FEQT_INCLUDED_SRC_guestctrl_UIGuestProcessControlWidget_h */
