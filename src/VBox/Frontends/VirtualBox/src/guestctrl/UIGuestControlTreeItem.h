/* $Id: UIGuestControlTreeItem.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGuestControlTreeItem class declaration.
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

#ifndef FEQT_INCLUDED_SRC_guestctrl_UIGuestControlTreeItem_h
#define FEQT_INCLUDED_SRC_guestctrl_UIGuestControlTreeItem_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QITreeWidget.h"
#include "UIMainEventListener.h"

/* COM includes: */
#include "CEventListener.h"
#include "CGuestSession.h"

/* Forward declarations: */
class CEventSource;
class CGuestProcessStateChangedEvent;
class CGuestSessionStateChangedEvent;

/** QITreeWidgetItem extension serving as a base class
    to UIGuestSessionTreeItem and UIGuestProcessTreeItem classes */
class UIGuestControlTreeItem : public QITreeWidgetItem
{

    Q_OBJECT;

public:

    UIGuestControlTreeItem(QITreeWidget *pTreeWidget, const QStringList &strings = QStringList());
    UIGuestControlTreeItem(UIGuestControlTreeItem *pTreeWidgetItem, const QStringList &strings = QStringList());
    virtual ~UIGuestControlTreeItem();
    virtual QString propertyString() const = 0;

private slots:

protected:

    void prepareListener(CEventSource comEventSource, QVector<KVBoxEventType>& eventTypes);
    void cleanupListener(CEventSource comEventSource);
    void prepare();

    ComObjPtr<UIMainEventListenerImpl> m_pQtListener;

private:

    virtual void prepareListener() = 0;
    virtual void prepareConnections() = 0;
    virtual void cleanupListener() = 0;
    virtual void setColumnText() = 0;

    /** Holds the COM event listener instance. */
    CEventListener m_comEventListener;

};

/** UIGuestControlTreeItem extension. Represents a instance of CGuestSession
    and acts as an event listener for this com object. */
class UIGuestSessionTreeItem : public UIGuestControlTreeItem
{
    Q_OBJECT;

signals:

    void sigGuessSessionUpdated();
    void sigGuestSessionErrorText(QString strError);

public:

    UIGuestSessionTreeItem(QITreeWidget *pTreeWidget, CGuestSession& guestSession, const QStringList &strings = QStringList());
    UIGuestSessionTreeItem(UIGuestControlTreeItem *pTreeWidgetItem, CGuestSession& guestSession, const QStringList &strings = QStringList());
    virtual ~UIGuestSessionTreeItem();
    const CGuestSession& guestSession() const;
    void errorString(QString strError);
    KGuestSessionStatus status() const;
    virtual QString propertyString() const RT_OVERRIDE;

protected:

    void prepareListener(CEventSource comEventSource, QVector<KVBoxEventType>& eventTypes);
    void cleanupListener(CEventSource comEventSource);

private slots:

    void sltGuestSessionUpdated(const CGuestSessionStateChangedEvent& cEvent);
    void sltGuestProcessRegistered(CGuestProcess guestProcess);
    void sltGuestProcessUnregistered(CGuestProcess guestProcess);


private:

    virtual void prepareListener() RT_OVERRIDE;
    virtual void prepareConnections() RT_OVERRIDE;
    virtual void cleanupListener()  RT_OVERRIDE;
    virtual void setColumnText()  RT_OVERRIDE;
    void addGuestProcess(CGuestProcess guestProcess);
    void initProcessSubTree();
    CGuestSession m_comGuestSession;
};

/** UIGuestControlTreeItem extension. Represents a instance of CGuestProcess
    and acts as an event listener for this com object. */
class UIGuestProcessTreeItem : public UIGuestControlTreeItem
{
    Q_OBJECT;

signals:

    void sigGuestProcessErrorText(QString strError);

public:

    UIGuestProcessTreeItem(QITreeWidget *pTreeWidget, CGuestProcess& guestProcess, const QStringList &strings = QStringList());
    UIGuestProcessTreeItem(UIGuestControlTreeItem *pTreeWidgetItem, CGuestProcess& guestProcess, const QStringList &strings = QStringList());
    const CGuestProcess& guestProcess() const;
    virtual ~UIGuestProcessTreeItem();
    KProcessStatus status() const;
    virtual QString propertyString() const RT_OVERRIDE;

protected:

    void prepareListener(CEventSource comEventSource, QVector<KVBoxEventType>& eventTypes);
    void cleanupListener(CEventSource comEventSource);

private slots:

    void sltGuestProcessUpdated(const CGuestProcessStateChangedEvent &cEvent);

private:

    virtual void prepareListener() RT_OVERRIDE;
    virtual void prepareConnections() RT_OVERRIDE;
    virtual void cleanupListener()  RT_OVERRIDE;
    virtual void setColumnText()  RT_OVERRIDE;

    CGuestProcess m_comGuestProcess;
};

#endif /* !FEQT_INCLUDED_SRC_guestctrl_UIGuestControlTreeItem_h */
