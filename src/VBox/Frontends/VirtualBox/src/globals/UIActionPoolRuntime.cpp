/* $Id: UIActionPoolRuntime.cpp 108918 2025-03-24 16:07:18Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIActionPoolRuntime class implementation.
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

/* Qt includes: */
#include <QActionGroup>
#include <QApplication>

/* GUI includes: */
#include "UICommon.h"
#include "UIActionPoolRuntime.h"
#include "UIConverter.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIShortcutPool.h"

/* COM includes: */
#include "CExtPack.h"
#include "CExtPackManager.h"

/* External includes: */
#include <math.h>


/* Namespaces: */
using namespace UIExtraDataDefs;


/** Menu action extension, used as 'Machine' menu class. */
class UIActionMenuRuntimeMachine : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeMachine(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuType_Machine;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuType_Machine);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType_Machine);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Machine"));
    }
};

/** Simple action extension, used as 'Show Settings' action class. */
class UIActionSimpleRuntimeShowSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowSettings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_settings_16px.png", ":/vm_settings_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SettingsDialog;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SettingsDialog);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SettingsDialog);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SettingsDialog");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("S");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display the virtual machine settings window"));
    }
};

/** Simple action extension, used as 'Perform Take Snapshot' action class. */
class UIActionSimpleRuntimePerformTakeSnapshot : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformTakeSnapshot(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/snapshot_take_16px.png", ":/snapshot_take_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_TakeSnapshot;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_TakeSnapshot);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_TakeSnapshot);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TakeSnapshot");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("T");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Take Sn&apshot..."));
        setStatusTip(QApplication::translate("UIActionPool", "Take a snapshot of the virtual machine"));
    }
};

/** Simple action extension, used as 'Show Information Dialog' action class. */
class UIActionSimpleRuntimeShowInformationDialog : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowInformationDialog(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/session_info_16px.png", ":/session_info_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_InformationDialog;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_InformationDialog);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_InformationDialog);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("InformationDialog");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("N");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Session I&nformation..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display the virtual machine session information window"));
    }
};

/** Simple action extension, used as 'Show File Manager Dialog' action class. */
class UIActionSimpleRuntimeShowFileManagerDialog : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowFileManagerDialog(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/file_manager_16px.png", ":/file_manager_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_FileManagerDialog;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_FileManagerDialog);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_FileManagerDialog);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerDialog");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "File Manager..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display the virtual machine file manager window"));
    }
};

/** Toggle action extension, used as 'Pause' action class. */
class UIActionToggleRuntimePause : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimePause(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/vm_pause_on_16px.png", ":/vm_pause_16px.png",
                         ":/vm_pause_on_disabled_16px.png", ":/vm_pause_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Pause;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Pause);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Pause);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Pause");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("P");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Pause"));
        setStatusTip(QApplication::translate("UIActionPool", "Suspend the execution of the virtual machine"));
    }
};

/** Simple action extension, used as 'Perform Reset' action class. */
class UIActionSimpleRuntimePerformReset : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformReset(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_reset_16px.png", ":/vm_reset_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Reset;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Reset);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Reset);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Reset");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("R");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Reset"));
        setStatusTip(QApplication::translate("UIActionPool", "Reset the virtual machine"));
    }
};

/** Simple action extension, used as 'Perform Detach' action class. */
class UIActionSimpleRuntimePerformDetach : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformDetach(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_create_shortcut_16px.png", ":/vm_create_shortcut_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Detach;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Detach);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Detach);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("DetachUI");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Detach GUI"));
        setStatusTip(QApplication::translate("UIActionPool", "Detach the GUI from headless VM"));
    }
};

/** Simple action extension, used as 'Perform Save State' action class. */
class UIActionSimpleRuntimePerformSaveState : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformSaveState(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_save_state_16px.png", ":/vm_save_state_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SaveState;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SaveState);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SaveState);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SaveState");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Save State"));
        setStatusTip(QApplication::translate("UIActionPool", "Save the state of the virtual machine"));
    }
};

/** Simple action extension, used as 'Perform Shutdown' action class. */
class UIActionSimpleRuntimePerformShutdown : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformShutdown(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_shutdown_16px.png", ":/vm_shutdown_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Shutdown;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Shutdown);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Shutdown);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Shutdown");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
#ifdef VBOX_WS_MAC
        return QKeySequence("U");
#else
        return QKeySequence("H");
#endif
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "ACPI Sh&utdown"));
        setStatusTip(QApplication::translate("UIActionPool", "Send the ACPI Shutdown signal to the virtual machine"));
    }
};

/** Simple action extension, used as 'Perform PowerOff' action class. */
class UIActionSimpleRuntimePerformPowerOff : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformPowerOff(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_poweroff_16px.png", ":/vm_poweroff_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_PowerOff;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_PowerOff);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_PowerOff);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("PowerOff");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Po&wer Off"));
        setStatusTip(QApplication::translate("UIActionPool", "Power off the virtual machine"));
    }
};

/** Simple action extension, used as 'Show Logs' action class. */
class UIActionSimpleRuntimeShowLogs : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowLogs(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_show_logs_16px.png", ":/vm_show_logs_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_LogDialog;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_LogDialog);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_LogDialog);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("LogWindow");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Show &Log..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display the log viewer window"));
    }
};

/** Menu action extension, used as 'View' menu class. */
class UIActionMenuRuntimeView : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeView(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuType_View;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuType_View);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType_View);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&View"));
    }
};

/** Menu action extension, used as 'View Popup' menu class. */
class UIActionMenuRuntimeViewPopup : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeViewPopup(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuType_View;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuType_View);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType_View);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE {}
};

/** Toggle action extension, used as 'Full-screen Mode' action class. */
class UIActionToggleRuntimeFullscreenMode : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeFullscreenMode(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/fullscreen_on_16px.png", ":/fullscreen_16px.png",
                         ":/fullscreen_on_disabled_16px.png", ":/fullscreen_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Fullscreen;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Fullscreen);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Fullscreen);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FullscreenMode");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("F");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Full-screen Mode"));
        setStatusTip(QApplication::translate("UIActionPool", "Switch between normal and full-screen mode"));
    }
};

/** Toggle action extension, used as 'Seamless Mode' action class. */
class UIActionToggleRuntimeSeamlessMode : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeSeamlessMode(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/seamless_on_16px.png", ":/seamless_16px.png",
                         ":/seamless_on_disabled_16px.png", ":/seamless_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Seamless;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Seamless);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Seamless);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SeamlessMode");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("L");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Seam&less Mode"));
        setStatusTip(QApplication::translate("UIActionPool", "Switch between normal and seamless desktop integration mode"));
    }
};

/** Toggle action extension, used as 'Scaled Mode' action class. */
class UIActionToggleRuntimeScaledMode : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeScaledMode(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/scale_on_16px.png", ":/scale_16px.png",
                         ":/scale_on_disabled_16px.png", ":/scale_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Scale;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Scale);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Scale);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ScaleMode");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("C");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "S&caled Mode"));
        setStatusTip(QApplication::translate("UIActionPool", "Switch between normal and scaled mode"));
    }
};

#ifndef VBOX_WS_MAC
/** Simple action extension, used as 'Perform Minimize Window' action class. */
class UIActionSimpleRuntimePerformMinimizeWindow : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformMinimizeWindow(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/minimize_16px.png", ":/minimize_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_MinimizeWindow;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_MinimizeWindow);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_MinimizeWindow);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("WindowMinimize");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("M");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Minimize Window"));
        setStatusTip(QApplication::translate("UIActionPool", "Minimize active window"));
    }
};
#endif /* !VBOX_WS_MAC */

/** Simple action extension, used as 'Perform Window Adjust' action class. */
class UIActionSimpleRuntimePerformWindowAdjust : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformWindowAdjust(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/adjust_win_size_16px.png", ":/adjust_win_size_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_AdjustWindow;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_AdjustWindow);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_AdjustWindow);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("WindowAdjust");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("A");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Adjust Window Size"));
        setStatusTip(QApplication::translate("UIActionPool", "Adjust window size and position to best fit the guest display"));
    }
};

/** Toggle action extension, used as 'Guest Autoresize' action class. */
class UIActionToggleRuntimeGuestAutoresize : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeGuestAutoresize(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/auto_resize_on_on_16px.png", ":/auto_resize_on_16px.png",
                         ":/auto_resize_on_on_disabled_16px.png", ":/auto_resize_on_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_GuestAutoresize;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_GuestAutoresize);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_GuestAutoresize);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("GuestAutoresize");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Auto-resize &Guest Display"));
        setStatusTip(QApplication::translate("UIActionPool", "Automatically resize the guest display when the window is resized"));
    }
};

/** Simple action extension, used as 'Perform Take Screenshot' action class. */
class UIActionSimpleRuntimePerformTakeScreenshot : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformTakeScreenshot(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/screenshot_take_16px.png", ":/screenshot_take_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_TakeScreenshot;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_TakeScreenshot);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_TakeScreenshot);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TakeScreenshot");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("E");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Take Screensh&ot..."));
        setStatusTip(QApplication::translate("UIActionPool", "Take guest display screenshot"));
    }
};

/** Menu action extension, used as 'View' menu class. */
class UIActionMenuRuntimeRecording : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeRecording(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Recording;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Recording);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Recording);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Recording"));
    }
};

/** Simple action extension, used as 'Show Recording Settings' action class. */
class UIActionSimpleRuntimeShowRecordingSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowRecordingSettings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/video_capture_settings_16px.png", ":/video_capture_settings_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_RecordingSettings;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_RecordingSettings);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_RecordingSettings);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RecordingSettingsDialog");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Recording Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display virtual machine settings window to configure video/audio recording"));
    }
};

/** Toggle action extension, used as 'Recording' action class. */
class UIActionToggleRuntimeRecording : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeRecording(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/video_capture_on_16px.png", ":/video_capture_16px.png",
                         ":/video_capture_on_disabled_16px.png", ":/video_capture_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_StartRecording;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_StartRecording);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_StartRecording);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Recording");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Recording"));
        setStatusTip(QApplication::translate("UIActionPool", "Enable guest video/audio recording"));
    }
};

