/* $Id: UIVirtualBoxManagerWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxManagerWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIVirtualBoxManagerWidget_h
#define FEQT_INCLUDED_SRC_manager_UIVirtualBoxManagerWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUuid>
#include <QWidget>

/* GUI includes: */
#include "UISlidingAnimation.h"
#include "UIToolPaneGlobal.h"
#include "UIToolPaneMachine.h"

/* Forward declarations: */
class QStackedWidget;
class QTimer;
class QISplitter;
class QIToolBar;
class UIActionPool;
class UIChooser;
class UITabBar;
class UITools;
class UIVirtualBoxManager;
class UIVirtualMachineItem;

/** QWidget extension used as VirtualBox Manager Widget instance. */
class UIVirtualBoxManagerWidget : public QWidget
{
    Q_OBJECT;

    /** Possible selection types. */
    enum SelectionType
    {
        SelectionType_Invalid,
        SelectionType_SingleLocalGroupItem,
        SelectionType_SingleCloudGroupItem,
        SelectionType_FirstIsGlobalItem,
        SelectionType_FirstIsLocalMachineItem,
        SelectionType_FirstIsCloudMachineItem
    };

signals:

    /** @name Tool-bar stuff.
      * @{ */
        /* Notifies listeners about tool-bar height change. */
        void sigToolBarHeightChange(int iHeight);
    /** @} */

    /** @name Chooser pane stuff.
      * @{ */
        /** Notifies about Chooser-pane index change. */
        void sigChooserPaneIndexChange();
        /** Notifies about Chooser-pane group saving change. */
        void sigGroupSavingStateChanged();
        /** Notifies about Chooser-pane cloud update change. */
        void sigCloudUpdateStateChanged();

        /** Notifies about state change for cloud machine with certain @a uId. */
        void sigCloudMachineStateChange(const QUuid &uId);

        /** Notify listeners about start or show request. */
        void sigStartOrShowRequest();
        /** Notifies listeners about machine search widget visibility changed to @a fVisible. */
        void sigMachineSearchWidgetVisibilityChanged(bool fVisible);
    /** @} */

    /** @name Tools pane stuff.
      * @{ */
        /** Notifies about Global Tool type change. */
        void sigToolTypeChangeGlobal();
        /** Notifies about Machine Tool type change. */
        void sigToolTypeChangeMachine();
    /** @} */

    /** @name Tools / Media pane stuff.
      * @{ */
        /** Notifies listeners about creation procedure was requested. */
        void sigCreateMedium();
        /** Notifies listeners about copy procedure was requested for medium with specified @a uMediumId. */
        void sigCopyMedium(const QUuid &uMediumId);
    /** @} */

    /** @name Tools / Details pane stuff.
      * @{ */
        /** Notifies aboud Details-pane link clicked. */
        void sigMachineSettingsLinkClicked(const QString &strCategory, const QString &strControl, const QUuid &uId);
    /** @} */

    /** @name Tools / Snapshots pane stuff.
      * @{ */
        /** Notifies listeners about current Snapshots pane item change. */
        void sigCurrentSnapshotItemChange();
    /** @} */

    /** @name Tools / Generic pane stuff.
      * @{ */
        /** Notifies listeners about request to detach pane with tool type @p enmToolType. */
        void sigDetachToolPane(UIToolType enmToolType);
    /** @} */

public:

    /** Constructs VirtualBox Manager widget. */
    UIVirtualBoxManagerWidget(UIVirtualBoxManager *pParent);
    /** Destructs VirtualBox Manager widget. */
    virtual ~UIVirtualBoxManagerWidget() RT_OVERRIDE;

    /** @name Common stuff.
      * @{ */
        /** Returns the action-pool instance. */
        UIActionPool *actionPool() const { return m_pActionPool; }
    /** @} */

    /** @name Chooser pane stuff.
      * @{ */
        /** Returns current-item. */
        UIVirtualMachineItem *currentItem() const;
        /** Returns a list of current-items. */
        QList<UIVirtualMachineItem*> currentItems() const;

        /** Returns whether group item is selected. */
        bool isGroupItemSelected() const;
        /** Returns whether global item is selected. */
        bool isGlobalItemSelected() const;
        /** Returns whether machine item is selected. */
        bool isMachineItemSelected() const;
        /** Returns whether local machine item is selected. */
        bool isLocalMachineItemSelected() const;
        /** Returns whether cloud machine item is selected. */
        bool isCloudMachineItemSelected() const;

