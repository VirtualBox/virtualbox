/* $Id: UISettingsPage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISettingsPage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_UISettingsPage_h
#define FEQT_INCLUDED_SRC_settings_UISettingsPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"
#include "UIExtraDataDefs.h"
#include "UISettingsDefs.h"

/* COM includes: */
#include "CConsole.h"
#include "CHost.h"
#include "CMachine.h"
#include "CSystemProperties.h"

/* Forward declarations: */
class QLabel;
class QLayout;
class QVariant;
class QWidget;
class UISettingsPageValidator;

/* Using declarations: */
using namespace UISettingsDefs;


/** Global settings data wrapper. */
struct UISettingsDataGlobal
{
    /** Constructs NULL global settings data struct. */
    UISettingsDataGlobal() {}
    /** Constructs global settings data struct on the basis of @a comHost and @a comProperties. */
    UISettingsDataGlobal(const CHost &comHost, const CSystemProperties &comProperties)
        : m_host(comHost), m_properties(comProperties) {}
    /** Holds the host reference. */
    CHost              m_host;
    /** Holds the properties reference. */
    CSystemProperties  m_properties;
};
Q_DECLARE_METATYPE(UISettingsDataGlobal);


/** Machine settings data wrapper. */
struct UISettingsDataMachine
{
    /** Constructs NULL machine settings data struct. */
    UISettingsDataMachine() {}
    /** Constructs machine settings data struct on the basis of @a comMachine and @a comConsole. */
    UISettingsDataMachine(const CMachine &comMachine, const CConsole &comConsole)
        : m_machine(comMachine), m_console(comConsole) {}
    /** Holds the machine reference. */
    CMachine  m_machine;
    /** Holds the console reference. */
    CConsole  m_console;
};
Q_DECLARE_METATYPE(UISettingsDataMachine);


/** Validation message. */
typedef QPair<QString, QStringList> UIValidationMessage;


/** UIEditor sub-class used as settings page interface. */
class UISettingsPage : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about particular operation progress change.
      * @param iOperations  holds the number of operations CProgress have,
      * @param strOperation holds the description of the current CProgress operation,
      * @param iOperation   holds the index of the current CProgress operation,
      * @param iPercent     holds the percentage of the current CProgress operation. */
    void sigOperationProgressChange(ulong iOperations, QString strOperation,
                                    ulong iOperation, ulong iPercent);

    /** Notifies listeners about particular COM error.
      * @param strErrorInfo holds the details of the error happened. */
    void sigOperationProgressError(QString strErrorInfo);

public:

    /** Loads settings from external object(s) packed inside @a data to cache.
      * @note  This task WILL be performed in other than the GUI thread, no widget interactions! */
    virtual void loadToCacheFrom(QVariant &data) = 0;
    /** Loads data from cache to corresponding widgets.
      * @note  This task WILL be performed in the GUI thread only, all widget interactions here! */
    virtual void getFromCache() = 0;

    /** Saves data from corresponding widgets to cache.
      * @note  This task WILL be performed in the GUI thread only, all widget interactions here! */
    virtual void putToCache() = 0;
    /** Saves settings from cache to external object(s) packed inside @a data.
      * @note  This task WILL be performed in other than the GUI thread, no widget interactions! */
    virtual void saveFromCacheTo(QVariant &data) = 0;

    /** Notifies listeners about particular COM error.
      * @param  strErrorInfo  Brings the details of the error happened. */
    void notifyOperationProgressError(const QString &strErrorInfo);

    /** Defines @a pValidator. */
    void setValidator(UISettingsPageValidator *pValidator);
    /** Defines whether @a fIsValidatorBlocked which means not used at all. */
    void setValidatorBlocked(bool fIsValidatorBlocked) { m_fIsValidatorBlocked = fIsValidatorBlocked; }
    /** Performs page validation composing a list of @a messages. */
    virtual bool validate(QList<UIValidationMessage> &messages) { Q_UNUSED(messages); return true; }

    /** Returns first navigation widget. */
    QWidget *firstWidget() const { return m_pFirstWidget; }
    /** Defines the first navigation widget for TAB-order. */
    virtual void setOrderAfter(QWidget *pWidget) { m_pFirstWidget = pWidget; }

    /** Defines @a enmConfigurationAccessLevel. */
    virtual void setConfigurationAccessLevel(ConfigurationAccessLevel enmConfigurationAccessLevel);
    /** Returns configuration access level. */
    ConfigurationAccessLevel configurationAccessLevel() const { return m_enmConfigurationAccessLevel; }
    /** Returns whether configuration access level is Full. */
    bool isMachineOffline() const { return configurationAccessLevel() == ConfigurationAccessLevel_Full; }
    /** Returns whether configuration access level corresponds to machine in Powered Off state. */
    bool isMachinePoweredOff() const { return configurationAccessLevel() == ConfigurationAccessLevel_Partial_PoweredOff; }
    /** Returns whether configuration access level corresponds to machine in Saved state. */
    bool isMachineSaved() const { return configurationAccessLevel() == ConfigurationAccessLevel_Partial_Saved; }
    /** Returns whether configuration access level corresponds to machine in one of Running states. */
    bool isMachineOnline() const { return configurationAccessLevel() == ConfigurationAccessLevel_Partial_Running; }
    /** Returns whether configuration access level corresponds to machine in one of allowed states. */
    bool isMachineInValidMode() const { return isMachineOffline() || isMachinePoweredOff() || isMachineSaved() || isMachineOnline(); }

    /** Returns whether the page content was changed. */
    virtual bool changed() const = 0;

    /** Defines page @a cId. */
    void setId(int cId) { m_cId = cId; }
    /** Returns page ID. */
    int id() const { return m_cId; }

    /** Returns page internal name. */
    virtual QString internalName() const = 0;

    /** Returns page warning pixmap. */
    virtual QPixmap warningPixmap() const = 0;

    /** Defines whether page is @a fProcessed. */
    void setProcessed(bool fProcessed) { m_fProcessed = fProcessed; }
    /** Returns whether page is processed. */
    bool processed() const { return m_fProcessed; }

    /** Defines whether page processing is @a fFailed. */
    void setFailed(bool fFailed) { m_fFailed = fFailed; }
    /** Returns whether page processing is failed. */
    bool failed() const { return m_fFailed; }

    /** Performs page polishing. */
    virtual void polishPage() {}

