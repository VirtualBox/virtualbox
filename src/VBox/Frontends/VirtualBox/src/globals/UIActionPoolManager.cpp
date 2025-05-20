/* $Id: UIActionPoolManager.cpp 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIActionPoolManager class implementation.
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
#include <QApplication>
#include <QActionGroup>

/* GUI includes: */
#include "UIActionPoolManager.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UIShortcutPool.h"
#include "UIDefs.h"

/* COM includes: */
#include "CExtPack.h"
#include "CExtPackManager.h"

/* TEMPORARY! */
#if defined(_MSC_VER) && !defined(RT_ARCH_AMD64)
# pragma optimize("g", off)
#endif

/* Namespaces: */
using namespace UIExtraDataDefs;


/** Menu action extension, used as 'File' menu class. */
class UIActionMenuManagerFile : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerFile(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
#ifdef VBOX_WS_MAC
        setName(QApplication::translate("UIActionPool", "&File", "Mac OS X version"));
#else /* VBOX_WS_MAC */
        setName(QApplication::translate("UIActionPool", "&File", "Non Mac OS X version"));
#endif /* !VBOX_WS_MAC */
    }
};

/** Simple action extension, used as 'Show Import Appliance Wizard' action class. */
class UIActionSimpleManagerFileShowImportApplianceWizard : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerFileShowImportApplianceWizard(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/import_32px.png", ":/import_16px.png",
                         ":/import_disabled_32px.png", ":/import_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ImportAppliance");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+I");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Import"));
        setName(QApplication::translate("UIActionPool", "&Import Appliance..."));
        setStatusTip(QApplication::translate("UIActionPool", "Import an appliance into VirtualBox"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Show Export Appliance Wizard' action class. */
class UIActionSimpleManagerFileShowExportApplianceWizard : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerFileShowExportApplianceWizard(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/export_32px.png", ":/export_16px.png",
                         ":/export_disabled_32px.png", ":/export_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ExportAppliance");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+E");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Export"));
        setName(QApplication::translate("UIActionPool", "&Export Appliance..."));
        setStatusTip(QApplication::translate("UIActionPool", "Export one or more VirtualBox virtual machines as an appliance"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Menu action extension, used as 'Global Tools' menu class. */
class UIActionMenuManagerToolsGlobal : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerToolsGlobal(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/tools_menu_24px.png") /// @todo replace with 16px icon
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Tools"));
    }
};

/** Simple action extension, used as 'Show Home Screen' action class. */
class UIActionToggleManagerToolsGlobalShowHomeScreen : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsGlobalShowHomeScreen(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Home));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/welcome_screen_24px.png", ":/welcome_screen_24px.png",
                                        ":/welcome_screen_24px.png", ":/welcome_screen_24px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("HomeScreen");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Home"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the Home Screen"));
    }
};

/** Simple action extension, used as 'Machine Manager' action class. */
class UIActionToggleManagerToolsGlobalShowMachineManager : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsGlobalShowMachineManager(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Machines));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/machine_24px.png", ":/machine_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToolsGlobalMachineManager");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Machines"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the Machine Manager"));
    }
};

/** Simple action extension, used as 'Show Extension Pack Manager' action class. */
class UIActionToggleManagerToolsGlobalShowExtensionPackManager : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsGlobalShowExtensionPackManager(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Extensions));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/extension_pack_manager_24px.png", ":/extension_pack_manager_16px.png",
                                        ":/extension_pack_manager_disabled_24px.png", ":/extension_pack_manager_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ExtensionPackManager");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+T");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Extensions"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the Extension Pack Manager"));
    }
};

/** Simple action extension, used as 'Show Virtual Media Manager' action class. */
class UIActionToggleManagerToolsGlobalShowVirtualMediaManager : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsGlobalShowVirtualMediaManager(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Media));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/media_manager_24px.png", ":/media_manager_16px.png",
                                        ":/media_manager_disabled_24px.png", ":/media_manager_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VirtualMediaManager");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+D");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Media"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the Virtual Media Manager"));
    }
};

/** Simple action extension, used as 'Show Network Manager' action class. */
class UIActionToggleManagerToolsGlobalShowNetworkManager : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsGlobalShowNetworkManager(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Network));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/host_iface_manager_24px.png", ":/host_iface_manager_16px.png",
                                        ":/host_iface_manager_disabled_24px.png", ":/host_iface_manager_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("HostNetworkManager");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+H");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Network"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the Network Manager"));
    }
};

/** Simple action extension, used as 'Show Cloud Profile Manager' action class. */
class UIActionToggleManagerToolsGlobalShowCloudProfileManager : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsGlobalShowCloudProfileManager(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Cloud));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/cloud_profile_manager_24px.png", ":/cloud_profile_manager_16px.png",
                                        ":/cloud_profile_manager_disabled_24px.png", ":/cloud_profile_manager_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("CloudProfileManager");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+P");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Cloud"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the Cloud Profile Manager"));
    }
};

/** Simple action extension, used as 'Show VM Activity Overview' action class. */
class UIActionToggleManagerToolsGlobalShowVMActivityOverview : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsGlobalShowVMActivityOverview(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Activities));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/resources_monitor_24px.png", ":/resources_monitor_16px.png",
                                        ":/resources_monitor_disabled_24px.png", ":/resources_monitor_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToolsGlobalVMActivityOverview");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Resource Usage"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the VM Activity Overview"));
    }
};

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
/** Simple action extension, used as 'Show Extra-data Manager' action class. */
class UIActionSimpleManagerFileShowExtraDataManager : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerFileShowExtraDataManager(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/edata_manager_16px.png", ":/edata_manager_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ExtraDataManager");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+X");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "E&xtra Data Manager..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display the Extra Data Manager window"));
    }
};
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

/** Simple action extension, used as 'Perform Exit' action class. */
class UIActionSimpleManagerFilePerformExit : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerFilePerformExit(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/exit_16px.png", ":/exit_16px.png")
    {
        setMenuRole(QAction::QuitRole);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Exit");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Q");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Quit"));
        setStatusTip(QApplication::translate("UIActionPool", "Close application"));
    }
};


/** Menu action extension, used as 'Group' menu class. */
class UIActionMenuManagerGroup : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerGroup(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Group"));
    }
};

/** Simple action extension, used as 'Perform Create Machine' action class. */
class UIActionSimpleManagerGroupPerformCreateMachine : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerGroupPerformCreateMachine(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_new_32px.png", ":/vm_new_16px.png",
                         ":/vm_new_disabled_32px.png", ":/vm_new_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("NewVM");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+N");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        /// @todo replace that one with separate "New" before 6.2
        setIconText(QApplication::translate("UIActionPool", "&New...").remove('.'));
        setName(QApplication::translate("UIActionPool", "&New Machine..."));
        setStatusTip(QApplication::translate("UIActionPool", "Create new virtual machine"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Add Machine' action class. */
class UIActionSimpleManagerGroupPerformAddMachine : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerGroupPerformAddMachine(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_add_32px.png", ":/vm_add_16px.png",
                         ":/vm_add_disabled_32px.png", ":/vm_add_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("AddVM");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+A");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "&Open"));
        setName(QApplication::translate("UIActionPool", "&Open Machine..."));
        setStatusTip(QApplication::translate("UIActionPool", "Open existing virtual machine"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Rename Group' action class. */
class UIActionSimpleManagerGroupPerformRename : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerGroupPerformRename(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_group_name_16px.png", ":/vm_group_name_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RenameVMGroup");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Rena&me Group..."));
        setStatusTip(QApplication::translate("UIActionPool", "Rename selected virtual machine group"));
    }
};

/** Simple action extension, used as 'Perform Remove Group' action class. */
class UIActionSimpleManagerGroupPerformRemove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerGroupPerformRemove(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_group_remove_16px.png", ":/vm_group_remove_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("AddVMGroup");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Ungroup"));
        setStatusTip(QApplication::translate("UIActionPool", "Ungroup items of selected virtual machine group"));
    }
};

/** Simple action extension, used as 'Perform Sort Group' action class. */
class UIActionSimpleManagerGroupPerformSort : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerGroupPerformSort(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/sort_16px.png", ":/sort_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SortGroup");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Sort"));
        setStatusTip(QApplication::translate("UIActionPool", "Sort items of selected virtual machine group alphabetically"));
    }
};


/** Menu action extension, used as 'Machine' menu class. */
class UIActionMenuManagerMachine : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMachine(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Machine"));
    }
};

/** Simple action extension, used as 'Perform Create Machine' action class. */
class UIActionSimpleManagerMachinePerformCreate : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachinePerformCreate(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_new_32px.png", ":/vm_new_16px.png",
                         ":/vm_new_disabled_32px.png", ":/vm_new_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("NewVM");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+N");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&New..."));
        setStatusTip(QApplication::translate("UIActionPool", "Create new virtual machine"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Add Machine' action class. */
class UIActionSimpleManagerMachinePerformAdd : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachinePerformAdd(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_add_32px.png", ":/vm_add_16px.png",
                         ":/vm_add_disabled_32px.png", ":/vm_add_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("AddVM");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+A");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Open..."));
        setStatusTip(QApplication::translate("UIActionPool", "Open existing virtual machine"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Move to Group => New' action class. */
class UIActionSimpleManagerMachineMoveToGroupNew : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachineMoveToGroupNew(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("AddVMGroup");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "New Group"));
        setStatusTip(QApplication::translate("UIActionPool", "Create new group based on selected virtual machines"));
    }
};

/** Simple action extension, used as 'Show Machine Settings' action class. */
class UIActionSimpleManagerMachineShowSettings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachineShowSettings(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_settings_32px.png", ":/vm_settings_16px.png",
                         ":/vm_settings_disabled_32px.png", ":/vm_settings_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SettingsVM");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+S");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Settings..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display the virtual machine settings window"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Clone Machine' action class. */
class UIActionSimpleManagerMachinePerformClone : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachinePerformClone(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_clone_16px.png", ":/vm_clone_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("CloneVM");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+O");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Cl&one..."));
        setStatusTip(QApplication::translate("UIActionPool", "Clone selected virtual machine"));
    }
};

/** Simple action extension, used as 'Perform Move Machine' action class. */
class UIActionSimpleManagerMachinePerformMove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachinePerformMove(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_move_16px.png", ":/vm_move_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MoveVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Move..."));
        setStatusTip(QApplication::translate("UIActionPool", "Move selected virtual machine"));
    }
};

/** Simple action extension, used as 'Perform Export Machine locally' action class. */
class UIActionSimpleManagerMachinePerformExportLocally : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachinePerformExportLocally(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/export_16px.png", ":/export_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ExportLocally");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "E&xport Locally..."));
        setStatusTip(QApplication::translate("UIActionPool", "Export selected virtual machine locally"));
    }
};

/** Simple action extension, used as 'Perform Export Machine to OCI' action class. */
class UIActionSimpleManagerMachinePerformExportToOCI : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachinePerformExportToOCI(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/export_16px.png", ":/export_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ExportToOCI");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "E&xport to OCI..."));
        setStatusTip(QApplication::translate("UIActionPool", "Export selected virtual machine to OCI"));
    }
};

/** Simple action extension, used as 'Perform Remove Machine' action class. */
class UIActionSimpleManagerMachinePerformRemove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachinePerformRemove(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_delete_32px.png", ":/vm_delete_16px.png",
                         ":/vm_delete_disabled_32px.png", ":/vm_delete_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RemoveVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Remove..."));
        setStatusTip(QApplication::translate("UIActionPool", "Remove selected virtual machines"));
    }
};

/** Simple action extension, used as 'Perform Sort Parent' action class. */
class UIActionSimpleManagerMachinePerformSortParent : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerMachinePerformSortParent(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/sort_16px.png", ":/sort_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SortGroup");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Sort"));
        setStatusTip(QApplication::translate("UIActionPool", "Sort group of first selected virtual machine alphabetically"));
    }
};


/** Menu action extension, used as 'Move to Group' menu class. */
class UIActionMenuManagerCommonMoveToGroup : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCommonMoveToGroup(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/vm_group_create_16px.png", ":/vm_group_create_disabled_16px.png")
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Move to Gro&up"));
    }
};

/** Menu action extension, used as 'Start' menu class. */
class UIActionMenuManagerCommonStart : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCommonStart(UIActionPool *pParent)
        : UIActionMenu(pParent,
                       ":/vm_start_32px.png", ":/vm_start_16px.png",
                       ":/vm_start_disabled_32px.png", ":/vm_start_disabled_16px.png")
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "S&tart"));
        setStatusTip(QApplication::translate("UIActionPool", "Start selected virtual machines"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Menu action extension, used as 'Show' menu class. */
class UIActionSimpleManagerCommonShow : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonShow(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_start_32px.png", ":/vm_start_16px.png",
                         ":/vm_start_disabled_32px.png", ":/vm_start_disabled_16px.png")
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "S&how"));
        setStatusTip(QApplication::translate("UIActionPool", "Switch to the windows of selected virtual machines"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Normal Start' action class. */
class UIActionSimpleManagerCommonPerformStartNormal : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonPerformStartNormal(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_start_16px.png", ":/vm_start_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("StartVMNormal");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Start with &GUI"));
        setStatusTip(QApplication::translate("UIActionPool", "Start selected virtual machines with GUI"));
    }
};

/** Simple action extension, used as 'Perform Headless Start' action class. */
class UIActionSimpleManagerCommonPerformStartHeadless : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonPerformStartHeadless(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_start_headless_16px.png", ":/vm_start_headless_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("StartVMHeadless");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Start with&out GUI"));
        setStatusTip(QApplication::translate("UIActionPool", "Start selected virtual machines in the background"));
    }
};

/** Simple action extension, used as 'Perform Detachable Start' action class. */
class UIActionSimpleManagerCommonPerformStartDetachable : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonPerformStartDetachable(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_start_separate_16px.png", ":/vm_start_separate_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("StartVMDetachable");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Start with &detachable GUI"));
        setStatusTip(QApplication::translate("UIActionPool", "Start selected virtual machines with option of continuing in the background"));
    }
};

/** Toggle action extension, used as 'Pause and Resume' action class. */
class UIActionToggleManagerCommonPauseAndResume : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerCommonPauseAndResume(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/vm_pause_on_16px.png", ":/vm_pause_16px.png",
                         ":/vm_pause_on_disabled_16px.png", ":/vm_pause_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("PauseVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Pause"));
        setStatusTip(QApplication::translate("UIActionPool", "Suspend execution of selected virtual machines"));
    }
};

/** Simple action extension, used as 'Perform Reset' action class. */
class UIActionSimpleManagerCommonPerformReset : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonPerformReset(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_reset_16px.png", ":/vm_reset_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ResetVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Reset"));
        setStatusTip(QApplication::translate("UIActionPool", "Reset selected virtual machines"));
    }
};

/** Simple action extension, used as 'Perform Detach' action class. */
class UIActionSimpleManagerCommonPerformDetach : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonPerformDetach(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_create_shortcut_16px.png", ":/vm_create_shortcut_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("DetachUIVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Detach GUI"));
        setStatusTip(QApplication::translate("UIActionPool", "Detach the GUI from headless VM"));
    }
};

/** Simple menu action extension, used as 'Perform Discard' action class. */
class UIActionSimpleManagerCommonPerformDiscard : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonPerformDiscard(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_discard_32px.png", ":/vm_discard_16px.png",
                         ":/vm_discard_disabled_32px.png", ":/vm_discard_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("DiscardVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Discard"));
        setName(QApplication::translate("UIActionPool", "D&iscard Saved State..."));
        setStatusTip(QApplication::translate("UIActionPool", "Discard saved state of selected virtual machines"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Refresh' action class. */
class UIActionSimpleManagerCommonPerformRefresh : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonPerformRefresh(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/refresh_32px.png", ":/refresh_16px.png",
                         ":/refresh_disabled_32px.png", ":/refresh_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RefreshVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Re&fresh"));
        setStatusTip(QApplication::translate("UIActionPool", "Refresh accessibility state of selected virtual machines"));
    }
};

/** Simple action extension, used as 'Show in File Manager' action class. */
class UIActionSimpleManagerCommonShowInFileManager : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonShowInFileManager(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_open_filemanager_16px.png", ":/vm_open_filemanager_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ShowVMInFileManager");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
#if defined(VBOX_WS_MAC)
        setName(QApplication::translate("UIActionPool", "S&how in Finder"));
        setStatusTip(QApplication::translate("UIActionPool", "Show the VirtualBox Machine Definition files in Finder"));
#elif defined(VBOX_WS_WIN)
        setName(QApplication::translate("UIActionPool", "S&how in Explorer"));
        setStatusTip(QApplication::translate("UIActionPool", "Show the VirtualBox Machine Definition files in Explorer"));
#else
        setName(QApplication::translate("UIActionPool", "S&how in File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Show the VirtualBox Machine Definition files in the File Manager"));
#endif
    }
};

/** Simple action extension, used as 'Perform Create Shortcut' action class. */
class UIActionSimpleManagerCommonPerformCreateShortcut : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerCommonPerformCreateShortcut(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_create_shortcut_16px.png", ":/vm_create_shortcut_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("CreateVMAlias");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
#if defined(VBOX_WS_MAC)
        setName(QApplication::translate("UIActionPool", "Cr&eate Alias on Desktop"));
        setStatusTip(QApplication::translate("UIActionPool", "Create alias files to the VirtualBox Machine Definition files on your desktop"));
#else
        setName(QApplication::translate("UIActionPool", "Cr&eate Shortcut on Desktop"));
        setStatusTip(QApplication::translate("UIActionPool", "Create shortcut files to the VirtualBox Machine Definition files on your desktop"));
#endif
    }
};

/** Toggle action extension, used as 'Search' action class. */
class UIActionToggleManagerCommonToggleSearch : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerCommonToggleSearch(UIActionPool *pParent)
        : UIActionToggle(pParent,
                         ":/search_16px.png", ":/search_16px.png",
                         ":/search_16px.png", ":/search_16px.png") /// @todo use icons with check-boxes
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SearchVM");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+F");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "S&earch"));
        setStatusTip(QApplication::translate("UIActionPool", "Search virtual machines with respect to a search term"));
    }
};


/** Menu action extension, used as 'Console' menu class. */
class UIActionMenuManagerConsole : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerConsole(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/cloud_machine_console_16px.png")
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "C&onsole"));
    }
};

/** Simple action extension, used as 'Perform Create Console Connection' action class. */
class UIActionSimpleManagerConsolePerformCreateConnection : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerConsolePerformCreateConnection(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_machine_console_create_connection_16px.png",
                         ":/cloud_machine_console_create_connection_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("CreateConsoleConnection");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Create Connection"));
        setStatusTip(QApplication::translate("UIActionPool", "Create console connection to be able to use ssh/vnc clients"));
    }
};

/** Simple action extension, used as 'Perform Delete Console Connection' action class. */
class UIActionSimpleManagerConsolePerformDeleteConnection : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerConsolePerformDeleteConnection(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_machine_console_delete_connection_16px.png",
                         ":/cloud_machine_console_delete_connection_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("DeleteConsoleConnection");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Delete Connection"));
        setStatusTip(QApplication::translate("UIActionPool", "Delete console connection to disconnect ssh/vnc clients"));
    }
};

/** Simple action extension, used as 'Perform Configure Applications' action class. */
class UIActionSimpleManagerConsolePerformConfigureApplications : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerConsolePerformConfigureApplications(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_machine_console_configure_external_terminal_16px.png",
                         ":/cloud_machine_console_configure_external_terminal_disabled_16px.png")
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_CloudConsole));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ConfigureConsoleApplications");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Configure Console Applications"));
        setStatusTip(QApplication::translate("UIActionPool", "Open configuration dialog to edit console application settings"));
    }
};

/** Simple action extension, used as 'Copy Command' action class. */
class UIActionSimpleManagerConsolePerformCopyCommand : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerConsolePerformCopyCommand(UIActionPool *pParent, bool fSerial, bool fUnix)
        : UIActionSimple(pParent)
        , m_fSerial(fSerial)
        , m_fUnix(fUnix)
    {
        if (m_fSerial)
            setIcon(UIIconPool::iconSet(":/cloud_machine_console_get_serial_console_command_16px.png",
                                        ":/cloud_machine_console_get_serial_console_command_disabled_16px.png"));
        else
            setIcon(UIIconPool::iconSet(":/cloud_machine_console_get_vnc_console_command_16px.png",
                                        ":/cloud_machine_console_get_vnc_console_command_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return   m_fSerial
               ? QString("CopyConsoleCommandSerial")
               : QString("CopyConsoleCommandVNC");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        if (m_fSerial)
        {
            if (m_fUnix)
                setName(QApplication::translate("UIActionPool", "&Copy Command (serial) for Unix"));
            else
                setName(QApplication::translate("UIActionPool", "&Copy Command (serial) for Windows"));
            setStatusTip(QApplication::translate("UIActionPool", "Copy console command for serial connection"));
        }
        else
        {
            if (m_fUnix)
                setName(QApplication::translate("UIActionPool", "&Copy Command (VNC) for Unix"));
            else
                setName(QApplication::translate("UIActionPool", "&Copy Command (VNC) for Windows"));
            setStatusTip(QApplication::translate("UIActionPool", "Copy console command for VNC connection"));
        }
    }

private:

    /** Holds whether this command is of serial type. */
    bool  m_fSerial;
    /** Holds whether this command is for unix. */
    bool  m_fUnix;
};

/** Simple action extension, used as 'Show Log' action class. */
class UIActionSimpleManagerConsolePerformShowLog : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerConsolePerformShowLog(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_show_logs_16px.png",
                         ":/vm_show_logs_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ShowConsoleLog");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Show &Log"));
        setStatusTip(QApplication::translate("UIActionPool", "Show cloud console log"));
    }
};


/** Menu action extension, used as 'Stop' menu class. */
class UIActionMenuManagerStop : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerStop(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/exit_16px.png")
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Stop"));
    }
};

/** Simple action extension, used as 'Perform Save' action class. */
class UIActionSimpleManagerStopPerformSave : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerStopPerformSave(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_save_state_16px.png", ":/vm_save_state_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SaveVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Save State"));
        setStatusTip(QApplication::translate("UIActionPool", "Save state of selected virtual machines"));
    }
};

/** Simple action extension, used as 'Perform Terminate' action class. */
class UIActionSimpleManagerStopPerformTerminate : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerStopPerformTerminate(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_discard_16px.png", ":/vm_discard_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TerminateVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Terminate"));
        setName(QApplication::translate("UIActionPool", "&Terminate Cloud Instance..."));
        setStatusTip(QApplication::translate("UIActionPool", "Terminate cloud instance of selected virtual machines"));
        setToolTip(simplifyText(text()) + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Shutdown' action class. */
class UIActionSimpleManagerStopPerformShutdown : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerStopPerformShutdown(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_shutdown_16px.png", ":/vm_shutdown_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ACPIShutdownVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Sh&ut Down"));
        setStatusTip(QApplication::translate("UIActionPool", "Send ACPI shutdown signal to selected virtual machines"));
    }
};

/** Simple action extension, used as 'Perform PowerOff' action class. */
class UIActionSimpleManagerStopPerformPowerOff : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerStopPerformPowerOff(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/vm_poweroff_16px.png", ":/vm_poweroff_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("PowerOffVM");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Po&wer Off"));
        setStatusTip(QApplication::translate("UIActionPool", "Power off selected virtual machines"));
    }
};


/** Menu action extension, used as 'Machine Tools' menu class. */
class UIActionMenuManagerToolsMachine : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerToolsMachine(UIActionPool *pParent)
        : UIActionMenu(pParent, ":/tools_menu_24px.png") /// @todo replace with 16px icon
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Tools"));
    }
};

/** Simple action extension, used as 'Show Machine Details' action class. */
class UIActionToggleManagerToolsMachineShowDetails : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsMachineShowDetails(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Details));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/machine_details_manager_24px.png", ":/machine_details_manager_16px.png",
                                        ":/machine_details_manager_disabled_24px.png", ":/machine_details_manager_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToolsMachineDetails");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Details"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the machine details pane"));
    }
};

/** Simple action extension, used as 'Show Machine Snapshots' action class. */
class UIActionToggleManagerToolsMachineShowSnapshots : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsMachineShowSnapshots(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Snapshots));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/snapshot_manager_24px.png", ":/snapshot_manager_16px.png",
                                        ":/snapshot_manager_disabled_24px.png", ":/snapshot_manager_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToolsMachineSnapshots");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Snapshots"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the machine snapshots pane"));
    }
};

/** Simple action extension, used as 'Show Machine Logs' action class. */
class UIActionToggleManagerToolsMachineShowLogs : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsMachineShowLogs(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_Logs));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/vm_show_logs_32px.png", ":/vm_show_logs_16px.png",
                                        ":/vm_show_logs_disabled_32px.png", ":/vm_show_logs_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToolsMachineLogViewer");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+L");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Logs"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the machine logs pane"));
    }
};

/** Simple action extension, used as 'Show VM Activity Monitor' action class. */
class UIActionToggleManagerToolsMachineShowActivity : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsMachineShowActivity(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_VMActivity));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/performance_monitor_32px.png", ":/performance_monitor_16px.png",
                                        ":/performance_monitor_disabled_32px.png", ":/performance_monitor_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToolsMachineVMActivityMonitor");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Activity"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the machine activity monitor pane"));
    }
};

/** Simple action extension, used as 'Show File Manager' action class. */
class UIActionToggleManagerToolsMachineShowFileManager : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleManagerToolsMachineShowFileManager(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setProperty("UIToolType", QVariant::fromValue(UIToolType_FileManager));
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/file_manager_24px.png", ":/file_manager_16px.png",
                                        ":/file_manager_disabled_24px.png", ":/file_manager_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToolsMachineFileManager");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the File Manager"));
    }
};


/** Menu action extension, used as 'Snapshot' menu class. */
class UIActionMenuManagerSnapshot : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerSnapshot(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Snapshot"));
    }
};

/** Simple action extension, used as 'Perform Take' action class. */
class UIActionMenuManagerSnapshotPerformTake : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerSnapshotPerformTake(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/snapshot_take_32px.png", ":/snapshot_take_16px.png",
                         ":/snapshot_take_disabled_32px.png", ":/snapshot_take_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("TakeSnapshot");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+T");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Take..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Snapshot Pane"));
        setStatusTip(QApplication::translate("UIActionPool", "Take a snapshot of the current virtual machine state"));
        setToolTip(  QApplication::translate("UIActionPool", "Take Snapshot")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Delete' action class. */
class UIActionMenuManagerSnapshotPerformDelete : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerSnapshotPerformDelete(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/snapshot_delete_32px.png", ":/snapshot_delete_16px.png",
                         ":/snapshot_delete_disabled_32px.png", ":/snapshot_delete_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("DeleteSnapshot");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+D");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Delete..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Snapshot Pane"));
        setStatusTip(QApplication::translate("UIActionPool", "Delete selected snapshot of the virtual machine"));
        setToolTip(  QApplication::translate("UIActionPool", "Delete Snapshot")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Restore' action class. */
class UIActionMenuManagerSnapshotPerformRestore : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerSnapshotPerformRestore(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/snapshot_restore_32px.png", ":/snapshot_restore_16px.png",
                         ":/snapshot_restore_disabled_32px.png", ":/snapshot_restore_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RestoreSnapshot");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+R");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Restore..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Snapshot Pane"));
        setStatusTip(QApplication::translate("UIActionPool", "Restore selected snapshot of the virtual machine"));
        setToolTip(  QApplication::translate("UIActionPool", "Restore Snapshot")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used as 'Toggle Snapshot Properties' action class. */
class UIActionMenuManagerSnapshotToggleProperties : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerSnapshotToggleProperties(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/snapshot_show_details_32px.png", ":/snapshot_show_details_16px.png",
                                        ":/snapshot_show_details_disabled_32px.png", ":/snapshot_show_details_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleSnapshotProperties");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+P");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Properties"));
        setShortcutScope(QApplication::translate("UIActionPool", "Snapshot Pane"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with the selected snapshot properties"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Snapshot Properties")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Clone' action class. */
class UIActionMenuManagerSnapshotPerformClone : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerSnapshotPerformClone(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/vm_clone_32px.png", ":/vm_clone_16px.png",
                         ":/vm_clone_disabled_32px.png", ":/vm_clone_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("CloneSnapshot");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+C");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Clone..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Snapshot Pane"));
        setStatusTip(QApplication::translate("UIActionPool", "Clone selected virtual machine"));
        setToolTip(  QApplication::translate("UIActionPool", "Clone Virtual Machine")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};


/** Menu action extension, used as 'Extension' menu class. */
class UIActionMenuManagerExtension : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerExtension(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Extension"));
    }
};

/** Simple action extension, used as 'Perform Install' action class. */
class UIActionSimpleManagerExtensionPerformInstall : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerExtensionPerformInstall(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/extension_pack_install_32px.png",          ":/extension_pack_install_16px.png",
                         ":/extension_pack_install_disabled_32px.png", ":/extension_pack_install_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("InstallExtension");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+I");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Install..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Extension Pack Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Install extension pack"));
        setToolTip(  QApplication::translate("UIActionPool", "Install Extension Pack")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Uninstall' action class. */
class UIActionSimpleManagerExtensionPerformUninstall : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleManagerExtensionPerformUninstall(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/extension_pack_uninstall_32px.png",          ":/extension_pack_uninstall_16px.png",
                         ":/extension_pack_uninstall_disabled_32px.png", ":/extension_pack_uninstall_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("UninstallExtension");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+U");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Uninstall..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Extension Pack Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Uninstall selected extension pack"));
        setToolTip(  QApplication::translate("UIActionPool", "Uninstall Extension Pack")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};


/** Menu action extension, used as 'Medium' menu class. */
class UIActionMenuManagerMedium : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMedium(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Medium"));
    }
};

/** Simple action extension, used as 'Perform Add' action class. */
class UIActionMenuManagerMediumPerformAdd : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumPerformAdd(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(0, UIIconPool::iconSetFull(":/hd_add_32px.png",          ":/hd_add_16px.png",
                                           ":/hd_add_disabled_32px.png", ":/hd_add_disabled_16px.png"));
        setIcon(1, UIIconPool::iconSetFull(":/cd_add_32px.png",          ":/cd_add_16px.png",
                                           ":/cd_add_disabled_32px.png", ":/cd_add_disabled_16px.png"));
        setIcon(2, UIIconPool::iconSetFull(":/fd_add_32px.png",          ":/fd_add_16px.png",
                                           ":/fd_add_disabled_32px.png", ":/fd_add_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("AddMedium");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+A");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Add..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Add a disk image"));
        setToolTip(  QApplication::translate("UIActionPool", "Add Disk Image")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Create' action class. */
class UIActionMenuManagerMediumPerformCreate : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumPerformCreate(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(0, UIIconPool::iconSetFull(":/hd_create_32px.png",          ":/hd_create_16px.png",
                                           ":/hd_create_disabled_32px.png", ":/hd_create_disabled_16px.png"));
        setIcon(1, UIIconPool::iconSetFull(":/cd_create_32px.png",          ":/cd_create_16px.png",
                                           ":/cd_create_disabled_32px.png", ":/cd_create_disabled_16px.png"));
        setIcon(2, UIIconPool::iconSetFull(":/fd_create_32px.png",          ":/fd_create_16px.png",
                                           ":/fd_create_disabled_32px.png", ":/fd_create_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("CreateMedium");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Create..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Create a new disk image"));
        setToolTip(  QApplication::translate("UIActionPool", "Create Disk Image")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Copy' action class. */
class UIActionMenuManagerMediumPerformCopy : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumPerformCopy(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(0, UIIconPool::iconSetFull(":/hd_copy_32px.png",          ":/hd_copy_16px.png",
                                           ":/hd_copy_disabled_32px.png", ":/hd_copy_disabled_16px.png"));
        setIcon(1, UIIconPool::iconSetFull(":/cd_copy_32px.png",          ":/cd_copy_16px.png",
                                           ":/cd_copy_disabled_32px.png", ":/cd_copy_disabled_16px.png"));
        setIcon(2, UIIconPool::iconSetFull(":/fd_copy_32px.png",          ":/fd_copy_16px.png",
                                           ":/fd_copy_disabled_32px.png", ":/fd_copy_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("CopyMedium");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+C");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Copy..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Copy selected disk image"));
        setToolTip(  QApplication::translate("UIActionPool", "Copy Disk Image")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Move' action class. */
class UIActionMenuManagerMediumPerformMove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumPerformMove(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(0, UIIconPool::iconSetFull(":/hd_move_32px.png",          ":/hd_move_16px.png",
                                           ":/hd_move_disabled_32px.png", ":/hd_move_disabled_16px.png"));
        setIcon(1, UIIconPool::iconSetFull(":/cd_move_32px.png",          ":/cd_move_16px.png",
                                           ":/cd_move_disabled_32px.png", ":/cd_move_disabled_16px.png"));
        setIcon(2, UIIconPool::iconSetFull(":/fd_move_32px.png",          ":/fd_move_16px.png",
                                           ":/fd_move_disabled_32px.png", ":/fd_move_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MoveMedium");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+M");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Move..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Move selected disk image"));
        setToolTip(  QApplication::translate("UIActionPool", "Move Disk Image")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Remove' action class. */
class UIActionMenuManagerMediumPerformRemove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumPerformRemove(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(0, UIIconPool::iconSetFull(":/hd_remove_32px.png",          ":/hd_remove_16px.png",
                                           ":/hd_remove_disabled_32px.png", ":/hd_remove_disabled_16px.png"));
        setIcon(1, UIIconPool::iconSetFull(":/cd_remove_32px.png",          ":/cd_remove_16px.png",
                                           ":/cd_remove_disabled_32px.png", ":/cd_remove_disabled_16px.png"));
        setIcon(2, UIIconPool::iconSetFull(":/fd_remove_32px.png",          ":/fd_remove_16px.png",
                                           ":/fd_remove_disabled_32px.png", ":/fd_remove_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RemoveMedium");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+R");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Remove..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Remove selected disk image"));
        setToolTip(  QApplication::translate("UIActionPool", "Remove Disk Image")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Release' action class. */
class UIActionMenuManagerMediumPerformRelease : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumPerformRelease(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(0, UIIconPool::iconSetFull(":/hd_release_32px.png",          ":/hd_release_16px.png",
                                           ":/hd_release_disabled_32px.png", ":/hd_release_disabled_16px.png"));
        setIcon(1, UIIconPool::iconSetFull(":/cd_release_32px.png",          ":/cd_release_16px.png",
                                           ":/cd_release_disabled_32px.png", ":/cd_release_disabled_16px.png"));
        setIcon(2, UIIconPool::iconSetFull(":/fd_release_32px.png",          ":/fd_release_16px.png",
                                           ":/fd_release_disabled_32px.png", ":/fd_release_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ReleaseMedium");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+L");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Re&lease..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Release selected disk image"));
        setToolTip(  QApplication::translate("UIActionPool", "Release Disk Image")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used as 'Toggle Medium Properties' action class. */
class UIActionMenuManagerMediumToggleProperties : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumToggleProperties(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        /// @todo use icons with check-boxes
        setIcon(0, UIIconPool::iconSetFull(":/hd_modify_32px.png",          ":/hd_modify_16px.png",
                                           ":/hd_modify_disabled_32px.png", ":/hd_modify_disabled_16px.png"));
        setIcon(1, UIIconPool::iconSetFull(":/cd_modify_32px.png",          ":/cd_modify_16px.png",
                                           ":/cd_modify_disabled_32px.png", ":/cd_modify_disabled_16px.png"));
        setIcon(2, UIIconPool::iconSetFull(":/fd_modify_32px.png",          ":/fd_modify_16px.png",
                                           ":/fd_modify_disabled_32px.png", ":/fd_modify_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleMediumProperties");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+P");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Properties"));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with selected disk image properties"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Disk Image Properties")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used as 'Toggle Search Pane' action class. */
class UIActionMenuManagerMediumToggleSearch : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumToggleSearch(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        /// @todo use icons with check-boxes
        setIcon(0, UIIconPool::iconSetFull(":/hd_search_32px.png",          ":/hd_search_16px.png",
                                           ":/hd_search_disabled_32px.png", ":/hd_search_disabled_16px.png"));
        setIcon(1, UIIconPool::iconSetFull(":/cd_search_32px.png",          ":/cd_search_16px.png",
                                           ":/cd_search_disabled_32px.png", ":/cd_search_disabled_16px.png"));
        setIcon(2, UIIconPool::iconSetFull(":/fd_search_32px.png",          ":/fd_search_16px.png",
                                           ":/fd_search_disabled_32px.png", ":/fd_search_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleMediumSearch");
    }

    /** Returns standard shortcut. */
    virtual QKeySequence standardShortcut(UIType) const RT_OVERRIDE
    {
        return actionPool()->isTemporary() ? QKeySequence() : QKeySequence(QKeySequence::Find);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Search"));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the disk image search pane"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Disk Image Search Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Refresh' action class. */
class UIActionMenuManagerMediumPerformRefresh : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumPerformRefresh(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/refresh_32px.png",          ":/refresh_16px.png",
                         ":/refresh_disabled_32px.png", ":/refresh_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RefreshMedia");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+F");
    }

    /** Returns standard shortcut. */
    virtual QKeySequence standardShortcut(UIType) const RT_OVERRIDE
    {
        return actionPool()->isTemporary() ? QKeySequence() : QKeySequence(QKeySequence::Refresh);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Re&fresh..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Refresh the list of disk images"));
        setToolTip(  QApplication::translate("UIActionPool", "Refresh Disk Images")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Clear' action class. */
class UIActionMenuManagerMediumPerformClear : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumPerformClear(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(1, UIIconPool::iconSetFull(":/cd_clear_32px.png",          ":/cd_clear_16px.png",
                                           ":/cd_clear_disabled_32px.png", ":/cd_clear_disabled_16px.png"));
        setIcon(2, UIIconPool::iconSetFull(":/fd_clear_32px.png",          ":/fd_clear_16px.png",
                                           ":/fd_clear_disabled_32px.png", ":/fd_clear_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Clear");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Clear"));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Remove all inaccessible media"));
        setToolTip(  QApplication::translate("UIActionPool", "Remove Inaccessible Media")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Edit' action class. */
class UIActionMenuManagerMediumPerformEdit : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerMediumPerformEdit(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(1, UIIconPool::iconSetFull(":/cd_edit_32px.png",          ":/cd_edit_16px.png",
                                           ":/cd_edit_disabled_32px.png", ":/cd_edit_disabled_16px.png"));

    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Edit");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Edit"));
        setShortcutScope(QApplication::translate("UIActionPool", "Media Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Edit selected media"));
        setToolTip(  QApplication::translate("UIActionPool", "Edit Media")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Menu action extension, used as 'Network' menu class. */
class UIActionMenuManagerNetwork : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerNetwork(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Network"));
    }
};

/** Simple action extension, used as 'Perform Create' action class. */
class UIActionMenuManagerNetworkPerformCreate : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerNetworkPerformCreate(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/host_iface_add_32px.png",          ":/host_iface_add_16px.png",
                         ":/host_iface_add_disabled_32px.png", ":/host_iface_add_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("CreateNetwork");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+C");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Create..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Network Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Create new host-only network"));
        setToolTip(  QApplication::translate("UIActionPool", "Create Host-only Network")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Remove' action class. */
class UIActionMenuManagerNetworkPerformRemove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerNetworkPerformRemove(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/host_iface_remove_32px.png",          ":/host_iface_remove_16px.png",
                         ":/host_iface_remove_disabled_32px.png", ":/host_iface_remove_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RemoveNetwork");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+R");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Remove..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Network Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Remove selected host-only network"));
        setToolTip(  QApplication::translate("UIActionPool", "Remove Host-only Network")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used as 'Toggle Network Properties' action class. */
class UIActionMenuManagerNetworkToggleProperties : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerNetworkToggleProperties(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/host_iface_edit_32px.png",          ":/host_iface_edit_16px.png",
                                        ":/host_iface_edit_disabled_32px.png", ":/host_iface_edit_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleNetworkProperties");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+P");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Properties"));
        setShortcutScope(QApplication::translate("UIActionPool", "Network Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with selected host-only network properties"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Host-only Network Properties")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Refresh' action class. */
class UIActionMenuManagerNetworkPerformRefresh : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerNetworkPerformRefresh(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/refresh_32px.png",          ":/refresh_16px.png",
                         ":/refresh_disabled_32px.png", ":/refresh_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RefreshNetworks");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+F");
    }

    /** Returns standard shortcut. */
    virtual QKeySequence standardShortcut(UIType) const RT_OVERRIDE
    {
        return actionPool()->isTemporary() ? QKeySequence() : QKeySequence(QKeySequence::Refresh);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Re&fresh..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Network Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Refresh the list of host-only networks"));
        setToolTip(  QApplication::translate("UIActionPool", "Refresh Host-only Networks")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};


/** Menu action extension, used as 'Cloud' menu class. */
class UIActionMenuManagerCloud : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloud(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Cloud"));
    }
};

/** Simple action extension, used as 'Perform Add' action class. */
class UIActionMenuManagerCloudPerformAdd : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudPerformAdd(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_profile_add_32px.png",          ":/cloud_profile_add_16px.png",
                         ":/cloud_profile_add_disabled_32px.png", ":/cloud_profile_add_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("AddCloudProfile");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+A");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Add"));
        setName(QApplication::translate("UIActionPool", "&Add Profile..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Profile Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Add new cloud profile"));
        setToolTip(  QApplication::translate("UIActionPool", "Add Cloud Profile")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Import' action class. */
class UIActionMenuManagerCloudPerformImport : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudPerformImport(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_profile_restore_32px.png",          ":/cloud_profile_restore_16px.png",
                         ":/cloud_profile_restore_disabled_32px.png", ":/cloud_profile_restore_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ImportCloudProfiles");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+I");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Import"));
        setName(QApplication::translate("UIActionPool", "&Import Profiles..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Profile Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Import the list of cloud profiles from external files"));
        setToolTip(  QApplication::translate("UIActionPool", "Import Cloud Profiles")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Remove' action class. */
class UIActionMenuManagerCloudPerformRemove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudPerformRemove(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_profile_remove_32px.png",          ":/cloud_profile_remove_16px.png",
                         ":/cloud_profile_remove_disabled_32px.png", ":/cloud_profile_remove_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RemoveCloudProfile");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+R");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Remove"));
        setName(QApplication::translate("UIActionPool", "&Remove Profile..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Profile Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Remove selected cloud profile"));
        setToolTip(  QApplication::translate("UIActionPool", "Remove Cloud Profile")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used as 'Toggle Properties' action class. */
class UIActionMenuManagerCloudToggleProperties : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudToggleProperties(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/cloud_profile_edit_32px.png",          ":/cloud_profile_edit_16px.png",
                                        ":/cloud_profile_edit_disabled_32px.png", ":/cloud_profile_edit_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleCloudProfileProperties");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+P");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Properties"));
        setName(QApplication::translate("UIActionPool", "Profile &Properties"));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Profile Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with selected cloud profile properties"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Cloud Profile Properties")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Try Page' action class. */
class UIActionMenuManagerCloudShowTryPage : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudShowTryPage(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_profile_try_32px.png",          ":/cloud_profile_try_16px.png",
                         ":/cloud_profile_try_disabled_32px.png", ":/cloud_profile_try_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ShowCloudProfileTryPage");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+T");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Try"));
        setName(QApplication::translate("UIActionPool", "&Try Oracle Cloud for Free..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Profile Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Try Oracle cloud for free"));
        setToolTip(  QApplication::translate("UIActionPool", "Try Oracle Cloud for Free")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Show Help' action class. */
class UIActionMenuManagerCloudShowHelp : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudShowHelp(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_profile_help_32px.png",          ":/cloud_profile_help_16px.png",
                         ":/cloud_profile_help_disabled_32px.png", ":/cloud_profile_help_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ShowCloudProfileHelp");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+H");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Help"));
        setName(QApplication::translate("UIActionPool", "&Show Help..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Profile Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Show cloud profile help"));
        setToolTip(  QApplication::translate("UIActionPool", "Show Cloud Profile Help")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};


/** Menu action extension, used as 'Cloud Console' menu class. */
class UIActionMenuManagerCloudConsole : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudConsole(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Console"));
    }
};

/** Simple action extension, used as 'Perform Console Application Add' action class. */
class UIActionMenuManagerCloudConsolePerformApplicationAdd : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudConsolePerformApplicationAdd(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_console_application_add_32px.png",          ":/cloud_console_application_add_16px.png",
                         ":/cloud_console_application_add_disabled_32px.png", ":/cloud_console_application_add_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("AddCloudConsoleApplication");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Add Application..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Console Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Add new cloud console application"));
        setToolTip(  QApplication::translate("UIActionPool", "Add Cloud Console Application")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Console Application Remove' action class. */
class UIActionMenuManagerCloudConsolePerformApplicationRemove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudConsolePerformApplicationRemove(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_console_application_remove_32px.png",          ":/cloud_console_application_remove_16px.png",
                         ":/cloud_console_application_remove_disabled_32px.png", ":/cloud_console_application_remove_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RemoveCloudConsoleApplication");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Remove Application..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Console Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Remove selected cloud console application"));
        setToolTip(  QApplication::translate("UIActionPool", "Remove Cloud Console Application")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Console Profile Add' action class. */
class UIActionMenuManagerCloudConsolePerformProfileAdd : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudConsolePerformProfileAdd(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_console_profile_add_32px.png",          ":/cloud_console_profile_add_16px.png",
                         ":/cloud_console_profile_add_disabled_32px.png", ":/cloud_console_profile_add_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("AddCloudConsoleProfile");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Add Profile..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Console Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Add new cloud console profile"));
        setToolTip(  QApplication::translate("UIActionPool", "Add Cloud Console Profile")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Console Profile Remove' action class. */
class UIActionMenuManagerCloudConsolePerformProfileRemove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudConsolePerformProfileRemove(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cloud_console_profile_remove_32px.png",          ":/cloud_console_profile_remove_16px.png",
                         ":/cloud_console_profile_remove_disabled_32px.png", ":/cloud_console_profile_remove_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RemoveCloudConsoleProfile");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Remove Profile..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Console Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Remove selected cloud console profile"));
        setToolTip(  QApplication::translate("UIActionPool", "Remove Cloud Console Profile")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used as 'Toggle Cloud Console Properties' action class. */
class UIActionMenuManagerCloudConsoleToggleProperties : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerCloudConsoleToggleProperties(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        /// @todo use icons with check-boxes
        setIcon(UIIconPool::iconSetFull(":/cloud_console_edit_32px.png",          ":/cloud_console_edit_16px.png",
                                        ":/cloud_console_edit_disabled_32px.png", ":/cloud_console_edit_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleCloudConsoleProperties");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+P");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setIconText(QApplication::translate("UIActionPool", "Properties"));
        setName(QApplication::translate("UIActionPool", "Console &Properties"));
        setShortcutScope(QApplication::translate("UIActionPool", "Cloud Console Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with selected cloud console properties"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Cloud Console Properties")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};


/** Menu action extension, used as 'Resources' menu class. */
class UIActionMenuVMActivityOverview : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuVMActivityOverview(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Resources"));
    }
};

/** Menu action extension, used as 'Columns' menu class. */
class UIActionMenuManagerVMActivityOverviewColumns : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerVMActivityOverviewColumns(UIActionPool *pParent)
        : UIActionMenu(pParent,
                       ":/resources_monitor_columns_32px.png", ":/resources_monitor_columns_16px.png",
                       ":/resources_monitor_columns_disabled_32px.png", ":/resources_monitor_columns_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Columns"));
        setShortcutScope(QApplication::translate("UIActionPool", "VM Activity Overview"));
        setStatusTip(QApplication::translate("UIActionPool", "Show/Hide Columns"));
        setToolTip(  QApplication::translate("UIActionPool", "Show/Hide Columns")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Switch to Machine Activity' action class. */
class UIActionMenuManagerVMActivityOverviewSwitchToMachineActivity : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuManagerVMActivityOverviewSwitchToMachineActivity(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/resources_monitor_jump_to_vm_32px.png",          ":/resources_monitor_jump_to_vm_16px.png",
                         ":/resources_monitor_jump_to_vm_disabled_32px.png", ":/resources_monitor_jump_to_vm_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VMActivityOverviewSwitchToMachineActivity");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "VM Activity"));
        setShortcutScope(QApplication::translate("UIActionPool", "VM Activity Overview"));
        setStatusTip(QApplication::translate("UIActionPool", "View activity for selected virtual machine"));
        setToolTip(  QApplication::translate("UIActionPool", "View activity for selected virtual machine")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};


/*********************************************************************************************************************************
*   Class UIActionPoolManager implementation.                                                                                    *
*********************************************************************************************************************************/

UIActionPoolManager::UIActionPoolManager(bool fTemporary /* = false */)
    : UIActionPool(UIType_ManagerUI, fTemporary)
{
}

void UIActionPoolManager::preparePool()
{
    /* 'File' actions: */
    m_pool[UIActionIndexMN_M_File] = new UIActionMenuManagerFile(this);
    m_pool[UIActionIndexMN_M_File_S_ImportAppliance] = new UIActionSimpleManagerFileShowImportApplianceWizard(this);
    m_pool[UIActionIndexMN_M_File_S_ExportAppliance] = new UIActionSimpleManagerFileShowExportApplianceWizard(this);
    m_pool[UIActionIndexMN_M_File_M_Tools] = new UIActionMenuManagerToolsGlobal(this);
    m_pool[UIActionIndexMN_M_File_M_Tools_T_HomeScreen] = new UIActionToggleManagerToolsGlobalShowHomeScreen(this);
    m_pool[UIActionIndexMN_M_File_M_Tools_T_MachineManager] = new UIActionToggleManagerToolsGlobalShowMachineManager(this);
    m_pool[UIActionIndexMN_M_File_M_Tools_T_ExtensionPackManager] = new UIActionToggleManagerToolsGlobalShowExtensionPackManager(this);
    m_pool[UIActionIndexMN_M_File_M_Tools_T_VirtualMediaManager] = new UIActionToggleManagerToolsGlobalShowVirtualMediaManager(this);
    m_pool[UIActionIndexMN_M_File_M_Tools_T_NetworkManager] = new UIActionToggleManagerToolsGlobalShowNetworkManager(this);
    m_pool[UIActionIndexMN_M_File_M_Tools_T_CloudProfileManager] = new UIActionToggleManagerToolsGlobalShowCloudProfileManager(this);
    m_pool[UIActionIndexMN_M_File_M_Tools_T_VMActivityOverview] = new UIActionToggleManagerToolsGlobalShowVMActivityOverview(this);
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    m_pool[UIActionIndexMN_M_File_S_ShowExtraDataManager] = new UIActionSimpleManagerFileShowExtraDataManager(this);
#endif
    m_pool[UIActionIndexMN_M_File_S_Close] = new UIActionSimpleManagerFilePerformExit(this);

    /* 'Home' actions: */
    m_pool[UIActionIndexMN_M_Home] = new UIActionMenuManagerMachine(this);
    m_pool[UIActionIndexMN_M_Home_S_New] = new UIActionSimpleManagerMachinePerformCreate(this);
    m_pool[UIActionIndexMN_M_Home_S_Add] = new UIActionSimpleManagerMachinePerformAdd(this);

    /* 'Group' actions: */
    m_pool[UIActionIndexMN_M_Group] = new UIActionMenuManagerGroup(this);
    m_pool[UIActionIndexMN_M_Group_S_New] = new UIActionSimpleManagerGroupPerformCreateMachine(this);
    m_pool[UIActionIndexMN_M_Group_S_Add] = new UIActionSimpleManagerGroupPerformAddMachine(this);
    m_pool[UIActionIndexMN_M_Group_S_Rename] = new UIActionSimpleManagerGroupPerformRename(this);
    m_pool[UIActionIndexMN_M_Group_S_Remove] = new UIActionSimpleManagerGroupPerformRemove(this);
    m_pool[UIActionIndexMN_M_Group_M_MoveToGroup] = new UIActionMenuManagerCommonMoveToGroup(this);
    m_pool[UIActionIndexMN_M_Group_M_Start] = new UIActionMenuManagerCommonStart(this);
    m_pool[UIActionIndexMN_M_Group_M_Start_S_Normal] = new UIActionSimpleManagerCommonPerformStartNormal(this);
    m_pool[UIActionIndexMN_M_Group_M_Start_S_Headless] = new UIActionSimpleManagerCommonPerformStartHeadless(this);
    m_pool[UIActionIndexMN_M_Group_M_Start_S_Detachable] = new UIActionSimpleManagerCommonPerformStartDetachable(this);
    m_pool[UIActionIndexMN_M_Group_S_Show] = new UIActionSimpleManagerCommonShow(this);
    m_pool[UIActionIndexMN_M_Group_T_Pause] = new UIActionToggleManagerCommonPauseAndResume(this);
    m_pool[UIActionIndexMN_M_Group_S_Reset] = new UIActionSimpleManagerCommonPerformReset(this);
    m_pool[UIActionIndexMN_M_Group_S_Detach] = new UIActionSimpleManagerCommonPerformDetach(this);
    m_pool[UIActionIndexMN_M_Group_M_Console] = new UIActionMenuManagerConsole(this);
    m_pool[UIActionIndexMN_M_Group_M_Console_S_CreateConnection] = new UIActionSimpleManagerConsolePerformCreateConnection(this);
    m_pool[UIActionIndexMN_M_Group_M_Console_S_DeleteConnection] = new UIActionSimpleManagerConsolePerformDeleteConnection(this);
    m_pool[UIActionIndexMN_M_Group_M_Console_S_ConfigureApplications] = new UIActionSimpleManagerConsolePerformConfigureApplications(this);
    m_pool[UIActionIndexMN_M_Group_M_Stop] = new UIActionMenuManagerStop(this);
    m_pool[UIActionIndexMN_M_Group_M_Stop_S_SaveState] = new UIActionSimpleManagerStopPerformSave(this);
    m_pool[UIActionIndexMN_M_Group_M_Stop_S_Terminate] = new UIActionSimpleManagerStopPerformTerminate(this);
    m_pool[UIActionIndexMN_M_Group_M_Stop_S_Shutdown] = new UIActionSimpleManagerStopPerformShutdown(this);
    m_pool[UIActionIndexMN_M_Group_M_Stop_S_PowerOff] = new UIActionSimpleManagerStopPerformPowerOff(this);
    m_pool[UIActionIndexMN_M_Group_M_Tools] = new UIActionMenuManagerToolsMachine(this);
    m_pool[UIActionIndexMN_M_Group_M_Tools_T_Details] = new UIActionToggleManagerToolsMachineShowDetails(this);
    m_pool[UIActionIndexMN_M_Group_M_Tools_T_Snapshots] = new UIActionToggleManagerToolsMachineShowSnapshots(this);
    m_pool[UIActionIndexMN_M_Group_M_Tools_T_Logs] = new UIActionToggleManagerToolsMachineShowLogs(this);
    m_pool[UIActionIndexMN_M_Group_M_Tools_T_Activity] = new UIActionToggleManagerToolsMachineShowActivity(this);
    m_pool[UIActionIndexMN_M_Group_M_Tools_T_FileManager] = new UIActionToggleManagerToolsMachineShowFileManager(this);
    m_pool[UIActionIndexMN_M_Group_S_Discard] = new UIActionSimpleManagerCommonPerformDiscard(this);
    m_pool[UIActionIndexMN_M_Group_S_Refresh] = new UIActionSimpleManagerCommonPerformRefresh(this);
    m_pool[UIActionIndexMN_M_Group_S_ShowInFileManager] = new UIActionSimpleManagerCommonShowInFileManager(this);
    m_pool[UIActionIndexMN_M_Group_S_CreateShortcut] = new UIActionSimpleManagerCommonPerformCreateShortcut(this);
    m_pool[UIActionIndexMN_M_Group_S_Sort] = new UIActionSimpleManagerGroupPerformSort(this);
    m_pool[UIActionIndexMN_M_Group_T_Search] = new UIActionToggleManagerCommonToggleSearch(this);

    /* 'Machine' actions: */
    m_pool[UIActionIndexMN_M_Machine] = new UIActionMenuManagerMachine(this);
    m_pool[UIActionIndexMN_M_Machine_S_New] = new UIActionSimpleManagerMachinePerformCreate(this);
    m_pool[UIActionIndexMN_M_Machine_S_Add] = new UIActionSimpleManagerMachinePerformAdd(this);
    m_pool[UIActionIndexMN_M_Machine_S_Settings] = new UIActionSimpleManagerMachineShowSettings(this);
    m_pool[UIActionIndexMN_M_Machine_S_Clone] = new UIActionSimpleManagerMachinePerformClone(this);
    m_pool[UIActionIndexMN_M_Machine_S_Move] = new UIActionSimpleManagerMachinePerformMove(this);
    m_pool[UIActionIndexMN_M_Machine_S_ExportToOCI] = new UIActionSimpleManagerMachinePerformExportToOCI(this);
    m_pool[UIActionIndexMN_M_Machine_S_Remove] = new UIActionSimpleManagerMachinePerformRemove(this);
    m_pool[UIActionIndexMN_M_Machine_M_MoveToGroup] = new UIActionMenuManagerCommonMoveToGroup(this);
    m_pool[UIActionIndexMN_M_Machine_M_MoveToGroup_S_New] = new UIActionSimpleManagerMachineMoveToGroupNew(this);
    m_pool[UIActionIndexMN_M_Machine_M_Start] = new UIActionMenuManagerCommonStart(this);
    m_pool[UIActionIndexMN_M_Machine_M_Start_S_Normal] = new UIActionSimpleManagerCommonPerformStartNormal(this);
    m_pool[UIActionIndexMN_M_Machine_M_Start_S_Headless] = new UIActionSimpleManagerCommonPerformStartHeadless(this);
    m_pool[UIActionIndexMN_M_Machine_M_Start_S_Detachable] = new UIActionSimpleManagerCommonPerformStartDetachable(this);
    m_pool[UIActionIndexMN_M_Machine_S_Show] = new UIActionSimpleManagerCommonShow(this);
    m_pool[UIActionIndexMN_M_Machine_T_Pause] = new UIActionToggleManagerCommonPauseAndResume(this);
    m_pool[UIActionIndexMN_M_Machine_S_Reset] = new UIActionSimpleManagerCommonPerformReset(this);
    m_pool[UIActionIndexMN_M_Machine_S_Detach] = new UIActionSimpleManagerCommonPerformDetach(this);
    m_pool[UIActionIndexMN_M_Machine_M_Console] = new UIActionMenuManagerConsole(this);
    m_pool[UIActionIndexMN_M_Machine_M_Console_S_CreateConnection] = new UIActionSimpleManagerConsolePerformCreateConnection(this);
    m_pool[UIActionIndexMN_M_Machine_M_Console_S_DeleteConnection] = new UIActionSimpleManagerConsolePerformDeleteConnection(this);
    m_pool[UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialUnix] = new UIActionSimpleManagerConsolePerformCopyCommand(this, true, true);
    m_pool[UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialWindows] = new UIActionSimpleManagerConsolePerformCopyCommand(this, true, false);
    m_pool[UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCUnix] = new UIActionSimpleManagerConsolePerformCopyCommand(this, false, true);
    m_pool[UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCWindows] = new UIActionSimpleManagerConsolePerformCopyCommand(this, false, false);
    m_pool[UIActionIndexMN_M_Machine_M_Console_S_ConfigureApplications] = new UIActionSimpleManagerConsolePerformConfigureApplications(this);
    m_pool[UIActionIndexMN_M_Machine_M_Console_S_ShowLog] = new UIActionSimpleManagerConsolePerformShowLog(this);
    m_pool[UIActionIndexMN_M_Machine_M_Stop] = new UIActionMenuManagerStop(this);
    m_pool[UIActionIndexMN_M_Machine_M_Stop_S_SaveState] = new UIActionSimpleManagerStopPerformSave(this);
    m_pool[UIActionIndexMN_M_Machine_M_Stop_S_Terminate] = new UIActionSimpleManagerStopPerformTerminate(this);
    m_pool[UIActionIndexMN_M_Machine_M_Stop_S_Shutdown] = new UIActionSimpleManagerStopPerformShutdown(this);
    m_pool[UIActionIndexMN_M_Machine_M_Stop_S_PowerOff] = new UIActionSimpleManagerStopPerformPowerOff(this);
    m_pool[UIActionIndexMN_M_Machine_M_Tools] = new UIActionMenuManagerToolsMachine(this);
    m_pool[UIActionIndexMN_M_Machine_M_Tools_T_Details] = new UIActionToggleManagerToolsMachineShowDetails(this);
    m_pool[UIActionIndexMN_M_Machine_M_Tools_T_Snapshots] = new UIActionToggleManagerToolsMachineShowSnapshots(this);
    m_pool[UIActionIndexMN_M_Machine_M_Tools_T_Logs] = new UIActionToggleManagerToolsMachineShowLogs(this);
    m_pool[UIActionIndexMN_M_Machine_M_Tools_T_Activity] = new UIActionToggleManagerToolsMachineShowActivity(this);
    m_pool[UIActionIndexMN_M_Machine_M_Tools_T_FileManager] = new UIActionToggleManagerToolsMachineShowFileManager(this);
    m_pool[UIActionIndexMN_M_Machine_S_Discard] = new UIActionSimpleManagerCommonPerformDiscard(this);
    m_pool[UIActionIndexMN_M_Machine_S_Refresh] = new UIActionSimpleManagerCommonPerformRefresh(this);
    m_pool[UIActionIndexMN_M_Machine_S_ShowInFileManager] = new UIActionSimpleManagerCommonShowInFileManager(this);
    m_pool[UIActionIndexMN_M_Machine_S_CreateShortcut] = new UIActionSimpleManagerCommonPerformCreateShortcut(this);
    m_pool[UIActionIndexMN_M_Machine_S_SortParent] = new UIActionSimpleManagerMachinePerformSortParent(this);
    m_pool[UIActionIndexMN_M_Machine_T_Search] = new UIActionToggleManagerCommonToggleSearch(this);

    /* Snapshot Pane actions: */
    m_pool[UIActionIndexMN_M_Snapshot] = new UIActionMenuManagerSnapshot(this);
    m_pool[UIActionIndexMN_M_Snapshot_S_Take] = new UIActionMenuManagerSnapshotPerformTake(this);
    m_pool[UIActionIndexMN_M_Snapshot_S_Delete] = new UIActionMenuManagerSnapshotPerformDelete(this);
    m_pool[UIActionIndexMN_M_Snapshot_S_Restore] = new UIActionMenuManagerSnapshotPerformRestore(this);
    m_pool[UIActionIndexMN_M_Snapshot_T_Properties] = new UIActionMenuManagerSnapshotToggleProperties(this);
    m_pool[UIActionIndexMN_M_Snapshot_S_Clone] = new UIActionMenuManagerSnapshotPerformClone(this);

    /* Extension Pack Manager actions: */
    m_pool[UIActionIndexMN_M_ExtensionWindow] = new UIActionMenuManagerExtension(this);
    m_pool[UIActionIndexMN_M_Extension] = new UIActionMenuManagerExtension(this);
    m_pool[UIActionIndexMN_M_Extension_S_Install] = new UIActionSimpleManagerExtensionPerformInstall(this);
    m_pool[UIActionIndexMN_M_Extension_S_Uninstall] = new UIActionSimpleManagerExtensionPerformUninstall(this);

    /* Virtual Media Manager actions: */
    m_pool[UIActionIndexMN_M_MediumWindow] = new UIActionMenuManagerMedium(this);
    m_pool[UIActionIndexMN_M_Medium] = new UIActionMenuManagerMedium(this);
    m_pool[UIActionIndexMN_M_Medium_S_Add] = new UIActionMenuManagerMediumPerformAdd(this);
    m_pool[UIActionIndexMN_M_Medium_S_Create] = new UIActionMenuManagerMediumPerformCreate(this);
    m_pool[UIActionIndexMN_M_Medium_S_Copy] = new UIActionMenuManagerMediumPerformCopy(this);
    m_pool[UIActionIndexMN_M_Medium_S_Move] = new UIActionMenuManagerMediumPerformMove(this);
    m_pool[UIActionIndexMN_M_Medium_S_Remove] = new UIActionMenuManagerMediumPerformRemove(this);
    m_pool[UIActionIndexMN_M_Medium_S_Release] = new UIActionMenuManagerMediumPerformRelease(this);
    m_pool[UIActionIndexMN_M_Medium_T_Details] = new UIActionMenuManagerMediumToggleProperties(this);
    m_pool[UIActionIndexMN_M_Medium_T_Search] = new UIActionMenuManagerMediumToggleSearch(this);
    m_pool[UIActionIndexMN_M_Medium_S_Refresh] = new UIActionMenuManagerMediumPerformRefresh(this);
    m_pool[UIActionIndexMN_M_Medium_S_Clear] = new UIActionMenuManagerMediumPerformClear(this);
    m_pool[UIActionIndexMN_M_Medium_S_Edit] = new UIActionMenuManagerMediumPerformEdit(this);

    /* Network Manager actions: */
    m_pool[UIActionIndexMN_M_NetworkWindow] = new UIActionMenuManagerNetwork(this);
    m_pool[UIActionIndexMN_M_Network] = new UIActionMenuManagerNetwork(this);
    m_pool[UIActionIndexMN_M_Network_S_Create] = new UIActionMenuManagerNetworkPerformCreate(this);
    m_pool[UIActionIndexMN_M_Network_S_Remove] = new UIActionMenuManagerNetworkPerformRemove(this);
    m_pool[UIActionIndexMN_M_Network_T_Details] = new UIActionMenuManagerNetworkToggleProperties(this);
    m_pool[UIActionIndexMN_M_Network_S_Refresh] = new UIActionMenuManagerNetworkPerformRefresh(this);

    /* Cloud Profile Manager actions: */
    m_pool[UIActionIndexMN_M_CloudWindow] = new UIActionMenuManagerCloud(this);
    m_pool[UIActionIndexMN_M_Cloud] = new UIActionMenuManagerCloud(this);
    m_pool[UIActionIndexMN_M_Cloud_S_Add] = new UIActionMenuManagerCloudPerformAdd(this);
    m_pool[UIActionIndexMN_M_Cloud_S_Import] = new UIActionMenuManagerCloudPerformImport(this);
    m_pool[UIActionIndexMN_M_Cloud_S_Remove] = new UIActionMenuManagerCloudPerformRemove(this);
    m_pool[UIActionIndexMN_M_Cloud_T_Details] = new UIActionMenuManagerCloudToggleProperties(this);
    m_pool[UIActionIndexMN_M_Cloud_S_TryPage] = new UIActionMenuManagerCloudShowTryPage(this);
    m_pool[UIActionIndexMN_M_Cloud_S_Help] = new UIActionMenuManagerCloudShowHelp(this);

    /* Cloud Console Manager actions: */
    m_pool[UIActionIndexMN_M_CloudConsoleWindow] = new UIActionMenuManagerCloudConsole(this);
    m_pool[UIActionIndexMN_M_CloudConsole] = new UIActionMenuManagerCloudConsole(this);
    m_pool[UIActionIndexMN_M_CloudConsole_S_ApplicationAdd] = new UIActionMenuManagerCloudConsolePerformApplicationAdd(this);
    m_pool[UIActionIndexMN_M_CloudConsole_S_ApplicationRemove] = new UIActionMenuManagerCloudConsolePerformApplicationRemove(this);
    m_pool[UIActionIndexMN_M_CloudConsole_S_ProfileAdd] = new UIActionMenuManagerCloudConsolePerformProfileAdd(this);
    m_pool[UIActionIndexMN_M_CloudConsole_S_ProfileRemove] = new UIActionMenuManagerCloudConsolePerformProfileRemove(this);
    m_pool[UIActionIndexMN_M_CloudConsole_T_Details] = new UIActionMenuManagerCloudConsoleToggleProperties(this);

    /* VM Activity Overview actions: */
    m_pool[UIActionIndexMN_M_VMActivityOverview] = new UIActionMenuVMActivityOverview(this);
    m_pool[UIActionIndexMN_M_VMActivityOverview_M_Columns] = new UIActionMenuManagerVMActivityOverviewColumns(this);
    m_pool[UIActionIndexMN_M_VMActivityOverview_S_SwitchToMachineActivity] = new UIActionMenuManagerVMActivityOverviewSwitchToMachineActivity(this);

    /* 'File' action groups: */
    m_groupPool[UIActionIndexMN_M_File_M_Tools] = new QActionGroup(m_pool.value(UIActionIndexMN_M_File_M_Tools));
    m_groupPool[UIActionIndexMN_M_File_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_File_M_Tools_T_HomeScreen));
    m_groupPool[UIActionIndexMN_M_File_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_File_M_Tools_T_ExtensionPackManager));
    m_groupPool[UIActionIndexMN_M_File_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_File_M_Tools_T_VirtualMediaManager));
    m_groupPool[UIActionIndexMN_M_File_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_File_M_Tools_T_NetworkManager));
    m_groupPool[UIActionIndexMN_M_File_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_File_M_Tools_T_CloudProfileManager));
    m_groupPool[UIActionIndexMN_M_File_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_File_M_Tools_T_VMActivityOverview));
    m_groupPool[UIActionIndexMN_M_File_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_File_M_Tools_T_MachineManager));

    /* 'Group' action groups: */
    m_groupPool[UIActionIndexMN_M_Group_M_Tools] = new QActionGroup(m_pool.value(UIActionIndexMN_M_Group_M_Tools));
    m_groupPool[UIActionIndexMN_M_Group_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Group_M_Tools_T_Details));
    m_groupPool[UIActionIndexMN_M_Group_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Group_M_Tools_T_Snapshots));
    m_groupPool[UIActionIndexMN_M_Group_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Group_M_Tools_T_Logs));
    m_groupPool[UIActionIndexMN_M_Group_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Group_M_Tools_T_Activity));
    m_groupPool[UIActionIndexMN_M_Group_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Group_M_Tools_T_FileManager));

    /* 'Machine' action groups: */
    m_groupPool[UIActionIndexMN_M_Machine_M_Tools] = new QActionGroup(m_pool.value(UIActionIndexMN_M_Machine_M_Tools));
    m_groupPool[UIActionIndexMN_M_Machine_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Machine_M_Tools_T_Details));
    m_groupPool[UIActionIndexMN_M_Machine_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Machine_M_Tools_T_Snapshots));
    m_groupPool[UIActionIndexMN_M_Machine_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Machine_M_Tools_T_Logs));
    m_groupPool[UIActionIndexMN_M_Machine_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Machine_M_Tools_T_Activity));
    m_groupPool[UIActionIndexMN_M_Machine_M_Tools]->addAction(m_pool.value(UIActionIndexMN_M_Machine_M_Tools_T_FileManager));

    /* Prepare update-handlers for known menus: */
    m_menuUpdateHandlers[UIActionIndexMN_M_File].ptfm =                  &UIActionPoolManager::updateMenuFile;
    m_menuUpdateHandlers[UIActionIndexMN_M_File_M_Tools].ptfm =          &UIActionPoolManager::updateMenuFileTools;
    m_menuUpdateHandlers[UIActionIndexMN_M_Home].ptfm =                  &UIActionPoolManager::updateMenuHome;
    m_menuUpdateHandlers[UIActionIndexMN_M_Group].ptfm =                 &UIActionPoolManager::updateMenuGroup;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine].ptfm =               &UIActionPoolManager::updateMenuMachine;
    m_menuUpdateHandlers[UIActionIndexMN_M_Group_M_MoveToGroup].ptfm =   &UIActionPoolManager::updateMenuGroupMoveToGroup;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine_M_MoveToGroup].ptfm = &UIActionPoolManager::updateMenuMachineMoveToGroup;
    m_menuUpdateHandlers[UIActionIndexMN_M_Group_M_Start].ptfm =         &UIActionPoolManager::updateMenuGroupStart;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine_M_Start].ptfm =       &UIActionPoolManager::updateMenuMachineStart;
    m_menuUpdateHandlers[UIActionIndexMN_M_Group_M_Console].ptfm =       &UIActionPoolManager::updateMenuGroupConsole;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine_M_Console].ptfm =     &UIActionPoolManager::updateMenuMachineConsole;
    m_menuUpdateHandlers[UIActionIndexMN_M_Group_M_Stop].ptfm =          &UIActionPoolManager::updateMenuGroupClose;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine_M_Stop].ptfm =        &UIActionPoolManager::updateMenuMachineClose;
    m_menuUpdateHandlers[UIActionIndexMN_M_Group_M_Tools].ptfm =         &UIActionPoolManager::updateMenuGroupTools;
    m_menuUpdateHandlers[UIActionIndexMN_M_Machine_M_Tools].ptfm =       &UIActionPoolManager::updateMenuMachineTools;
    m_menuUpdateHandlers[UIActionIndexMN_M_ExtensionWindow].ptfm =       &UIActionPoolManager::updateMenuExtensionWindow;
    m_menuUpdateHandlers[UIActionIndexMN_M_Extension].ptfm =             &UIActionPoolManager::updateMenuExtension;
    m_menuUpdateHandlers[UIActionIndexMN_M_MediumWindow].ptfm =          &UIActionPoolManager::updateMenuMediumWindow;
    m_menuUpdateHandlers[UIActionIndexMN_M_Medium].ptfm =                &UIActionPoolManager::updateMenuMedium;
    m_menuUpdateHandlers[UIActionIndexMN_M_NetworkWindow].ptfm =         &UIActionPoolManager::updateMenuNetworkWindow;
    m_menuUpdateHandlers[UIActionIndexMN_M_Network].ptfm =               &UIActionPoolManager::updateMenuNetwork;
    m_menuUpdateHandlers[UIActionIndexMN_M_CloudWindow].ptfm =           &UIActionPoolManager::updateMenuCloudWindow;
    m_menuUpdateHandlers[UIActionIndexMN_M_Cloud].ptfm =                 &UIActionPoolManager::updateMenuCloud;
    m_menuUpdateHandlers[UIActionIndexMN_M_CloudConsoleWindow].ptfm =    &UIActionPoolManager::updateMenuCloudConsoleWindow;
    m_menuUpdateHandlers[UIActionIndexMN_M_CloudConsole].ptfm =          &UIActionPoolManager::updateMenuCloudConsole;
    m_menuUpdateHandlers[UIActionIndexMN_M_VMActivityOverview].ptfm =    &UIActionPoolManager::updateMenuVMActivityOverview;
    m_menuUpdateHandlers[UIActionIndexMN_M_Snapshot].ptfm =              &UIActionPoolManager::updateMenuSnapshot;

    /* Call to base-class: */
    UIActionPool::preparePool();
}

void UIActionPoolManager::prepareConnections()
{
    /* Prepare connections: */
    connect(gShortcutPool, &UIShortcutPool::sigManagerShortcutsReloaded,
            this, &UIActionPoolManager::sltApplyShortcuts);
    connect(gShortcutPool, &UIShortcutPool::sigRuntimeShortcutsReloaded,
            this, &UIActionPoolManager::sltApplyShortcuts);
    connect(gEDataManager, &UIExtraDataManager::sigSettingsExpertModeChange,
            this, &UIActionPoolManager::sltHandleSettingsExpertModeChange);

    /* Call to base-class: */
    UIActionPool::prepareConnections();
}

void UIActionPoolManager::updateMenu(int iIndex)
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
             (this->*(m_menuUpdateHandlers.value(iIndex).ptfm))();
}

void UIActionPoolManager::updateMenus()
{
    /* Clear menu list: */
    m_mainMenus.clear();

    /* 'File' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_File));
    updateMenuFile();

    /* 'File' / 'Tools' menu: */
    updateMenuFileTools();

    /* 'Home' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_Home));
    updateMenuHome();
    /* 'Group' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_Group));
    updateMenuGroup();
    /* 'Machine' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_Machine));
    updateMenuMachine();

    /* 'Machine' / 'Move to Group' menu: */
    updateMenuMachineMoveToGroup();
    /* 'Group' / 'Start' menu: */
    updateMenuGroupStart();
    /* 'Machine' / 'Start' menu: */
    updateMenuMachineStart();
    /* 'Group' / 'Close' menu: */
    updateMenuGroupClose();
    /* 'Machine' / 'Close' menu: */
    updateMenuMachineClose();
    /* 'Group' / 'Tools' menu: */
    updateMenuGroupTools();
    /* 'Machine' / 'Tools' menu: */
    updateMenuMachineTools();

    /* 'Extension Pack Manager' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_Extension));
    updateMenuExtensionWindow();
    updateMenuExtension();
    /* 'Virtual Media Manager' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_Medium));
    updateMenuMediumWindow();
    updateMenuMedium();
    /* 'Network Manager' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_Network));
    updateMenuNetworkWindow();
    updateMenuNetwork();
    /* 'Cloud Profile Manager' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_Cloud));
    updateMenuCloudWindow();
    updateMenuCloud();
    /* 'VM Activity Overview' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_VMActivityOverview));
    updateMenuVMActivityOverview();

    /* 'Snapshot' menu: */
    addMenu(m_mainMenus, action(UIActionIndexMN_M_Snapshot));
    updateMenuSnapshot();
    /* 'Log' menu: */
    addMenu(m_mainMenus, action(UIActionIndex_M_Log));
    updateMenuLogViewerWindow();
    updateMenuLogViewer();
    /* 'Activity' menu: */
    addMenu(m_mainMenus, action(UIActionIndex_M_Activity));
    updateMenuVMActivityMonitor();

    /* 'File Manager' menu*/
    addMenu(m_mainMenus, action(UIActionIndex_M_FileManager));
    updateMenuFileManager();

    /* 'Help' menu: */
    addMenu(m_mainMenus, action(UIActionIndex_Menu_Help));
    updateMenuHelp();
}

void UIActionPoolManager::setShortcutsVisible(int iIndex, bool fVisible)
{
    /* Prepare a list of actions: */
    QList<UIAction*> actions;

    /* Handle known menus: */
    switch (iIndex)
    {
        case UIActionIndexMN_M_Home:
        {
            actions << action(UIActionIndexMN_M_Home_S_New)
                    << action(UIActionIndexMN_M_Home_S_Add);
            break;
        }
        case UIActionIndexMN_M_Group:
        {
            actions << action(UIActionIndexMN_M_Group_S_New)
                    << action(UIActionIndexMN_M_Group_S_Add)
                    << action(UIActionIndexMN_M_Group_S_Rename)
                    << action(UIActionIndexMN_M_Group_S_Remove)
                    << action(UIActionIndexMN_M_Group_M_MoveToGroup)
                    << action(UIActionIndexMN_M_Group_M_Start)
                    << action(UIActionIndexMN_M_Group_S_Show)
                    << action(UIActionIndexMN_M_Group_T_Pause)
                    << action(UIActionIndexMN_M_Group_S_Reset)
                    // << action(UIActionIndexMN_M_Group_S_Detach)
                    << action(UIActionIndexMN_M_Group_S_Discard)
                    << action(UIActionIndexMN_M_Group_S_Refresh)
                    << action(UIActionIndexMN_M_Group_S_ShowInFileManager)
                    << action(UIActionIndexMN_M_Group_S_CreateShortcut)
                    << action(UIActionIndexMN_M_Group_S_Sort)
                    << action(UIActionIndexMN_M_Group_M_Start_S_Normal)
                    << action(UIActionIndexMN_M_Group_M_Start_S_Headless)
                    << action(UIActionIndexMN_M_Group_M_Start_S_Detachable)
                    << action(UIActionIndexMN_M_Group_M_Console_S_CreateConnection)
                    << action(UIActionIndexMN_M_Group_M_Console_S_DeleteConnection)
                    << action(UIActionIndexMN_M_Group_M_Console_S_ConfigureApplications)
                    << action(UIActionIndexMN_M_Group_M_Stop_S_SaveState)
                    << action(UIActionIndexMN_M_Group_M_Stop_S_Terminate)
                    << action(UIActionIndexMN_M_Group_M_Stop_S_Shutdown)
                    << action(UIActionIndexMN_M_Group_M_Stop_S_PowerOff)
                    << action(UIActionIndexMN_M_Group_M_Tools_T_Details)
                    << action(UIActionIndexMN_M_Group_M_Tools_T_Snapshots)
                    << action(UIActionIndexMN_M_Group_M_Tools_T_Logs)
                    << action(UIActionIndexMN_M_Group_M_Tools_T_Activity);
            break;
        }
        case UIActionIndexMN_M_Machine:
        {
            actions << action(UIActionIndexMN_M_Machine_S_New)
                    << action(UIActionIndexMN_M_Machine_S_Add)
                    << action(UIActionIndexMN_M_Machine_S_Settings)
                    << action(UIActionIndexMN_M_Machine_S_Clone)
                    << action(UIActionIndexMN_M_Machine_S_Move)
                    << action(UIActionIndexMN_M_Machine_S_ExportToOCI)
                    << action(UIActionIndexMN_M_Machine_S_Remove)
                    << action(UIActionIndexMN_M_Machine_M_MoveToGroup)
                    << action(UIActionIndexMN_M_Machine_M_Start)
                    << action(UIActionIndexMN_M_Machine_S_Show)
                    << action(UIActionIndexMN_M_Machine_T_Pause)
                    << action(UIActionIndexMN_M_Machine_S_Reset)
                    // << action(UIActionIndexMN_M_Machine_S_Detach)
                    << action(UIActionIndexMN_M_Machine_S_Discard)
                    << action(UIActionIndexMN_M_Machine_S_Refresh)
                    << action(UIActionIndexMN_M_Machine_S_ShowInFileManager)
                    << action(UIActionIndexMN_M_Machine_S_CreateShortcut)
                    << action(UIActionIndexMN_M_Machine_S_SortParent)
                    << action(UIActionIndexMN_M_Machine_M_MoveToGroup_S_New)
                    << action(UIActionIndexMN_M_Machine_M_Start_S_Normal)
                    << action(UIActionIndexMN_M_Machine_M_Start_S_Headless)
                    << action(UIActionIndexMN_M_Machine_M_Start_S_Detachable)
                    << action(UIActionIndexMN_M_Machine_M_Console_S_CreateConnection)
                    << action(UIActionIndexMN_M_Machine_M_Console_S_DeleteConnection)
                    << action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialUnix)
                    << action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandSerialWindows)
                    << action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCUnix)
                    << action(UIActionIndexMN_M_Machine_M_Console_S_CopyCommandVNCWindows)
                    << action(UIActionIndexMN_M_Machine_M_Console_S_ConfigureApplications)
                    << action(UIActionIndexMN_M_Machine_M_Console_S_ShowLog)
                    << action(UIActionIndexMN_M_Machine_M_Stop_S_SaveState)
                    << action(UIActionIndexMN_M_Machine_M_Stop_S_Terminate)
                    << action(UIActionIndexMN_M_Machine_M_Stop_S_Shutdown)
                    << action(UIActionIndexMN_M_Machine_M_Stop_S_PowerOff)
                    << action(UIActionIndexMN_M_Machine_M_Tools_T_Details)
                    << action(UIActionIndexMN_M_Machine_M_Tools_T_Snapshots)
                    << action(UIActionIndexMN_M_Machine_M_Tools_T_Logs)
                    << action(UIActionIndexMN_M_Machine_M_Tools_T_Activity);
            break;
        }
        default:
            break;
    }

    /* Update shortcut visibility: */
    foreach (UIAction *pAction, actions)
        fVisible ? pAction->showShortcut() : pAction->hideShortcut();
}

QString UIActionPoolManager::shortcutsExtraDataID() const
{
    return GUI_Input_SelectorShortcuts;
}

void UIActionPoolManager::updateShortcuts()
{
    /* Call to base-class: */
    UIActionPool::updateShortcuts();
    /* Create temporary Runtime UI pool to do the same: */
    if (!isTemporary())
        UIActionPool::createTemporary(UIType_RuntimeUI);
}

void UIActionPoolManager::sltHandleSettingsExpertModeChange()
{
    /* Invalidate corresponding menus: */
    m_invalidations << UIActionIndexMN_M_File_M_Tools
                    << UIActionIndexMN_M_Group_M_Tools
                    << UIActionIndexMN_M_Machine_M_Tools
                    << UIActionIndexMN_M_Snapshot;
}

void UIActionPoolManager::updateMenuFile()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_File)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* The Application / 'File' menu contents is very different depending on host type. */

#ifdef VBOX_WS_MAC

    /* 'About' action goes to Application menu: */
    pMenu->addAction(action(UIActionIndex_M_Application_S_About));
# ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    /* 'Check for Updates' action goes to Application menu: */
    if (gEDataManager->applicationUpdateEnabled())
        pMenu->addAction(action(UIActionIndex_M_Application_S_CheckForUpdates));
# endif
    /* 'Reset Warnings' action goes to Application menu: */
    pMenu->addAction(action(UIActionIndex_M_Application_S_ResetWarnings));
    /* 'Preferences' action goes to Application menu: */
    pMenu->addAction(action(UIActionIndex_M_Application_S_Preferences));
    /* 'Close' action goes to Application menu: */
    pMenu->addAction(action(UIActionIndexMN_M_File_S_Close));

    /* 'Import Appliance' action goes to 'File' menu: */
    pMenu->addAction(action(UIActionIndexMN_M_File_S_ImportAppliance));
    /* 'Export Appliance' action goes to 'File' menu: */
    pMenu->addAction(action(UIActionIndexMN_M_File_S_ExportAppliance));
# ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    /* 'Show Extra-data Manager' action goes to 'File' menu for Debug build: */
    pMenu->addAction(action(UIActionIndexMN_M_File_S_ShowExtraDataManager));
# endif
    /* Separator after Import/Export actions of the 'File' menu: */
    pMenu->addSeparator();
    /* 'Tools' submenu goes to 'File' menu: */
    pMenu->addMenu(action(UIActionIndexMN_M_File_M_Tools)->menu());
#else /* !VBOX_WS_MAC */

    /* 'Preferences' action goes to 'File' menu: */
    pMenu->addAction(action(UIActionIndex_M_Application_S_Preferences));
    /* Separator after 'Preferences' action of the 'File' menu: */
    pMenu->addSeparator();
    /* 'Import Appliance' action goes to 'File' menu: */
    pMenu->addAction(action(UIActionIndexMN_M_File_S_ImportAppliance));
    /* 'Export Appliance' action goes to 'File' menu: */
    pMenu->addAction(action(UIActionIndexMN_M_File_S_ExportAppliance));
    /* Separator after 'Export Appliance' action of the 'File' menu: */
    pMenu->addSeparator();
# ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    /* 'Extra-data Manager' action goes to 'File' menu for Debug build: */
    pMenu->addAction(action(UIActionIndexMN_M_File_S_ShowExtraDataManager));
    /* Separator after 'Extra-data Manager' action of the 'File' menu: */
    pMenu->addSeparator();
# endif
    /* 'Tools' submenu goes to 'File' menu: */
    pMenu->addMenu(action(UIActionIndexMN_M_File_M_Tools)->menu());
    /* Separator after 'Tools' submenu of the 'File' menu: */
    pMenu->addSeparator();
# ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    /* 'Check for Updates' action goes to 'File' menu: */
    if (gEDataManager->applicationUpdateEnabled())
        pMenu->addAction(action(UIActionIndex_M_Application_S_CheckForUpdates));
# endif
    /* 'Reset Warnings' action goes 'File' menu: */
    pMenu->addAction(action(UIActionIndex_M_Application_S_ResetWarnings));
    /* Separator after 'Reset Warnings' action of the 'File' menu: */
    pMenu->addSeparator();
    /* 'Close' action goes to 'File' menu: */
    pMenu->addAction(action(UIActionIndexMN_M_File_S_Close));

#endif /* !VBOX_WS_MAC */

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_File);
}

void UIActionPoolManager::updateMenuFileTools()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_File_M_Tools)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Populate 'File' / 'Tools' menu: */
    const bool fExpertMode = gEDataManager->isSettingsInExpertMode();
    pMenu->addAction(action(UIActionIndexMN_M_File_M_Tools_T_HomeScreen));
    pMenu->addAction(action(UIActionIndexMN_M_File_M_Tools_T_MachineManager));
    pMenu->addAction(action(UIActionIndexMN_M_File_M_Tools_T_ExtensionPackManager));
    if (fExpertMode)
    {
        pMenu->addAction(action(UIActionIndexMN_M_File_M_Tools_T_VirtualMediaManager));
        pMenu->addAction(action(UIActionIndexMN_M_File_M_Tools_T_NetworkManager));
    }
    pMenu->addAction(action(UIActionIndexMN_M_File_M_Tools_T_CloudProfileManager));
    pMenu->addAction(action(UIActionIndexMN_M_File_M_Tools_T_VMActivityOverview));
    //pMenu->addAction(action(UIActionIndexMN_M_File_M_Tools_T_MachineManager));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_File_M_Tools);
}

void UIActionPoolManager::updateMenuHome()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Home)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Populate 'Home' menu: */
    pMenu->addAction(action(UIActionIndexMN_M_Home_S_New));
    pMenu->addAction(action(UIActionIndexMN_M_Home_S_Add));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Home);
}

void UIActionPoolManager::updateMenuGroup()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Group)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

#ifdef VBOX_WS_MAC
    // WORKAROUND:
    // On macOS you can't leave menu empty and still have it in
    // the menu-bar, you have to leave there at least something.
    // Remaining stuff will be appended from UIVirtualBoxManager.
    pMenu->addAction(action(UIActionIndexMN_M_Group_S_New));
#endif

    /* This menu always remains invalid.. */
}

void UIActionPoolManager::updateMenuMachine()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Machine)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

#ifdef VBOX_WS_MAC
    // WORKAROUND:
    // On macOS you can't leave menu empty and still have it in
    // the menu-bar, you have to leave there at least something.
    // Remaining stuff will be appended from UIVirtualBoxManager.
    pMenu->addAction(action(UIActionIndexMN_M_Machine_S_New));
#endif

    /* This menu always remains invalid.. */
}

void UIActionPoolManager::updateMenuGroupMoveToGroup()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Group_M_MoveToGroup)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* This menu always remains invalid.. */
}

void UIActionPoolManager::updateMenuMachineMoveToGroup()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Machine_M_MoveToGroup)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Populate 'Machine' / 'Move to Group' menu: */
    pMenu->addAction(action(UIActionIndexMN_M_Machine_M_MoveToGroup_S_New));

    /* This menu always remains invalid.. */
}

void UIActionPoolManager::updateMenuGroupStart()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Group_M_Start)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Populate 'Group' / 'Start' menu: */
    pMenu->addAction(action(UIActionIndexMN_M_Group_M_Start_S_Normal));
    pMenu->addAction(action(UIActionIndexMN_M_Group_M_Start_S_Headless));
    pMenu->addAction(action(UIActionIndexMN_M_Group_M_Start_S_Detachable));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Group_M_Start);
}

void UIActionPoolManager::updateMenuMachineStart()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Machine_M_Start)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Populate 'Machine' / 'Start' menu: */
    pMenu->addAction(action(UIActionIndexMN_M_Machine_M_Start_S_Normal));
    pMenu->addAction(action(UIActionIndexMN_M_Machine_M_Start_S_Headless));
    pMenu->addAction(action(UIActionIndexMN_M_Machine_M_Start_S_Detachable));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Machine_M_Start);
}

void UIActionPoolManager::updateMenuGroupConsole()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Group_M_Console)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* This menu always remains invalid.. */
}

void UIActionPoolManager::updateMenuMachineConsole()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Machine_M_Console)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* This menu always remains invalid.. */
}

void UIActionPoolManager::updateMenuGroupClose()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Group_M_Stop)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

#ifdef VBOX_WS_MAC
    // WORKAROUND:
    // On macOS you can't leave menu empty and still have it in
    // the menu-bar, you have to leave there at least something.
    // Remaining stuff will be appended from UIVirtualBoxManager.
    pMenu->addAction(action(UIActionIndexMN_M_Group_M_Stop_S_PowerOff));
#endif

    /* This menu always remains invalid.. */
}

void UIActionPoolManager::updateMenuMachineClose()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Machine_M_Stop)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

#ifdef VBOX_WS_MAC
    // WORKAROUND:
    // On macOS you can't leave menu empty and still have it in
    // the menu-bar, you have to leave there at least something.
    // Remaining stuff will be appended from UIVirtualBoxManager.
    pMenu->addAction(action(UIActionIndexMN_M_Machine_M_Stop_S_PowerOff));
#endif

    /* This menu always remains invalid.. */
}

void UIActionPoolManager::updateMenuGroupTools()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Group_M_Tools)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Populate 'Group' / 'Tools' menu: */
    const bool fExpertMode = gEDataManager->isSettingsInExpertMode();
    pMenu->addAction(action(UIActionIndexMN_M_Group_M_Tools_T_Details));
    pMenu->addAction(action(UIActionIndexMN_M_Group_M_Tools_T_Snapshots));
    pMenu->addAction(action(UIActionIndexMN_M_Group_M_Tools_T_Logs));
    pMenu->addAction(action(UIActionIndexMN_M_Group_M_Tools_T_Activity));
    if (fExpertMode)
        pMenu->addAction(action(UIActionIndexMN_M_Group_M_Tools_T_FileManager));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Group_M_Tools);
}

void UIActionPoolManager::updateMenuMachineTools()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Machine_M_Tools)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Populate 'Machine' / 'Tools' menu: */
    const bool fExpertMode = gEDataManager->isSettingsInExpertMode();
    pMenu->addAction(action(UIActionIndexMN_M_Machine_M_Tools_T_Details));
    pMenu->addAction(action(UIActionIndexMN_M_Machine_M_Tools_T_Snapshots));
    pMenu->addAction(action(UIActionIndexMN_M_Machine_M_Tools_T_Logs));
    pMenu->addAction(action(UIActionIndexMN_M_Machine_M_Tools_T_Activity));
    if (fExpertMode)
        pMenu->addAction(action(UIActionIndexMN_M_Machine_M_Tools_T_FileManager));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Machine_M_Tools);
}

void UIActionPoolManager::updateMenuExtensionWindow()
{
    /* Update corresponding menu: */
    updateMenuExtensionWrapper(action(UIActionIndexMN_M_ExtensionWindow)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_ExtensionWindow);
}

void UIActionPoolManager::updateMenuExtension()
{
    /* Update corresponding menu: */
    updateMenuExtensionWrapper(action(UIActionIndexMN_M_Extension)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Extension);
}

void UIActionPoolManager::updateMenuExtensionWrapper(UIMenu *pMenu)
{
    /* Clear contents: */
    pMenu->clear();

    /* 'Add' action: */
    addAction(pMenu, action(UIActionIndexMN_M_Extension_S_Install));
    /* 'Remove' action: */
    addAction(pMenu, action(UIActionIndexMN_M_Extension_S_Uninstall));
}

void UIActionPoolManager::updateMenuMediumWindow()
{
    /* Update corresponding menu: */
    updateMenuMediumWrapper(action(UIActionIndexMN_M_MediumWindow)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_MediumWindow);
}

void UIActionPoolManager::updateMenuMedium()
{
    /* Update corresponding menu: */
    updateMenuMediumWrapper(action(UIActionIndexMN_M_Medium)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Medium);
}

void UIActionPoolManager::updateMenuMediumWrapper(UIMenu *pMenu)
{
    /* Clear contents: */
    pMenu->clear();

    /* Separator? */
    bool fSeparator = false;

    /* 'Add' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_S_Add)) || fSeparator;
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_S_Create)) || fSeparator;

    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Edit' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_S_Edit)) || fSeparator;
    /* 'Copy' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_S_Copy)) || fSeparator;
    /* 'Move' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_S_Move)) || fSeparator;
    /* 'Remove' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_S_Remove)) || fSeparator;
    /* 'Release' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_S_Release)) || fSeparator;
    /* 'Clear' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_S_Clear)) || fSeparator;
    /* 'Search' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_T_Search)) || fSeparator;
    /* 'Properties' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_T_Details)) || fSeparator;

    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Refresh' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Medium_S_Refresh)) || fSeparator;;

    /* Remove if fSeparator is used: */
    Q_UNUSED(fSeparator);
}

void UIActionPoolManager::updateMenuNetworkWindow()
{
    /* Update corresponding menu: */
    updateMenuNetworkWrapper(action(UIActionIndexMN_M_NetworkWindow)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_NetworkWindow);
}

void UIActionPoolManager::updateMenuNetwork()
{
    /* Update corresponding menu: */
    updateMenuNetworkWrapper(action(UIActionIndexMN_M_Network)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Network);
}

void UIActionPoolManager::updateMenuNetworkWrapper(UIMenu *pMenu)
{
    /* Clear contents: */
    pMenu->clear();

    /* Separator? */
    bool fSeparator = false;

    /* 'Create' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Network_S_Create)) || fSeparator;

    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Remove' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Network_S_Remove)) || fSeparator;
    /* 'Properties' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Network_T_Details)) || fSeparator;

//    /* Separator? */
//    if (fSeparator)
//    {
//        pMenu->addSeparator();
//        fSeparator = false;
//    }

//    /* 'Refresh' action: */
//    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Network_S_Refresh)) || fSeparator;;

    /* Remove if fSeparator is used: */
    Q_UNUSED(fSeparator);
}

void UIActionPoolManager::updateMenuCloudWindow()
{
    /* Update corresponding menu: */
    updateMenuCloudWrapper(action(UIActionIndexMN_M_CloudWindow)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_CloudWindow);
}

void UIActionPoolManager::updateMenuCloud()
{
    /* Update corresponding menu: */
    updateMenuCloudWrapper(action(UIActionIndexMN_M_Cloud)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Cloud);
}

void UIActionPoolManager::updateMenuCloudWrapper(UIMenu *pMenu)
{
    /* Clear contents: */
    pMenu->clear();

    /* Separator? */
    bool fSeparator = false;

    /* 'Add' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Cloud_S_Add)) || fSeparator;
    /* 'Import' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Cloud_S_Import)) || fSeparator;

    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Remove' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Cloud_S_Remove)) || fSeparator;
    /* 'Properties' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Cloud_T_Details)) || fSeparator;

    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Try Page' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Cloud_S_TryPage)) || fSeparator;
    /* 'Help' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_Cloud_S_Help)) || fSeparator;

    /* Remove if fSeparator is used: */
    Q_UNUSED(fSeparator);
}

void UIActionPoolManager::updateMenuCloudConsoleWindow()
{
    /* Update corresponding menu: */
    updateMenuCloudConsoleWrapper(action(UIActionIndexMN_M_CloudConsoleWindow)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_CloudConsoleWindow);
}

void UIActionPoolManager::updateMenuCloudConsole()
{
    /* Update corresponding menu: */
    updateMenuCloudConsoleWrapper(action(UIActionIndexMN_M_CloudConsole)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_CloudConsole);
}

void UIActionPoolManager::updateMenuCloudConsoleWrapper(UIMenu *pMenu)
{
    /* Clear contents: */
    pMenu->clear();

    /* Separator? */
    bool fSeparator = false;

    /* 'Add Application' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_CloudConsole_S_ApplicationAdd)) || fSeparator;
    /* 'Remove Application' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_CloudConsole_S_ApplicationRemove)) || fSeparator;

    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Add Profile' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_CloudConsole_S_ProfileAdd)) || fSeparator;
    /* 'Remove Profile' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_CloudConsole_S_ProfileRemove)) || fSeparator;

    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Properties' action: */
    fSeparator = addAction(pMenu, action(UIActionIndexMN_M_CloudConsole_T_Details)) || fSeparator;

    /* Remove if fSeparator is used: */
    Q_UNUSED(fSeparator);
}

void UIActionPoolManager::updateMenuVMActivityOverview()
{
    /* Update corresponding menu: */
    updateMenuVMActivityOverviewWrapper(action(UIActionIndexMN_M_VMActivityOverview)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_VMActivityOverview);
}

void UIActionPoolManager::updateMenuVMActivityOverviewWrapper(UIMenu *pMenu)
{
    /* Clear contents: */
    pMenu->clear();
    addAction(pMenu, action(UIActionIndexMN_M_VMActivityOverview_M_Columns));
    addAction(pMenu, action(UIActionIndexMN_M_VMActivityOverview_S_SwitchToMachineActivity));
}

void UIActionPoolManager::updateMenuSnapshot()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndexMN_M_Snapshot)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Populate Snapshot-menu: */
    const bool fExpertMode = gEDataManager->isSettingsInExpertMode();
    pMenu->addAction(action(UIActionIndexMN_M_Snapshot_S_Take));
    pMenu->addAction(action(UIActionIndexMN_M_Snapshot_S_Delete));
    pMenu->addAction(action(UIActionIndexMN_M_Snapshot_S_Restore));
    pMenu->addAction(action(UIActionIndexMN_M_Snapshot_T_Properties));
    if (fExpertMode)
        pMenu->addAction(action(UIActionIndexMN_M_Snapshot_S_Clone));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndexMN_M_Snapshot);
}


#include "UIActionPoolManager.moc"
