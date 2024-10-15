/* $Id: UIExtraDataManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIExtraDataManager class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extradata_UIExtraDataManager_h
#define FEQT_INCLUDED_SRC_extradata_UIExtraDataManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QUuid>
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
# include <QPointer>
#endif

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* Forward declarations: */
class UIExtraDataEventHandler;
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
class UIExtraDataManagerWindow;
#endif

/** Defines the map of extra data values. The index is an extra-data key. */
typedef QMap<QString, QString> ExtraDataMap;
/** Defines the map of extra data maps. */
typedef QMap<QUuid, ExtraDataMap> MapOfExtraDataMaps;

/** Singleton QObject extension
  * providing GUI with corresponding extra-data values,
  * and notifying it whenever any of those values changed. */
class SHARED_LIBRARY_STUFF UIExtraDataManager : public QObject
{
    Q_OBJECT;

    /** Constructs Extra-data Manager. */
    UIExtraDataManager();
    /** Destructs Extra-data Manager. */
    virtual ~UIExtraDataManager();

signals:

    /** Notifies about extra-data map acknowledging. */
    void sigExtraDataMapAcknowledging(const QUuid &uID);

    /** Notifies about extra-data change. */
    void sigExtraDataChange(const QUuid &uID, const QString &strKey, const QString &strValue);

    /** Notifies about notification-center alignment change. */
    void sigNotificationCenterAlignmentChange();
    /** Notifies about notification-center order change. */
    void sigNotificationCenterOrderChange();

    /** Notifies about settings expert mode change. */
    void sigSettingsExpertModeChange();

    /** Notifies about GUI language change. */
    void sigLanguageChange(QString strLanguage);

    /** Notifies about Selector UI keyboard shortcut change. */
    void sigSelectorUIShortcutChange();
    /** Notifies about Runtime UI keyboard shortcut change. */
    void sigRuntimeUIShortcutChange();
    /** Notifies about Runtime UI host-key combination change. */
    void sigRuntimeUIHostKeyCombinationChange();

    /** Notifies about Cloud Profile Manager restriction change. */
    void sigCloudProfileManagerRestrictionChange();

    /** Notifies about Cloud Console Manager data change. */
    void sigCloudConsoleManagerDataChange();
    /** Notifies about Cloud Console Manager restriction change. */
    void sigCloudConsoleManagerRestrictionChange();

    /** Notifies about VirtualBox Manager / Details pane categories change. */
    void sigDetailsCategoriesChange();
    /** Notifies about VirtualBox Manager / Details pane options change. */
    void sigDetailsOptionsChange(DetailsElementType enmType);

    /** Notifies about visual state change. */
    void sigVisualStateChange(const QUuid &uMachineID);

    /** Notifies about menu-bar configuration change. */
    void sigMenuBarConfigurationChange(const QUuid &uMachineID);
    /** Notifies about status-bar configuration change. */
    void sigStatusBarConfigurationChange(const QUuid &uMachineID);

    /** Notifies about HID LEDs synchronization state change. */
    void sigHidLedsSyncStateChange(bool fEnabled);

    /** Notifies about the scale-factor change. */
    void sigScaleFactorChange(const QUuid &uMachineID);

    /** Notifies about the scaling optimization type change. */
    void sigScalingOptimizationTypeChange(const QUuid &uMachineID);

    /** Notifies about font scale factor. */
    void sigFontScaleFactorChanged(int iFontScaleFactor);

#ifdef VBOX_WS_MAC
    /** Notifies about the HiDPI optimization type change. */
    void sigHiDPIOptimizationTypeChange(const QUuid &uMachineID);

    /** Mac OS X: Notifies about 'dock icon' appearance change. */
    void sigDockIconAppearanceChange(bool fEnabled);
    /** Mac OS X: Notifies about 'dock icon overlay' appearance change. */
    void sigDockIconOverlayAppearanceChange(bool fEnabled);
#endif /* VBOX_WS_MAC */

#if defined (VBOX_WS_NIX) || defined (VBOX_WS_WIN)
    /* Is emitted when host screen saver inhibition state changes. */
    void sigDisableHostScreenSaverStateChange(bool fDisable);
#endif

public:

    /** Global extra-data ID. */
    static const QUuid GlobalID;

    /** Singleton object contructor. */
    static void create();
    /** Singleton object destructor. */
    static void destroy();
    /** Static Extra-data Manager instance/constructor. */
    static UIExtraDataManager* instance();

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    /** Static show and raise API. */
    static void openWindow(QWidget *pCenterWidget);
#endif

    /** @name Base
      * @{ */
        /** Returns whether Extra-data Manager cached the map with passed @a uID. */
        bool contains(const QUuid &uID) const { return m_data.contains(uID); }
        /** Returns read-only extra-data map for passed @a uID. */
        const ExtraDataMap map(const QUuid &uID) const { return m_data.value(uID); }

        /** Hot-load machine extra-data map. */
        void hotloadMachineExtraDataMap(const QUuid &uID);

        /** Returns extra-data value corresponding to passed @a strKey as QString.
          * If valid @a uID is set => applies to machine extra-data, otherwise => to global one. */
        QString extraDataString(const QString &strKey, const QUuid &uID = GlobalID);
        /** Defines extra-data value corresponding to passed @a strKey as strValue.
          * If valid @a uID is set => applies to machine extra-data, otherwise => to global one. */
        void setExtraDataString(const QString &strKey, const QString &strValue, const QUuid &uID = GlobalID);

        /** Returns extra-data value corresponding to passed @a strKey as QStringList.
          * If valid @a uID is set => applies to machine extra-data, otherwise => to global one. */
        QStringList extraDataStringList(const QString &strKey, const QUuid &uID = GlobalID);
        /** Defines extra-data value corresponding to passed @a strKey as value.
          * If valid @a uID is set => applies to machine extra-data, otherwise => to global one. */
        void setExtraDataStringList(const QString &strKey, const QStringList &value, const QUuid &uID = GlobalID);
    /** @} */