/** Toggle action extension, used as 'VRDE Server' action class. */
class UIActionToggleRuntimeVRDEServer : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeVRDEServer(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/vrdp_on_16px.png", ":/vrdp_16px.png",
                         ":/vrdp_on_disabled_16px.png", ":/vrdp_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_VRDEServer;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_VRDEServer);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_VRDEServer);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VRDPServer");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "R&emote Display"));
        setStatusTip(QApplication::translate("UIActionPool", "Allow remote desktop (RDP) connections to this machine"));
    }
};

/** Menu action extension, used as 'MenuBar' menu class. */
class UIActionMenuRuntimeMenuBar : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeMenuBar(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/menubar_16px.png", ":/menubar_disabled_16px.png")
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBar;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBar);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBar);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Menu Bar"));
    }
};

/** Simple action extension, used as 'Show MenuBar Settings Window' action class. */
class UIActionSimpleRuntimeShowMenuBarSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowMenuBarSettings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/menubar_settings_16px.png", ":/menubar_settings_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBarSettings;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBarSettings);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBarSettings);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MenuBarSettings");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Menu Bar Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display window to configure menu-bar"));
    }
};

#ifndef VBOX_WS_MAC
/** Toggle action extension, used as 'MenuBar' action class. */
class UIActionToggleRuntimeMenuBar : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeMenuBar(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/menubar_on_16px.png", ":/menubar_16px.png",
                         ":/menubar_on_disabled_16px.png", ":/menubar_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleMenuBar;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleMenuBar);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleMenuBar);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleMenuBar");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Show Menu &Bar"));
        setStatusTip(QApplication::translate("UIActionPool", "Enable menu-bar"));
    }
};
#endif /* !VBOX_WS_MAC */

/** Menu action extension, used as 'StatusBar' menu class. */
class UIActionMenuRuntimeStatusBar : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeStatusBar(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/statusbar_16px.png", ":/statusbar_disabled_16px.png")
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBar;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBar);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBar);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Status Bar"));
    }
};

/** Simple action extension, used as 'Show StatusBar Settings Window' action class. */
class UIActionSimpleRuntimeShowStatusBarSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowStatusBarSettings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/statusbar_settings_16px.png", ":/statusbar_settings_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBarSettings;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBarSettings);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBarSettings);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("StatusBarSettings");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Status Bar Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display window to configure status-bar"));
    }
};

/** Toggle action extension, used as 'StatusBar' action class. */
class UIActionToggleRuntimeStatusBar : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeStatusBar(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/statusbar_on_16px.png", ":/statusbar_16px.png",
                         ":/statusbar_on_disabled_16px.png", ":/statusbar_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleStatusBar;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleStatusBar);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleStatusBar);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleStatusBar");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Show Status &Bar"));
        setStatusTip(QApplication::translate("UIActionPool", "Enable status-bar"));
    }
};

/** Menu action extension, used as 'Input' menu class. */
class UIActionMenuRuntimeInput : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeInput(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuType_Input;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuType_Input);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType_Input);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Input"));
    }
};

/** Menu action extension, used as 'Keyboard' menu class. */
class UIActionMenuRuntimeKeyboard : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeKeyboard(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/keyboard_16px.png")
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_Keyboard;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_Keyboard);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_Keyboard);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Keyboard"));
    }
};

/** Simple action extension, used as 'Show Keyboard Settings' action class. */
class UIActionSimpleRuntimeShowKeyboardSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowKeyboardSettings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/keyboard_settings_16px.png", ":/keyboard_settings_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_KeyboardSettings;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_KeyboardSettings);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_KeyboardSettings);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("KeyboardSettings");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Keyboard Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display global preferences window to configure keyboard shortcuts"));
    }
};

/** Simple action extension, used as 'Show Soft Keyboard' action class. */
class UIActionSimpleRuntimeShowSoftKeyboard : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowSoftKeyboard(UIActionPool *pParent)
        : UIActionSimple(pParent, UIIconPool::iconSet(":/soft_keyboard_16px.png"), true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_SoftKeyboard;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_SoftKeyboard);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_SoftKeyboard);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SoftKeyboard");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Soft Keyboard..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display soft keyboard"));
    }
};

/** Simple action extension, used as 'Perform Type CAD' action class. */
class UIActionSimpleRuntimePerformTypeCAD : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformTypeCAD(UIActionPool *pParent)
        : UIActionSimple(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCAD;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCAD);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCAD);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TypeCAD");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Del");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Insert %1", "that means send the %1 key sequence to the virtual machine").arg("Ctrl-Alt-Del"));
        setStatusTip(QApplication::translate("UIActionPool", "Send the %1 sequence to the virtual machine").arg("Ctrl-Alt-Del"));
    }
};

#ifdef VBOX_WS_NIX
/** X11: Simple action extension, used as 'Perform Type CABS' action class. */
class UIActionSimpleRuntimePerformTypeCABS : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformTypeCABS(UIActionPool *pParent)
        : UIActionSimple(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCABS;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCABS);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCABS);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TypeCABS");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Backspace");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Insert %1", "that means send the %1 key sequence to the virtual machine").arg("Ctrl-Alt-Backspace"));
        setStatusTip(QApplication::translate("UIActionPool", "Send the %1 sequence to the virtual machine").arg("Ctrl-Alt-Backspace"));
    }
};
#endif /* VBOX_WS_NIX */

/** Simple action extension, used as 'Perform Type Ctrl Break' action class. */
class UIActionSimpleRuntimePerformTypeCtrlBreak : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformTypeCtrlBreak(UIActionPool *pParent)
        : UIActionSimple(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCtrlBreak;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCtrlBreak);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCtrlBreak);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TypeCtrlBreak");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Insert %1", "that means send the %1 key sequence to the virtual machine").arg("Ctrl-Break"));
        setStatusTip(QApplication::translate("UIActionPool", "Send the %1 sequence to the virtual machine").arg("Ctrl-Break"));
    }
};

/** Simple action extension, used as 'Perform Type Insert' action class. */
class UIActionSimpleRuntimePerformTypeInsert : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformTypeInsert(UIActionPool *pParent)
        : UIActionSimple(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeInsert;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeInsert);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeInsert);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TypeInsert");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Insert %1", "that means send the %1 key sequence to the virtual machine").arg("Insert"));
        setStatusTip(QApplication::translate("UIActionPool", "Send the %1 sequence to the virtual machine").arg("Insert"));
    }
};

/** Simple action extension, used as 'Perform Type PrintScreen' action class. */
class UIActionSimpleRuntimePerformTypePrintScreen : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformTypePrintScreen(UIActionPool *pParent)
        : UIActionSimple(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypePrintScreen;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypePrintScreen);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypePrintScreen);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TypePrintScreen");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Insert %1", "that means send the %1 key sequence to the virtual machine").arg("Print Screen"));
        setStatusTip(QApplication::translate("UIActionPool", "Send the %1 sequence to the virtual machine").arg("Print Screen"));
    }
};

/** Simple action extension, used as 'Perform Type Alt PrintScreen' action class. */
class UIActionSimpleRuntimePerformTypeAltPrintScreen : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformTypeAltPrintScreen(UIActionPool *pParent)
        : UIActionSimple(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeAltPrintScreen;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeAltPrintScreen);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeAltPrintScreen);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TypeAltPrintScreen");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Insert %1", "that means send the %1 key sequence to the virtual machine").arg("Alt Print Screen"));
        setStatusTip(QApplication::translate("UIActionPool", "Send the %1 sequence to the virtual machine").arg("Alt Print Screen"));
    }
};

/** Toggle action extension, used as 'Perform Host Key Combo Press/Release' action class. */
class UIActionToggleRuntimePerformTypeHostKeyCombo : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimePerformTypeHostKeyCombo(UIActionPool *pParent)
        : UIActionToggle(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeHostKeyCombo;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeHostKeyCombo);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeHostKeyCombo);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TypeHostKeyCombo");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
#ifdef VBOX_WS_MAC
        return QKeySequence("Insert");
#else
        return QKeySequence("Insert");
#endif
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Insert %1", "that means send the %1 key sequence to the virtual machine").arg("Host Key Combo"));
        setStatusTip(QApplication::translate("UIActionPool", "Send the %1 sequence to the virtual machine").arg("Host Key Combo"));
    }
};

/** Menu action extension, used as 'Mouse' menu class. */
class UIActionMenuRuntimeMouse : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeMouse(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_Mouse;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_Mouse);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_Mouse);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Mouse"));
    }
};

/** Toggle action extension, used as 'Mouse Integration' action class. */
class UIActionToggleRuntimeMouseIntegration : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeMouseIntegration(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/mouse_can_seamless_on_16px.png", ":/mouse_can_seamless_16px.png",
                         ":/mouse_can_seamless_on_disabled_16px.png", ":/mouse_can_seamless_disabled_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_MouseIntegration;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuInputActionType_MouseIntegration);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType_MouseIntegration);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MouseIntegration");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Mouse Integration"));
        setStatusTip(QApplication::translate("UIActionPool", "Enable host mouse pointer integration"));
    }
};


/** Menu action extension, used as 'Devices' menu class. */
class UIActionMenuRuntimeDevices : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeDevices(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuType_Devices;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuType_Devices);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType_Devices);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Devices"));
    }
};

/** Menu action extension, used as 'Hard Drives' menu class. */
class UIActionMenuRuntimeHardDrives : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeHardDrives(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/hd_16px.png", ":/hd_disabled_16px.png")
    {
        setShowToolTip(true);
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrives;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrives);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrives);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Hard Disks"));
    }
};

