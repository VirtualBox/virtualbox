/* $Id: UIVisoCreator.h 109396 2025-05-02 14:26:17Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVisoCreator classes declaration.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_medium_viso_UIVisoCreator_h
#define FEQT_INCLUDED_SRC_medium_viso_UIVisoCreator_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QModelIndex>

/* GUI includes: */
#include "QIMainDialog.h"
#include "QIWithRestorableGeometry.h"
#include "UIFileManagerHostTable.h"

/* Forward declarations: */
class QGridLayout;
class QLabel;
class QVBoxLayout;
class QILabel;
class QMenu;
class QIDialogButtonBox;
class QIToolBar;
class UIActionPool;
class UIVisoContentBrowser;
class UIVisoSettingWidget;


class UIVisoHostBrowser : public UIFileManagerHostTable
{

    Q_OBJECT;

public:

    UIVisoHostBrowser(UIActionPool *pActionPool, QWidget *pParent = 0);
    void prepareMainMenu(QMenu *pMenu);

private slots:

    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    virtual void createFileViewContextMenu(const QWidget *pWidget, const QPoint &point) RT_OVERRIDE RT_FINAL;
    QPointer<QMenu>        m_pSubMenu;
};


/** A QIMainDialog extension. It hosts two UIVisoBrowserBase extensions, one for host and one
  * for VISO file system. It has the main menu, main toolbar, and a vertical toolbar and corresponding
  * actions. */
class SHARED_LIBRARY_STUFF UIVisoCreatorWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigSetCancelButtonShortCut(QKeySequence keySequence);
    void sigVisoNameChanged(const QString &strVisoName);
    void sigVisoFilePathChanged(const QString &strPath);
    void sigSettingDialogToggle(bool fDialogShown);
    void sigSave();

public:

    struct Settings
    {
        Settings()
            : m_strVisoName("ad-hoc-viso")
            , m_fShowHiddenObjects(true){}
        QString m_strVisoName;
        /** Additions viso options to be inserted to the viso file as separate lines. */
        QStringList m_customOptions;
        /* If host file browser shows hidden file objects. */
        bool m_fShowHiddenObjects;
    };

    UIVisoCreatorWidget(UIActionPool *pActionPool, QWidget *pParent,
                        bool fShowToolBar, const QString& strVisoSavePath, const QString& strMachineName);
    /** Returns the content of the .viso file. Each element of the list corresponds to a line in the .viso file. */
    QStringList       entryList() const;
    QString           importedISOPath() const;
    const QString    &visoName() const;
    /** Returns custom ISO options (if any). */
    const QStringList &customOptions() const;
    /** Returns the current path that the host browser is listing. */
    QString currentPath() const;
    void    setCurrentPath(const QString &strPath);
    QMenu *menu() const;
    QString visoFileFullPath() const;

#ifdef VBOX_WS_MAC
    /** Returns the toolbar. */
    QIToolBar *toolbar() const { return m_pToolBar; }
#endif

private slots:

    void sltAddObjectsToViso();
    void sltSettingsActionToggled(bool fChecked);
    void sltHostBrowserTableSelectionChanged(bool fHasSelection);
    void sltContentBrowserTableSelectionChanged(bool fIsSelectionEmpty);
    void sltOpenAction();
    void sltSaveAsAction();
    void sltISOImportAction();
    void sltISORemoveAction();
    void sltISOContentImportedOrRemoved(bool fImported);
    void sltSettingsChanged();
    void sltPanelContainerHidden();
    void sltVisoVolumeIdParsed(const QString &strVolumeId);

private:

    void prepareWidgets();
    void prepareConnections();
    void prepareActions();
    void setVisoName(const QString& strName);

    /** Creates and configures the vertical toolbar. Should be called after prepareActions() */
    void prepareVerticalToolBar();
    /* Populates the main menu and toolbard with already created actions.
     * Leave out the vertical toolbar which is handled in prepareVerticalToolBar. */
    void populateMenuMainToolbar();

    void toggleSettingsWidget(bool fShown);
    QStringList findISOFiles(const QStringList &pathList) const;
    void setVisoFilePath(const QString& strPath);

    /** @name Main toolbar (and main menu) actions
      * @{ */
        QAction         *m_pActionPreferences;
    /** @} */

    /** @name These actions are addded to vertical toolbar, context menus, and the main menu.
      * @{ */
        QAction              *m_pAddAction;
        QAction              *m_pOpenAction;
        QAction              *m_pSaveAsAction;
        QAction              *m_pImportISOAction;
        QAction              *m_pRemoveISOAction;
    /** @} */

    QVBoxLayout            *m_pMainLayout;
    UIVisoContentBrowser   *m_pVISOContentBrowser;
    UIVisoHostBrowser      *m_pHostFileBrowser;

    QIToolBar             *m_pToolBar;
    QIToolBar             *m_pVerticalToolBar;
    Settings               m_settings;
    QMenu                 *m_pMainMenu;
    QPointer<UIActionPool> m_pActionPool;
    bool                   m_fShowToolBar;
    UIVisoSettingWidget   *m_pSettingsWidget;
    QWidget               *m_pBrowserContainerWidget;
    QString                m_strVisoFilePath;
    bool                   m_fShowSettingsDialog;
};


class SHARED_LIBRARY_STUFF UIVisoCreatorDialog : public QIWithRestorableGeometry<QIMainDialog>
{
    Q_OBJECT;

public:

    UIVisoCreatorDialog(UIActionPool *pActionPool, QWidget *pParent,
                        const QString& strVisoSavePath, const QString& strMachineName = QString());

    QStringList  entryList() const;
    QString visoName() const;
    QString importedISOPath() const;
    QStringList customOptions() const;
    QString currentPath() const;
    void    setCurrentPath(const QString &strPath);
    QString visoFileFullPath() const;
    /** Creates a VISO by using the VISO creator dialog.
      * @param  pParent           Passes the dialog parent.
      * @param  strDefaultFolder  Passes the folder to save the VISO file.
      * @param  strMachineName    Passes the name of the machine.
      * returns QUuid of the opened medium when successful. */
    static QUuid createViso(UIActionPool *pActionPool, QWidget *pParent,
                            const QString &strDefaultFolder = QString(),
                            const QString &strMachineName  = QString());

protected:

    virtual bool event(QEvent *pEvent) RT_OVERRIDE RT_FINAL;

private slots:

    void sltSetCancelButtonShortCut(QKeySequence keySequence);
    void sltVisoNameChanged(const QString &strName);
    void sltVisoFilePathChanged(const QString &strPath);
    void sltSave();
    void sltRetranslateUI();

private:

    bool saveVISOFile();
    void prepareWidgets(const QString& strVisoSavePath, const QString &strMachineName);
    void loadSettings();
    void saveDialogGeometry();
    void updateWindowTitle();

    UIVisoCreatorWidget *m_pVisoCreatorWidget;
    QIDialogButtonBox   *m_pButtonBox;
    QPointer<UIActionPool> m_pActionPool;
    int                  m_iGeometrySaveTimerId;
};
#endif /* !FEQT_INCLUDED_SRC_medium_viso_UIVisoCreator_h */