    /** @name General
      * @{ */
        /** Returns a list of restricted dialogs. */
        UIExtraDataMetaDefs::DialogType restrictedDialogTypes(const QUuid &uID);
        /** Defines a list of restricted dialogs. */
        void setRestrictedDialogTypes(UIExtraDataMetaDefs::DialogType enmTypes, const QUuid &uID);

        /** Returns color theme type. */
        UIColorThemeType colorTheme();
        /** Defines color theme @a enmType. */
        void setColorTheme(const UIColorThemeType &enmType);
    /** @} */

    /** @name Messaging
      * @{ */
        /** Returns the list of suppressed messages for the Message/Popup center frameworks. */
        QStringList suppressedMessages(const QUuid &uID = GlobalID);
        /** Defines the @a list of suppressed messages for the Message/Popup center frameworks. */
        void setSuppressedMessages(const QStringList &list);

        /** Returns the list of messages for the Message/Popup center frameworks with inverted check-box state. */
        QStringList messagesWithInvertedOption();

#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
        /** Returns whether successfull notification-progresses should NOT close automatically. */
        bool keepSuccessfullNotificationProgresses();
        /** Defines whether successfull notification-progresses should NOT close (@a fKeep) automatically. */
        void setKeepSuccessfullNotificationProgresses(bool fKeep);
#endif /* VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON */

        /** Returns notification-center alignment. */
        Qt::Alignment notificationCenterAlignment();
        /** Defines notification-progresses @a enmOrder. */
        void setNotificationCenterAlignment(Qt::Alignment enmOrder);

        /** Returns notification-center order. */
        Qt::SortOrder notificationCenterOrder();
        /** Defines notification-progresses @a enmOrder. */
        void setNotificationCenterOrder(Qt::SortOrder enmOrder);

        /** Returns whether BETA build label should be hidden. */
        bool preventBetaBuildLabel();
#if !defined(VBOX_BLEEDING_EDGE) && !defined(DEBUG)
        /** Returns version for which user wants to prevent BETA build warning. */
        QString preventBetaBuildWarningForVersion();
#endif
    /** @} */

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    /** @name Application Update
      * @{ */
        /** Returns whether Application Update functionality enabled. */
        bool applicationUpdateEnabled();

        /** Returns Application Update data. */
        QString applicationUpdateData();
        /** Defines Application Update data as @a strValue. */
        void setApplicationUpdateData(const QString &strValue);

        /** Returns Application Update check counter. */
        qulonglong applicationUpdateCheckCounter();
        /** Increments Application Update check counter. */
        void incrementApplicationUpdateCheckCounter();
    /** @} */
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */

    /** @name Progress
      * @{ */
        /** Returns whether legacy progress handling method is requested. */
        bool legacyProgressHandlingRequested();
    /** @} */

    /** @name Settings
      * @{ */
        /** Returns whether GUI @a enmFeature is enabled. */
        bool guiFeatureEnabled(GUIFeatureType enmFeature);

        /** Returns restricted global settings pages. */
        QList<GlobalSettingsPageType> restrictedGlobalSettingsPages();
        /** Returns restricted machine settings pages. */
        QList<MachineSettingsPageType> restrictedMachineSettingsPages(const QUuid &uID);

        /** Returns whether settings are in expert mode. */
        bool isSettingsInExpertMode();
        /** Defines whether settings are in @a fExpertMode. */
        void setSettingsInExpertMode(bool fExpertMode);
    /** @} */

    /** @name Settings: Language
      * @{ */
        /** Returns the GUI language ID. */
        QString languageId();
        /** Defines the GUI @a strLanguageId. */
        void setLanguageId(const QString &strLanguageId);
    /** @} */

    /** @name Settings: Display
      * @{ */
        /** Returns maximum guest-screen resolution policy. */
        MaximumGuestScreenSizePolicy maxGuestResolutionPolicy();
        /** Defines maximum guest-screen resolution @a enmPolicy or @a resolution itself for Fixed policy. */
        void setMaxGuestScreenResolution(MaximumGuestScreenSizePolicy enmPolicy, const QSize resolution = QSize());
        /** Returns maximum guest-screen resolution for fixed policy. */
        QSize maxGuestResolutionForPolicyFixed();
        /** Defines maximum guest-screen @a resolution for fixed policy. */
        void setMaxGuestResolutionForPolicyFixed(const QSize &resolution);

        /** Returns whether hovered machine-window should be activated. */
        bool activateHoveredMachineWindow();
        /** Defines whether hovered machine-window should be @a fActivated. */
        void setActivateHoveredMachineWindow(bool fActivate);
        /* Return whether host screen saver is disabled when a vm is running. */
        bool disableHostScreenSaver();
        /* Sets whether host screen saver is disabled when a vm is running. */
        void setDisableHostScreenSaver(bool fActivate);
        /* Set global font scale factor as percentage. 100% is for no scaling. */
        void setFontScaleFactor(int iFontScaleFactor);
        int  fontScaleFactor();
    /** @} */

    /** @name Settings: Keyboard
      * @{ */
        /** Returns the Runtime UI host-key combination. */
        QString hostKeyCombination();
        /** Defines the Runtime UI host-key combination. */
        void setHostKeyCombination(const QString &strHostCombo);

        /** Returns shortcut overrides for shortcut-pool with @a strPoolExtraDataID. */
        QStringList shortcutOverrides(const QString &strPoolExtraDataID);

        /** Returns whether the Runtime UI auto-capture is enabled. */
        bool autoCaptureEnabled();
        /** Defines whether the Runtime UI auto-capture is @a fEnabled. */
        void setAutoCaptureEnabled(bool fEnabled);