/** Simple action extension, used as 'Show Hard Drives Settings' action class. */
class UIActionSimpleRuntimeShowHardDrivesSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowHardDrivesSettings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/hd_settings_16px.png", ":/hd_settings_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrivesSettings;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrivesSettings);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrivesSettings);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("HardDriveSettingsDialog");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Hard Disk Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display virtual machine settings window to configure hard disks"));
    }
};

/** Menu action extension, used as 'Optical Drives' menu class. */
class UIActionMenuRuntimeOpticalDevices : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeOpticalDevices(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/cd_16px.png", ":/cd_disabled_16px.png")
    {
        setShowToolTip(true);
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_OpticalDevices;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_OpticalDevices);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_OpticalDevices);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Optical Drives"));
    }
};

/** Menu action extension, used as 'Floppy Drives' menu class. */
class UIActionMenuRuntimeFloppyDevices : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeFloppyDevices(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/fd_16px.png", ":/fd_disabled_16px.png")
    {
        setShowToolTip(true);
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_FloppyDevices;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_FloppyDevices);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_FloppyDevices);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Floppy Drives"));
    }
};

/** Menu action extension, used as 'Audio' menu class. */
class UIActionMenuRuntimeAudio : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeAudio(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/audio_16px.png", ":/audio_all_off_16px.png")
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Audio;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Audio);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Audio);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Audio"));
    }
};

/** Toggle action extension, used as 'Audio Output' action class. */
class UIActionToggleRuntimeAudioOutput : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeAudioOutput(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/audio_output_on_16px.png", ":/audio_output_16px.png",
                         ":/audio_output_on_16px.png", ":/audio_output_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioOutput;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioOutput);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioOutput);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleAudioOutput");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Audio Output"));
        setStatusTip(QApplication::translate("UIActionPool", "Enable audio output"));
    }
};

/** Toggle action extension, used as 'Audio Input' action class. */
class UIActionToggleRuntimeAudioInput : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeAudioInput(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/audio_input_on_16px.png", ":/audio_input_16px.png",
                         ":/audio_input_on_16px.png", ":/audio_input_16px.png",
                         true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioInput;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioInput);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioInput);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleAudioInput");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Audio Input"));
        setStatusTip(QApplication::translate("UIActionPool", "Enable audio input"));
    }
};

/** Menu action extension, used as 'Network Adapters' menu class. */
class UIActionMenuRuntimeNetworkAdapters : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeNetworkAdapters(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/nw_16px.png", ":/nw_disabled_16px.png")
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Network;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Network);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Network);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Network"));
    }
};

/** Simple action extension, used as 'Show Network Settings' action class. */
class UIActionSimpleRuntimeShowNetworkSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowNetworkSettings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/nw_settings_16px.png", ":/nw_settings_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_NetworkSettings;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_NetworkSettings);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_NetworkSettings);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("NetworkSettingsDialog");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Network Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display virtual machine settings window to configure network adapters"));
    }
};

/** Menu action extension, used as 'USB Devices' menu class. */
class UIActionMenuRuntimeUSBDevices : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeUSBDevices(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/usb_16px.png", ":/usb_disabled_16px.png")
    {
        setShowToolTip(true);
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevices;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevices);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevices);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&USB"));
    }
};

/** Simple action extension, used as 'Show USB Devices Settings' action class. */
class UIActionSimpleRuntimeShowUSBDevicesSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowUSBDevicesSettings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/usb_settings_16px.png", ":/usb_settings_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevicesSettings;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevicesSettings);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevicesSettings);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("USBDevicesSettingsDialog");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&USB Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display virtual machine settings window to configure USB devices"));
    }
};

/** Menu action extension, used as 'Web Cams' menu class. */
class UIActionMenuRuntimeWebCams : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeWebCams(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/web_camera_16px.png", ":/web_camera_disabled_16px.png")
    {
        setShowToolTip(true);
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_WebCams;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_WebCams);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_WebCams);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Webcams"));
    }
};

/** Menu action extension, used as 'Shared Clipboard' menu class. */
class UIActionMenuRuntimeSharedClipboard : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeSharedClipboard(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/shared_clipboard_16px.png", ":/shared_clipboard_disabled_16px.png")
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedClipboard;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedClipboard);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedClipboard);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Shared &Clipboard"));
    }
};

/** Menu action extension, used as 'Drag & Drop' menu class. */
class UIActionMenuRuntimeDragAndDrop : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeDragAndDrop(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/drag_drop_16px.png", ":/drag_drop_disabled_16px.png")
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_DragAndDrop;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_DragAndDrop);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_DragAndDrop);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Drag and Drop"));
    }
};

/** Menu action extension, used as 'Shared Folders' menu class. */
class UIActionMenuRuntimeSharedFolders : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeSharedFolders(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/sf_16px.png", ":/sf_disabled_16px.png")
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFolders;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFolders);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFolders);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Shared Folders"));
    }
};

/** Simple action extension, used as 'Show Shared Folders Settings' action class. */
class UIActionSimpleRuntimeShowSharedFoldersSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowSharedFoldersSettings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/sf_settings_16px.png", ":/sf_settings_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFoldersSettings;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFoldersSettings);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFoldersSettings);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SharedFoldersSettingsDialog");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Shared Folders Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display virtual machine settings window to configure shared folders"));
    }
};

/** Simple action extension, used as 'Perform Insert Guest Additions Disk' action class. */
class UIActionSimpleRuntimePerformInsertGuestAdditionsDisk : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformInsertGuestAdditionsDisk(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/guesttools_16px.png", ":/guesttools_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_InsertGuestAdditionsDisk;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_InsertGuestAdditionsDisk);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_InsertGuestAdditionsDisk);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("InsertGuestAdditionsDisk");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Insert Guest Additions CD image..."));
        setStatusTip(QApplication::translate("UIActionPool", "Insert the Guest Additions disk file into the virtual optical drive"));
    }
};

/** Simple action extension, used as 'Perform Upgrade Guest Additions' action class. */
class UIActionSimpleRuntimePerformUpgradeGuestAdditions : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimePerformUpgradeGuestAdditions(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/guesttools_update_16px.png", ":/guesttools_update_disabled_16px.png", true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_UpgradeGuestAdditions;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_UpgradeGuestAdditions);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_UpgradeGuestAdditions);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("UpgradeGuestAdditions");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Upgrade Guest Additions..."));
        setStatusTip(QApplication::translate("UIActionPool", "Upgrade guest additions"));
    }
};

#ifdef VBOX_WITH_DEBUGGER_GUI
/** Menu action extension, used as 'Debug' menu class. */
class UIActionMenuRuntimeDebug : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuRuntimeDebug(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuType_Debug;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuType_Debug);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType_Debug);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "De&bug"));
    }
};

/** Simple action extension, used as 'Show Statistics' action class. */
class UIActionSimpleRuntimeShowStatistics : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowStatistics(UIActionPool *pParent)
        : UIActionSimple(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Statistics;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Statistics);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDebug(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Statistics);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("StatisticWindow");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Statistics...", "debug action"));
    }
};

/** Simple action extension, used as 'Show Command Line' action class. */
class UIActionSimpleRuntimeShowCommandLine : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeShowCommandLine(UIActionPool *pParent)
        : UIActionSimple(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_CommandLine;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_CommandLine);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDebug(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_CommandLine);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("CommandLineWindow");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Command Line...", "debug action"));
    }
};

/** Toggle action extension, used as 'Logging' action class. */
class UIActionToggleRuntimeLogging : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleRuntimeLogging(UIActionPool *pParent)
        : UIActionToggle(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Logging;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Logging);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDebug(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Logging);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Logging");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Logging", "debug action"));
    }
};

/** Simple action extension, used as 'Guest Control Terminal' action class. */
class UIActionSimpleRuntimeGuestControlConsole : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleRuntimeGuestControlConsole(UIActionPool *pParent)
        : UIActionSimple(pParent, true)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_GuestControlConsole;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_GuestControlConsole);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->toRuntime()->isAllowedInMenuDebug(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_GuestControlConsole);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("GuestControlConsole");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Guest Control Terminal...", "debug action"));
    }
};
#endif /* VBOX_WITH_DEBUGGER_GUI */

#ifdef VBOX_WS_MAC
/** macOS: Menu action extension, used as 'Dock' menu class. */
class UIActionMenuDock : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuDock(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE {}
};

/** macOS: Menu action extension, used as 'Dock Settings' menu class. */
class UIActionMenuDockSettings : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuDockSettings(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Dock Icon"));
    }
};

/** macOS: Toggle action extension, used as 'Dock Preview Monitor' action class. */
class UIActionToggleDockPreviewMonitor : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleDockPreviewMonitor(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("DockPreviewMonitor");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Show Monitor Preview"));
    }
};

/** macOS: Toggle action extension, used as 'Dock Disable Monitor' action class. */
class UIActionToggleDockDisableMonitor : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleDockDisableMonitor(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("DockDisableMonitor");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Show Application Icon"));
    }
};

/** macOS: Toggle action extension, used as 'Dock Icon Disable Overlay' action class. */
class UIActionToggleDockIconDisableOverlay : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleDockIconDisableOverlay(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("DockOverlayDisable");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Disable Dock Icon Overlay"));
    }
};
#endif /* VBOX_WS_MAC */


/*********************************************************************************************************************************
*   Class UIActionPoolRuntime implementation.                                                                                    *
*********************************************************************************************************************************/

UIActionPoolRuntime::UIActionPoolRuntime(bool fTemporary /* = false */)
    : UIActionPool(UIType_RuntimeUI, fTemporary)
    , m_cHostScreens(0)
    , m_cGuestScreens(0)
    , m_fGuestSupportsGraphics(false)
{
}

void UIActionPoolRuntime::setHostScreenCount(int cCount)
{
    m_cHostScreens = cCount;
    m_invalidations << UIActionIndexRT_M_View << UIActionIndexRT_M_ViewPopup;
}

void UIActionPoolRuntime::setGuestScreenCount(int cCount)
{
    m_cGuestScreens = cCount;
    m_invalidations << UIActionIndexRT_M_View << UIActionIndexRT_M_ViewPopup;
}

void UIActionPoolRuntime::setGuestScreenSize(int iGuestScreen, const QSize &size)
{
    m_mapGuestScreenSize[iGuestScreen] = size;
    m_invalidations << UIActionIndexRT_M_View << UIActionIndexRT_M_ViewPopup;
}

void UIActionPoolRuntime::setGuestScreenVisible(int iGuestScreen, bool fVisible)
{
    m_mapGuestScreenIsVisible[iGuestScreen] = fVisible;
    m_invalidations << UIActionIndexRT_M_View << UIActionIndexRT_M_ViewPopup;
}

void UIActionPoolRuntime::setGuestSupportsGraphics(bool fSupports)
{
    m_fGuestSupportsGraphics = fSupports;
    m_invalidations << UIActionIndexRT_M_View << UIActionIndexRT_M_ViewPopup;
}

void UIActionPoolRuntime::setHostScreenForGuestScreenMap(const QMap<int, int> &scheme)
{
    m_mapHostScreenForGuestScreen = scheme;
    m_invalidations << UIActionIndexRT_M_View << UIActionIndexRT_M_ViewPopup;
}

QMap<int, int> UIActionPoolRuntime::hostScreenForGuestScreenMap() const
{
    return m_mapHostScreenForGuestScreen;
}

bool UIActionPoolRuntime::isAllowedInMenuMachine(UIExtraDataMetaDefs::RuntimeMenuMachineActionType type) const
{
    foreach (const UIExtraDataMetaDefs::RuntimeMenuMachineActionType &restriction, m_restrictedActionsMenuMachine.values())
        if (restriction & type)
            return false;
    return true;
}

void UIActionPoolRuntime::setRestrictionForMenuMachine(UIActionRestrictionLevel level,
                                                       UIExtraDataMetaDefs::RuntimeMenuMachineActionType restriction)
{
    m_restrictedActionsMenuMachine[level] = restriction;
    m_invalidations << UIActionIndexRT_M_Machine;
}

bool UIActionPoolRuntime::isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType type) const
{
    foreach (const UIExtraDataMetaDefs::RuntimeMenuViewActionType &restriction, m_restrictedActionsMenuView.values())
        if (restriction & type)
            return false;
    return true;
}

void UIActionPoolRuntime::setRestrictionForMenuView(UIActionRestrictionLevel level,
                                                    UIExtraDataMetaDefs::RuntimeMenuViewActionType restriction)
{
    m_restrictedActionsMenuView[level] = restriction;
    m_invalidations << UIActionIndexRT_M_View << UIActionIndexRT_M_ViewPopup;
}

bool UIActionPoolRuntime::isAllowedInMenuInput(UIExtraDataMetaDefs::RuntimeMenuInputActionType type) const
{
    foreach (const UIExtraDataMetaDefs::RuntimeMenuInputActionType &restriction, m_restrictedActionsMenuInput.values())
        if (restriction & type)
            return false;
    return true;
}

void UIActionPoolRuntime::setRestrictionForMenuInput(UIActionRestrictionLevel level,
                                                     UIExtraDataMetaDefs::RuntimeMenuInputActionType restriction)
{
    m_restrictedActionsMenuInput[level] = restriction;
    m_invalidations << UIActionIndexRT_M_Input;
}

bool UIActionPoolRuntime::isAllowedInMenuDevices(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType type) const
{
    foreach (const UIExtraDataMetaDefs::RuntimeMenuDevicesActionType &restriction, m_restrictedActionsMenuDevices.values())
        if (restriction & type)
            return false;
    return true;
}

void UIActionPoolRuntime::setRestrictionForMenuDevices(UIActionRestrictionLevel level,
                                                       UIExtraDataMetaDefs::RuntimeMenuDevicesActionType restriction)
{
    m_restrictedActionsMenuDevices[level] = restriction;
    m_invalidations << UIActionIndexRT_M_Devices;
}

#ifdef VBOX_WITH_DEBUGGER_GUI
bool UIActionPoolRuntime::isAllowedInMenuDebug(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType type) const
{
    foreach (const UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType &restriction, m_restrictedActionsMenuDebug.values())
        if (restriction & type)
            return false;
    return true;
}

void UIActionPoolRuntime::setRestrictionForMenuDebugger(UIActionRestrictionLevel level,
                                                        UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType restriction)
{
    m_restrictedActionsMenuDebug[level] = restriction;
    m_invalidations << UIActionIndexRT_M_Debug;
}
#endif /* VBOX_WITH_DEBUGGER_GUI */

