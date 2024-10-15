/* $Id: UIMediumSelector.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumSelector class declaration.
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

#ifndef FEQT_INCLUDED_SRC_medium_UIMediumSelector_h
#define FEQT_INCLUDED_SRC_medium_UIMediumSelector_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIMainDialog.h"
#include "QIWithRestorableGeometry.h"
#include "UIMedium.h"
#include "UIMediumDefs.h"


/* Forward declarations: */
class QAction;
class QTreeWidgetItem;
class QITreeWidget;
class QITreeWidgetItem;
class QVBoxLayout;
class QIDialogButtonBox;
class QIToolBar;
class UIActionPool;
class UIMediumItem;
class UIMediumSearchWidget;

/** QIDialog extension providing GUI with a dialog to select an existing medium. */
class SHARED_LIBRARY_STUFF UIMediumSelector : public QIWithRestorableGeometry<QIMainDialog>
{

    Q_OBJECT;

signals:

public:

    UIMediumSelector(const QUuid &uCurrentMediumId, UIMediumDeviceType enmMediumType, const QString &machineName,
                     const QString &machineSettingsFilePath, const QString &strMachineGuestOSTypeId,
                     const QUuid &uMachineID, QWidget *pParent, UIActionPool *pActionPool);
    /** Disables/enables the create action and controls its visibility. */
    void         setEnableCreateAction(bool fEnable);
    QList<QUuid> selectedMediumIds() const;

    enum ReturnCode
    {
        ReturnCode_Rejected = 0,
        ReturnCode_Accepted,
        ReturnCode_LeftEmpty,
        ReturnCode_Max
    };

    /** Creates and shows a UIMediumSelector dialog.
      * @param  parent                   Passes the parent of the dialog,
      * @param  enmMediumType            Passes the medium type,
      * @param  uCurrentMediumId         Passes  the id of the currently selected medium,
      * @param  uSelectedMediumUuid      Gets  the selected medium id from selection dialog,
      * @param  strMachineFolder         Passes the machine folder,
      * @param  strMachineName           Passes the name of the machine,
      * @param  strMachineGuestOSTypeId  Passes the type ID of machine's guest os,
      * @param  fEnableCreate            Passes whether to show/enable create action in the medium selector dialog,
      * @param  uMachineID               Passes the machine UUID,
      * @param  pActionPool              Passes the action pool instance pointer,
      * returns the return code of the UIMediumSelector::ReturnCode as int. In case of a medium selection
      *         UUID of the selected medium is returned in @param uSelectedMediumUuid.*/
    static int openMediumSelectorDialog(QWidget *pParent, UIMediumDeviceType  enmMediumType, const QUuid &uCurrentMediumId,
                                        QUuid &uSelectedMediumUuid, const QString &strMachineFolder, const QString &strMachineName,
                                        const QString &strMachineGuestOSTypeId, bool fEnableCreate, const QUuid &uMachineID,
                                        UIActionPool *pActionPool);

protected:

    /** @name Event-handling stuff.
      * @{ */
        void showEvent(QShowEvent *pEvent) RT_OVERRIDE RT_FINAL;
        bool event(QEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** @} */

private slots:

    void sltButtonLeaveEmpty();
    void sltButtonCancel();
    void sltButtonChoose();
    void sltAddMedium();
    void sltCreateMedium();
    void sltHandleItemSelectionChanged();
    void sltHandleTreeWidgetDoubleClick(QTreeWidgetItem * item, int column);
    void sltHandleMediumCreated(const QUuid &uMediumId);
    void sltHandleMediumEnumerationStart();
    void sltHandleMediumEnumerated();
    void sltHandleMediumEnumerationFinish();
    void sltHandleRefresh();
    void sltHandlePerformSearch();
    void sltHandleTreeContextMenuRequest(const QPoint &point);
    void sltHandleTreeExpandAllSignal();
    void sltHandleTreeCollapseAllSignal();
    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Configures all. */
            void configure();
            void prepareWidgets();
            void prepareActions();
            void prepareMenuAndToolBar();
            void prepareConnections();
        /** Perform final preparations. */
        void finalize();
    /** @} */

    void          repopulateTreeWidget();
    /** Disable/enable 'ok' button on the basis of having a selected item */
    void          updateChooseButton();
    UIMediumItem* addTreeItem(const UIMedium &medium, QITreeWidgetItem *pParent);
    void          restoreSelection(const QList<QUuid> &selectedMediums, QVector<UIMediumItem*> &mediumList);
    /** Recursively create the hard disk hierarchy under the tree widget */
    UIMediumItem* createHardDiskItem(const UIMedium &medium, QITreeWidgetItem *pParent);
    UIMediumItem* searchItem(const QTreeWidgetItem *pParent, const QUuid &mediumId);
    /** Remember the default foreground brush of the tree so that we can reset tree items' foreground later */
    void          saveDefaultForeground();
    void          selectMedium(const QUuid &uMediumID);
    void          setTitle();
    void          saveDialogGeometry();
    void          loadSettings();
    QWidget              *m_pCentralWidget;
    QVBoxLayout          *m_pMainLayout;
    QITreeWidget         *m_pTreeWidget;
    UIMediumDeviceType    m_enmMediumType;
    QIDialogButtonBox    *m_pButtonBox;
    QPushButton          *m_pCancelButton;
    QPushButton          *m_pChooseButton;
    QPushButton          *m_pLeaveEmptyButton;
    QMenu                *m_pMainMenu;
    QIToolBar            *m_pToolBar;
    QAction              *m_pActionAdd;
    QAction              *m_pActionCreate;
    QAction              *m_pActionRefresh;
    /** All the known media that are already attached to some vm are added under the following top level tree item */
    QITreeWidgetItem     *m_pAttachedSubTreeRoot;
    /** All the known media that are not attached to any vm are added under the following top level tree item */
    QITreeWidgetItem     *m_pNotAttachedSubTreeRoot;
    QWidget              *m_pParent;
    UIMediumSearchWidget *m_pSearchWidget;
    /** The list all items added to tree. kept in sync. with tree to make searching easier (faster). */
    QList<UIMediumItem*>  m_mediumItemList;
    /** List of items that are matching to the search. */
    QList<UIMediumItem*>  m_mathingItemList;
    /** Index of the currently shown (scrolled) item in the m_mathingItemList. */
    int                   m_iCurrentShownIndex;
    QBrush                m_defaultItemForeground;
    QString               m_strMachineFolder;
    QString               m_strMachineName;
    QString               m_strMachineGuestOSTypeId;
    QUuid                 m_uMachineID;
    QUuid                 m_uCurrentMediumId;
    UIActionPool         *m_pActionPool;
    int                   m_iGeometrySaveTimerId;
};

#endif /* !FEQT_INCLUDED_SRC_medium_UIMediumSelector_h */