        /** Returns the Runtime UI remapped scan codes. */
        QString remappedScanCodes();
    /** @} */

    /** @name Settings: Proxy
      * @{ */
        /** Returns VBox proxy settings. */
        QString proxySettings();
        /** Defines VBox proxy @a strSettings. */
        void setProxySettings(const QString &strSettings);
    /** @} */

    /** @name Settings: Storage
      * @{ */
        /** Returns recent folder for hard-drives. */
        QString recentFolderForHardDrives();
        /** Returns recent folder for optical-disks. */
        QString recentFolderForOpticalDisks();
        /** Returns recent folder for floppy-disks. */
        QString recentFolderForFloppyDisks();
        /** Defines recent folder for hard-drives as @a strValue. */
        void setRecentFolderForHardDrives(const QString &strValue);
        /** Defines recent folder for optical-disk as @a strValue. */
        void setRecentFolderForOpticalDisks(const QString &strValue);
        /** Defines recent folder for floppy-disk as @a strValue. */
        void setRecentFolderForFloppyDisks(const QString &strValue);

        /** Returns the list of recently used hard-drives. */
        QStringList recentListOfHardDrives();
        /** Returns the list of recently used optical-disk. */
        QStringList recentListOfOpticalDisks();
        /** Returns the list of recently used floppy-disk. */
        QStringList recentListOfFloppyDisks();
        /** Defines the list of recently used hard-drives as @a value. */
        void setRecentListOfHardDrives(const QStringList &value);
        /** Defines the list of recently used optical-disks as @a value. */
        void setRecentListOfOpticalDisks(const QStringList &value);
        /** Defines the list of recently used floppy-disks as @a value. */
        void setRecentListOfFloppyDisks(const QStringList &value);
    /** @} */

    /** @name Settings: Network
      * @{ */
        /** Returns the list of restricted network attachment types. */
        UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork restrictedNetworkAttachmentTypes();
    /** @} */