void UIActionPoolRuntime::preparePool()
{
    /* 'Machine' actions: */
    m_pool[UIActionIndexRT_M_Machine] = new UIActionMenuRuntimeMachine(this);
    m_pool[UIActionIndexRT_M_Machine_S_Settings] = new UIActionSimpleRuntimeShowSettings(this);
    m_pool[UIActionIndexRT_M_Machine_S_TakeSnapshot] = new UIActionSimpleRuntimePerformTakeSnapshot(this);
    m_pool[UIActionIndexRT_M_Machine_S_ShowInformation] = new UIActionSimpleRuntimeShowInformationDialog(this);
    m_pool[UIActionIndexRT_M_Machine_S_ShowFileManager] = new UIActionSimpleRuntimeShowFileManagerDialog(this);
    m_pool[UIActionIndexRT_M_Machine_T_Pause] = new UIActionToggleRuntimePause(this);
    m_pool[UIActionIndexRT_M_Machine_S_Reset] = new UIActionSimpleRuntimePerformReset(this);
    m_pool[UIActionIndexRT_M_Machine_S_Detach] = new UIActionSimpleRuntimePerformDetach(this);
    m_pool[UIActionIndexRT_M_Machine_S_SaveState] = new UIActionSimpleRuntimePerformSaveState(this);
    m_pool[UIActionIndexRT_M_Machine_S_Shutdown] = new UIActionSimpleRuntimePerformShutdown(this);
    m_pool[UIActionIndexRT_M_Machine_S_PowerOff] = new UIActionSimpleRuntimePerformPowerOff(this);
    m_pool[UIActionIndexRT_M_Machine_S_ShowLogDialog] = new UIActionSimpleRuntimeShowLogs(this);

    /* 'View' actions: */
    m_pool[UIActionIndexRT_M_View] = new UIActionMenuRuntimeView(this);
    m_pool[UIActionIndexRT_M_ViewPopup] = new UIActionMenuRuntimeViewPopup(this);
    m_pool[UIActionIndexRT_M_View_T_Fullscreen] = new UIActionToggleRuntimeFullscreenMode(this);
    m_pool[UIActionIndexRT_M_View_T_Seamless] = new UIActionToggleRuntimeSeamlessMode(this);
    m_pool[UIActionIndexRT_M_View_T_Scale] = new UIActionToggleRuntimeScaledMode(this);
#ifndef VBOX_WS_MAC
    m_pool[UIActionIndexRT_M_View_S_MinimizeWindow] = new UIActionSimpleRuntimePerformMinimizeWindow(this);
#endif /* !VBOX_WS_MAC */
    m_pool[UIActionIndexRT_M_View_S_AdjustWindow] = new UIActionSimpleRuntimePerformWindowAdjust(this);
    m_pool[UIActionIndexRT_M_View_T_GuestAutoresize] = new UIActionToggleRuntimeGuestAutoresize(this);
    m_pool[UIActionIndexRT_M_View_S_TakeScreenshot] = new UIActionSimpleRuntimePerformTakeScreenshot(this);
    m_pool[UIActionIndexRT_M_View_M_Recording] = new UIActionMenuRuntimeRecording(this);
    m_pool[UIActionIndexRT_M_View_M_Recording_S_Settings] = new UIActionSimpleRuntimeShowRecordingSettings(this);
    m_pool[UIActionIndexRT_M_View_M_Recording_T_Start] = new UIActionToggleRuntimeRecording(this);
    m_pool[UIActionIndexRT_M_View_T_VRDEServer] = new UIActionToggleRuntimeVRDEServer(this);
    m_pool[UIActionIndexRT_M_View_M_MenuBar] = new UIActionMenuRuntimeMenuBar(this);
    m_pool[UIActionIndexRT_M_View_M_MenuBar_S_Settings] = new UIActionSimpleRuntimeShowMenuBarSettings(this);
#ifndef VBOX_WS_MAC
    m_pool[UIActionIndexRT_M_View_M_MenuBar_T_Visibility] = new UIActionToggleRuntimeMenuBar(this);
#endif /* !VBOX_WS_MAC */
    m_pool[UIActionIndexRT_M_View_M_StatusBar] = new UIActionMenuRuntimeStatusBar(this);
    m_pool[UIActionIndexRT_M_View_M_StatusBar_S_Settings] = new UIActionSimpleRuntimeShowStatusBarSettings(this);
    m_pool[UIActionIndexRT_M_View_M_StatusBar_T_Visibility] = new UIActionToggleRuntimeStatusBar(this);

    /* 'Input' actions: */
    m_pool[UIActionIndexRT_M_Input] = new UIActionMenuRuntimeInput(this);
    m_pool[UIActionIndexRT_M_Input_M_Keyboard] = new UIActionMenuRuntimeKeyboard(this);
    m_pool[UIActionIndexRT_M_Input_M_Keyboard_S_Settings] = new UIActionSimpleRuntimeShowKeyboardSettings(this);
    m_pool[UIActionIndexRT_M_Input_M_Keyboard_S_SoftKeyboard] = new UIActionSimpleRuntimeShowSoftKeyboard(this);
    m_pool[UIActionIndexRT_M_Input_M_Keyboard_S_TypeCAD] = new UIActionSimpleRuntimePerformTypeCAD(this);
#ifdef VBOX_WS_NIX
    m_pool[UIActionIndexRT_M_Input_M_Keyboard_S_TypeCABS] = new UIActionSimpleRuntimePerformTypeCABS(this);
#endif /* VBOX_WS_NIX */
    m_pool[UIActionIndexRT_M_Input_M_Keyboard_S_TypeCtrlBreak] = new UIActionSimpleRuntimePerformTypeCtrlBreak(this);
    m_pool[UIActionIndexRT_M_Input_M_Keyboard_S_TypeInsert] = new UIActionSimpleRuntimePerformTypeInsert(this);
    m_pool[UIActionIndexRT_M_Input_M_Keyboard_S_TypePrintScreen] = new UIActionSimpleRuntimePerformTypePrintScreen(this);
    m_pool[UIActionIndexRT_M_Input_M_Keyboard_S_TypeAltPrintScreen] = new UIActionSimpleRuntimePerformTypeAltPrintScreen(this);
    m_pool[UIActionIndexRT_M_Input_M_Keyboard_T_TypeHostKeyCombo] = new UIActionToggleRuntimePerformTypeHostKeyCombo(this);
    m_pool[UIActionIndexRT_M_Input_M_Mouse] = new UIActionMenuRuntimeMouse(this);
    m_pool[UIActionIndexRT_M_Input_M_Mouse_T_Integration] = new UIActionToggleRuntimeMouseIntegration(this);

    /* 'Devices' actions: */
    m_pool[UIActionIndexRT_M_Devices] = new UIActionMenuRuntimeDevices(this);
    m_pool[UIActionIndexRT_M_Devices_M_HardDrives] = new UIActionMenuRuntimeHardDrives(this);
    m_pool[UIActionIndexRT_M_Devices_M_HardDrives_S_Settings] = new UIActionSimpleRuntimeShowHardDrivesSettings(this);
    m_pool[UIActionIndexRT_M_Devices_M_OpticalDevices] = new UIActionMenuRuntimeOpticalDevices(this);
    m_pool[UIActionIndexRT_M_Devices_M_FloppyDevices] = new UIActionMenuRuntimeFloppyDevices(this);
    m_pool[UIActionIndexRT_M_Devices_M_Audio] = new UIActionMenuRuntimeAudio(this);
    m_pool[UIActionIndexRT_M_Devices_M_Audio_T_Output] = new UIActionToggleRuntimeAudioOutput(this);
    m_pool[UIActionIndexRT_M_Devices_M_Audio_T_Input] = new UIActionToggleRuntimeAudioInput(this);
    m_pool[UIActionIndexRT_M_Devices_M_Network] = new UIActionMenuRuntimeNetworkAdapters(this);
    m_pool[UIActionIndexRT_M_Devices_M_Network_S_Settings] = new UIActionSimpleRuntimeShowNetworkSettings(this);
    m_pool[UIActionIndexRT_M_Devices_M_USBDevices] = new UIActionMenuRuntimeUSBDevices(this);
    m_pool[UIActionIndexRT_M_Devices_M_USBDevices_S_Settings] = new UIActionSimpleRuntimeShowUSBDevicesSettings(this);
    m_pool[UIActionIndexRT_M_Devices_M_WebCams] = new UIActionMenuRuntimeWebCams(this);
    m_pool[UIActionIndexRT_M_Devices_M_SharedClipboard] = new UIActionMenuRuntimeSharedClipboard(this);
    m_pool[UIActionIndexRT_M_Devices_M_DragAndDrop] = new UIActionMenuRuntimeDragAndDrop(this);
    m_pool[UIActionIndexRT_M_Devices_M_SharedFolders] = new UIActionMenuRuntimeSharedFolders(this);
    m_pool[UIActionIndexRT_M_Devices_M_SharedFolders_S_Settings] = new UIActionSimpleRuntimeShowSharedFoldersSettings(this);
    m_pool[UIActionIndexRT_M_Devices_S_InsertGuestAdditionsDisk] = new UIActionSimpleRuntimePerformInsertGuestAdditionsDisk(this);
    m_pool[UIActionIndexRT_M_Devices_S_UpgradeGuestAdditions] = new UIActionSimpleRuntimePerformUpgradeGuestAdditions(this);

#ifdef VBOX_WITH_DEBUGGER_GUI
    /* 'Debug' actions: */
    m_pool[UIActionIndexRT_M_Debug] = new UIActionMenuRuntimeDebug(this);
    m_pool[UIActionIndexRT_M_Debug_S_ShowStatistics] = new UIActionSimpleRuntimeShowStatistics(this);
    m_pool[UIActionIndexRT_M_Debug_S_ShowCommandLine] = new UIActionSimpleRuntimeShowCommandLine(this);
    m_pool[UIActionIndexRT_M_Debug_T_Logging] = new UIActionToggleRuntimeLogging(this);
    m_pool[UIActionIndexRT_M_Debug_S_GuestControlConsole] = new UIActionSimpleRuntimeGuestControlConsole(this);
#endif /* VBOX_WITH_DEBUGGER_GUI */

#ifdef VBOX_WS_MAC
    /* 'Dock' actions: */
    m_pool[UIActionIndexRT_M_Dock] = new UIActionMenuDock(this);
    m_pool[UIActionIndexRT_M_Dock_M_DockSettings] = new UIActionMenuDockSettings(this);
    m_pool[UIActionIndexRT_M_Dock_M_DockSettings_T_PreviewMonitor] = new UIActionToggleDockPreviewMonitor(this);
    m_pool[UIActionIndexRT_M_Dock_M_DockSettings_T_DisableMonitor] = new UIActionToggleDockDisableMonitor(this);
    m_pool[UIActionIndexRT_M_Dock_M_DockSettings_T_DisableOverlay] = new UIActionToggleDockIconDisableOverlay(this);
#endif /* VBOX_WS_MAC */

    /* Prepare update-handlers for known menus: */
    m_menuUpdateHandlers[UIActionIndexRT_M_Machine].ptfr =                 &UIActionPoolRuntime::updateMenuMachine;
    m_menuUpdateHandlers[UIActionIndexRT_M_View].ptfr =                    &UIActionPoolRuntime::updateMenuView;
    m_menuUpdateHandlers[UIActionIndexRT_M_ViewPopup].ptfr =               &UIActionPoolRuntime::updateMenuViewPopup;
    m_menuUpdateHandlers[UIActionIndexRT_M_View_M_Recording].ptfr =        &UIActionPoolRuntime::updateMenuViewRecording;
    m_menuUpdateHandlers[UIActionIndexRT_M_View_M_MenuBar].ptfr =          &UIActionPoolRuntime::updateMenuViewMenuBar;
    m_menuUpdateHandlers[UIActionIndexRT_M_View_M_StatusBar].ptfr =        &UIActionPoolRuntime::updateMenuViewStatusBar;
    m_menuUpdateHandlers[UIActionIndexRT_M_Input].ptfr =                   &UIActionPoolRuntime::updateMenuInput;
    m_menuUpdateHandlers[UIActionIndexRT_M_Input_M_Keyboard].ptfr =        &UIActionPoolRuntime::updateMenuInputKeyboard;
    m_menuUpdateHandlers[UIActionIndexRT_M_Input_M_Mouse].ptfr =           &UIActionPoolRuntime::updateMenuInputMouse;
    m_menuUpdateHandlers[UIActionIndexRT_M_Devices].ptfr =                 &UIActionPoolRuntime::updateMenuDevices;
    m_menuUpdateHandlers[UIActionIndexRT_M_Devices_M_HardDrives].ptfr =    &UIActionPoolRuntime::updateMenuDevicesHardDrives;
    m_menuUpdateHandlers[UIActionIndexRT_M_Devices_M_Audio].ptfr =         &UIActionPoolRuntime::updateMenuDevicesAudio;
    m_menuUpdateHandlers[UIActionIndexRT_M_Devices_M_Network].ptfr =       &UIActionPoolRuntime::updateMenuDevicesNetwork;
    m_menuUpdateHandlers[UIActionIndexRT_M_Devices_M_USBDevices].ptfr =    &UIActionPoolRuntime::updateMenuDevicesUSBDevices;
    m_menuUpdateHandlers[UIActionIndexRT_M_Devices_M_SharedFolders].ptfr = &UIActionPoolRuntime::updateMenuDevicesSharedFolders;
#ifdef VBOX_WITH_DEBUGGER_GUI
    m_menuUpdateHandlers[UIActionIndexRT_M_Debug].ptfr =                   &UIActionPoolRuntime::updateMenuDebug;
#endif /* VBOX_WITH_DEBUGGER_GUI */

    /* Call to base-class: */
    UIActionPool::preparePool();
}

void UIActionPoolRuntime::prepareConnections()
{
    /* Prepare connections: */
    connect(gShortcutPool, &UIShortcutPool::sigManagerShortcutsReloaded,
            this, &UIActionPoolRuntime::sltApplyShortcuts);
    connect(gShortcutPool, &UIShortcutPool::sigRuntimeShortcutsReloaded,
            this, &UIActionPoolRuntime::sltApplyShortcuts);
    connect(gEDataManager, &UIExtraDataManager::sigMenuBarConfigurationChange,
            this, &UIActionPoolRuntime::sltHandleConfigurationChange);

    /* Call to base-class: */
    UIActionPool::prepareConnections();
}

void UIActionPoolRuntime::updateConfiguration()
{
    /* Get machine ID: */
    const QUuid uMachineID = uiCommon().managedVMUuid();
    if (uMachineID.isNull())
        return;

    /* Recache common action restrictions: */
    m_restrictedMenus[UIActionRestrictionLevel_Base] =                  gEDataManager->restrictedRuntimeMenuTypes(uMachineID);
    m_restrictedActionsMenuApplication[UIActionRestrictionLevel_Base] = gEDataManager->restrictedRuntimeMenuApplicationActionTypes(uMachineID);
    m_restrictedActionsMenuMachine[UIActionRestrictionLevel_Base] =     gEDataManager->restrictedRuntimeMenuMachineActionTypes(uMachineID);
    m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] =        gEDataManager->restrictedRuntimeMenuViewActionTypes(uMachineID);
    m_restrictedActionsMenuInput[UIActionRestrictionLevel_Base] =       gEDataManager->restrictedRuntimeMenuInputActionTypes(uMachineID);
    m_restrictedActionsMenuDevices[UIActionRestrictionLevel_Base] =     gEDataManager->restrictedRuntimeMenuDevicesActionTypes(uMachineID);