        /** Returns whether single group is selected. */
        bool isSingleGroupSelected() const;
        /** Returns whether single local group is selected. */
        bool isSingleLocalGroupSelected() const;
        /** Returns whether single cloud provider group is selected. */
        bool isSingleCloudProviderGroupSelected() const;
        /** Returns whether single cloud profile group is selected. */
        bool isSingleCloudProfileGroupSelected() const;
        /** Returns whether all items of one group are selected. */
        bool isAllItemsOfOneGroupSelected() const;

        /** Returns full name of currently selected group. */
        QString fullGroupName() const;

        /** Returns whether group saving is in progress. */
        bool isGroupSavingInProgress() const;
        /** Returns whether at least one cloud profile currently being updated. */
        bool isCloudProfileUpdateInProgress() const;

        /** Switches to global item. */
        void switchToGlobalItem();
        /** Opens group name editor. */
        void openGroupNameEditor();
        /** Disbands group. */
        void disbandGroup();
        /** Removes machine. */
        void removeMachine();
        /** Moves machine to a group with certain @a strName. */
        void moveMachineToGroup(const QString &strName = QString());
        /** Returns possible groups for machine with passed @a uId to move to. */
        QStringList possibleGroupsForMachineToMove(const QUuid &uId);
        /** Returns possible groups for group with passed @a strFullName to move to. */
        QStringList possibleGroupsForGroupToMove(const QString &strFullName);
        /** Refreshes machine. */
        void refreshMachine();
        /** Sorts group. */
        void sortGroup();
        /** Toggle machine search widget to be @a fVisible. */
        void setMachineSearchWidgetVisibility(bool fVisible);
    /** @} */

    /** @name Tools pane stuff.
      * @{ */
        /** Defines Global tools @a enmType. */
        void setToolsTypeGlobal(UIToolType enmType);
        /** Returns Global tools type. */
        UIToolType toolsTypeGlobal() const;

        /** Defines Machine tools @a enmType. */
        void setToolsTypeMachine(UIToolType enmType);
        /** Returns Machine tools type. */
        UIToolType toolsTypeMachine() const;

        /** Returns a type of curent Global tool. */
        UIToolType currentGlobalTool() const;
        /** Returns a type of curent Machine tool. */
        UIToolType currentMachineTool() const;
        /** Returns whether Global tool of passed @a enmType is opened. */
        bool isGlobalToolOpened(UIToolType enmType) const;
        /** Returns whether Machine tool of passed @a enmType is opened. */
        bool isMachineToolOpened(UIToolType enmType) const;
        /** Switches Global tool to passed @a enmType. */
        void switchGlobalToolTo(UIToolType enmType);
        /** Switches Machine tool to passed @a enmType. */
        void switchMachineToolTo(UIToolType enmType);
        /** Closes Global tool of passed @a enmType. */
        void closeGlobalTool(UIToolType enmType);
        /** Closes Machine tool of passed @a enmType. */
        void closeMachineTool(UIToolType enmType);
    /** @} */

    /** @name Tools / Snapshot pane stuff.
      * @{ */
        /** Returns whether current-state item of Snapshot pane is selected. */
        bool isCurrentStateItemSelected() const;

        /** Returns currently selected snapshot ID if any. */
        QUuid currentSnapshotId();
    /** @} */

    /** @name Tool-bar stuff.
      * @{ */
        /** Updates tool-bar menu buttons. */
        void updateToolBarMenuButtons(bool fSeparateMenuSection);
    /** @} */

    /** @name Help browser stuff.
      * @{ */
        /** Returns the current help key word. */
        QString currentHelpKeyword() const;
    /** @} */

public slots:

    /** @name Tool-bar stuff.
      * @{ */
        /** Handles tool-bar context-menu request for passed @a position. */
        void sltHandleToolBarContextMenuRequest(const QPoint &position);
    /** @} */

private slots:

    /** @name Event handling stuff.
      * @{ */
        /** Handles translation event. */
        void sltRetranslateUI();
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Handles request to commit data. */
        void sltHandleCommitData();
    /** @} */

    /** @name CVirtualBox event handling stuff.
      * @{ */
        /** Handles CVirtualBox event about state change for machine with @a uId. */
        void sltHandleStateChange(const QUuid &uId);
    /** @} */

    /** @name CVirtualBox extra-data event handling stuff.
      * @{ */
        /** Handles signal about settings expert mode change. */
        void sltHandleSettingsExpertModeChange();
    /** @} */

    /** @name Splitter stuff.
      * @{ */
        /** Handles signal about splitter move. */
        void sltHandleSplitterMove();
        /** Handles request to save splitter settings. */
        void sltSaveSplitterSettings();
    /** @} */