    /** @name VISO Creator
      * @{ */
        /** Returns recent folder for VISO creation content. */
        QString visoCreatorRecentFolder();
        /** Defines recent folder for VISO creation content as @a strValue. */
        void setVISOCreatorRecentFolder(const QString &strValue);
        /** Returns viso creator geometry using @a pWidget as the hint. */
        QRect visoCreatorDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry);
        /** Set viso creator geometry. */
        void setVisoCreatorDialogGeometry(const QRect &geometry, bool fMaximized);
        /** Returns whether viso creator dialog should be maximized. */
        bool visoCreatorDialogShouldBeMaximized();
    /** @} */

    /** @name VirtualBox Manager
      * @{ */
        /** Returns selector-window geometry using @a pWidget as the hint. */
        QRect selectorWindowGeometry(QWidget *pWidget);
        /** Returns whether selector-window should be maximized. */
        bool selectorWindowShouldBeMaximized();
        /** Defines selector-window @a geometry and @a fMaximized state. */
        void setSelectorWindowGeometry(const QRect &geometry, bool fMaximized);

        /** Returns selector-window splitter hints. */
        QList<int> selectorWindowSplitterHints();
        /** Defines selector-window splitter @a hints. */
        void setSelectorWindowSplitterHints(const QList<int> &hints);

        /** Returns whether selector-window tool-bar visible. */
        bool selectorWindowToolBarVisible();
        /** Defines whether selector-window tool-bar @a fVisible. */
        void setSelectorWindowToolBarVisible(bool fVisible);

        /** Returns whether selector-window tool-bar text visible. */
        bool selectorWindowToolBarTextVisible();
        /** Defines whether selector-window tool-bar text @a fVisible. */
        void setSelectorWindowToolBarTextVisible(bool fVisible);

        /** Returns last selected tool set of VirtualBox Manager. */
        QList<UIToolType> toolsPaneLastItemsChosen();
        /** Defines last selected tool @a set of VirtualBox Manager. */
        void setToolsPaneLastItemsChosen(const QList<UIToolType> &set);
        /** Returns the list of detached tools of VirtualBox Manager. */
        QList<UIToolType> detachedTools();
        /** Defines the list of detached @a tools of VirtualBox Manager. */
        void setDetachedTools(const QList<UIToolType> &tools);

        /** Returns whether selector-window status-bar visible. */
        bool selectorWindowStatusBarVisible();
        /** Defines whether selector-window status-bar @a fVisible. */
        void setSelectorWindowStatusBarVisible(bool fVisible);

        /** Returns all the existing selector-window chooser-pane' group definition keys. */
        QStringList knownMachineGroupDefinitionKeys();
        /** Returns selector-window chooser-pane' group definitions for passed @a strGroupID. */
        QStringList machineGroupDefinitions(const QString &strGroupID);
        /** Defines selector-window chooser-pane' group @a definitions for passed @a strGroupID. */
        void setMachineGroupDefinitions(const QString &strGroupID, const QStringList &definitions);

        /** Returns last-item ID of the item chosen in selector-window chooser-pane. */
        QString selectorWindowLastItemChosen();
        /** Defines @a lastItemID of the item chosen in selector-window chooser-pane. */
        void setSelectorWindowLastItemChosen(const QString &strItemID);

        /** Returns selector-window details-pane' elements. */
        QMap<DetailsElementType, bool> selectorWindowDetailsElements();
        /** Defines selector-window details-pane' @a elements. */
        void setSelectorWindowDetailsElements(const QMap<DetailsElementType, bool> &elements);

        /** Returns selector-window details-pane' preview update interval. */
        PreviewUpdateIntervalType selectorWindowPreviewUpdateInterval();
        /** Defines selector-window details-pane' preview update @a interval. */
        void setSelectorWindowPreviewUpdateInterval(PreviewUpdateIntervalType interval);

        /** Returns VirtualBox Manager / Details pane options for certain @a enmElementType. */
        QStringList vboxManagerDetailsPaneElementOptions(DetailsElementType enmElementType);
        /** Defines VirtualBox Manager / Details pane @a options for certain @a enmElementType. */
        void setVBoxManagerDetailsPaneElementOptions(DetailsElementType enmElementType, const QStringList &options);
    /** @} */

    /** @name Snapshot Manager
      * @{ */
        /** Returns whether Snapshot Manager details expanded. */
        bool snapshotManagerDetailsExpanded();
        /** Defines whether Snapshot Manager details @a fExpanded. */
        void setSnapshotManagerDetailsExpanded(bool fExpanded);
    /** @} */

    /** @name Virtual Media Manager
      * @{ */
        /** Returns whether Virtual Media Manager details expanded. */
        bool virtualMediaManagerDetailsExpanded();
        /** Defines whether Virtual Media Manager details @a fExpanded. */
        void setVirtualMediaManagerDetailsExpanded(bool fExpanded);
        /** Returns whether Virtual Media Manager search widget expanded. */
        bool virtualMediaManagerSearchWidgetExpanded();
        /** Defines whether Virtual Media Manager search widget @a fExpanded. */
        void setVirtualMediaManagerSearchWidgetExpanded(bool fExpanded);
    /** @} */

    /** @name Host Network Manager
      * @{ */
        /** Returns whether Host Network Manager details expanded. */
        bool hostNetworkManagerDetailsExpanded();
        /** Defines whether Host Network Manager details @a fExpanded. */
        void setHostNetworkManagerDetailsExpanded(bool fExpanded);
    /** @} */

    /** @name Cloud Profile Manager
      * @{ */
        /** Returns Cloud Profile Manager restrictions. */
        QStringList cloudProfileManagerRestrictions();
        /** Defines Cloud Profile Manager @a restrictions. */
        void setCloudProfileManagerRestrictions(const QStringList &restrictions);

        /** Returns whether Cloud Profile Manager details expanded. */
        bool cloudProfileManagerDetailsExpanded();
        /** Defines whether Cloud Profile Manager details @a fExpanded. */
        void setCloudProfileManagerDetailsExpanded(bool fExpanded);
    /** @} */

    /** @name Cloud Console Manager
      * @{ */
        /** Returns registered Cloud Console Manager applications. */
        QStringList cloudConsoleManagerApplications();
        /** Returns registered Cloud Console Manager profiles for application with @a strId. */
        QStringList cloudConsoleManagerProfiles(const QString &strId);

        /** Returns definition for Cloud Console Manager application with @a strId. */
        QString cloudConsoleManagerApplication(const QString &strId);
        /** Defines @a strDefinition for Cloud Console Manager application with @a strId. */
        void setCloudConsoleManagerApplication(const QString &strId, const QString &strDefinition);

        /** Returns definition for Cloud Console Manager profile with @a strProfileId for application with @a strApplicationId. */
        QString cloudConsoleManagerProfile(const QString &strApplicationId, const QString &strProfileId);
        /** Returns @a strDefinition for Cloud Console Manager profile with @a strProfileId for application with @a strApplicationId. */
        void setCloudConsoleManagerProfile(const QString &strApplicationId, const QString &strProfileId, const QString &strDefinition);

        /** Returns Cloud Console Manager restrictions. */
        QStringList cloudConsoleManagerRestrictions();
        /** Defines Cloud Console Manager @a restrictions. */
        void setCloudConsoleManagerRestrictions(const QStringList &restrictions);

        /** Returns whether Cloud Console Manager details expanded. */
        bool cloudConsoleManagerDetailsExpanded();
        /** Defines whether Cloud Console Manager details @a fExpanded. */
        void setCloudConsoleManagerDetailsExpanded(bool fExpanded);
    /** @} */

    /** @name Cloud Console
      * @{ */
        /** Returns Cloud Console public key path. */
        QString cloudConsolePublicKeyPath();
        /** Defines Cloud Console public key @a strPath. */
        void setCloudConsolePublicKeyPath(const QString &strPath);
    /** @} */

    /** @name Virtual Machine
      * @{ */
        /** Returns whether machine should be shown in VirtualBox Manager Chooser-pane. */
        bool showMachineInVirtualBoxManagerChooser(const QUuid &uID);
        /** Returns whether machine should be shown in VirtualBox Manager Details-pane. */
        bool showMachineInVirtualBoxManagerDetails(const QUuid &uID);

        /** Returns whether machine reconfiguration enabled. */
        bool machineReconfigurationEnabled(const QUuid &uID);
        /** Returns whether machine snapshot operations enabled. */
        bool machineSnapshotOperationsEnabled(const QUuid &uID);

        /** Except Mac OS X: Returns redefined machine-window icon names. */
        QStringList machineWindowIconNames(const QUuid &uID);
#ifndef VBOX_WS_MAC
        /** Except Mac OS X: Returns redefined machine-window name postfix. */
        QString machineWindowNamePostfix(const QUuid &uID);
#endif

        /** Returns geometry for machine-window with @a uScreenIndex in @a visualStateType. */
        QRect machineWindowGeometry(UIVisualStateType visualStateType, ulong uScreenIndex, const QUuid &uID);
        /** Returns whether machine-window with @a uScreenIndex in @a visualStateType should be maximized. */
        bool machineWindowShouldBeMaximized(UIVisualStateType visualStateType, ulong uScreenIndex, const QUuid &uID);
        /** Defines @a geometry and @a fMaximized state for machine-window with @a uScreenIndex in @a visualStateType. */
        void setMachineWindowGeometry(UIVisualStateType visualStateType, ulong uScreenIndex, const QRect &geometry, bool fMaximized, const QUuid &uID);

