/* $Id: UIFileManagerGuestTable.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileManagerGuestTable class declaration.
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

#ifndef FEQT_INCLUDED_SRC_guestctrl_UIFileManagerGuestTable_h
#define FEQT_INCLUDED_SRC_guestctrl_UIFileManagerGuestTable_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
# include <QUuid>

/* COM includes: */
#include "CEventListener.h"
#include "CEventSource.h"
#include "CGuest.h"
#include "CGuestSession.h"
#include "CMachine.h"
#include "CSession.h"
#include "CConsole.h"


/* GUI includes: */
#include "UIFileManagerTable.h"
#include "UIMainEventListener.h"

/* Forward declarations: */
class CGuestSessionStateChangedEvent;
class UIActionPool;
class UIFileSystemItem;
class UIGuestSessionWidget;

/** This class scans the guest file system by using the VBox Guest Control API
 *  and populates the UIGuestControlFileModel*/
class UIFileManagerGuestTable : public UIFileManagerTable
{
    Q_OBJECT;

signals:

    void sigNewFileOperation(const CProgress &comProgress, const QString &strTableName);
    void sigStateChanged(bool fSessionRunning);

public:

    UIFileManagerGuestTable(UIActionPool *pActionPool, const CMachine &comMachine, QWidget *pParent = 0);
    ~UIFileManagerGuestTable();
    void copyGuestToHost(const QString& hostDestinationPath);
    void copyHostToGuest(const QStringList &hostSourcePathList,
                         const QString &strDestination = QString());
    QUuid machineId();
    bool isGuestSessionRunning() const;
    void setIsCurrent(bool fIsCurrent);
    virtual bool  isWindowsFileSystem() const RT_OVERRIDE RT_FINAL;

protected:

    virtual bool    readDirectory(const QString& strPath, UIFileSystemItem *parent, bool isStartDir = false) RT_OVERRIDE RT_FINAL;
    virtual void    deleteByItem(UIFileSystemItem *item) RT_OVERRIDE RT_FINAL;
    virtual void    goToHomeDirectory() RT_OVERRIDE RT_FINAL;
    virtual bool    renameItem(UIFileSystemItem *item, const QString &strOldPath) RT_OVERRIDE RT_FINAL;
    virtual bool    createDirectory(const QString &path, const QString &directoryName) RT_OVERRIDE RT_FINAL;
    virtual QString fsObjectPropertyString() RT_OVERRIDE RT_FINAL;
    virtual void    showProperties() RT_OVERRIDE RT_FINAL;
    virtual void    determineDriveLetters() RT_OVERRIDE RT_FINAL;
    virtual void    determinePathSeparator() RT_OVERRIDE RT_FINAL;
    virtual void    prepareToolbar() RT_OVERRIDE RT_FINAL;
    virtual void    createFileViewContextMenu(const QWidget *pWidget, const QPoint &point) RT_OVERRIDE RT_FINAL;
    /** @name Copy/Cut guest-to-guest stuff.
     * @{ */
        /** Disable/enable paste action depending on the m_eFileOperationType. */
        virtual void  setPasteActionEnabled(bool fEnabled) RT_OVERRIDE RT_FINAL;
        virtual void  pasteCutCopiedObjects() RT_OVERRIDE RT_FINAL;
    /** @} */
    virtual void  toggleForwardBackwardActions() RT_OVERRIDE RT_FINAL;
    virtual void  setState();
    virtual void  setSessionDependentWidgetsEnabled();

private slots:

    void sltGuestSessionPanelToggled(bool fChecked);
    void sltGuestSessionUnregistered(CGuestSession guestSession);
    void sltGuestSessionRegistered(CGuestSession guestSession);
    void sltGuestSessionStateChanged(const CGuestSessionStateChangedEvent &cEvent);
    void sltOpenGuestSession(QString strUserName, QString strPassword);
    void sltHandleCloseSessionRequest();
    void sltMachineStateChange(const QUuid &uMachineId, const KMachineState state);
    void sltCommitDataSignalReceived();
    void sltAdditionsStateChange();
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    enum State
    {
        State_InvalidMachineReference,
        State_MachineNotRunning,
        State_NoGuestAdditions,
        State_GuestAdditionsTooOld,
        State_SessionPossible,
        State_SessionRunning,
        State_MachinePaused,
        State_SessionError,
        State_Max
    };

    KFsObjType  fileType(const CFsObjInfo &fsInfo);
    KFsObjType  fileType(const CGuestFsObjInfo &fsInfo);

    void prepareActionConnections();
    bool checkGuestSession();
    QString permissionString(const CFsObjInfo &fsInfo);
    bool isFileObjectHidden(const CFsObjInfo &fsInfo);

    void prepareListener(ComObjPtr<UIMainEventListenerImpl> &Qtistener,
                         CEventListener &comEventListener,
                         CEventSource comEventSource, QVector<KVBoxEventType>& eventTypes);

    void cleanupListener(ComObjPtr<UIMainEventListenerImpl> &QtListener,
                         CEventListener &comEventListener,
                         CEventSource comEventSource);
    void cleanupGuestListener();
    void cleanupGuestSessionListener();
    void cleanupConsoleListener();
    void prepareGuestSessionPanel();
    bool openGuestSession(const QString& strUserName, const QString& strPassword);
    void closeGuestSession();
    bool openMachineSession();
    bool closeMachineSession();
    /* Return 0 if GA is not detected, -1 if it is there but older than @p pszMinimumGuestAdditionVersion, and 1 otherwise. */
    int isGuestAdditionsAvailable(const char* pszMinimumVersion);
    void setStateAndEnableWidgets();

    void initFileTable();
    void cleanAll();
    void manageConnection(bool fConnect, QAction *pAction, void (UIFileManagerGuestTable::*fptr)(void));
    CGuest          m_comGuest;
    CGuestSession   m_comGuestSession;
    CSession        m_comSession;
    CMachine        m_comMachine;
    CConsole        m_comConsole;

    ComObjPtr<UIMainEventListenerImpl> m_pQtGuestListener;
    ComObjPtr<UIMainEventListenerImpl> m_pQtSessionListener;
    ComObjPtr<UIMainEventListenerImpl> m_pQtConsoleListener;
    CEventListener m_comSessionListener;
    CEventListener m_comGuestListener;
    CEventListener m_comConsoleListener;
    UIGuestSessionWidget *m_pGuestSessionWidget;
    /** True if this table is the current table in parents tab widget. */
    bool m_fIsCurrent;
    State m_enmState;
    const char *pszMinimumGuestAdditionVersion;
};

#endif /* !FEQT_INCLUDED_SRC_guestctrl_UIFileManagerGuestTable_h */
