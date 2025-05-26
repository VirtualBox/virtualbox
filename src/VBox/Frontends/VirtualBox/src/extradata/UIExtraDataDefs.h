/* $Id: UIExtraDataDefs.h 168992 2025-05-26 13:30:39Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - Extra-data related definitions.
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

#ifndef FEQT_INCLUDED_SRC_extradata_UIExtraDataDefs_h
#define FEQT_INCLUDED_SRC_extradata_UIExtraDataDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QMetaType>
#include <QObject>

/* GUI includes: */
#include "UILibraryDefs.h"


/** Typedef for QPair of QStrings. */
typedef QPair<QString, QString> QIStringPair;
typedef QList<QIStringPair> QIStringPairList;


/** Extra-data namespace. */
namespace UIExtraDataDefs
{
    /** @name General
      * @{ */
        /** Holds restricted dialogs. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedDialogs;

        /** Holds the color theme type. */
        SHARED_LIBRARY_STUFF extern const char *GUI_ColorTheme;
    /** @} */

    /** @name Messaging
      * @{ */
        /** Holds the list of suppressed messages for the Message/Popup center frameworks. */
        SHARED_LIBRARY_STUFF extern const char *GUI_SuppressMessages;
        /** Holds the list of messages for the Message/Popup center frameworks with inverted check-box state. */
        SHARED_LIBRARY_STUFF extern const char *GUI_InvertMessageOption;
#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
        /** Holds whether successfull notification-progresses should NOT close automatically. */
        SHARED_LIBRARY_STUFF extern const char *GUI_NotificationCenter_KeepSuccessfullProgresses;
#endif
        /** Holds notification-center alignment. */
        SHARED_LIBRARY_STUFF extern const char *GUI_NotificationCenter_Alignment;
        /** Holds notification-center order. */
        SHARED_LIBRARY_STUFF extern const char *GUI_NotificationCenter_Order;
        /** Holds whether BETA build label should be hidden. */
        SHARED_LIBRARY_STUFF extern const char *GUI_PreventBetaLabel;
#if !defined(VBOX_BLEEDING_EDGE) && !defined(DEBUG)
        /** Holds version for which user wants to prevent BETA build warning. */
        SHARED_LIBRARY_STUFF extern const char *GUI_PreventBetaWarning;
#endif
    /** @} */

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    /** @name Application Update
      * @{ */
        /** Holds whether Application Update functionality enabled. */
        SHARED_LIBRARY_STUFF extern const char *GUI_PreventApplicationUpdate;
        /** Holds Application Update data. */
        SHARED_LIBRARY_STUFF extern const char *GUI_UpdateDate;
        /** Holds Application Update check counter. */
        SHARED_LIBRARY_STUFF extern const char *GUI_UpdateCheckCount;
    /** @} */
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */

    /** @name Progress
      * @{ */
        /** Holds whether legacy progress handling method is requested. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Progress_LegacyMode;
    /** @} */

    /** @name Settings
      * @{ */
        /** Holds GUI feature list. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Customizations;
        /** Holds restricted Global Settings pages. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedGlobalSettingsPages;
        /** Holds restricted Machine Settings pages. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedMachineSettingsPages;
        /** Holds whether settings are in expert mode. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Settings_ExpertMode;
    /** @} */

    /** @name Settings: Language
      * @{ */
        /** Holds GUI language ID. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LanguageID;
    /** @} */

    /** @name Settings: Display
      * @{ */
        /** Holds maximum guest-screen resolution policy/value. */
        SHARED_LIBRARY_STUFF extern const char *GUI_MaxGuestResolution;
        /** Holds whether hovered machine-window should be activated. */
        SHARED_LIBRARY_STUFF extern const char *GUI_ActivateHoveredMachineWindow;
        /** Holds whether the host scrrn saver is disabled when a vm is running. */
        SHARED_LIBRARY_STUFF extern const char *GUI_DisableHostScreenSaver;
    /** @} */

    /** @name Settings: Keyboard
      * @{ */
        /** Holds Selector UI shortcut overrides. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Input_SelectorShortcuts;
        /** Holds Runtime UI shortcut overrides. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Input_MachineShortcuts;
        /** Holds Runtime UI host-key combination. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Input_HostKeyCombination;
        /** Holds whether Runtime UI auto-capture is enabled. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Input_AutoCapture;
        /** Holds Runtime UI remapped scan codes. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RemapScancodes;
    /** @} */

    /** @name Settings: Proxy
      * @{ */
        /** Holds VBox proxy settings. */
        SHARED_LIBRARY_STUFF extern const char *GUI_ProxySettings;
    /** @} */

    /** @name Settings: Storage
      * @{ */
        /** Holds recent folder for hard-drives. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RecentFolderHD;
        /** Holds recent folder for optical-disks. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RecentFolderCD;
        /** Holds recent folder for floppy-disks. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RecentFolderFD;
        /** Holds the list of recently used hard-drives. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RecentListHD;
        /** Holds the list of recently used optical-disks. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RecentListCD;
        /** Holds the list of recently used floppy-disks. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RecentListFD;
    /** @} */

    /** @name Settings: Network
      * @{ */
        /** Holds the list of restricted network attachment types. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedNetworkAttachmentTypes;
    /** @} */

    /** @name VISO Creator
      * @{ */
        /** Holds recent folder for VISO creation content. */
        SHARED_LIBRARY_STUFF extern const char *GUI_VISOCreator_RecentFolder;
        SHARED_LIBRARY_STUFF extern const char *GUI_VISOCreator_DialogGeometry;
    /** @} */

    /** @name VirtualBox Manager
      * @{ */
        /** Holds selector-window geometry. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LastSelectorWindowPosition;
        /** Holds selector-window splitter hints. */
        SHARED_LIBRARY_STUFF extern const char *GUI_SplitterSizes;
        /** Holds whether selector-window tool-bar visible. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Toolbar;
        /** Holds whether selector-window tool-bar text visible. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Toolbar_Text;
        /** Holds the selector-window machine tools order. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Toolbar_MachineTools_Order;
        /** Holds the selector-window global tools order. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Toolbar_GlobalTools_Order;
        /** Holds the last selected tool set of VirtualBox Manager. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Tools_LastItemsSelected;
        /** Holds the list of detached tools of VirtualBox Manager. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Tools_Detached;
        /** Holds whether tools tool-bar text visible. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Tools_Text;
        /** Holds whether selector-window status-bar visible. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Statusbar;
        /** Prefix used by composite extra-data keys,
          * which holds selector-window chooser-pane' groups definitions. */
        SHARED_LIBRARY_STUFF extern const char *GUI_GroupDefinitions;
        /** Holds last item chosen in selector-window chooser-pane. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LastItemSelected;
        /** Holds selector-window details-pane' elements. */
        /// @todo remove GUI_DetailsPageBoxes in 6.2
        SHARED_LIBRARY_STUFF extern const char *GUI_DetailsPageBoxes;
        /// @todo remove GUI_PreviewUpdate in 6.2
        /** Holds selector-window details-pane' preview update interval. */
        SHARED_LIBRARY_STUFF extern const char *GUI_PreviewUpdate;
        /** Holds VirtualBox Manager Details-pane elements. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Details_Elements;
        /** Holds VirtualBox Manager Details-pane / Preview element update interval. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Details_Elements_Preview_UpdateInterval;
    /** @} */

    /** @name Snapshot Manager
      * @{ */
        /** Holds whether Snapshot Manager details expanded. */
        SHARED_LIBRARY_STUFF extern const char *GUI_SnapshotManager_Details_Expanded;
    /** @} */

    /** @name Virtual Media Manager
      * @{ */
        /** Holds whether Virtual Media Manager details expanded. */
        SHARED_LIBRARY_STUFF extern const char *GUI_VirtualMediaManager_Details_Expanded;
        /** Holds whether Virtual Media Manager search widget expanded. */
        SHARED_LIBRARY_STUFF extern const char *GUI_VirtualMediaManager_Search_Widget_Expanded;
    /** @} */

    /** @name Host Network Manager
      * @{ */
        /** Holds whether Host Network Manager details expanded. */
        SHARED_LIBRARY_STUFF extern const char *GUI_HostNetworkManager_Details_Expanded;
    /** @} */

    /** @name Cloud Profile Manager
      * @{ */
        /** Holds Cloud Profile Manager restrictions. */
        SHARED_LIBRARY_STUFF extern const char *GUI_CloudProfileManager_Restrictions;
        /** Holds whether Cloud Profile Manager details expanded. */
        SHARED_LIBRARY_STUFF extern const char *GUI_CloudProfileManager_Details_Expanded;
    /** @} */

    /** @name Cloud Console Manager
      * @{ */
        /** Holds Cloud Console Manager applications/profiles. */
        SHARED_LIBRARY_STUFF extern const char *GUI_CloudConsoleManager_Application;
        /** Holds Cloud Console Manager restrictions. */
        SHARED_LIBRARY_STUFF extern const char *GUI_CloudConsoleManager_Restrictions;
        /** Holds whether Cloud Console Manager details expanded. */
        SHARED_LIBRARY_STUFF extern const char *GUI_CloudConsoleManager_Details_Expanded;
    /** @} */

    /** @name Cloud Console
      * @{ */
        /** Holds Cloud Console public key path. */
        SHARED_LIBRARY_STUFF extern const char *GUI_CloudConsole_PublicKey_Path;
    /** @} */

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    /** @name Extra-data Manager
      * @{ */
        /** Holds extra-data manager geometry. */
        SHARED_LIBRARY_STUFF extern const char *GUI_ExtraDataManager_Geometry;
        /** Holds extra-data manager splitter hints. */
        SHARED_LIBRARY_STUFF extern const char *GUI_ExtraDataManager_SplitterHints;
    /** @} */
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