#ifndef VBOX_WS_MAC
        /** Returns whether Runtime UI menu-bar is enabled. */
        bool menuBarEnabled(const QUuid &uID);
        /** Defines whether Runtime UI menu-bar is @a fEnabled. */
        void setMenuBarEnabled(bool fEnabled, const QUuid &uID);
#endif /* !VBOX_WS_MAC */

        /** Returns whether Runtime UI menu-bar context-menu is enabled. */
        bool menuBarContextMenuEnabled(const QUuid &uID);
        /** Defines whether Runtime UI menu-bar context-menu is @a fEnabled. */
        void setMenuBarContextMenuEnabled(bool fEnabled, const QUuid &uID);

        /** Returns restricted Runtime UI menu types. */
        UIExtraDataMetaDefs::MenuType restrictedRuntimeMenuTypes(const QUuid &uID);
        /** Defines restricted Runtime UI menu types. */
        void setRestrictedRuntimeMenuTypes(UIExtraDataMetaDefs::MenuType types, const QUuid &uID);

        /** Returns restricted Runtime UI action types for Application menu. */
        UIExtraDataMetaDefs::MenuApplicationActionType restrictedRuntimeMenuApplicationActionTypes(const QUuid &uID);
        /** Defines restricted Runtime UI action types for Application menu. */
        void setRestrictedRuntimeMenuApplicationActionTypes(UIExtraDataMetaDefs::MenuApplicationActionType types, const QUuid &uID);

        /** Returns restricted Runtime UI action types for Machine menu. */
        UIExtraDataMetaDefs::RuntimeMenuMachineActionType restrictedRuntimeMenuMachineActionTypes(const QUuid &uID);
        /** Defines restricted Runtime UI action types for Machine menu. */
        void setRestrictedRuntimeMenuMachineActionTypes(UIExtraDataMetaDefs::RuntimeMenuMachineActionType types, const QUuid &uID);

        /** Returns restricted Runtime UI action types for View menu. */
        UIExtraDataMetaDefs::RuntimeMenuViewActionType restrictedRuntimeMenuViewActionTypes(const QUuid &uID);
        /** Defines restricted Runtime UI action types for View menu. */
        void setRestrictedRuntimeMenuViewActionTypes(UIExtraDataMetaDefs::RuntimeMenuViewActionType types, const QUuid &uID);

        /** Returns restricted Runtime UI action types for Input menu. */
        UIExtraDataMetaDefs::RuntimeMenuInputActionType restrictedRuntimeMenuInputActionTypes(const QUuid &uID);
        /** Defines restricted Runtime UI action types for Input menu. */
        void setRestrictedRuntimeMenuInputActionTypes(UIExtraDataMetaDefs::RuntimeMenuInputActionType types, const QUuid &uID);

        /** Returns restricted Runtime UI action types for Devices menu. */
        UIExtraDataMetaDefs::RuntimeMenuDevicesActionType restrictedRuntimeMenuDevicesActionTypes(const QUuid &uID);
        /** Defines restricted Runtime UI action types for Devices menu. */
        void setRestrictedRuntimeMenuDevicesActionTypes(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType types, const QUuid &uID);

#ifdef VBOX_WITH_DEBUGGER_GUI
        /** Returns restricted Runtime UI action types for Debugger menu. */
        UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType restrictedRuntimeMenuDebuggerActionTypes(const QUuid &uID);
        /** Defines restricted Runtime UI action types for Debugger menu. */
        void setRestrictedRuntimeMenuDebuggerActionTypes(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType types, const QUuid &uID);
#endif /* VBOX_WITH_DEBUGGER_GUI */

#ifdef VBOX_WS_MAC
        /** Mac OS X: Returns restricted Runtime UI action types for Window menu. */
        UIExtraDataMetaDefs::MenuWindowActionType restrictedRuntimeMenuWindowActionTypes(const QUuid &uID);
        /** Mac OS X: Defines restricted Runtime UI action types for Window menu. */
        void setRestrictedRuntimeMenuWindowActionTypes(UIExtraDataMetaDefs::MenuWindowActionType types, const QUuid &uID);
#endif /* VBOX_WS_MAC */

        /** Returns restricted Runtime UI action types for Help menu. */
        UIExtraDataMetaDefs::MenuHelpActionType restrictedRuntimeMenuHelpActionTypes(const QUuid &uID);
        /** Defines restricted Runtime UI action types for Help menu. */
        void setRestrictedRuntimeMenuHelpActionTypes(UIExtraDataMetaDefs::MenuHelpActionType types, const QUuid &uID);

        /** Returns restricted Runtime UI visual-states. */
        UIVisualStateType restrictedVisualStates(const QUuid &uID);

        /** Returns requested Runtime UI visual-state. */
        UIVisualStateType requestedVisualState(const QUuid &uID);
        /** Defines requested Runtime UI visual-state as @a visualState. */
        void setRequestedVisualState(UIVisualStateType visualState, const QUuid &uID);

#ifdef VBOX_WS_NIX
        /** Returns whether legacy full-screen mode is requested. */
        bool legacyFullscreenModeRequested();

        /** Returns whether internal machine-window name should be unique. */
        bool distinguishMachineWindowGroups(const QUuid &uID);
        /** Defines whether internal machine-window name should be unique. */
        void setDistinguishMachineWindowGroups(const QUuid &uID, bool fEnabled);
