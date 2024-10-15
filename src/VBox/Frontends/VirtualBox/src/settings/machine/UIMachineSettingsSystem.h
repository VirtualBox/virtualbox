/* $Id: UIMachineSettingsSystem.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsSystem class declaration.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsSystem_h
#define FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsSystem_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* COM includes: */
#include "KChipsetType.h"

/* GUI includes: */
#include "UISettingsPage.h"

/* Forward declarations: */
struct UIDataSettingsMachineSystem;
typedef UISettingsCache<UIDataSettingsMachineSystem> UISettingsCacheMachineSystem;
class QITabWidget;
class UIAccelerationFeaturesEditor;
class UIBaseMemoryEditor;
class UIBootOrderEditor;
class UIChipsetEditor;
class UIExecutionCapEditor;
class UIMotherboardFeaturesEditor;
class UIParavirtProviderEditor;
class UIPointingHIDEditor;
class UIProcessorFeaturesEditor;
class UITpmEditor;
class UIVirtualCPUEditor;

/** Machine settings: System page. */
class SHARED_LIBRARY_STUFF UIMachineSettingsSystem : public UISettingsPageMachine
{
    Q_OBJECT;

public:

    /** Constructs System settings page. */
    UIMachineSettingsSystem();
    /** Destructs System settings page. */
    virtual ~UIMachineSettingsSystem() RT_OVERRIDE;

    /** Returns whether the HW Virt Ex is supported. */
    bool isHWVirtExSupported() const;

    /** Returns whether the Nested Paging is supported. */
    bool isNestedPagingSupported() const;
    /** Returns whether the Nested Paging is enabled. */
    bool isNestedPagingEnabled() const;

    /** Returns whether the Nested HW Virt Ex is supported. */
    bool isNestedHWVirtExSupported() const;
    /** Returns whether the Nested HW Virt Ex is enabled. */
    bool isNestedHWVirtExEnabled() const;

    /** Returns whether the HID is enabled. */
    bool isHIDEnabled() const;

    /** Returns the chipset type. */
    KChipsetType chipsetType() const;

    /** Defines whether the USB is enabled. */
    void setUSBEnabled(bool fEnabled);

protected:

    /** Returns whether the page content was changed. */
    virtual bool changed() const RT_OVERRIDE;

    /** Loads settings from external object(s) packed inside @a data to cache.
      * @note  This task WILL be performed in other than the GUI thread, no widget interactions! */
    virtual void loadToCacheFrom(QVariant &data) RT_OVERRIDE;
    /** Loads data from cache to corresponding widgets.
      * @note  This task WILL be performed in the GUI thread only, all widget interactions here! */
    virtual void getFromCache() RT_OVERRIDE;

    /** Saves data from corresponding widgets to cache.
      * @note  This task WILL be performed in the GUI thread only, all widget interactions here! */
    virtual void putToCache() RT_OVERRIDE;
    /** Saves settings from cache to external object(s) packed inside @a data.
      * @note  This task WILL be performed in other than the GUI thread, no widget interactions! */
    virtual void saveFromCacheTo(QVariant &data) RT_OVERRIDE;

    /** Performs validation, updates @a messages list if something is wrong. */
    virtual bool validate(QList<UIValidationMessage> &messages) RT_OVERRIDE;

    /** Defines TAB order for passed @a pWidget. */
    virtual void setOrderAfter(QWidget *pWidget) RT_OVERRIDE;

    /** Handles filter change. */
    virtual void handleFilterChange() RT_OVERRIDE;

    /** Performs final page polishing. */
    virtual void polishPage() RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** Handles firmware type change. */
    void sltHandleFirmwareTypeChanged();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares 'Motherboard' tab. */
    void prepareTabMotherboard();
    /** Prepares 'Processor' tab. */
    void prepareTabProcessor();
    /** Prepares 'Acceleration' tab. */
    void prepareTabAcceleration();
    /** Prepares connections. */
    void prepareConnections();
    /** Cleanups all. */
    void cleanup();

    /** Saves existing data from cache. */
    bool saveData();
    /** Saves existing 'Motherboard' data from cache. */
    bool saveMotherboardData();
    /** Saves existing 'Processor' data from cache. */
    bool saveProcessorData();
    /** Saves existing 'Acceleration' data from cache. */
    bool saveAccelerationData();

    /** Updates minimum layout hint. */
    void updateMinimumLayoutHint();

    /** Holds whether the USB is enabled. */
    bool m_fIsUSBEnabled;

    /** Holds the page data cache instance. */
    UISettingsCacheMachineSystem *m_pCache;

    /** @name Widgets
     * @{ */
        /** Holds the tab-widget instance. */
        QITabWidget *m_pTabWidget;

        /** Holds the 'Motherboard' tab instance. */
        UIEditor                    *m_pTabMotherboard;
        /** Holds the base memory editor instance. */
        UIBaseMemoryEditor          *m_pEditorBaseMemory;
        /** Holds the boot order editor instance. */
        UIBootOrderEditor           *m_pEditorBootOrder;
        /** Holds the chipset editor instance. */
        UIChipsetEditor             *m_pEditorChipset;
        /** Holds the TPM editor instance. */
        UITpmEditor                 *m_pEditorTpm;
        /** Holds the pointing HID editor instance. */
        UIPointingHIDEditor         *m_pEditorPointingHID;
        /** Holds the motherboard features editor instance. */
        UIMotherboardFeaturesEditor *m_pEditorMotherboardFeatures;

        /** Holds the 'Processor' tab instance. */
        UIEditor                  *m_pTabProcessor;
        /** Holds the VCPU editor instance. */
        UIVirtualCPUEditor        *m_pEditorVCPU;
        /** Holds the exec cap editor instance. */
        UIExecutionCapEditor      *m_pEditorExecCap;
        /** Holds the motherboard features editor instance. */
        UIProcessorFeaturesEditor *m_pEditorProcessorFeatures;

        /** Holds the 'Acceleration' tab instance. */
        UIEditor                     *m_pTabAcceleration;
        /** Holds the paravirtualization provider editor instance. */
        UIParavirtProviderEditor     *m_pEditorParavirtProvider;
        /** Holds the acceleration features editor instance. */
        UIAccelerationFeaturesEditor *m_pEditorAccelerationFeatures;
   /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsSystem_h */