public slots:

    /** Performs validation. */
    void revalidate();

protected:

    /** Constructs settings page. */
    UISettingsPage();

private:

    /** Holds the configuration access level. */
    ConfigurationAccessLevel  m_enmConfigurationAccessLevel;

    /** Holds the page ID. */
    int  m_cId;

    /** Holds the first TAB-orer widget reference. */
    QWidget                 *m_pFirstWidget;
    /** Holds the page validator. */
    UISettingsPageValidator *m_pValidator;

    /** Holds whether page validation is blocked. */
    bool  m_fIsValidatorBlocked : 1;
    /** Holds whether page is processed. */
    bool  m_fProcessed : 1;
    /** Holds whether page processing is failed. */
    bool  m_fFailed : 1;
};


/** UISettingsPage extension used as Global Preferences page interface. */
class UISettingsPageGlobal : public UISettingsPage
{
    Q_OBJECT;

protected:

    /** Constructs global preferences page. */
    UISettingsPageGlobal();

    /** Returns internal page ID. */
    GlobalSettingsPageType internalID() const;

    /** Returns page internal name. */
    virtual QString internalName() const RT_OVERRIDE;

    /** Returns page warning pixmap. */
    virtual QPixmap warningPixmap() const RT_OVERRIDE;

    /** Returns whether the page content was changed. */
    virtual bool changed() const RT_OVERRIDE { return false; }

    /** Fetches data to m_properties & m_settings. */
    void fetchData(const QVariant &data);
    /** Uploads m_properties & m_settings to data. */
    void uploadData(QVariant &data) const;

    /** Holds the source of host preferences. */
    CHost              m_host;
    /** Holds the source of global preferences. */
    CSystemProperties  m_properties;
};


/** UISettingsPage extension used as Machine Settings page interface. */
class UISettingsPageMachine : public UISettingsPage
{
    Q_OBJECT;

protected:

    /** Constructs machine settings page. */
    UISettingsPageMachine();

    /** Returns internal page ID. */
    MachineSettingsPageType internalID() const;

    /** Returns page internal name. */
    virtual QString internalName() const RT_OVERRIDE;

    /** Returns page warning pixmap. */
    virtual QPixmap warningPixmap() const RT_OVERRIDE;

    /** Fetches data to m_machine & m_console. */
    void fetchData(const QVariant &data);
    /** Uploads m_machine & m_console to data. */
    void uploadData(QVariant &data) const;

    /** Holds the source of machine settings. */
    CMachine  m_machine;
    /** Holds the source of console settings. */
    CConsole  m_console;
};


/** UIEditor sub-class, used as settings page frame. */
class UISettingsPageFrame : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs details element passing @a pParent to the base-class.
      * @param  pPage  Brings the page to wrap with frame. */
    UISettingsPageFrame(UISettingsPage *pPage, QWidget *pParent = 0);

    /** Defines @a strName. */
    void setName(const QString &strName);

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();

    /** Holds the page reference. */
    UISettingsPage *m_pPage;

    /** Holds the element name.*/
    QString  m_strName;

    /** Holds the name label instance. */
    QLabel  *m_pLabelName;
    /** Holds the contents widget instance. */
    QWidget *m_pWidget;
    /** Holds the contents layout instance. */
    QLayout *m_pLayout;
};


#endif /* !FEQT_INCLUDED_SRC_settings_UISettingsPage_h */
