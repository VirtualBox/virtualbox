/* $Id: UIActionPoolManager.h 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIActionPoolManager class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIActionPoolManager_h
#define FEQT_INCLUDED_SRC_globals_UIActionPoolManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIActionPool.h"
#include "UILibraryDefs.h"


/** VirtualBox Manager action-pool index enum.
  * Naming convention is following:
  * 1. Every menu index prepended with 'M',
  * 2. Every simple-action index prepended with 'S',
  * 3. Every toggle-action index presended with 'T',
  * 5. Every sub-index contains full parent-index name. */
enum UIActionIndexMN
{
    /* 'File' menu actions: */
    UIActionIndexMN_M_File = UIActionIndex_Max + 1,
    UIActionIndexMN_M_File_S_ImportAppliance,
    UIActionIndexMN_M_File_S_ExportAppliance,
    UIActionIndexMN_M_File_M_Tools,
    UIActionIndexMN_M_File_M_Tools_T_HomeScreen,
    UIActionIndexMN_M_File_M_Tools_T_ExtensionPackManager,
    UIActionIndexMN_M_File_M_Tools_T_VirtualMediaManager,
    UIActionIndexMN_M_File_M_Tools_T_NetworkManager,
    UIActionIndexMN_M_File_M_Tools_T_CloudProfileManager,
    UIActionIndexMN_M_File_M_Tools_T_VMActivityOverview,
    UIActionIndexMN_M_File_M_Tools_T_MachineManager,
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    UIActionIndexMN_M_File_S_ShowExtraDataManager,
#endif
    UIActionIndexMN_M_File_S_Close,

    /* 'Home' menu actions: */
    UIActionIndexMN_M_Home,
    UIActionIndexMN_M_Home_S_New,
    UIActionIndexMN_M_Home_S_Add,

    /* 'Group' menu actions: */
    UIActionIndexMN_M_Group,
    UIActionIndexMN_M_Group_S_New,
    UIActionIndexMN_M_Group_S_Add,
    UIActionIndexMN_M_Group_S_Rename,
    UIActionIndexMN_M_Group_S_Remove,
    UIActionIndexMN_M_Group_M_MoveToGroup,
    UIActionIndexMN_M_Group_M_Start,
    UIActionIndexMN_M_Group_M_Start_S_Normal,
    UIActionIndexMN_M_Group_M_Start_S_Headless,
    UIActionIndexMN_M_Group_M_Start_S_Detachable,
    UIActionIndexMN_M_Group_S_Show,
    UIActionIndexMN_M_Group_T_Pause,
    UIActionIndexMN_M_Group_S_Reset,
    UIActionIndexMN_M_Group_S_Detach,
    UIActionIndexMN_M_Group_M_Console,
    UIActionIndexMN_M_Group_M_Console_S_CreateConnection,
    UIActionIndexMN_M_Group_M_Console_S_DeleteConnection,
    UIActionIndexMN_M_Group_M_Console_S_ConfigureApplications,
    UIActionIndexMN_M_Group_M_Stop,
    UIActionIndexMN_M_Group_M_Stop_S_SaveState,
    UIActionIndexMN_M_Group_M_Stop_S_Terminate,
    UIActionIndexMN_M_Group_M_Stop_S_Shutdown,
    UIActionIndexMN_M_Group_M_Stop_S_PowerOff,
    UIActionIndexMN_M_Group_M_Tools,
    UIActionIndexMN_M_Group_M_Tools_T_Details,
    UIActionIndexMN_M_Group_M_Tools_T_Snapshots,
    UIActionIndexMN_M_Group_M_Tools_T_Logs,
    UIActionIndexMN_M_Group_M_Tools_T_Activity,
    UIActionIndexMN_M_Group_M_Tools_T_FileManager,
    UIActionIndexMN_M_Group_S_Discard,
    UIActionIndexMN_M_Group_S_Refresh,
    UIActionIndexMN_M_Group_S_ShowInFileManager,
    UIActionIndexMN_M_Group_S_CreateShortcut,
    UIActionIndexMN_M_Group_S_Sort,
    UIActionIndexMN_M_Group_T_Search,