#endif /* VBOX_WS_NIX */

        /** Returns whether guest-screen auto-resize according machine-window size is enabled. */
        bool guestScreenAutoResizeEnabled(const QUuid &uID);
        /** Defines whether guest-screen auto-resize according machine-window size is @a fEnabled. */
        void setGuestScreenAutoResizeEnabled(bool fEnabled, const QUuid &uID);

        /** Returns last guest-screen visibility status for screen with @a uScreenIndex. */
        bool lastGuestScreenVisibilityStatus(ulong uScreenIndex, const QUuid &uID);
        /** Defines whether last guest-screen visibility status was @a fEnabled for screen with @a uScreenIndex. */
        void setLastGuestScreenVisibilityStatus(ulong uScreenIndex, bool fEnabled, const QUuid &uID);

        /** Returns last guest-screen size-hint for screen with @a uScreenIndex. */
        QSize lastGuestScreenSizeHint(ulong uScreenIndex, const QUuid &uID);
        /** Defines last guest-screen @a sizeHint for screen with @a uScreenIndex. */
        void setLastGuestScreenSizeHint(ulong uScreenIndex, const QSize &sizeHint, const QUuid &uID);

        /** Returns host-screen index corresponding to passed guest-screen @a iGuestScreenIndex. */
        int hostScreenForPassedGuestScreen(int iGuestScreenIndex, const QUuid &uID);
        /** Defines @a iHostScreenIndex corresponding to passed guest-screen @a iGuestScreenIndex. */
        void setHostScreenForPassedGuestScreen(int iGuestScreenIndex, int iHostScreenIndex, const QUuid &uID);

        /** Returns whether automatic mounting/unmounting of guest-screens enabled. */
        bool autoMountGuestScreensEnabled(const QUuid &uID);

#ifndef VBOX_WS_MAC
        /** Returns whether mini-toolbar is enabled for full and seamless screens. */
        bool miniToolbarEnabled(const QUuid &uID);
        /** Defines whether mini-toolbar is @a fEnabled for full and seamless screens. */
        void setMiniToolbarEnabled(bool fEnabled, const QUuid &uID);

        /** Returns whether mini-toolbar should auto-hide itself. */
        bool autoHideMiniToolbar(const QUuid &uID);
        /** Defines whether mini-toolbar should @a fAutoHide itself. */
        void setAutoHideMiniToolbar(bool fAutoHide, const QUuid &uID);

        /** Returns mini-toolbar alignment. */
        Qt::AlignmentFlag miniToolbarAlignment(const QUuid &uID);
        /** Returns mini-toolbar @a alignment. */
        void setMiniToolbarAlignment(Qt::AlignmentFlag alignment, const QUuid &uID);
#endif /* VBOX_WS_MAC */

        /** Returns whether Runtime UI status-bar is enabled. */
        bool statusBarEnabled(const QUuid &uID);
        /** Defines whether Runtime UI status-bar is @a fEnabled. */
        void setStatusBarEnabled(bool fEnabled, const QUuid &uID);

        /** Returns whether Runtime UI status-bar context-menu is enabled. */
        bool statusBarContextMenuEnabled(const QUuid &uID);
        /** Defines whether Runtime UI status-bar context-menu is @a fEnabled. */
        void setStatusBarContextMenuEnabled(bool fEnabled, const QUuid &uID);

        /** Returns restricted Runtime UI status-bar indicator list. */
        QList<IndicatorType> restrictedStatusBarIndicators(const QUuid &uID);
        /** Defines restricted Runtime UI status-bar indicator @a list. */
        void setRestrictedStatusBarIndicators(const QList<IndicatorType> &list, const QUuid &uID);

        /** Returns Runtime UI status-bar indicator order list. */
        QList<IndicatorType> statusBarIndicatorOrder(const QUuid &uID);
        /** Defines Runtime UI status-bar indicator order @a list. */
        void setStatusBarIndicatorOrder(const QList<IndicatorType> &list, const QUuid &uID);

#ifdef VBOX_WS_MAC
        /** Mac OS X: Returns whether Dock icon should be updated at runtime. */
        bool realtimeDockIconUpdateEnabled(const QUuid &uID);
        /** Mac OS X: Defines whether Dock icon update should be fEnabled at runtime. */
        void setRealtimeDockIconUpdateEnabled(bool fEnabled, const QUuid &uID);

        /** Mac OS X: Returns guest-screen which Dock icon should reflect at runtime. */
        int realtimeDockIconUpdateMonitor(const QUuid &uID);
        /** Mac OS X: Defines guest-screen @a iIndex which Dock icon should reflect at runtime. */
        void setRealtimeDockIconUpdateMonitor(int iIndex, const QUuid &uID);

        /** Mac OS X: Returns whether Dock icon overlay is disabled. */
        bool dockIconDisableOverlay(const QUuid &uID);
        /** Mac OS X: Defines whether Dock icon overlay is @a fDisabled. */
        void setDockIconDisableOverlay(bool fDisabled, const QUuid &uID);