#ifdef VBOX_WITH_DEBUGGER_GUI
    m_restrictedActionsMenuDebug[UIActionRestrictionLevel_Base] =       gEDataManager->restrictedRuntimeMenuDebuggerActionTypes(uMachineID);
#endif /* VBOX_WITH_DEBUGGER_GUI */
#ifdef VBOX_WS_MAC
    m_restrictedActionsMenuWindow[UIActionRestrictionLevel_Base] =      gEDataManager->restrictedRuntimeMenuWindowActionTypes(uMachineID);
#endif /* VBOX_WS_MAC */
    m_restrictedActionsMenuHelp[UIActionRestrictionLevel_Base] =        gEDataManager->restrictedRuntimeMenuHelpActionTypes(uMachineID);

    /* Recache visual state action restrictions: */
    UIVisualStateType restrictedVisualStates = gEDataManager->restrictedVisualStates(uMachineID);
    {
        if (restrictedVisualStates & UIVisualStateType_Fullscreen)
            m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuViewActionType)
                (m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuViewActionType_Fullscreen);
        if (restrictedVisualStates & UIVisualStateType_Seamless)
            m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuViewActionType)
                (m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuViewActionType_Seamless);
        if (restrictedVisualStates & UIVisualStateType_Scale)
            m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuViewActionType)
                (m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuViewActionType_Scale);
    }

    /* Recache reconfiguration action restrictions: */
    bool fReconfigurationAllowed = gEDataManager->machineReconfigurationEnabled(uMachineID);
    if (!fReconfigurationAllowed)
    {
        m_restrictedActionsMenuMachine[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuMachineActionType)
            (m_restrictedActionsMenuMachine[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SettingsDialog);
        m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuViewActionType)
            (m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuViewActionType_RecordingSettings);
        m_restrictedActionsMenuInput[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuInputActionType)
            (m_restrictedActionsMenuInput[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuInputActionType_KeyboardSettings);
        m_restrictedActionsMenuInput[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuInputActionType)
            (m_restrictedActionsMenuInput[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuInputActionType_SoftKeyboard);
        m_restrictedActionsMenuDevices[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
            (m_restrictedActionsMenuDevices[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrivesSettings);
        m_restrictedActionsMenuDevices[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
            (m_restrictedActionsMenuDevices[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_NetworkSettings);
        m_restrictedActionsMenuDevices[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
            (m_restrictedActionsMenuDevices[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevicesSettings);
        m_restrictedActionsMenuDevices[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
            (m_restrictedActionsMenuDevices[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFoldersSettings);
    }

    /* Recache snapshot related action restrictions: */
    bool fSnapshotOperationsAllowed = gEDataManager->machineSnapshotOperationsEnabled(uMachineID);
    if (!fSnapshotOperationsAllowed)
    {
        m_restrictedActionsMenuMachine[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuMachineActionType)
            (m_restrictedActionsMenuMachine[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuMachineActionType_TakeSnapshot);
    }

    /* Recache extension-pack related action restrictions: */
    CExtPackManager extPackManager = gpGlobalSession->virtualBox().GetExtensionPackManager();
    if (extPackManager.isNull() || !extPackManager.IsExtPackUsable(GUI_ExtPackName))
    {
        m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::RuntimeMenuViewActionType)
            (m_restrictedActionsMenuView[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::RuntimeMenuViewActionType_VRDEServer);
    }

    /* Recache close related action restrictions: */
    MachineCloseAction restrictedCloseActions = gEDataManager->restrictedMachineCloseActions(uMachineID);
    bool fAllCloseActionsRestricted =    (!uiCommon().isSeparateProcess() || (restrictedCloseActions & MachineCloseAction_Detach))
                                      && (restrictedCloseActions & MachineCloseAction_SaveState)
                                      && (restrictedCloseActions & MachineCloseAction_Shutdown)
                                      && (restrictedCloseActions & MachineCloseAction_PowerOff);
    if (fAllCloseActionsRestricted)
    {
        m_restrictedActionsMenuApplication[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::MenuApplicationActionType)
            (m_restrictedActionsMenuApplication[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::MenuApplicationActionType_Close);
    }

    /* Call to base-class: */
    UIActionPool::updateConfiguration();
}

void UIActionPoolRuntime::updateMenu(int iIndex)
{
    /* If index belongs to base-class => delegate to base-class: */
    if (iIndex < UIActionIndex_Max)
        UIActionPool::updateMenu(iIndex);
    /* Otherwise,
     * if menu with such index is invalidated
     * and there is update-handler => handle it here: */
    else if (   iIndex > UIActionIndex_Max
             && m_invalidations.contains(iIndex)
             && m_menuUpdateHandlers.contains(iIndex))
             (this->*(m_menuUpdateHandlers.value(iIndex).ptfr))();
}

void UIActionPoolRuntime::updateMenus()
{
    /* Clear menu list: */
    m_mainMenus.clear();

    /* 'Application' menu: */
    addMenu(m_mainMenus, action(UIActionIndex_M_Application));
    updateMenuApplication();

    /* 'Machine' menu: */
    addMenu(m_mainMenus, action(UIActionIndexRT_M_Machine));
    updateMenuMachine();

    /* 'View' menu: */
    addMenu(m_mainMenus, action(UIActionIndexRT_M_View));
    updateMenuView();
    /* 'View' popup menu: */
    addMenu(m_mainMenus, action(UIActionIndexRT_M_ViewPopup), false);
    updateMenuViewPopup();

    /* 'Input' menu: */
    addMenu(m_mainMenus, action(UIActionIndexRT_M_Input));
    updateMenuInput();

    /* 'Devices' menu: */
    addMenu(m_mainMenus, action(UIActionIndexRT_M_Devices));
    updateMenuDevices();

#ifdef VBOX_WITH_DEBUGGER_GUI
    /* 'Debug' menu: */
    addMenu(m_mainMenus, action(UIActionIndexRT_M_Debug), uiCommon().isDebuggerEnabled());
    updateMenuDebug();
#endif

#ifdef VBOX_WS_MAC
    /* 'Window' menu: */
    addMenu(m_mainMenus, action(UIActionIndex_M_Window));
    updateMenuWindow();
#endif

    /* 'Help' menu: */
    addMenu(m_mainMenus, action(UIActionIndex_Menu_Help));
    updateMenuHelp();

    /* 'Log Viewer' menu: */
    updateMenuLogViewerWindow();

    /* 'File Manager' menu: */
    updateMenuFileManager();
}

QString UIActionPoolRuntime::shortcutsExtraDataID() const
{
    return GUI_Input_MachineShortcuts;
}

void UIActionPoolRuntime::updateShortcuts()
{
    /* Call to base-class: */
    UIActionPool::updateShortcuts();
    /* Create temporary Manager UI pool to do the same: */
    if (!isTemporary())
        UIActionPool::createTemporary(UIType_ManagerUI);
}

void UIActionPoolRuntime::sltHandleConfigurationChange(const QUuid &uMachineID)
{
    /* Skip unrelated machine IDs: */
    if (uiCommon().managedVMUuid() != uMachineID)
        return;

    /* Update configuration: */
    updateConfiguration();
}

void UIActionPoolRuntime::sltPrepareMenuViewScreen()
{
    /* Make sure sender is valid: */
    QMenu *pMenu = qobject_cast<QMenu*>(sender());
    AssertPtrReturnVoid(pMenu);

    /* Do we have to show resize, remap or rescale actions? */
    const bool fAllowToShowActionResize = isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Resize);
    const bool fAllowToShowActionRemap = isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Remap);
    const bool fAllowToShowActionRescale = isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Rescale);

    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* Resize actions: */
    if (fAllowToShowActionResize)
    {
        updateMenuViewResize(pMenu);
        fSeparator = true;
    }

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* Remap actions: */
    if (fAllowToShowActionRemap && (m_cHostScreens > 1 || m_cGuestScreens > 1))
    {
        updateMenuViewRemap(pMenu);
        fSeparator = true;
    }

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* Rescale actions: */
    if (fAllowToShowActionRescale)
    {
        updateMenuViewRescale(pMenu);
        fSeparator = true;
    }
}

void UIActionPoolRuntime::sltHandleActionTriggerViewScreenToggle()
{
    /* Make sure sender is valid: */
    QAction *pAction = qobject_cast<QAction*>(sender());
    AssertPtrReturnVoid(pAction);

    /* Send request to enable/disable guest-screen: */
    const int iGuestScreenIndex = pAction->property("Guest Screen Index").toInt();
    const bool fScreenEnabled = pAction->isChecked();
    emit sigNotifyAboutTriggeringViewScreenToggle(iGuestScreenIndex, fScreenEnabled);
}

void UIActionPoolRuntime::sltHandleActionTriggerViewScreenResize(QAction *pAction)
{
    /* Make sure sender is valid: */
    AssertPtrReturnVoid(pAction);

    /* Send request to resize guest-screen to required size: */
    const int iGuestScreenIndex = pAction->property("Guest Screen Index").toInt();
    const QSize size = pAction->property("Requested Size").toSize();
    emit sigNotifyAboutTriggeringViewScreenResize(iGuestScreenIndex, size);
}

void UIActionPoolRuntime::sltHandleActionTriggerViewScreenRemap(QAction *pAction)
{
    /* Make sure sender is valid: */
    AssertPtrReturnVoid(pAction);

    /* Send request to remap guest-screen to required host-screen: */
    const int iGuestScreenIndex = pAction->property("Guest Screen Index").toInt();
    const int iHostScreenIndex = pAction->property("Host Screen Index").toInt();
    emit sigNotifyAboutTriggeringViewScreenRemap(iGuestScreenIndex, iHostScreenIndex);
}

void UIActionPoolRuntime::sltHandleActionTriggerViewScreenRescale(QAction *pAction)
{
    /* Make sure sender is valid: */
    AssertPtrReturnVoid(pAction);

    /* Change scale-factor directly: */
    const double dScaleFactor = pAction->property("Requested Scale Factor").toDouble();
    const int iGuestScreenIndex = pAction->property("Guest Screen Index").toInt();
    gEDataManager->setScaleFactor(dScaleFactor, uiCommon().managedVMUuid(), iGuestScreenIndex);
}

void UIActionPoolRuntime::updateMenuMachine()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Machine)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'Settings Dialog' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_Settings)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Take Snapshot' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_TakeSnapshot)) || fSeparator;
    /* 'Information Dialog' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_ShowInformation)) || fSeparator;
    /* 'File Manager' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_ShowFileManager)) || fSeparator;
    /* 'Log Dialog' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_ShowLogDialog)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Pause' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_T_Pause)) || fSeparator;
    /* 'Reset' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_Reset)) || fSeparator;
    /* 'Detach' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_Detach)) || fSeparator;
    /* 'SaveState' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_SaveState)) || fSeparator;
    /* 'Shutdown' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_Shutdown)) || fSeparator;
    /* 'PowerOff' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Machine_S_PowerOff)) || fSeparator;

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_Machine);
}

void UIActionPoolRuntime::updateMenuView()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_View)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'Fullscreen' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_T_Fullscreen)) || fSeparator;
    /* 'Seamless' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_T_Seamless)) || fSeparator;
    /* 'Scale' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_T_Scale)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Adjust Window' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_S_AdjustWindow)) || fSeparator;
    /* 'Guest Autoresize' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_T_GuestAutoresize)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Take Screenshot' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_S_TakeScreenshot)) || fSeparator;
    /* 'Recording' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_M_Recording), false) || fSeparator;
    updateMenuViewRecording();
    /* 'Recording Start' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_M_Recording_T_Start)) || fSeparator;
    /* 'VRDE Server' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_T_VRDEServer)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Menu Bar' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_M_MenuBar)) || fSeparator;
    updateMenuViewMenuBar();
    /* 'Status Bar' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_M_StatusBar)) || fSeparator;
    updateMenuViewStatusBar();

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* Do we have to show resize, remap or rescale actions? */
    const bool fAllowToShowActionResize = isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Resize);
    const bool fAllowToShowActionRemap = isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Remap);
    const bool fAllowToShowActionRescale = isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Rescale);
    if (fAllowToShowActionResize || fAllowToShowActionRemap || fAllowToShowActionRescale)
    {
        for (int iGuestScreenIndex = 0; iGuestScreenIndex < m_cGuestScreens; ++iGuestScreenIndex)
        {
            /* Add 'Virtual Screen %1' menu: */
            QMenu *pSubMenu = pMenu->addMenu(UIIconPool::iconSet(":/virtual_screen_16px.png",
                                                                 ":/virtual_screen_disabled_16px.png"),
                                             QApplication::translate("UIActionPool", "Virtual Screen %1").arg(iGuestScreenIndex + 1));
            pSubMenu->setProperty("Guest Screen Index", iGuestScreenIndex);
            connect(pSubMenu, &QMenu::aboutToShow, this, &UIActionPoolRuntime::sltPrepareMenuViewScreen);
        }
    }

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_View);
}

void UIActionPoolRuntime::updateMenuViewPopup()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_ViewPopup)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'Adjust Window' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_S_AdjustWindow)) || fSeparator;
    /* 'Guest Autoresize' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_T_GuestAutoresize)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* Do we have to show resize or rescale actions? */
    const bool fAllowToShowActionResize = isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Resize);
    const bool fAllowToShowActionRescale = isAllowedInMenuView(UIExtraDataMetaDefs::RuntimeMenuViewActionType_Rescale);
    if (fAllowToShowActionResize || fAllowToShowActionRescale)
    {
        for (int iGuestScreenIndex = 0; iGuestScreenIndex < m_cGuestScreens; ++iGuestScreenIndex)
        {
            /* Add 'Virtual Screen %1' menu: */
            QMenu *pSubMenu = pMenu->addMenu(UIIconPool::iconSet(":/virtual_screen_16px.png",
                                                                 ":/virtual_screen_disabled_16px.png"),
                                             QApplication::translate("UIActionPool", "Virtual Screen %1").arg(iGuestScreenIndex + 1));
            pSubMenu->setProperty("Guest Screen Index", iGuestScreenIndex);
            connect(pSubMenu, &QMenu::aboutToShow, this, &UIActionPoolRuntime::sltPrepareMenuViewScreen);
        }
    }

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_ViewPopup);
}