    /* 'Machine' menu actions: */
    UIActionIndexMN_M_Machine,
    UIActionIndexMN_M_Machine_S_New,
    UIActionIndexMN_M_Machine_S_Add,
    UIActionIndexMN_M_Machine_S_Settings,
    UIActionIndexMN_M_Machine_S_Clone,
    UIActionIndexMN_M_Machine_S_Move,
    UIActionIndexMN_M_Machine_S_ExportToOCI,
    UIActionIndexMN_M_Machine_S_Remove,
    UIActionIndexMN_M_Machine_M_MoveToGroup,
    UIActionIndexMN_M_Machine_M_MoveToGroup_S_New,
    UIActionIndexMN_M_Machine_M_Start,
    UIActionIndexMN_M_Machine_M_Start_S_Normal,
    UIActionIndexMN_M_Machine_M_Start_S_Headless,
    UIActionIndexMN_M_Machine_M_Start_S_Detachable,
    UIActionIndexMN_M_Machine_S_Show,
    UIActionIndexMN_M_Machine_T_Pause,
    UIActionIndexMN_M_Machine_S_Reset,
    UIActionIndexMN_M_Machine_S_Detach,
    UIActionIndexMN_M_Machine_M_Console,
    UIActionIndexMN_M_Machine_M_Console_S_CreateConnection,
    UIActionIndexMN_M_Machine_M_Console_S_DeleteConnection,
    UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialUnix,
    UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialWindows,
    UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCUnix,
    UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCWindows,
    UIActionIndexMN_M_Machine_M_Console_S_ConfigureApplications,
    UIActionIndexMN_M_Machine_M_Console_S_ShowLog,
    UIActionIndexMN_M_Machine_M_Stop,
    UIActionIndexMN_M_Machine_M_Stop_S_SaveState,
    UIActionIndexMN_M_Machine_M_Stop_S_Terminate,
    UIActionIndexMN_M_Machine_M_Stop_S_Shutdown,
    UIActionIndexMN_M_Machine_M_Stop_S_PowerOff,
    UIActionIndexMN_M_Machine_M_Tools,
    UIActionIndexMN_M_Machine_M_Tools_T_Details,
    UIActionIndexMN_M_Machine_M_Tools_T_Snapshots,
    UIActionIndexMN_M_Machine_M_Tools_T_Logs,
    UIActionIndexMN_M_Machine_M_Tools_T_Activity,
    UIActionIndexMN_M_Machine_M_Tools_T_FileManager,
    UIActionIndexMN_M_Machine_S_Discard,
    UIActionIndexMN_M_Machine_S_Refresh,
    UIActionIndexMN_M_Machine_S_ShowInFileManager,
    UIActionIndexMN_M_Machine_S_CreateShortcut,
    UIActionIndexMN_M_Machine_S_SortParent,
    UIActionIndexMN_M_Machine_T_Search,

    /* Snapshot Pane actions: */
    UIActionIndexMN_M_Snapshot,
    UIActionIndexMN_M_Snapshot_S_Take,
    UIActionIndexMN_M_Snapshot_S_Delete,
    UIActionIndexMN_M_Snapshot_S_Restore,
    UIActionIndexMN_M_Snapshot_T_Properties,
    UIActionIndexMN_M_Snapshot_S_Clone,

    /* Extension Pack Manager actions: */
    UIActionIndexMN_M_ExtensionWindow,
    UIActionIndexMN_M_Extension,
    UIActionIndexMN_M_Extension_S_Install,
    UIActionIndexMN_M_Extension_S_Uninstall,

    /* Virtual Media Manager actions: */
    UIActionIndexMN_M_MediumWindow,
    UIActionIndexMN_M_Medium,
    UIActionIndexMN_M_Medium_S_Add,
    UIActionIndexMN_M_Medium_S_Create,
    UIActionIndexMN_M_Medium_S_Copy,
    UIActionIndexMN_M_Medium_S_Move,
    UIActionIndexMN_M_Medium_S_Remove,
    UIActionIndexMN_M_Medium_S_Release,
    UIActionIndexMN_M_Medium_T_Details,
    UIActionIndexMN_M_Medium_T_Search,
    UIActionIndexMN_M_Medium_S_Refresh,
    UIActionIndexMN_M_Medium_S_Clear,
    UIActionIndexMN_M_Medium_S_Edit,

    /* Network Manager actions: */
    UIActionIndexMN_M_NetworkWindow,
    UIActionIndexMN_M_Network,
    UIActionIndexMN_M_Network_S_Create,
    UIActionIndexMN_M_Network_S_Remove,
    UIActionIndexMN_M_Network_T_Details,
    UIActionIndexMN_M_Network_S_Refresh,

    /* Cloud Profile Manager actions: */
    UIActionIndexMN_M_CloudWindow,
    UIActionIndexMN_M_Cloud,
    UIActionIndexMN_M_Cloud_S_Add,
    UIActionIndexMN_M_Cloud_S_Import,
    UIActionIndexMN_M_Cloud_S_Remove,
    UIActionIndexMN_M_Cloud_T_Details,
    UIActionIndexMN_M_Cloud_S_TryPage,
    UIActionIndexMN_M_Cloud_S_Help,

    /* Cloud Console Manager actions: */
    UIActionIndexMN_M_CloudConsoleWindow,
    UIActionIndexMN_M_CloudConsole,
    UIActionIndexMN_M_CloudConsole_S_ApplicationAdd,
    UIActionIndexMN_M_CloudConsole_S_ApplicationRemove,
    UIActionIndexMN_M_CloudConsole_S_ProfileAdd,
    UIActionIndexMN_M_CloudConsole_S_ProfileRemove,
    UIActionIndexMN_M_CloudConsole_T_Details,

    /* VM VM Activity Overview actions: */
    UIActionIndexMN_M_VMActivityOverview,
    UIActionIndexMN_M_VMActivityOverview_M_Columns,
    UIActionIndexMN_M_VMActivityOverview_S_SwitchToMachineActivity,

    /* Maximum index: */
    UIActionIndexMN_Max
};


/** UIActionPool extension
  * representing action-pool singleton for Manager UI. */
class SHARED_LIBRARY_STUFF UIActionPoolManager : public UIActionPool
{
    Q_OBJECT;

protected:

    /** Constructs action-pool.
      * @param  fTemporary  Brings whether this action-pool is temporary,
      *                     used to (re-)initialize shortcuts-pool. */
    UIActionPoolManager(bool fTemporary = false);

    /** Prepares pool. */
    virtual void preparePool() RT_OVERRIDE;
    /** Prepares connections. */
    virtual void prepareConnections() RT_OVERRIDE;

    /** Updates menu. */
    virtual void updateMenu(int iIndex) RT_OVERRIDE;
    /** Updates menus. */
    virtual void updateMenus() RT_OVERRIDE;

    /** Defines whether shortcuts of menu actions with specified @a iIndex should be visible. */
    virtual void setShortcutsVisible(int iIndex, bool fVisible) RT_OVERRIDE;
    /** Returns extra-data ID to save keyboard shortcuts under. */
    virtual QString shortcutsExtraDataID() const RT_OVERRIDE;
    /** Updates shortcuts. */
    virtual void updateShortcuts() RT_OVERRIDE;

private slots:

    /** Handles signal about settings expert mode change. */
    void sltHandleSettingsExpertModeChange();

private:

    /** Updates 'File' menu. */
    void updateMenuFile();
    /** Updates 'File' / 'Tools' menu. */
    void updateMenuFileTools();
    /** Updates 'Home' menu. */
    void updateMenuHome();
    /** Updates 'Group' menu. */
    void updateMenuGroup();
    /** Updates 'Machine' menu. */
    void updateMenuMachine();
    /** Updates 'Group' / 'Move to Group' menu. */
    void updateMenuGroupMoveToGroup();
    /** Updates 'Machine' / 'Move to Group' menu. */
    void updateMenuMachineMoveToGroup();
    /** Updates 'Group' / 'Start' menu. */
    void updateMenuGroupStart();
    /** Updates 'Machine' / 'Start' menu. */
    void updateMenuMachineStart();
    /** Updates 'Group' / 'Console' menu. */
    void updateMenuGroupConsole();
    /** Updates 'Machine' / 'Console' menu. */
    void updateMenuMachineConsole();
    /** Updates 'Group' / 'Close' menu. */
    void updateMenuGroupClose();
    /** Updates 'Machine' / 'Close' menu. */
    void updateMenuMachineClose();
    /** Updates 'Group' / 'Tools' menu. */
    void updateMenuGroupTools();
    /** Updates 'Machine' / 'Tools' menu. */
    void updateMenuMachineTools();

    /** Updates 'Extension Pack' window menu. */
    void updateMenuExtensionWindow();
    /** Updates 'Extension Pack' menu. */
    void updateMenuExtension();
    /** Updates 'Extension Pack' @a pMenu. */
    void updateMenuExtensionWrapper(UIMenu *pMenu);

    /** Updates 'Medium' window menu. */
    void updateMenuMediumWindow();
    /** Updates 'Medium' menu. */
    void updateMenuMedium();
    /** Updates 'Medium' @a pMenu. */
    void updateMenuMediumWrapper(UIMenu *pMenu);

    /** Updates 'Network' window menu. */
    void updateMenuNetworkWindow();
    /** Updates 'Network' menu. */
    void updateMenuNetwork();
    /** Updates 'Network' @a pMenu. */
    void updateMenuNetworkWrapper(UIMenu *pMenu);

    /** Updates 'Cloud' window menu. */
    void updateMenuCloudWindow();
    /** Updates 'Cloud' menu. */
    void updateMenuCloud();
    /** Updates 'Cloud' @a pMenu. */
    void updateMenuCloudWrapper(UIMenu *pMenu);

    /** Updates 'Cloud Console' window menu. */
    void updateMenuCloudConsoleWindow();
    /** Updates 'Cloud Console' menu. */
    void updateMenuCloudConsole();
    /** Updates 'Cloud Console' @a pMenu. */
    void updateMenuCloudConsoleWrapper(UIMenu *pMenu);

    /** Updates 'VM VM Activity Overview' menu. */
    void updateMenuVMActivityOverview();
    /** Updates 'VM VM Activity Overview' @a pMenu. */
    void updateMenuVMActivityOverviewWrapper(UIMenu *pMenu);

    /** Updates 'Snapshot' menu. */
    void updateMenuSnapshot();

    /** Enables factory in base-class. */
    friend class UIActionPool;
};


#endif /* !FEQT_INCLUDED_SRC_globals_UIActionPoolManager_h */