#endif /* VBOX_WS_MAC */

        /** Returns whether machine should pass CAD to guest. */
        bool passCADtoGuest(const QUuid &uID);

        /** Returns the mouse-capture policy. */
        MouseCapturePolicy mouseCapturePolicy(const QUuid &uID);

        /** Returns redefined guru-meditation handler type. */
        GuruMeditationHandlerType guruMeditationHandlerType(const QUuid &uID);

        /** Returns whether machine should perform HID LEDs synchronization. */
        bool hidLedsSyncState(const QUuid &uID);

        /** Returns the scale-factor. */
        double scaleFactor(const QUuid &uID, const int uScreenIndex);
        QList<double> scaleFactors(const QUuid &uID);
        /** Saves the @a dScaleFactor for the monitor with @a uScreenIndex. If the existing scale factor
          * list (from extra data) does not have scale factors for the screens with ids in [0, uScreenIndex)
          * the this function appends a default scale factor for said screens.*/
        void setScaleFactor(double dScaleFactor, const QUuid &uID, const int uScreenIndex);
        /** Replaces the scale factor list of the machine with @a uID with @a scaleFactors. */
        void setScaleFactors(const QList<double> &scaleFactors, const QUuid &uID);

        /** Returns the scaling optimization type. */
        ScalingOptimizationType scalingOptimizationType(const QUuid &uID);
    /** @} */

    /** @name Virtual Machine: Session Information dialog
      * @{ */
        /** Returns session information dialog geometry using @a pWidget and @a pParentWidget as hints. */
        QRect sessionInformationDialogGeometry(QWidget *pWidget, QWidget *pParentWidget);
        /** Returns whether information-window should be maximized or not. */
        bool sessionInformationDialogShouldBeMaximized();
        /** Defines information-window @a geometry and @a fMaximized state. */
        void setSessionInformationDialogGeometry(const QRect &geometry, bool fMaximized);
    /** @} */

    /** @name Guest Control related dialogs
      * @{ */
        void setGuestControlProcessControlSplitterHints(const QList<int> &hints);
        QList<int> guestControlProcessControlSplitterHints();
        QRect fileManagerDialogGeometry(QWidget *pWidget, QWidget *pParentWidget);
        bool fileManagerDialogShouldBeMaximized();
        void setFileManagerDialogGeometry(const QRect &geometry, bool fMaximized);
        QRect guestProcessControlDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry);
        bool guestProcessControlDialogShouldBeMaximized();
        void setGuestProcessControlDialogGeometry(const QRect &geometry, bool fMaximized);
        void setFileManagerVisiblePanels(const QStringList &panelNameList);
        QStringList fileManagerVisiblePanels();
    /** @} */

    /** @name Soft Keyboard
      * @{ */
        QRect softKeyboardDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry);
        void setSoftKeyboardDialogGeometry(const QRect &geometry, bool fMaximized);
        bool softKeyboardDialogShouldBeMaximized();
        void setSoftKeyboardOptions(bool fShowNumPad, bool fHideOSMenuKeys, bool fMultimediaKeys);
        void softKeyboardOptions(bool &fOutShowNumPad, bool &fOutHideOSMenuKeys, bool &fOutHideMultimediaKeys);
        void setSoftKeyboardColorTheme(const QStringList &colorStringList);
        QStringList softKeyboardColorTheme();
        void setSoftKeyboardSelectedColorTheme(const QString &strColorThemeName);
        QString softKeyboardSelectedColorTheme();
        void setSoftKeyboardSelectedLayout(const QUuid &uLayoutUid);
        QUuid softKeyboardSelectedLayout();
    /** @} */

    /** @name File Manager options
      * @{ */
        void setFileManagerOptions(bool fListDirectoriesFirst,
                                   bool fShowDeleteConfirmation,
                                   bool fshowHumanReadableSizes,
                                   bool fShowHiddenObjects);
        bool fileManagerListDirectoriesFirst();
        bool fileManagerShowDeleteConfirmation();
        bool fileManagerShowHumanReadableSizes();
        bool fileManagerShowHiddenObjects();
    /** @} */

    /** @name Virtual Machine: Close dialog
      * @{ */
        /** Returns default machine close action. */
        MachineCloseAction defaultMachineCloseAction(const QUuid &uID);
        /** Returns restricted machine close actions. */
        MachineCloseAction restrictedMachineCloseActions(const QUuid &uID);

        /** Returns last machine close action. */
        MachineCloseAction lastMachineCloseAction(const QUuid &uID);
        /** Defines last @a machineCloseAction. */
        void setLastMachineCloseAction(MachineCloseAction machineCloseAction, const QUuid &uID);

        /** Returns machine close hook script name as simple string. */
        QString machineCloseHookScript(const QUuid &uID);

        /** Returns whether machine should discard state on power off. */
        bool discardStateOnPowerOff(const QUuid &uID);
    /** @} */

#ifdef VBOX_WITH_DEBUGGER_GUI
    /** @name Virtual Machine: Debug UI
      * @{ */
        /** Returns debug flag value for passed @a strDebugFlagKey. */
        QString debugFlagValue(const QString &strDebugFlagKey);
    /** @} */
#endif /* VBOX_WITH_DEBUGGER_GUI */

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    /** @name VirtualBox: Extra-data Manager window
      * @{ */
        /** Returns Extra-data Manager geometry using @a pWidget and @a pParentWidget as hint. */
        QRect extraDataManagerGeometry(QWidget *pWidget, QWidget *pParentWidget);
        /** Returns whether Extra-data Manager should be maximized or not. */
        bool extraDataManagerShouldBeMaximized();
        /** Defines Extra-data Manager @a geometry and @a fMaximized state. */
        void setExtraDataManagerGeometry(const QRect &geometry, bool fMaximized);

        /** Returns Extra-data Manager splitter hints using @a pWidget as hint. */
        QList<int> extraDataManagerSplitterHints(QWidget *pWidget);
        /** Defines Extra-data Manager splitter @a hints. */
        void setExtraDataManagerSplitterHints(const QList<int> &hints);
    /** @} */
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

    /** @name Virtual Machine: Log Viewer dialog
      * @{ */
        /** Returns log-window geometry using @a pWidget, @a pParentWidget and @a defaultGeometry as hints. */
        QRect logWindowGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry);
        /** Returns whether log-window should be maximized or not. */
        bool logWindowShouldBeMaximized();
        /** Defines log-window @a geometry and @a fMaximized state. */
        void setLogWindowGeometry(const QRect &geometry, bool fMaximized);
    /** @} */

    /** @name Virtual Machine: Log Viewer widget options
      * @{ */
        void setLogViweverOptions(const QFont &font, bool wrapLines, bool showLineNumbers);
        /** Returns log-viewer line wrapping flag. */
        bool logViewerWrapLines();
        /** Returns log-viewer show line numbers flag. */
        bool logViewerShowLineNumbers();
        /** Tries to find system font by searching by family and style strings within the font database. */
        QFont logViewerFont();
        void setLogViewerVisiblePanels(const QStringList &panelNameList);
        QStringList logViewerVisiblePanels();
    /** @} */

    /** @name Help Browser
      * @{ */
        void setHelpBrowserLastUrlList(const QStringList &urlList);
        QStringList helpBrowserLastUrlList();
        void setHelpBrowserZoomPercentage(int iZoomPercentage);
        int helpBrowserZoomPercentage();
        QRect helpBrowserDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry);
        void setHelpBrowserDialogGeometry(const QRect &geometry, bool fMaximized);
        bool helpBrowserDialogShouldBeMaximized();
        void setHelpBrowserBookmarks(const QStringList &bookmarks);
        QStringList helpBrowserBookmarks();
    /** @} */

    /** @name Manager UI: VM Activity Monitor and Overview
      * @{ */
        void setVMActivityOverviewHiddenColumnList(const QStringList &hiddenColumnList);
        QStringList VMActivityOverviewHiddenColumnList();
        bool VMActivityOverviewShowAllMachines();
        void setVMActivityOverviewShowAllMachines(bool fShow);
        void setVMActivityMonitorDataSeriesColors(const QStringList &colorList);
        QStringList VMActivityMonitorDataSeriesColors();
        bool VMActivityMonitorShowVMExits();
        void setVMActivityMonitorShowVMExits(bool fShow);
    /** @} */

    /** @name Medium Selector
      * @{ */
        QRect mediumSelectorDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry);
        void setMediumSelectorDialogGeometry(const QRect &geometry, bool fMaximized);
        bool mediumSelectorDialogShouldBeMaximized();
    /** @} */