    /** @name Tool-bar stuff.
      * @{ */
        /** Handles signal about tool-bar resize to @a newSize. */
        void sltHandleToolBarResize(const QSize &newSize);
    /** @} */

    /** @name Chooser pane stuff.
      * @{ */
        /** Handles signal about Chooser-pane index change. */
        void sltHandleChooserPaneIndexChange();

        /** Handles signal about Chooser-pane selection invalidated. */
        void sltHandleChooserPaneSelectionInvalidated() { recacheCurrentMachineItemInformation(true /* fDontRaiseErrorPane */); }

        /** Handles sliding animation complete signal.
          * @param  enmDirection  Brings which direction was animation finished for. */
        void sltHandleSlidingAnimationComplete(SlidingDirection enmDirection);

        /** Handles state change for cloud profile with certain @a strProviderShortName and @a strProfileName. */
        void sltHandleCloudProfileStateChange(const QString &strProviderShortName,
                                              const QString &strProfileName);
        /** Handles state change for cloud machine with certain @a uId. */
        void sltHandleCloudMachineStateChange(const QUuid &uId);
    /** @} */

    /** @name Tools pane stuff.
      * @{ */
        /** Handles tool popup-menu request. */
        void sltHandleToolMenuRequested(const QPoint &position, UIVirtualMachineItem *pItem);

        /** Handles signal about global Tools-menu index change.
          * @param  enmType  Brings current tool type. */
        void sltHandleGlobalToolsMenuIndexChange(UIToolType enmType) { switchGlobalToolTo(enmType); }
        /** Handles signal about machine Tools-menu index change.
          * @param  enmType  Brings current tool type. */
        void sltHandleMachineToolsMenuIndexChange(UIToolType enmType) { switchMachineToolTo(enmType); }

        /** Handles signal requesting switch to Activity pane of machine with @a uMachineId. */
        void sltSwitchToMachineActivityPane(const QUuid &uMachineId);
        /** Handles signal requesting switch to Resources pane. */
        void sltSwitchToActivityOverviewPane();
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares widgets. */
        void prepareWidgets();
        /** Prepares connections. */
        void prepareConnections();
        /** Loads settings. */
        void loadSettings();

        /** Updates toolbar. */
        void updateToolbar();

        /** Cleanups connections. */
        void cleanupConnections();
        /** Cleanups widgets. */
        void cleanupWidgets();
        /** Cleanups all. */
        void cleanup();
    /** @} */

    /** @name Common stuff.
      * @{ */
        /** Recaches current machine item information.
          * @param  fDontRaiseErrorPane  Brings whether we should not raise error-pane. */
        void recacheCurrentMachineItemInformation(bool fDontRaiseErrorPane = false);
    /** @} */

    /** @name Tools stuff.
      * @{ */
        /** Updates Global tools menu. */
        void updateToolsMenuGlobal();
        /** Updates Machine tools menu for @a pItem specified. */
        void updateToolsMenuMachine(UIVirtualMachineItem *pItem);

        /** Handles current tool @a enmType change. */
        void handleCurrentToolTypeChange(UIToolType enmType);
    /** @} */

    /** Holds the action-pool instance. */
    UIActionPool *m_pActionPool;

    /** Holds the central splitter instance. */
    QISplitter *m_pSplitter;

    /** Holds the main toolbar instance. */
    QIToolBar *m_pToolBar;

    /** Holds the Chooser-pane instance. */
    UIChooser          *m_pPaneChooser;
    /** Holds the stacked-widget. */
    QStackedWidget     *m_pStackedWidget;
    /** Holds the Global Tools-pane instance. */
    UIToolPaneGlobal   *m_pPaneToolsGlobal;
    /** Holds the Machine Tools-pane instance. */
    UIToolPaneMachine  *m_pPaneToolsMachine;
    /** Holds the sliding-animation widget instance. */
    UISlidingAnimation *m_pSlidingAnimation;
    /** Holds the Global Tools-menu instance. */
    UITools            *m_pMenuToolsGlobal;
    /** Holds the Machine Tools-menu instance. */
    UITools            *m_pMenuToolsMachine;

    /** Holds the last selection type. */
    SelectionType  m_enmSelectionType;
    /** Holds whether the last selected item was accessible. */
    bool           m_fSelectedMachineItemAccessible;

    /** Holds the splitter settings save timer. */
    QTimer *m_pSplitterSettingsSaveTimer;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIVirtualBoxManagerWidget_h */
