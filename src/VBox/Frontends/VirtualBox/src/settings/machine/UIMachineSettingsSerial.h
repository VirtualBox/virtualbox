/* $Id: UIMachineSettingsSerial.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsSerial class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsSerial_h
#define FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsSerial_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UISettingsPage.h"

/* Forward declarations: */
class QITabWidget;
class UISerialSettingsEditor;
struct UIDataSettingsMachineSerial;
struct UIDataSettingsMachineSerialPort;
typedef UISettingsCache<UIDataSettingsMachineSerialPort> UISettingsCacheMachineSerialPort;
typedef UISettingsCachePool<UIDataSettingsMachineSerial, UISettingsCacheMachineSerialPort> UISettingsCacheMachineSerial;

/** Machine settings: Serial page. */
class SHARED_LIBRARY_STUFF UIMachineSettingsSerial : public UISettingsPageMachine
{
    Q_OBJECT;

public:

    /** Constructs Serial settings page. */
    UIMachineSettingsSerial();
    /** Destructs Serial settings page. */
    virtual ~UIMachineSettingsSerial() RT_OVERRIDE;

    /** Returns ports. */
    QVector<QPair<QString, QString> > ports() const { return m_ports; }
    /** Returns paths. */
    QVector<QString> paths() const { return m_paths; }

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

    /** Performs final page polishing. */
    virtual void polishPage() RT_OVERRIDE;

private slots:

    /** Handles port change. */
    void sltHandlePortChange();
    /** Handles path change. */
    void sltHandlePathChange();
    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepare tab. */
    void prepareTab();
    /** Prepares connections. */
    void prepareConnections(UISerialSettingsEditor *pTabEditor);
    /** Cleanups all. */
    void cleanup();

    /** Performs tab polishing for specified @a iSlot. */
    void polishTab(int iSlot);

    /** Loads port data for specified @a iSlot from @a portCache to corresponding widgets. */
    void getFromCache(int iSlot, const UISettingsCacheMachineSerialPort &portCache);
    /** Saves port data for specified @a iSlot from corresponding widgets to @a portCache. */
    void putToCache(int iSlot, UISettingsCacheMachineSerialPort &portCache);

    /** Returns IRQ for specified @a iSlot. */
    QString irq(int iSlot) const;
    /** Returns IO address for specified @a iSlot. */
    QString ioAddress(int iSlot) const;

    /** Performs validation for specified @a iSlot, updates @a messages list if something is wrong. */
    bool validate(int iSlot, QList<UIValidationMessage> &messages);

    /** Repopulates ports. */
    void refreshPorts();
    /** Repopulates paths. */
    void refreshPaths();

    /** Returns tab title for specified @a iSlot. */
    static QString tabTitle(int iSlot);

    /** Saves existing data from cache. */
    bool saveData();
    /** Saves existing port data from cache. */
    bool savePortData(int iSlot);

    /** Holds the ports. */
    QVector<QPair<QString, QString> >  m_ports;
    /** Holds the paths. */
    QVector<QString>                   m_paths;

    /** Holds the page data cache instance. */
    UISettingsCacheMachineSerial *m_pCache;

    /** Holds the tab-widget instance. */
    QITabWidget *m_pTabWidget;

    /** Holds the list of tab-editors. */
    QList<UISerialSettingsEditor*>  m_tabEditors;
};

#endif /* !FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsSerial_h */