private slots:

    /** Handles 'extra-data change' event: */
    void sltExtraDataChange(const QUuid &uMachineID, const QString &strKey, const QString &strValue);

private:

    /** Prepare Extra-data Manager. */
    void prepare();
    /** Prepare global extra-data map. */
    void prepareGlobalExtraDataMap();
    /** Prepare extra-data event-handler. */
    void prepareExtraDataEventHandler();
    /** Prepare extra-data settings. */
    void prepareExtraDataSettings();
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    // /** Prepare window. */
    // void prepareWindow();

    /** Cleanup window. */
    void cleanupWindow();
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */
    /** Cleanup extra-data event-handler. */
    void cleanupExtraDataEventHandler();
    // /** Cleanup extra-data map. */
    // void cleanupExtraDataMap();
    /** Cleanup Extra-data Manager. */
    void cleanup();

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    /** Open window. */
    void open(QWidget *pCenterWidget);
#endif

    /** Retrieves an extra-data key from both machine and global sources.
      *
      * If @a uID isn't #GlobalID, this will first check the extra-data associated
      * with the machine given by @a uID then fallback on the global extra-data.
      *
      * @returns String value if found, null string if not.
      * @param   strKey      The extra-data key to get.
      * @param   uID         Machine UUID or #GlobalID.
      * @param   strValue    Where to return the value when found. */
    QString extraDataStringUnion(const QString &strKey, const QUuid &uID);
    /** Determines whether feature corresponding to passed @a strKey is allowed.
      * If valid @a uID is set => applies to machine and global extra-data,
      * otherwise => only to global one. */
    bool isFeatureAllowed(const QString &strKey, const QUuid &uID = GlobalID);
    /** Determines whether feature corresponding to passed @a strKey is restricted.
      * If valid @a uID is set => applies to machine and global extra-data,
      * otherwise => only to global one. */
    bool isFeatureRestricted(const QString &strKey, const QUuid &uID = GlobalID);

    /** Translates bool flag into QString value. */
    QString toFeatureState(bool fState);
    /** Translates bool flag into 'allowed' value. */
    QString toFeatureAllowed(bool fAllowed);
    /** Translates bool flag into 'restricted' value. */
    QString toFeatureRestricted(bool fRestricted);

    /** Defines saved dialog geometry according to specified attributes.
      * @param  strKey      Brings geometry extra-data key of particular dialog.
      * @param  geometry    Brings the dialog geometry to save.
      * @param  fMaximized  Brings whether saved dialog geometry should be marked as maximized. */
    void setDialogGeometry(const QString &strKey, const QRect &geometry, bool fMaximized);
    /** Returns saved dialog geometry according to specified attributes.
      * @param  strKey           Brings geometry extra-data key of particular dialog.
      * @param  pWidget          Brings the widget to limit geometry bounds according to.
      * @param  pParentWidget    Brings the widget to center geometry rectangle according to.
      * @param  defaultGeometry  Brings the default geometry which should be used to
      *                          calculate resulting geometry if saved was not found. */
    QRect dialogGeometry(const QString &strKey, QWidget *pWidget, QWidget *pParentWidget = 0, const QRect &defaultGeometry = QRect());
    /** Returns true if the dialog should be maximized.
      * @param  strKey           Brings geometry extra-data key of particular dialog. */
    bool dialogShouldBeMaximized(const QString &strKey);

    /** Returns string consisting of @a strBase appended with @a uScreenIndex for the *non-primary* screen-index.
      * If @a fSameRuleForPrimary is 'true' same rule will be used for *primary* screen-index. Used for storing per-screen extra-data. */
    static QString extraDataKeyPerScreen(const QString &strBase, ulong uScreenIndex, bool fSameRuleForPrimary = false);

    /** Holds the singleton instance. */
    static UIExtraDataManager *s_pInstance;

    /** Holds extra-data event-handler instance. */
    UIExtraDataEventHandler *m_pHandler;

    /** Holds extra-data map instance. */
    MapOfExtraDataMaps m_data;

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    /** Holds Extra-data Manager window instance. */
    QPointer<UIExtraDataManagerWindow> m_pWindow;
#endif
};

/** Singleton Extra-data Manager 'official' name. */
#define gEDataManager UIExtraDataManager::instance()

#endif /* !FEQT_INCLUDED_SRC_extradata_UIExtraDataManager_h */