    /** @name Virtual Machine
      * @{ */
        /** Holds whether machine shouldn't be shown in selector-window chooser-pane. */
        SHARED_LIBRARY_STUFF extern const char *GUI_HideFromManager;
        /** Holds whether machine shouldn't be shown in selector-window details-pane. */
        SHARED_LIBRARY_STUFF extern const char *GUI_HideDetails;
        /** Holds whether machine reconfiguration disabled. */
        SHARED_LIBRARY_STUFF extern const char *GUI_PreventReconfiguration;
        /** Holds whether machine snapshot operations disabled. */
        SHARED_LIBRARY_STUFF extern const char *GUI_PreventSnapshotOperations;
        /** Except Mac OS X: Holds redefined machine-window icon names. */
        SHARED_LIBRARY_STUFF extern const char *GUI_MachineWindowIcons;
#ifndef VBOX_WS_MAC
        /** Except Mac OS X: Holds redefined machine-window name postfix. */
        SHARED_LIBRARY_STUFF extern const char *GUI_MachineWindowNamePostfix;
#endif
        /** Prefix used by composite extra-data keys,
          * which holds normal machine-window geometry per screen-index. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LastNormalWindowPosition;
        /** Prefix used by composite extra-data keys,
          * which holds scaled machine-window geometry per screen-index. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LastScaleWindowPosition;
        /** Holds machine-window geometry maximized state flag. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Geometry_State_Max;
#ifndef VBOX_WS_MAC
        /** Holds Runtime UI menu-bar availability status. */
        SHARED_LIBRARY_STUFF extern const char *GUI_MenuBar_Enabled;
#endif
        /** Holds Runtime UI menu-bar context-menu availability status. */
        SHARED_LIBRARY_STUFF extern const char *GUI_MenuBar_ContextMenu_Enabled;
        /** Holds restricted Runtime UI menu types. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedRuntimeMenus;
        /** Holds restricted Runtime UI action types for 'Application' menu. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedRuntimeApplicationMenuActions;
        /** Holds restricted Runtime UI action types for Machine menu. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedRuntimeMachineMenuActions;
        /** Holds restricted Runtime UI action types for View menu. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedRuntimeViewMenuActions;
        /** Holds restricted Runtime UI action types for Input menu. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedRuntimeInputMenuActions;
        /** Holds restricted Runtime UI action types for Devices menu. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedRuntimeDevicesMenuActions;
#ifdef VBOX_WITH_DEBUGGER_GUI
        /** Holds restricted Runtime UI action types for Debugger menu. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedRuntimeDebuggerMenuActions;
#endif
#ifdef VBOX_WS_MAC
        /** Mac OS X: Holds restricted Runtime UI action types for 'Window' menu. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedRuntimeWindowMenuActions;
#endif
        /** Holds restricted Runtime UI action types for Help menu. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedRuntimeHelpMenuActions;
        /** Holds restricted Runtime UI visual-states. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedVisualStates;
        /** Holds whether full screen visual-state is requested. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Fullscreen;
        /** Holds whether seamless visual-state is requested. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Seamless;
        /** Holds whether scaled visual-state is requested. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Scale;
#ifdef VBOX_WS_NIX
        /** Holds whether legacy full-screen mode is requested. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Fullscreen_LegacyMode;
        /** Holds whether internal machine-window names should be unique. */
        SHARED_LIBRARY_STUFF extern const char *GUI_DistinguishMachineWindowGroups;
#endif /* VBOX_WS_NIX */
        /** Holds whether guest-screen auto-resize according machine-window size is enabled. */
        SHARED_LIBRARY_STUFF extern const char *GUI_AutoresizeGuest;
        /** Prefix used by composite extra-data keys,
          * which holds last guest-screen visibility status per screen-index. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LastVisibilityStatusForGuestScreen;
        /** Prefix used by composite extra-data keys,
          * which holds last guest-screen size-hint per screen-index. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LastGuestSizeHint;
        /** Prefix used by composite extra-data keys,
          * which holds host-screen index per guest-screen index. */
        SHARED_LIBRARY_STUFF extern const char *GUI_VirtualScreenToHostScreen;
        /** Holds whether automatic mounting/unmounting of guest-screens enabled. */
        SHARED_LIBRARY_STUFF extern const char *GUI_AutomountGuestScreens;
#ifndef VBOX_WS_MAC
        /** Holds whether mini-toolbar is enabled for full and seamless screens. */
        SHARED_LIBRARY_STUFF extern const char *GUI_ShowMiniToolBar;
        /** Holds whether mini-toolbar should auto-hide itself. */
        SHARED_LIBRARY_STUFF extern const char *GUI_MiniToolBarAutoHide;
        /** Holds mini-toolbar alignment. */
        SHARED_LIBRARY_STUFF extern const char *GUI_MiniToolBarAlignment;
#endif /* !VBOX_WS_MAC */
        /** Holds Runtime UI status-bar availability status. */
        SHARED_LIBRARY_STUFF extern const char *GUI_StatusBar_Enabled;
        /** Holds Runtime UI status-bar context-menu availability status. */
        SHARED_LIBRARY_STUFF extern const char *GUI_StatusBar_ContextMenu_Enabled;
        /** Holds restricted Runtime UI status-bar indicators. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedStatusBarIndicators;
        /** Holds Runtime UI status-bar indicator order. */
        SHARED_LIBRARY_STUFF extern const char *GUI_StatusBar_IndicatorOrder;
#ifdef VBOX_WS_MAC
        /** Mac OS X: Holds whether Dock icon should be updated at runtime. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RealtimeDockIconUpdateEnabled;
        /** Mac OS X: Holds guest-screen which Dock icon should reflect at runtime. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RealtimeDockIconUpdateMonitor;
        /** Mac OS X: Holds whether Dock icon should have overlay disabled. */
        SHARED_LIBRARY_STUFF extern const char *GUI_DockIconDisableOverlay;
#endif /* VBOX_WS_MAC */
        /** Holds whether machine should pass CAD to guest. */
        SHARED_LIBRARY_STUFF extern const char *GUI_PassCAD;
        /** Holds the mouse capture policy. */
        SHARED_LIBRARY_STUFF extern const char *GUI_MouseCapturePolicy;
        /** Holds redefined guru-meditation handler type. */
        SHARED_LIBRARY_STUFF extern const char *GUI_GuruMeditationHandler;
        /** Holds whether machine should perform HID LEDs synchronization. */
        SHARED_LIBRARY_STUFF extern const char *GUI_HidLedsSync;
        /** Holds the scale-factor. */
        SHARED_LIBRARY_STUFF extern const char *GUI_ScaleFactor;
        /** Holds the scaling optimization type. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Scaling_Optimization;
        /** Holds the font scale factor. */
        SHARED_LIBRARY_STUFF extern const char *GUI_FontScaleFactor;
    /** @} */

    /** @name Virtual Machine: Information dialog
      * @{ */
        /** Holds information-window geometry. */
        SHARED_LIBRARY_STUFF extern const char *GUI_SessionInformationDialogGeometry;
    /** @} */

    /** @name Guest Control UI related data
      * @{ */
        extern const char *GUI_GuestControl_FileManagerDialogGeometry;
        extern const char *GUI_GuestControl_FileManagerVisiblePanels;
        extern const char *GUI_GuestControl_ProcessControlSplitterHints;
        extern const char *GUI_GuestControl_ProcessControlDialogGeometry;
    /** @} */

    /** @name Soft Keyboard related data
      * @{ */
        extern const char *GUI_SoftKeyboard_DialogGeometry;
        extern const char *GUI_SoftKeyboard_ColorTheme;
        extern const char *GUI_SoftKeyboard_SelectedColorTheme;
        extern const char *GUI_SoftKeyboard_SelectedLayout;
        extern const char *GUI_SoftKeyboard_Options;
        extern const char *GUI_SoftKeyboard_HideNumPad;
        extern const char *GUI_SoftKeyboard_HideOSMenuKeys;
        extern const char *GUI_SoftKeyboard_HideMultimediaKeys;
    /** @} */

    /** @name File Manager options
      * @{ */
        extern const char *GUI_GuestControl_FileManagerOptions;
        extern const char *GUI_GuestControl_FileManagerListDirectoriesFirst;
        extern const char *GUI_GuestControl_FileManagerShowDeleteConfirmation;
        extern const char *GUI_GuestControl_FileManagerShowHumanReadableSizes;
        extern const char *GUI_GuestControl_FileManagerShowHiddenObjects;
    /** @} */

    /** @name Virtual Machine: Close dialog
      * @{ */
        /** Holds default machine close action. */
        SHARED_LIBRARY_STUFF extern const char *GUI_DefaultCloseAction;
        /** Holds restricted machine close actions. */
        SHARED_LIBRARY_STUFF extern const char *GUI_RestrictedCloseActions;
        /** Holds last machine close action. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LastCloseAction;
        /** Holds machine close hook script name as simple string. */
        SHARED_LIBRARY_STUFF extern const char *GUI_CloseActionHook;
        /** Holds whether machine should discard state on power off. */
        SHARED_LIBRARY_STUFF extern const char *GUI_DiscardStateOnPowerOff;
    /** @} */

#ifdef VBOX_WITH_DEBUGGER_GUI
    /** @name Virtual Machine: Debug UI
      * @{ */
        /** Holds whether debugger UI enabled. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Dbg_Enabled;
        /** Holds whether debugger UI should be auto-shown. */
        SHARED_LIBRARY_STUFF extern const char *GUI_Dbg_AutoShow;
    /** @} */
#endif /* VBOX_WITH_DEBUGGER_GUI */