void UIActionPoolRuntime::updateMenuViewRecording()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_View_M_Recording)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'Recording Settings' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_M_Recording_S_Settings)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Start Recording' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_View_M_Recording_T_Start)) || fSeparator;

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_View_M_Recording);
}

void UIActionPoolRuntime::updateMenuViewMenuBar()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_View_M_MenuBar)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* 'Menu Bar Settings' action: */
    addAction(pMenu, action(UIActionIndexRT_M_View_M_MenuBar_S_Settings));
#ifndef VBOX_WS_MAC
    /* 'Toggle Menu Bar' action: */
    addAction(pMenu, action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility));
#endif

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_View_M_MenuBar);
}

void UIActionPoolRuntime::updateMenuViewStatusBar()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_View_M_StatusBar)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* 'Status Bar Settings' action: */
    addAction(pMenu, action(UIActionIndexRT_M_View_M_StatusBar_S_Settings));
    /* 'Toggle Status Bar' action: */
    addAction(pMenu, action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_View_M_StatusBar);
}

void UIActionPoolRuntime::updateMenuViewResize(QMenu *pMenu)
{
    AssertPtrReturnVoid(pMenu);

    /* Prepare content: */
    const QList<QSize> sizes = QList<QSize>()
                               << QSize(640, 480)
                               << QSize(800, 600)
                               << QSize(1024, 768)
                               << QSize(1152, 864)
                               << QSize(1280, 720)
                               << QSize(1280, 800)
                               << QSize(1366, 768)
                               << QSize(1440, 900)
                               << QSize(1600, 900)
                               << QSize(1680, 1050)
                               << QSize(1920, 1080)
                               << QSize(1920, 1200);

    /* Get corresponding screen index and frame-buffer size: */
    const int iGuestScreenIndex = pMenu->property("Guest Screen Index").toInt();
    const QSize screenSize = m_mapGuestScreenSize.value(iGuestScreenIndex);
    const bool fScreenEnabled = m_mapGuestScreenIsVisible.value(iGuestScreenIndex);

    /* For non-primary screens: */
    if (iGuestScreenIndex > 0)
    {
        /* Create 'toggle' action: */
        QAction *pToggleAction = pMenu->addAction(QApplication::translate("UIActionPool", "Enable", "Virtual Screen"),
                                                  this, SLOT(sltHandleActionTriggerViewScreenToggle()));
        if (pToggleAction)
        {
            /* Configure 'toggle' action: */
            pToggleAction->setEnabled(m_fGuestSupportsGraphics);
            pToggleAction->setProperty("Guest Screen Index", iGuestScreenIndex);
            pToggleAction->setCheckable(true);
            pToggleAction->setChecked(fScreenEnabled);
            /* Add separator: */
            pMenu->addSeparator();
        }
    }

    /* Create exclusive 'resize' action-group: */
    QActionGroup *pActionGroup = new QActionGroup(pMenu);
    if (pActionGroup)
    {
        /* Configure exclusive 'resize' action-group: */
        pActionGroup->setExclusive(true);

        /* For every available size: */
        foreach (const QSize &size, sizes)
        {
            /* Create exclusive 'resize' action: */
            QAction *pAction = pActionGroup->addAction(QApplication::translate("UIActionPool", "Resize to %1x%2", "Virtual Screen")
                                                                               .arg(size.width()).arg(size.height()));
            if (pAction)
            {
                /* Configure exclusive 'resize' action: */
                pAction->setEnabled(m_fGuestSupportsGraphics && fScreenEnabled);
                pAction->setProperty("Guest Screen Index", iGuestScreenIndex);
                pAction->setProperty("Requested Size", size);
                pAction->setCheckable(true);
                if (   screenSize.width() == size.width()
                    && screenSize.height() == size.height())
                    pAction->setChecked(true);
            }
        }

        /* Insert group actions into menu: */
        pMenu->addActions(pActionGroup->actions());
        /* Install listener for exclusive action-group: */
        connect(pActionGroup, &QActionGroup::triggered,
                this, &UIActionPoolRuntime::sltHandleActionTriggerViewScreenResize);
    }
}

void UIActionPoolRuntime::updateMenuViewRemap(QMenu *pMenu)
{
    AssertPtrReturnVoid(pMenu);

    /* Get corresponding screen index: */
    const int iGuestScreenIndex = pMenu->property("Guest Screen Index").toInt();
    const bool fScreenEnabled = m_mapGuestScreenIsVisible.value(iGuestScreenIndex);

    /* For non-primary screens: */
    if (iGuestScreenIndex > 0)
    {
        /* Create 'toggle' action: */
        QAction *pToggleAction = pMenu->addAction(QApplication::translate("UIActionPool", "Enable", "Virtual Screen"),
                                                  this, SLOT(sltHandleActionTriggerViewScreenToggle()));
        if (pToggleAction)
        {
            /* Configure 'toggle' action: */
            pToggleAction->setEnabled(m_fGuestSupportsGraphics);
            pToggleAction->setProperty("Guest Screen Index", iGuestScreenIndex);
            pToggleAction->setCheckable(true);
            pToggleAction->setChecked(fScreenEnabled);
            /* Add separator: */
            pMenu->addSeparator();
        }
    }

    /* Create exclusive 'remap' action-group: */
    QActionGroup *pActionGroup = new QActionGroup(pMenu);
    if (pActionGroup)
    {
        /* Configure exclusive 'remap' action-group: */
        pActionGroup->setExclusive(true);

        /* For every host-screen index: */
        for (int iHostScreenIndex = 0; iHostScreenIndex < m_cHostScreens; ++iHostScreenIndex)
        {
            /* Create exclusive 'remap' action: */
            QAction *pAction = pActionGroup->addAction(QApplication::translate("UIActionPool", "Use Host Screen %1")
                                                                               .arg(iHostScreenIndex + 1));
            if (pAction)
            {
                /* Configure exclusive 'remap' action: */
                pAction->setEnabled(m_fGuestSupportsGraphics && fScreenEnabled);
                pAction->setProperty("Guest Screen Index", iGuestScreenIndex);
                pAction->setProperty("Host Screen Index", iHostScreenIndex);
                pAction->setCheckable(true);
                if (   m_mapHostScreenForGuestScreen.contains(iGuestScreenIndex)
                    && m_mapHostScreenForGuestScreen.value(iGuestScreenIndex) == iHostScreenIndex)
                    pAction->setChecked(true);
            }
        }

        /* Insert group actions into menu: */
        pMenu->addActions(pActionGroup->actions());
        /* Install listener for exclusive action-group: */
        connect(pActionGroup, &QActionGroup::triggered,
                this, &UIActionPoolRuntime::sltHandleActionTriggerViewScreenRemap);
    }
}

void UIActionPoolRuntime::updateMenuViewRescale(QMenu *pMenu)
{
    AssertPtrReturnVoid(pMenu);

    /* Get corresponding screen index and scale-factor: */
    const int iGuestScreenIndex = pMenu->property("Guest Screen Index").toInt();
    const double dCurrentScaleFactor = gEDataManager->scaleFactor(uiCommon().managedVMUuid(), iGuestScreenIndex);

    /* Create exclusive 'rescale' action-group: */
    QActionGroup *pActionGroup = new QActionGroup(pMenu);
    if (pActionGroup)
    {
        /* Configure exclusive 'rescale' action-group: */
        pActionGroup->setExclusive(true);

        /* Get device-pixel-ratio: */
        bool fDevicePixelRatioMentioned = false;
        const double dDevicePixelRatio = qMin(UIDesktopWidgetWatchdog::devicePixelRatio(m_mapHostScreenForGuestScreen.value(iGuestScreenIndex)),
                                              10.0 /* meh, who knows? */);

        /* Calculate minimum, maximum and step: */
        const double dMinimum = 1.0;
        const double dMaximum = ceil(dMinimum + dDevicePixelRatio);
        const double dStep = 0.25;

        /* Now, iterate possible scale-factors: */
        double dScaleFactor = dMinimum;
        do
        {
            /* Create exclusive 'rescale' action: */
            QAction *pAction = pActionGroup->addAction(QString());
            if (pAction)
            {
                pAction->setProperty("Guest Screen Index", iGuestScreenIndex);
                /* For the 'unscaled' action: */
                if (dScaleFactor == 1.0)
                {
                    pAction->setProperty("Requested Scale Factor", dScaleFactor);
                    if (dDevicePixelRatio == 1.0)
                        pAction->setText(QApplication::translate("UIActionPool", "Scale to %1%", "scale-factor")
                                         .arg(dScaleFactor * 100));
                    else
                        pAction->setText(QApplication::translate("UIActionPool", "Scale to %1% (unscaled output)", "scale-factor")
                                         .arg(dScaleFactor * 100));
                }
                /* For the 'autoscaled' action: */
                else if (   (dScaleFactor >= dDevicePixelRatio)
                         && (dDevicePixelRatio != 1.0)
                         && !fDevicePixelRatioMentioned)
                {
                    pAction->setProperty("Requested Scale Factor", dDevicePixelRatio);
                    pAction->setText(QApplication::translate("UIActionPool", "Scale to %1% (autoscaled output)", "scale-factor")
                                     .arg(dDevicePixelRatio * 100));
                    fDevicePixelRatioMentioned = true;
                }
                /* For other actions: */
                else
                {
                    pAction->setProperty("Requested Scale Factor", dScaleFactor);
                    pAction->setText(QApplication::translate("UIActionPool", "Scale to %1%", "scale-factor")
                                     .arg(dScaleFactor * 100));
                }

                /* Configure exclusive 'scale-factor' action: */
                pAction->setCheckable(true);
                if (dScaleFactor == dCurrentScaleFactor)
                    pAction->setChecked(true);
            }

            /* Increment scale-factor: */
            dScaleFactor += dStep;
        }
        while (dScaleFactor <= dMaximum);

        /* Insert group actions into menu: */
        pMenu->addActions(pActionGroup->actions());
        /* Install listener for exclusive action-group: */
        connect(pActionGroup, &QActionGroup::triggered,
                this, &UIActionPoolRuntime::sltHandleActionTriggerViewScreenRescale);
    }
}

void UIActionPoolRuntime::updateMenuInput()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Input)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'Keyboard' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard)) || fSeparator;
    updateMenuInputKeyboard();
    /* 'Mouse' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Mouse), false) || fSeparator;
    updateMenuInputMouse();

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Mouse Integration' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Mouse_T_Integration)) || fSeparator;

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_Input);
}

void UIActionPoolRuntime::updateMenuInputKeyboard()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Input_M_Keyboard)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'Keyboard Settings' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard_S_Settings)) || fSeparator;
    /* 'Soft Keyboard' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard_S_SoftKeyboard)) || fSeparator;
    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Type CAD' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard_S_TypeCAD)) || fSeparator;
#ifdef VBOX_WS_NIX
    /* 'Type CABS' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard_S_TypeCABS)) || fSeparator;
#endif
    /* 'Type Ctrl-Break' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard_S_TypeCtrlBreak)) || fSeparator;
    /* 'Type Insert' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard_S_TypeInsert)) || fSeparator;
    /* 'Type Print Screen' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard_S_TypePrintScreen)) || fSeparator;
    /* 'Type Alt Print Screen' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard_S_TypeAltPrintScreen)) || fSeparator;
    /* 'Type Host Key Combo' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Input_M_Keyboard_T_TypeHostKeyCombo)) || fSeparator;

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_Input_M_Keyboard);
}

void UIActionPoolRuntime::updateMenuInputMouse()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Input_M_Mouse)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* 'Machine Integration' action: */
    addAction(pMenu, action(UIActionIndexRT_M_Input_M_Mouse_T_Integration));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_Input_M_Mouse);
}

void UIActionPoolRuntime::updateMenuDevices()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Devices)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'Hard Drives' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_HardDrives)) || fSeparator;
    updateMenuDevicesHardDrives();
    /* 'Optical Devices' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_OpticalDevices)) || fSeparator;
    /* 'Floppy Devices' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_FloppyDevices)) || fSeparator;
    /* 'Audio' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_Audio)) || fSeparator;
    updateMenuDevicesAudio();
    /* 'Network' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_Network)) || fSeparator;
    updateMenuDevicesNetwork();
    /* 'USB Devices' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_USBDevices)) || fSeparator;
    updateMenuDevicesUSBDevices();
    /* 'Web Cams' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_WebCams)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Shared Folders' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_SharedFolders)) || fSeparator;
    updateMenuDevicesSharedFolders();
    /* 'Shared Clipboard' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_SharedClipboard)) || fSeparator;
    /* 'Drag&Drop' submenu: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_DragAndDrop)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* Insert Guest Additions Disk action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_S_InsertGuestAdditionsDisk)) || fSeparator;
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_S_UpgradeGuestAdditions)) || fSeparator;

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_Devices);
}

void UIActionPoolRuntime::updateMenuDevicesHardDrives()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Devices_M_HardDrives)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* 'Hard Drives Settings' action: */
    addAction(pMenu, action(UIActionIndexRT_M_Devices_M_HardDrives_S_Settings));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_Devices_M_HardDrives);
}

void UIActionPoolRuntime::updateMenuDevicesAudio()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Devices_M_Audio)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* 'Output' action: */
    addAction(pMenu, action(UIActionIndexRT_M_Devices_M_Audio_T_Output));
    /* 'Input' action: */
    addAction(pMenu, action(UIActionIndexRT_M_Devices_M_Audio_T_Input));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_Devices_M_Audio);
}

void UIActionPoolRuntime::updateMenuDevicesNetwork()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Devices_M_Network)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'Network Settings' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_Network_S_Settings)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* This menu always remains invalid.. */
}

void UIActionPoolRuntime::updateMenuDevicesUSBDevices()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Devices_M_USBDevices)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'USB Devices Settings' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexRT_M_Devices_M_USBDevices_S_Settings)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* This menu always remains invalid.. */
}

void UIActionPoolRuntime::updateMenuDevicesSharedFolders()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Devices_M_SharedFolders)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* 'Shared Folders Settings' action: */
    addAction(pMenu, action(UIActionIndexRT_M_Devices_M_SharedFolders_S_Settings));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_Devices_M_SharedFolders);
}

#ifdef VBOX_WITH_DEBUGGER_GUI
void UIActionPoolRuntime::updateMenuDebug()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexRT_M_Debug)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* 'Statistics' action: */
    addAction(pMenu, action(UIActionIndexRT_M_Debug_S_ShowStatistics));
    /* 'Command Line' action: */
    addAction(pMenu, action(UIActionIndexRT_M_Debug_S_ShowCommandLine));
    /* 'Logging' action: */
    addAction(pMenu, action(UIActionIndexRT_M_Debug_T_Logging));
    /* 'Guest Control Terminal' action: */
    addAction(pMenu, action(UIActionIndexRT_M_Debug_S_GuestControlConsole));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexRT_M_Debug);
}
#endif /* VBOX_WITH_DEBUGGER_GUI */


#include "UIActionPoolRuntime.moc"