    /** @name Virtual Machine: Log-viewer
      * @{ */
        /** Holds log-viewer geometry. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LogWindowGeometry;
    /** @} */
    /** @name Virtual Machine: Log-viewer widget options
      * @{ */
        SHARED_LIBRARY_STUFF extern const char *GUI_LogViewerOptions;
        /** Holds log-viewer wrap line flag. */
        SHARED_LIBRARY_STUFF extern const char *GUI_LogViewerWrapLinesEnabled;
        SHARED_LIBRARY_STUFF extern const char *GUI_LogViewerShowLineNumbersDisabled;
        SHARED_LIBRARY_STUFF extern const char *GUI_LogViewerNoFontStyleName;
    /** @} */

    /** @name Help Browser
      * @{ */
        SHARED_LIBRARY_STUFF extern const char *GUI_HelpBrowser_LastURLList;
        SHARED_LIBRARY_STUFF extern const char *GUI_HelpBrowser_DialogGeometry;
        SHARED_LIBRARY_STUFF extern const char *GUI_HelpBrowser_Bookmarks;
        SHARED_LIBRARY_STUFF extern const char *GUI_HelpBrowser_ZoomPercentage;
    /** @} */

    /** @name Manager UI: VM Activity Monitor and Overview Related stuff
      * @{ */
        SHARED_LIBRARY_STUFF extern const char *GUI_VMActivityOverview_HiddenColumns;
        SHARED_LIBRARY_STUFF extern const char *GUI_VMActivityOverview_ShowAllMachines;
        SHARED_LIBRARY_STUFF extern const char *GUI_VMActivityMonitor_DataSeriesColors;
        SHARED_LIBRARY_STUFF extern const char *GUI_VMActivityMonitor_ShowVMExits;
    /** @} */

    /** @name Medium Selector stuff
      * @{ */
        SHARED_LIBRARY_STUFF extern const char *GUI_MediumSelector_DialogGeometry;
    /** @} */

    /** @name Old key support stuff.
      * @{ */
        /** Prepares obsolete keys map. */
        SHARED_LIBRARY_STUFF QMultiMap<QString, QString> prepareObsoleteKeysMap();

        /** Holds the obsolete keys map. */
        SHARED_LIBRARY_STUFF extern QMultiMap<QString, QString> g_mapOfObsoleteKeys;
    /** @} */

    /** @name Font scaling factor min-max.
      * @{ */
        extern const int iFontScaleMin;
        extern const int iFontScaleMax;
    /** @} */
}


/** Extra-data meta definitions. */
class SHARED_LIBRARY_STUFF UIExtraDataMetaDefs : public QObject
{
    Q_OBJECT;

    /* Menu related stuff: */
    Q_ENUMS(MenuType);
    Q_ENUMS(MenuApplicationActionType);
    Q_ENUMS(MenuHelpActionType);
    Q_ENUMS(RuntimeMenuMachineActionType);
    Q_ENUMS(RuntimeMenuViewActionType);
    Q_ENUMS(RuntimeMenuInputActionType);
    Q_ENUMS(RuntimeMenuDevicesActionType);
#ifdef VBOX_WITH_DEBUGGER_GUI
    Q_ENUMS(RuntimeMenuDebuggerActionType);
#endif
#ifdef VBOX_WS_MAC
    Q_ENUMS(MenuWindowActionType);
#endif

public:

    /** Common UI: Dialog types. */
    enum DialogType
    {
        DialogType_Invalid     = 0,
        DialogType_VISOCreator = RT_BIT(0),
        DialogType_BootFailure = RT_BIT(1),
        DialogType_All         = 0xFFFF
    };
    Q_ENUM(DialogType);

    /** Common UI: Menu types. */
    enum MenuType
    {
        MenuType_Invalid     = 0,
        MenuType_Application = RT_BIT(0),
        MenuType_Machine     = RT_BIT(1),
        MenuType_View        = RT_BIT(2),
        MenuType_Input       = RT_BIT(3),
        MenuType_Devices     = RT_BIT(4),
#ifdef VBOX_WITH_DEBUGGER_GUI
        MenuType_Debug       = RT_BIT(5),
#endif
#ifdef VBOX_WS_MAC
        MenuType_Window      = RT_BIT(6),
#endif
        MenuType_Help        = RT_BIT(7),
        MenuType_All         = 0xFF
    };

    /** Menu "Application": Action types. */
    enum MenuApplicationActionType
    {
        MenuApplicationActionType_Invalid              = 0,
#ifdef VBOX_WS_MAC
        MenuApplicationActionType_About                = RT_BIT(0),
#endif
        MenuApplicationActionType_Preferences          = RT_BIT(1),
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        MenuApplicationActionType_NetworkAccessManager = RT_BIT(2),
        MenuApplicationActionType_CheckForUpdates      = RT_BIT(3),
#endif
        MenuApplicationActionType_ResetWarnings        = RT_BIT(4),
        MenuApplicationActionType_Close                = RT_BIT(5),
        MenuApplicationActionType_All                  = 0xFFFF
    };

    /** Menu "Help": Action types. */
    enum MenuHelpActionType
    {
        MenuHelpActionType_Invalid              = 0,
        MenuHelpActionType_Contents             = RT_BIT(0),
        MenuHelpActionType_WebSite              = RT_BIT(1),
        MenuHelpActionType_BugTracker           = RT_BIT(2),
        MenuHelpActionType_Forums               = RT_BIT(3),
        MenuHelpActionType_Oracle               = RT_BIT(4),
        MenuHelpActionType_OnlineDocumentation  = RT_BIT(5),
#ifndef VBOX_WS_MAC
        MenuHelpActionType_About                = RT_BIT(6),
#endif
        MenuHelpActionType_All                  = 0xFFFF
    };

    /** Runtime UI: Menu "Machine": Action types. */
    enum RuntimeMenuMachineActionType
    {
        RuntimeMenuMachineActionType_Invalid                       = 0,
        RuntimeMenuMachineActionType_SettingsDialog                = RT_BIT(0),
        RuntimeMenuMachineActionType_TakeSnapshot                  = RT_BIT(1),
        RuntimeMenuMachineActionType_InformationDialog             = RT_BIT(2),
        RuntimeMenuMachineActionType_FileManagerDialog             = RT_BIT(3),
        RuntimeMenuMachineActionType_GuestProcessControlDialog     = RT_BIT(4),
        RuntimeMenuMachineActionType_Pause                         = RT_BIT(5),
        RuntimeMenuMachineActionType_Reset                         = RT_BIT(6),
        RuntimeMenuMachineActionType_Detach                        = RT_BIT(7),
        RuntimeMenuMachineActionType_SaveState                     = RT_BIT(8),
        RuntimeMenuMachineActionType_Shutdown                      = RT_BIT(9),
        RuntimeMenuMachineActionType_PowerOff                      = RT_BIT(10),
        RuntimeMenuMachineActionType_LogDialog                     = RT_BIT(11),
        RuntimeMenuMachineActionType_Nothing                       = RT_BIT(12),
        RuntimeMenuMachineActionType_All                           = 0xFFFF
    };

    /** Runtime UI: Menu "View": Action types. */
    enum RuntimeMenuViewActionType
    {
        RuntimeMenuViewActionType_Invalid              = 0,
        RuntimeMenuViewActionType_Fullscreen           = RT_BIT(0),
        RuntimeMenuViewActionType_Seamless             = RT_BIT(1),
        RuntimeMenuViewActionType_Scale                = RT_BIT(2),
#ifndef VBOX_WS_MAC
        RuntimeMenuViewActionType_MinimizeWindow       = RT_BIT(3),
#endif
        RuntimeMenuViewActionType_AdjustWindow         = RT_BIT(4),
        RuntimeMenuViewActionType_GuestAutoresize      = RT_BIT(5),
        RuntimeMenuViewActionType_TakeScreenshot       = RT_BIT(6),
        RuntimeMenuViewActionType_Recording            = RT_BIT(7),
        RuntimeMenuViewActionType_RecordingSettings    = RT_BIT(8),
        RuntimeMenuViewActionType_StartRecording       = RT_BIT(9),
        RuntimeMenuViewActionType_VRDEServer           = RT_BIT(10),
        RuntimeMenuViewActionType_MenuBar              = RT_BIT(11),
        RuntimeMenuViewActionType_MenuBarSettings      = RT_BIT(12),
#ifndef VBOX_WS_MAC
        RuntimeMenuViewActionType_ToggleMenuBar        = RT_BIT(13),
#endif
        RuntimeMenuViewActionType_StatusBar            = RT_BIT(14),
        RuntimeMenuViewActionType_StatusBarSettings    = RT_BIT(15),
        RuntimeMenuViewActionType_ToggleStatusBar      = RT_BIT(16),
        RuntimeMenuViewActionType_Resize               = RT_BIT(17),
        RuntimeMenuViewActionType_Remap                = RT_BIT(18),
        RuntimeMenuViewActionType_Rescale              = RT_BIT(19),
        RuntimeMenuViewActionType_All                  = 0xFFFF
    };

    /** Runtime UI: Menu "Input": Action types. */
    enum RuntimeMenuInputActionType
    {
        RuntimeMenuInputActionType_Invalid            = 0,
        RuntimeMenuInputActionType_Keyboard           = RT_BIT(0),
        RuntimeMenuInputActionType_KeyboardSettings   = RT_BIT(1),
        RuntimeMenuInputActionType_SoftKeyboard       = RT_BIT(2),
        RuntimeMenuInputActionType_TypeCAD            = RT_BIT(3),
#ifdef VBOX_WS_NIX
        RuntimeMenuInputActionType_TypeCABS           = RT_BIT(4),
#endif
        RuntimeMenuInputActionType_TypeCtrlBreak      = RT_BIT(5),
        RuntimeMenuInputActionType_TypeInsert         = RT_BIT(6),
        RuntimeMenuInputActionType_TypePrintScreen    = RT_BIT(7),
        RuntimeMenuInputActionType_TypeAltPrintScreen = RT_BIT(8),
        RuntimeMenuInputActionType_Mouse              = RT_BIT(9),
        RuntimeMenuInputActionType_MouseIntegration   = RT_BIT(10),
        RuntimeMenuInputActionType_TypeHostKeyCombo   = RT_BIT(11),
        RuntimeMenuInputActionType_All                = 0xFFFF
    };

    /** Runtime UI: Menu "Devices": Action types. */
    enum RuntimeMenuDevicesActionType
    {
        RuntimeMenuDevicesActionType_Invalid                   = 0,
        RuntimeMenuDevicesActionType_HardDrives                = RT_BIT(0),
        RuntimeMenuDevicesActionType_HardDrivesSettings        = RT_BIT(1),
        RuntimeMenuDevicesActionType_OpticalDevices            = RT_BIT(2),
        RuntimeMenuDevicesActionType_FloppyDevices             = RT_BIT(3),
        RuntimeMenuDevicesActionType_Audio                     = RT_BIT(4),
        RuntimeMenuDevicesActionType_AudioOutput               = RT_BIT(5),
        RuntimeMenuDevicesActionType_AudioInput                = RT_BIT(6),
        RuntimeMenuDevicesActionType_Network                   = RT_BIT(7),
        RuntimeMenuDevicesActionType_NetworkSettings           = RT_BIT(8),
        RuntimeMenuDevicesActionType_USBDevices                = RT_BIT(9),
        RuntimeMenuDevicesActionType_USBDevicesSettings        = RT_BIT(10),
        RuntimeMenuDevicesActionType_WebCams                   = RT_BIT(11),
        RuntimeMenuDevicesActionType_SharedClipboard           = RT_BIT(12),
        RuntimeMenuDevicesActionType_DragAndDrop               = RT_BIT(13),
        RuntimeMenuDevicesActionType_SharedFolders             = RT_BIT(14),
        RuntimeMenuDevicesActionType_SharedFoldersSettings     = RT_BIT(15),
        RuntimeMenuDevicesActionType_InsertGuestAdditionsDisk  = RT_BIT(16),
        RuntimeMenuDevicesActionType_UpgradeGuestAdditions     = RT_BIT(17),
        RuntimeMenuDevicesActionType_Nothing                   = RT_BIT(18),
        RuntimeMenuDevicesActionType_All                       = 0xFFFF
    };

#ifdef VBOX_WITH_DEBUGGER_GUI
    /** Runtime UI: Menu "Debugger": Action types. */
    enum RuntimeMenuDebuggerActionType
    {
        RuntimeMenuDebuggerActionType_Invalid              = 0,
        RuntimeMenuDebuggerActionType_Statistics           = RT_BIT(0),
        RuntimeMenuDebuggerActionType_CommandLine          = RT_BIT(1),
        RuntimeMenuDebuggerActionType_Logging              = RT_BIT(2),
        RuntimeMenuDebuggerActionType_GuestControlConsole  = RT_BIT(3),
        RuntimeMenuDebuggerActionType_All                  = 0xFFFF
    };
#endif /* VBOX_WITH_DEBUGGER_GUI */

#ifdef VBOX_WS_MAC
    /** Menu "Window": Action types. */
    enum MenuWindowActionType
    {
        MenuWindowActionType_Invalid  = 0,
        MenuWindowActionType_Minimize = RT_BIT(0),
        MenuWindowActionType_Switch   = RT_BIT(1),
        MenuWindowActionType_All      = 0xFFFF
    };
#endif /* VBOX_WS_MAC */


    /** VirtualBox Manager UI: Details element: "General" option types. */
    enum DetailsElementOptionTypeGeneral
    {
        DetailsElementOptionTypeGeneral_Invalid  = 0,
        DetailsElementOptionTypeGeneral_Name     = RT_BIT(0),
        DetailsElementOptionTypeGeneral_OS       = RT_BIT(1),
        DetailsElementOptionTypeGeneral_Location = RT_BIT(2),
        DetailsElementOptionTypeGeneral_Groups   = RT_BIT(3),
        DetailsElementOptionTypeGeneral_Default  = 0xFFFB
    };
    Q_ENUM(DetailsElementOptionTypeGeneral);

    /** VirtualBox Manager UI: Details element: "System" option types. */
    enum DetailsElementOptionTypeSystem
    {
        DetailsElementOptionTypeSystem_Invalid         = 0,
        DetailsElementOptionTypeSystem_RAM             = RT_BIT(0),
        DetailsElementOptionTypeSystem_CPUCount        = RT_BIT(1),
        DetailsElementOptionTypeSystem_CPUExecutionCap = RT_BIT(2),
        DetailsElementOptionTypeSystem_BootOrder       = RT_BIT(3),
        DetailsElementOptionTypeSystem_ChipsetType     = RT_BIT(4),
        DetailsElementOptionTypeSystem_TpmType         = RT_BIT(5),
        DetailsElementOptionTypeSystem_Firmware        = RT_BIT(6),
        DetailsElementOptionTypeSystem_SecureBoot      = RT_BIT(7),
        DetailsElementOptionTypeSystem_Acceleration    = RT_BIT(8),
        DetailsElementOptionTypeSystem_Default         = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeSystem);

    /** VirtualBox Manager UI: Details element: "Display" option types. */
    enum DetailsElementOptionTypeDisplay
    {
        DetailsElementOptionTypeDisplay_Invalid            = 0,
        DetailsElementOptionTypeDisplay_VRAM               = RT_BIT(0),
        DetailsElementOptionTypeDisplay_ScreenCount        = RT_BIT(1),
        DetailsElementOptionTypeDisplay_ScaleFactor        = RT_BIT(2),
        DetailsElementOptionTypeDisplay_GraphicsController = RT_BIT(3),
        DetailsElementOptionTypeDisplay_Acceleration       = RT_BIT(4),
        DetailsElementOptionTypeDisplay_VRDE               = RT_BIT(5),
        DetailsElementOptionTypeDisplay_Recording          = RT_BIT(6),
        DetailsElementOptionTypeDisplay_Default            = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeDisplay);

    /** VirtualBox Manager UI: Details element: "Storage" option types. */
    enum DetailsElementOptionTypeStorage
    {
        DetailsElementOptionTypeStorage_Invalid        = 0,
        DetailsElementOptionTypeStorage_HardDisks      = RT_BIT(0),
        DetailsElementOptionTypeStorage_OpticalDevices = RT_BIT(1),
        DetailsElementOptionTypeStorage_FloppyDevices  = RT_BIT(2),
        DetailsElementOptionTypeStorage_Default        = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeStorage);

    /** VirtualBox Manager UI: Details element: "Audio" option types. */
    enum DetailsElementOptionTypeAudio
    {
        DetailsElementOptionTypeAudio_Invalid    = 0,
        DetailsElementOptionTypeAudio_Driver     = RT_BIT(0),
        DetailsElementOptionTypeAudio_Controller = RT_BIT(1),
        DetailsElementOptionTypeAudio_IO         = RT_BIT(2),
        DetailsElementOptionTypeAudio_Default    = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeAudio);

    /** VirtualBox Manager UI: Details element: "Network" option types. */
    enum DetailsElementOptionTypeNetwork
    {
        DetailsElementOptionTypeNetwork_Invalid         = 0,
        DetailsElementOptionTypeNetwork_NotAttached     = RT_BIT(0),
        DetailsElementOptionTypeNetwork_NAT             = RT_BIT(1),
        DetailsElementOptionTypeNetwork_BridgedAdapter  = RT_BIT(2),
        DetailsElementOptionTypeNetwork_InternalNetwork = RT_BIT(3),
        DetailsElementOptionTypeNetwork_HostOnlyAdapter = RT_BIT(4),
        DetailsElementOptionTypeNetwork_GenericDriver   = RT_BIT(5),
        DetailsElementOptionTypeNetwork_NATNetwork      = RT_BIT(6),
#ifdef VBOX_WITH_CLOUD_NET
        DetailsElementOptionTypeNetwork_CloudNetwork    = RT_BIT(7),
#endif /* VBOX_WITH_CLOUD_NET */
#ifdef VBOX_WITH_VMNET
        DetailsElementOptionTypeNetwork_HostOnlyNetwork = RT_BIT(8),
#endif /* VBOX_WITH_VMNET */
        DetailsElementOptionTypeNetwork_Default         = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeNetwork);

    /** VirtualBox Manager UI: Details element: "Serial" option types. */
    enum DetailsElementOptionTypeSerial
    {
        DetailsElementOptionTypeSerial_Invalid      = 0,
        DetailsElementOptionTypeSerial_Disconnected = RT_BIT(0),
        DetailsElementOptionTypeSerial_HostPipe     = RT_BIT(1),
        DetailsElementOptionTypeSerial_HostDevice   = RT_BIT(2),
        DetailsElementOptionTypeSerial_RawFile      = RT_BIT(3),
        DetailsElementOptionTypeSerial_TCP          = RT_BIT(4),
        DetailsElementOptionTypeSerial_Default      = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeSerial);

    /** VirtualBox Manager UI: Details element: "USB" option types. */
    enum DetailsElementOptionTypeUsb
    {
        DetailsElementOptionTypeUsb_Invalid       = 0,
        DetailsElementOptionTypeUsb_Controller    = RT_BIT(0),
        DetailsElementOptionTypeUsb_DeviceFilters = RT_BIT(1),
        DetailsElementOptionTypeUsb_Default       = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeUsb);

    /** VirtualBox Manager UI: Details element: "SharedFolders" option types. */
    enum DetailsElementOptionTypeSharedFolders
    {
        DetailsElementOptionTypeSharedFolders_Invalid = 0,
        DetailsElementOptionTypeSharedFolders_Default = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeSharedFolders);

    /** VirtualBox Manager UI: Details element: "UserInterface" option types. */
    enum DetailsElementOptionTypeUserInterface
    {
        DetailsElementOptionTypeUserInterface_Invalid     = 0,
        DetailsElementOptionTypeUserInterface_VisualState = RT_BIT(0),
        DetailsElementOptionTypeUserInterface_MenuBar     = RT_BIT(1),
        DetailsElementOptionTypeUserInterface_StatusBar   = RT_BIT(2),
        DetailsElementOptionTypeUserInterface_MiniToolbar = RT_BIT(3),
        DetailsElementOptionTypeUserInterface_Default     = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeUserInterface);

    /** VirtualBox Manager UI: Details element: "Description" option types. */
    enum DetailsElementOptionTypeDescription
    {
        DetailsElementOptionTypeDescription_Invalid = 0,
        DetailsElementOptionTypeDescription_Default = 0xFFFF
    };
    Q_ENUM(DetailsElementOptionTypeDescription);
};


/** Common UI: GUI feature types. */
enum GUIFeatureType
{
    GUIFeatureType_None           = 0,
    GUIFeatureType_NoSelector     = RT_BIT(0),
#ifdef VBOX_WS_MAC
    GUIFeatureType_NoUserElements = RT_BIT(1),
#else
    GUIFeatureType_NoMenuBar      = RT_BIT(1),
#endif
    GUIFeatureType_NoStatusBar    = RT_BIT(2),
    GUIFeatureType_All            = 0xFF
};


/** Common UI: Global settings page types. */
enum GlobalSettingsPageType
{
    GlobalSettingsPageType_Invalid,
    GlobalSettingsPageType_General,
    GlobalSettingsPageType_Input,
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    GlobalSettingsPageType_Update,
#endif
    GlobalSettingsPageType_Language,
    GlobalSettingsPageType_Display,
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    GlobalSettingsPageType_Proxy,
#endif
    GlobalSettingsPageType_Interface,
    GlobalSettingsPageType_Max
};
Q_DECLARE_METATYPE(GlobalSettingsPageType);


/** Common UI: Machine settings page types. */
enum MachineSettingsPageType
{
    MachineSettingsPageType_Invalid,
    MachineSettingsPageType_General,
    MachineSettingsPageType_System,
    MachineSettingsPageType_Display,
    MachineSettingsPageType_Storage,
    MachineSettingsPageType_Audio,
    MachineSettingsPageType_Network,
    MachineSettingsPageType_Serial,
    MachineSettingsPageType_USB,
    MachineSettingsPageType_SF,
    MachineSettingsPageType_Interface,
    MachineSettingsPageType_Max
};
Q_DECLARE_METATYPE(MachineSettingsPageType);


/** Common UI: Shared Folder types. */
enum UISharedFolderType
{
    UISharedFolderType_Global,
    UISharedFolderType_Machine,
    UISharedFolderType_Console
};


/** Remote mode types. */
enum UIRemoteMode
{
    UIRemoteMode_Any,
    UIRemoteMode_On,
    UIRemoteMode_Off
};
Q_DECLARE_METATYPE(UIRemoteMode);


/** Common UI: Wizard types. */
enum WizardType
{
    WizardType_Invalid,
    WizardType_NewVM,
    WizardType_CloneVM,
    WizardType_ExportAppliance,
    WizardType_ImportAppliance,
    WizardType_NewCloudVM,
    WizardType_AddCloudVM,
    WizardType_NewVD,
    WizardType_CloneVD
};


/** Common UI: Wizard modes. */
enum WizardMode
{
    WizardMode_Basic,
    WizardMode_Expert
};


/** Common UI: Color Theme types. */
enum UIColorThemeType
{
    UIColorThemeType_Auto,
    UIColorThemeType_Light,
    UIColorThemeType_Dark,
};
Q_DECLARE_METATYPE(UIColorThemeType);


/** Tool item classes. */
enum UIToolClass
{
    UIToolClass_Invalid,
    UIToolClass_Aux,
    UIToolClass_Global,
    UIToolClass_Machine
};


/** Tool item types. */
enum UIToolType
{
    UIToolType_Invalid,
    /* Aux types: */
    UIToolType_Toggle,
    /* Global types: */
    UIToolType_Home,
    UIToolType_Machines,
    UIToolType_Extensions,
    UIToolType_Media,
    UIToolType_Network,
    UIToolType_Cloud,
    UIToolType_CloudConsole,
    UIToolType_Resources,
    /* Machine types: */
    UIToolType_Error,
    UIToolType_Details,
    UIToolType_Snapshots,
    UIToolType_Logs,
    UIToolType_VMActivity,
    UIToolType_FileManager
};
Q_DECLARE_METATYPE(UIToolType);


/** Contains stuff related to tools handling. */
namespace UIToolStuff
{
    /** Returns @a enmClass for passed @a enmType. */
    SHARED_LIBRARY_STUFF UIToolClass castTypeToClass(UIToolType enmType);
    /** Returns whether passed @a enmType is of passed @a enmClass. */
    SHARED_LIBRARY_STUFF bool isTypeOfClass(UIToolType enmType, UIToolClass enmClass);
}


/** Selector UI: Details-element types. */
enum DetailsElementType
{
    DetailsElementType_Invalid,
    DetailsElementType_General,
    DetailsElementType_System,
    DetailsElementType_Preview,
    DetailsElementType_Display,
    DetailsElementType_Storage,
    DetailsElementType_Audio,
    DetailsElementType_Network,
    DetailsElementType_Serial,
    DetailsElementType_USB,
    DetailsElementType_SF,
    DetailsElementType_UI,
    DetailsElementType_Description,
    DetailsElementType_Max
};
Q_DECLARE_METATYPE(DetailsElementType);


/** Selector UI: Preview update interval types. */
enum PreviewUpdateIntervalType
{
    PreviewUpdateIntervalType_Disabled,
    PreviewUpdateIntervalType_500ms,
    PreviewUpdateIntervalType_1000ms,
    PreviewUpdateIntervalType_2000ms,
    PreviewUpdateIntervalType_5000ms,
    PreviewUpdateIntervalType_10000ms,
    PreviewUpdateIntervalType_Max
};


/** Selector UI: Disk encryption cipher types. */
enum UIDiskEncryptionCipherType
{
    UIDiskEncryptionCipherType_Unchanged,
    UIDiskEncryptionCipherType_XTS256,
    UIDiskEncryptionCipherType_XTS128,
    UIDiskEncryptionCipherType_Max
};
Q_DECLARE_METATYPE(UIDiskEncryptionCipherType);


/** Runtime UI: Visual-state types. */
enum UIVisualStateType
{
    UIVisualStateType_Invalid    = 0,
    UIVisualStateType_Normal     = RT_BIT(0),
    UIVisualStateType_Fullscreen = RT_BIT(1),
    UIVisualStateType_Seamless   = RT_BIT(2),
    UIVisualStateType_Scale      = RT_BIT(3),
    UIVisualStateType_All        = 0xFF
};
Q_DECLARE_METATYPE(UIVisualStateType);


/** Runtime UI: Indicator types. */
enum IndicatorType
{
    IndicatorType_Invalid,
    IndicatorType_HardDisks,
    IndicatorType_OpticalDisks,
    IndicatorType_FloppyDisks,
    IndicatorType_Audio,
    IndicatorType_Network,
    IndicatorType_USB,
    IndicatorType_SharedFolders,
    IndicatorType_Display,
    IndicatorType_Recording,
    IndicatorType_Features,
    IndicatorType_Mouse,
    IndicatorType_Keyboard,
    IndicatorType_KeyboardExtension,
    IndicatorType_Max
};
Q_DECLARE_METATYPE(IndicatorType);


/** Runtime UI: Machine close actions. */
enum MachineCloseAction
{
    MachineCloseAction_Invalid                    = 0,
    MachineCloseAction_Detach                     = RT_BIT(0),
    MachineCloseAction_SaveState                  = RT_BIT(1),
    MachineCloseAction_Shutdown                   = RT_BIT(2),
    MachineCloseAction_PowerOff                   = RT_BIT(3),
    MachineCloseAction_PowerOff_RestoringSnapshot = RT_BIT(4),
    MachineCloseAction_All                        = 0xFF
};
Q_DECLARE_METATYPE(MachineCloseAction);


/** Runtime UI: Mouse capture policy types. */
enum MouseCapturePolicy
{
    MouseCapturePolicy_Default,
    MouseCapturePolicy_HostComboOnly,
    MouseCapturePolicy_Disabled
};


/** Runtime UI: Guru Meditation handler types. */
enum GuruMeditationHandlerType
{
    GuruMeditationHandlerType_Default,
    GuruMeditationHandlerType_PowerOff,
    GuruMeditationHandlerType_Ignore
};


/** Runtime UI: Scaling optimization types. */
enum ScalingOptimizationType
{
    ScalingOptimizationType_None,
    ScalingOptimizationType_Performance
};


#ifndef VBOX_WS_MAC
/** Runtime UI: Mini-toolbar alignment. */
enum MiniToolbarAlignment
{
    MiniToolbarAlignment_Disabled,
    MiniToolbarAlignment_Bottom,
    MiniToolbarAlignment_Top
};
#endif /* !VBOX_WS_MAC */


/** Runtime UI: Information-element types. */
enum InformationElementType
{
    InformationElementType_Invalid,
    InformationElementType_General,
    InformationElementType_System,
    InformationElementType_Preview,
    InformationElementType_Display,
    InformationElementType_Storage,
    InformationElementType_Audio,
    InformationElementType_Network,
    InformationElementType_Serial,
    InformationElementType_USB,
    InformationElementType_SharedFolders,
    InformationElementType_UI,
    InformationElementType_Description,
    InformationElementType_RuntimeAttributes,
    InformationElementType_StorageStatistics,
    InformationElementType_NetworkStatistics
};
Q_DECLARE_METATYPE(InformationElementType);


/** Runtime UI: Maximum guest-screen size policy types.
  * @note This policy determines which guest-screen sizes we wish to
  *       handle. We also accept anything smaller than the current size. */
enum MaximumGuestScreenSizePolicy
{
    /** Anything at all. */
    MaximumGuestScreenSizePolicy_Any,
    /** Anything up to a fixed size. */
    MaximumGuestScreenSizePolicy_Fixed,
    /** Anything up to host-screen available space. */
    MaximumGuestScreenSizePolicy_Automatic
};
Q_DECLARE_METATYPE(MaximumGuestScreenSizePolicy);


/** Manager UI: VM Activity Overview Column types.
  * @note The first element must be 0 and the rest must be consecutive */
enum VMActivityOverviewColumn
{
    VMActivityOverviewColumn_Name = 0,
    VMActivityOverviewColumn_CPUGuestLoad,
    VMActivityOverviewColumn_CPUVMMLoad,
    VMActivityOverviewColumn_RAMUsedAndTotal,
    VMActivityOverviewColumn_RAMUsedPercentage,
    VMActivityOverviewColumn_NetworkUpRate,
    VMActivityOverviewColumn_NetworkDownRate,
    VMActivityOverviewColumn_NetworkUpTotal,
    VMActivityOverviewColumn_NetworkDownTotal,
    VMActivityOverviewColumn_DiskIOReadRate,
    VMActivityOverviewColumn_DiskIOWriteRate,
    VMActivityOverviewColumn_DiskIOReadTotal,
    VMActivityOverviewColumn_DiskIOWriteTotal,
    VMActivityOverviewColumn_VMExits,
    VMActivityOverviewColumn_Max
};

#endif /* !FEQT_INCLUDED_SRC_extradata_UIExtraDataDefs_h */
