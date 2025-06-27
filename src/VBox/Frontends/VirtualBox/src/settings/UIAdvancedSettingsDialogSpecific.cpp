/* $Id: UIAdvancedSettingsDialogSpecific.cpp 110015 2025-06-27 12:30:28Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIAdvancedSettingsDialogSpecific class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QStackedWidget>

/* GUI includes: */
#include "UIAdvancedSettingsDialogSpecific.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UILocalMachineStuff.h"
#include "UIMessageCenter.h"
#include "UISettingsDefs.h"
#include "UISettingsSerializer.h"
#include "UISettingsSelector.h"
#include "UIVirtualBoxEventHandler.h"

/* GUI includes: Global Preferences: */
#include "UIGlobalSettingsDisplay.h"
#include "UIGlobalSettingsGeneral.h"
#include "UIGlobalSettingsInput.h"
#include "UIGlobalSettingsLanguage.h"
#ifdef VBOX_WS_WIN
# include "UIGlobalSettingsInterface.h"
#endif
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
# include "UIGlobalSettingsProxy.h"
# include "UIGlobalSettingsUpdate.h"
#endif

/* GUI includes: Machine Settings: */
#include "UIMachineSettingsAudio.h"
#include "UIMachineSettingsDisplay.h"
#include "UIMachineSettingsGeneral.h"
#include "UIMachineSettingsInterface.h"
#include "UIMachineSettingsNetwork.h"
#include "UIMachineSettingsSerial.h"
#include "UIMachineSettingsSF.h"
#include "UIMachineSettingsStorage.h"
#include "UIMachineSettingsSystem.h"
#include "UIMachineSettingsUSB.h"

/* COM includes: */
#include "CExtPackManager.h"
#include "CGraphicsAdapter.h"
#include "CPlatform.h"
#include "CUSBController.h"


/*********************************************************************************************************************************
*   Class UIAdvancedSettingsDialogGlobal implementation.                                                                         *
*********************************************************************************************************************************/

UIAdvancedSettingsDialogGlobal::UIAdvancedSettingsDialogGlobal(QWidget *pParent,
                                                               const QString &strCategory /* = QString() */,
                                                               const QString &strControl /* = QString() */)
    : UIAdvancedSettingsDialog(pParent, strCategory, strControl)
{
    prepare();
}

void UIAdvancedSettingsDialogGlobal::sltRetranslateUI()
{
    /* Selector itself: */
    m_pSelector->widget()->setWhatsThis(tr("Allows to navigate through Global Property categories"));

    /* General page: */
    m_pSelector->setItemText(GlobalSettingsPageType_General, tr("General"));

    /* Input page: */
    m_pSelector->setItemText(GlobalSettingsPageType_Input, tr("Input"));

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    /* Update page: */
    m_pSelector->setItemText(GlobalSettingsPageType_Update, tr("Update"));
#endif

    /* Language page: */
    m_pSelector->setItemText(GlobalSettingsPageType_Language, tr("Language"));

    /* Display page: */
    m_pSelector->setItemText(GlobalSettingsPageType_Display, tr("Display"));

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    /* Proxy page: */
    m_pSelector->setItemText(GlobalSettingsPageType_Proxy, tr("Proxy"));
#endif

#ifdef VBOX_WS_WIN
    /* Interface page: */
    m_pSelector->setItemText(GlobalSettingsPageType_Interface, tr("Interface"));
#endif

    /* Polish the selector: */
    m_pSelector->polish();

    /* Base-class UI translation: */
    UIAdvancedSettingsDialog::sltRetranslateUI();

    /* Set dialog's name: */
    setWindowTitle(title());
}

bool UIAdvancedSettingsDialogGlobal::load()
{
    /* Get host & properties: */
    CHost comHost = gpGlobalSession->host();
    CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    /* Prepare global data: */
    qRegisterMetaType<UISettingsDataGlobal>();
    UISettingsDataGlobal data(comHost, comProperties);
    QVariant varData = QVariant::fromValue(data);

    /* Call to base-class: */
    UIAdvancedSettingsDialog::loadData(varData);

    /* True by default: */
    return true;
}

void UIAdvancedSettingsDialogGlobal::save()
{
    /* Get host & properties: */
    CHost comHost = gpGlobalSession->host();
    CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    /* Prepare global data: */
    qRegisterMetaType<UISettingsDataGlobal>();
    UISettingsDataGlobal data(comHost, comProperties);
    QVariant varData = QVariant::fromValue(data);

    /* Call to base-class: */
    UIAdvancedSettingsDialog::saveData(varData);

    /* Get updated host: */
    CHost comNewHost = varData.value<UISettingsDataGlobal>().m_host;
    /* If host is not OK => show the error: */
    if (!comNewHost.isOk())
        msgCenter().cannotSetHostSettings(comNewHost, this);

    /* Get updated properties: */
    CSystemProperties comNewProperties = varData.value<UISettingsDataGlobal>().m_properties;
    /* If properties are not OK => show the error: */
    if (!comNewProperties.isOk())
        msgCenter().cannotSetSystemProperties(comNewProperties, this);

    /* Handle serializartion finished: */
    sltHandleSerializationFinished();
}

QString UIAdvancedSettingsDialogGlobal::title() const
{
    return tr("VirtualBox - Preferences", "global preferences dialog name");
}

void UIAdvancedSettingsDialogGlobal::prepare()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/global_settings_32px.png", ":/global_settings_16px.png"));
#endif

    /* Creating settings pages: */
    QList<GlobalSettingsPageType> restrictedGlobalSettingsPages = gEDataManager->restrictedGlobalSettingsPages();
    for (int iPageIndex = GlobalSettingsPageType_General; iPageIndex < GlobalSettingsPageType_Max; ++iPageIndex)
    {
        /* Make sure page was not restricted: */
        if (restrictedGlobalSettingsPages.contains(static_cast<GlobalSettingsPageType>(iPageIndex)))
            continue;

        /* Make sure page is available: */
        if (isPageAvailable(iPageIndex))
        {
            UISettingsPage *pSettingsPage = 0;
            switch (iPageIndex)
            {
                /* General page: */
                case GlobalSettingsPageType_General:
                {
                    pSettingsPage = new UIGlobalSettingsGeneral;
                    addItem(":/machine_32px.png", ":/machine_24px.png", ":/machine_16px.png",
                            iPageIndex, "#general", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "preferences");
                    break;
                }
                /* Input page: */
                case GlobalSettingsPageType_Input:
                {
                    pSettingsPage = new UIGlobalSettingsInput;
                    addItem(":/keyboard_32px.png", ":/keyboard_24px.png", ":/keyboard_16px.png",
                            iPageIndex, "#input", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "preferences");
                    break;
                }
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
                /* Update page: */
                case GlobalSettingsPageType_Update:
                {
                    pSettingsPage = new UIGlobalSettingsUpdate;
                    addItem(":/refresh_32px.png", ":/refresh_24px.png", ":/refresh_16px.png",
                            iPageIndex, "#update", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "preferences");
                    break;
                }
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
                /* Language page: */
                case GlobalSettingsPageType_Language:
                {
                    pSettingsPage = new UIGlobalSettingsLanguage;
                    addItem(":/site_32px.png", ":/site_24px.png", ":/site_16px.png",
                            iPageIndex, "#language", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "preferences");
                    break;
                }
                /* Display page: */
                case GlobalSettingsPageType_Display:
                {
                    pSettingsPage = new UIGlobalSettingsDisplay;
                    addItem(":/vrdp_32px.png", ":/vrdp_24px.png", ":/vrdp_16px.png",
                            iPageIndex, "#display", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "preferences");
                    break;
                }
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
                /* Proxy page: */
                case GlobalSettingsPageType_Proxy:
                {
                    pSettingsPage = new UIGlobalSettingsProxy;
                    addItem(":/proxy_32px.png", ":/proxy_24px.png", ":/proxy_16px.png",
                            iPageIndex, "#proxy", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "preferences");
                    break;
                }
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
#ifdef VBOX_WS_WIN
                /* Interface page: */
                case GlobalSettingsPageType_Interface:
                {
                    pSettingsPage = new UIGlobalSettingsInterface;
                    addItem(":/interface_32px.png", ":/interface_24px.png", ":/interface_16px.png",
                            iPageIndex, "#userInterface", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "preferences");
                    break;
                }
#endif /* VBOX_WS_WIN */
                default:
                    break;
            }
        }
    }

    /* Assign default (full) configuration access level: */
    setConfigurationAccessLevel(ConfigurationAccessLevel_Full);

    /* Apply language settings: */
    sltRetranslateUI();
}

bool UIAdvancedSettingsDialogGlobal::isPageAvailable(int) const
{
    /* Add restrictions here.. */
    return true;
}


/*********************************************************************************************************************************
*   Class UIAdvancedSettingsDialogMachine implementation.                                                                        *
*********************************************************************************************************************************/

UIAdvancedSettingsDialogMachine::UIAdvancedSettingsDialogMachine(QWidget *pParent,
                                                                 const QUuid &uMachineId,
                                                                 UIActionPool *pActionPool,
                                                                 const QString &strCategory /* = QString() */,
                                                                 const QString &strControl /* = QString() */)
    : UIAdvancedSettingsDialog(pParent, strCategory, strControl)
    , m_uMachineId(uMachineId)
    , m_pActionPool(pActionPool)
{
    prepare();
}

void UIAdvancedSettingsDialogMachine::setNewMachineId(const QUuid &uMachineId,
                                                      const QString &strCategory /* = QString() */,
                                                      const QString &strControl /* = QString() */)
{
    /* Cache new machine stuff: */
    m_uMachineId = uMachineId;
    m_strCategory = strCategory;
    m_strControl = strControl;

    /* Get corresponding machine (required to determine dialog type and page availability): */
    m_machine = gpGlobalSession->virtualBox().FindMachine(m_uMachineId.toString());
    AssertReturnVoid(!m_machine.isNull());
    m_enmSessionState = m_machine.GetSessionState();
    m_enmMachineState = m_machine.GetState();

    /* Calculate initial configuration access level: */
    setConfigurationAccessLevel(::configurationAccessLevel(m_enmSessionState, m_enmMachineState));

    /* Apply language settings: */
    sltRetranslateUI();

    /* Choose page/tab: */
    choosePageAndTab(true /* keep previous by default */);

    /* Load finally: */
    load();
}

void UIAdvancedSettingsDialogMachine::sltRetranslateUI()
{
    /* Selector itself: */
    m_pSelector->widget()->setWhatsThis(tr("Allows to navigate through VM Settings categories"));

    /* We have to make sure that the Network, Serial pages are retranslated
     * before they are revalidated. Cause: They do string comparing within
     * UICommon which is retranslated at that point already: */
    QEvent event(QEvent::LanguageChange);
    if (QWidget *pPage = m_pSelector->idToPage(MachineSettingsPageType_Network))
        qApp->sendEvent(pPage, &event);
    if (QWidget *pPage = m_pSelector->idToPage(MachineSettingsPageType_Serial))
        qApp->sendEvent(pPage, &event);

    /* General page: */
    m_pSelector->setItemText(MachineSettingsPageType_General, tr("General"));

    /* System page: */
    m_pSelector->setItemText(MachineSettingsPageType_System, tr("System"));

    /* Display page: */
    m_pSelector->setItemText(MachineSettingsPageType_Display, tr("Display"));

    /* Storage page: */
    m_pSelector->setItemText(MachineSettingsPageType_Storage, tr("Storage"));

    /* Audio page: */
    m_pSelector->setItemText(MachineSettingsPageType_Audio, tr("Audio"));

    /* Network page: */
    m_pSelector->setItemText(MachineSettingsPageType_Network, tr("Network"));

    /* Serial page: */
    m_pSelector->setItemText(MachineSettingsPageType_Serial, tr("Serial Ports"));

    /* USB page: */
    m_pSelector->setItemText(MachineSettingsPageType_USB, tr("USB"));

    /* SFolders page: */
    m_pSelector->setItemText(MachineSettingsPageType_SF, tr("Shared Folders"));

    /* Interface page: */
    m_pSelector->setItemText(MachineSettingsPageType_Interface, tr("User Interface"));

    /* Polish the selector: */
    m_pSelector->polish();

    /* Base-class UI translation: */
    UIAdvancedSettingsDialog::sltRetranslateUI();

    /* Set dialog's name: */
    setWindowTitle(title());
}

bool UIAdvancedSettingsDialogMachine::load()
{
    /* Check that session is NOT created: */
    if (!m_session.isNull())
        return false;

    /* Prepare session: */
    m_session = configurationAccessLevel() == ConfigurationAccessLevel_Null ? CSession() :
                configurationAccessLevel() == ConfigurationAccessLevel_Full ? openSession(m_uMachineId) :
                                                                              openExistingSession(m_uMachineId);
    /* Check that session was created: */
    if (m_session.isNull())
        return false;

    /* Get machine and console: */
    m_machine = m_session.GetMachine();
    m_console = configurationAccessLevel() == ConfigurationAccessLevel_Full ? CConsole() : m_session.GetConsole();
    /* Prepare machine data: */
    qRegisterMetaType<UISettingsDataMachine>();
    UISettingsDataMachine data(m_machine, m_console);
    QVariant varData = QVariant::fromValue(data);

    /* Call to base-class: */
    UIAdvancedSettingsDialog::loadData(varData);

    /* True by default: */
    return true;
}

void UIAdvancedSettingsDialogMachine::save()
{
    /* Check that session is NOT created: */
    if (!m_session.isNull())
        return;

    /* Prepare session: */
    m_session = configurationAccessLevel() == ConfigurationAccessLevel_Null ? CSession() :
                configurationAccessLevel() == ConfigurationAccessLevel_Full ? openSession(m_uMachineId) :
                                                                              openExistingSession(m_uMachineId);
    /* Check that session was created: */
    if (m_session.isNull())
        return;

    /* Get machine and console: */
    m_machine = m_session.GetMachine();
    m_console = configurationAccessLevel() == ConfigurationAccessLevel_Full ? CConsole() : m_session.GetConsole();
    /* Prepare machine data: */
    qRegisterMetaType<UISettingsDataMachine>();
    UISettingsDataMachine data(m_machine, m_console);
    QVariant varData = QVariant::fromValue(data);

    /* Call to base-class: */
    UIAdvancedSettingsDialog::saveData(varData);

    /* Get updated machine: */
    m_machine = varData.value<UISettingsDataMachine>().m_machine;
    /* If machine is OK => perform final operations: */
    if (m_machine.isOk())
    {
        UIMachineSettingsSystem *pSystemPage =
            qobject_cast<UIMachineSettingsSystem*>(m_pSelector->idToPage(MachineSettingsPageType_System));
#ifdef VBOX_WITH_3D_ACCELERATION
        UIMachineSettingsDisplay *pDisplayPage =
            qobject_cast<UIMachineSettingsDisplay*>(m_pSelector->idToPage(MachineSettingsPageType_Display));
#endif /* VBOX_WITH_3D_ACCELERATION */

#ifdef VBOX_WITH_3D_ACCELERATION
        /* Adjust graphics controller type if necessary: */
        if (   pDisplayPage
            && pDisplayPage->isAcceleration3DSelected()
            && pDisplayPage->graphicsControllerTypeCurrent() != pDisplayPage->graphicsControllerTypeRecommended())
            m_machine.GetGraphicsAdapter().SetGraphicsControllerType(pDisplayPage->graphicsControllerTypeRecommended());
#endif /* VBOX_WITH_3D_ACCELERATION */

        /* Enable OHCI controller if HID is enabled but no USB controllers present: */
        if (pSystemPage && pSystemPage->isHIDEnabled() && m_machine.GetUSBControllers().isEmpty())
            m_machine.AddUSBController("OHCI", KUSBControllerType_OHCI);

        /* Save settings finally: */
        m_machine.SaveSettings();
    }

    /* If machine is NOT OK => show the error message: */
    if (!m_machine.isOk())
        msgCenter().cannotSaveMachineSettings(m_machine, this);

    /* Handle serializartion finished: */
    sltHandleSerializationFinished();
}

QString UIAdvancedSettingsDialogMachine::title() const
{
    QString strDialogTitle;
    /* Get corresponding machine (required to compose dialog title): */
    CMachine comMachine = gpGlobalSession->virtualBox().FindMachine(m_uMachineId.toString());
    if (!comMachine.isNull())
        strDialogTitle = tr("%1 - Settings", "machine settings dialog name, starts from machine name").arg(comMachine.GetName());
    return strDialogTitle;
}

void UIAdvancedSettingsDialogMachine::recorrelate(UISettingsPage *pSettingsPage)
{
    switch (pSettingsPage->id())
    {
        /* General page correlations: */
        case MachineSettingsPageType_General:
        {
            /* Make changes on 'general' page influent 'display' page: */
            UIMachineSettingsGeneral *pGeneralPage = qobject_cast<UIMachineSettingsGeneral*>(pSettingsPage);
            UIMachineSettingsDisplay *pDisplayPage = qobject_cast<UIMachineSettingsDisplay*>(m_pSelector->idToPage(MachineSettingsPageType_Display));
            if (pGeneralPage && pDisplayPage)
                pDisplayPage->setGuestOSTypeId(pGeneralPage->guestOSTypeId());
            break;
        }
        /* System page correlations: */
        case MachineSettingsPageType_System:
        {
            /* Make changes on 'system' page influent 'general' and 'storage' page: */
            UIMachineSettingsSystem *pSystemPage = qobject_cast<UIMachineSettingsSystem*>(pSettingsPage);
            UIMachineSettingsStorage *pStoragePage = qobject_cast<UIMachineSettingsStorage*>(m_pSelector->idToPage(MachineSettingsPageType_Storage));
            if (pSystemPage && pStoragePage)
                pStoragePage->setChipsetType(pSystemPage->chipsetType());
            break;
        }
        /* USB page correlations: */
        case MachineSettingsPageType_USB:
        {
            /* Make changes on 'usb' page influent 'system' page: */
            UIMachineSettingsUSB *pUsbPage = qobject_cast<UIMachineSettingsUSB*>(pSettingsPage);
            UIMachineSettingsSystem *pSystemPage = qobject_cast<UIMachineSettingsSystem*>(m_pSelector->idToPage(MachineSettingsPageType_System));
            if (pUsbPage && pSystemPage)
                pSystemPage->setUSBEnabled(pUsbPage->isUSBEnabled());
            break;
        }
        default:
            break;
    }
}

void UIAdvancedSettingsDialogMachine::sltCategoryChanged(int cId)
{
    /* Raise priority of requested page: */
    if (serializeProcess())
        serializeProcess()->raisePriorityOfPage(cId);

    /* Call to base-class: */
    UIAdvancedSettingsDialog::sltCategoryChanged(cId);
}

void UIAdvancedSettingsDialogMachine::sltHandleSerializationFinished()
{
    /* Call to base-class: */
    UIAdvancedSettingsDialog::sltHandleSerializationFinished();

    /* Unlock the session if exists: */
    if (!m_session.isNull())
    {
        m_session.UnlockMachine();
        m_session = CSession();
        m_machine = CMachine();
        m_console = CConsole();
    }
}

void UIAdvancedSettingsDialogMachine::sltSessionStateChanged(const QUuid &uMachineId, const KSessionState enmSessionState)
{
    /* Ignore if serialization is in progress: */
    if (isSerializationInProgress())
        return;
    /* Ignore if thats NOT our VM: */
    if (uMachineId != m_uMachineId)
        return;

    /* Ignore if state was NOT actually changed: */
    if (m_enmSessionState == enmSessionState)
        return;
    /* Update current session state: */
    m_enmSessionState = enmSessionState;

    /* Recalculate configuration access level: */
    updateConfigurationAccessLevel();
}

void UIAdvancedSettingsDialogMachine::sltMachineStateChanged(const QUuid &uMachineId, const KMachineState enmMachineState)
{
    /* Ignore if serialization is in progress: */
    if (isSerializationInProgress())
        return;
    /* Ignore if thats NOT our VM: */
    if (uMachineId != m_uMachineId)
        return;

    /* Ignore if state was NOT actually changed: */
    if (m_enmMachineState == enmMachineState)
        return;
    /* Update current machine state: */
    m_enmMachineState = enmMachineState;

    /* Recalculate configuration access level: */
    updateConfigurationAccessLevel();
}

void UIAdvancedSettingsDialogMachine::sltMachineDataChanged(const QUuid &uMachineId)
{
    /* Ignore if serialization is in progress: */
    if (isSerializationInProgress())
        return;
    /* Ignore if thats NOT our VM: */
    if (uMachineId != m_uMachineId)
        return;

    /* Check if user had changed something and warn him about he will loose settings on reloading: */
    if (isSettingsChanged() && !msgCenter().confirmSettingsReloading(this))
        return;

    /* Reload data: */
    load();
}

void UIAdvancedSettingsDialogMachine::prepare()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/vm_settings_32px.png", ":/vm_settings_16px.png"));
#endif

    /* Make sure settings window will be updated on session/machine state/data changes: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSessionStateChange,
            this, &UIAdvancedSettingsDialogMachine::sltSessionStateChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIAdvancedSettingsDialogMachine::sltMachineStateChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineDataChange,
            this, &UIAdvancedSettingsDialogMachine::sltMachineDataChanged);

    /* Get corresponding machine (required to determine dialog type and page availability): */
    m_machine = gpGlobalSession->virtualBox().FindMachine(m_uMachineId.toString());
    AssertMsg(!m_machine.isNull(), ("Can't find corresponding machine!\n"));
    m_enmSessionState = m_machine.GetSessionState();
    m_enmMachineState = m_machine.GetState();

    /* Creating settings pages: */
    QList<MachineSettingsPageType> restrictedMachineSettingsPages = gEDataManager->restrictedMachineSettingsPages(m_uMachineId);
    for (int iPageIndex = MachineSettingsPageType_General; iPageIndex < MachineSettingsPageType_Max; ++iPageIndex)
    {
        /* Make sure page was not restricted: */
        if (restrictedMachineSettingsPages.contains(static_cast<MachineSettingsPageType>(iPageIndex)))
            continue;

        /* Make sure page is available: */
        if (isPageAvailable(iPageIndex))
        {
            UISettingsPage *pSettingsPage = 0;
            switch (iPageIndex)
            {
                /* General page: */
                case MachineSettingsPageType_General:
                {
                    pSettingsPage = new UIMachineSettingsGeneral;
                    addItem(":/machine_32px.png", ":/machine_24px.png", ":/machine_16px.png",
                            iPageIndex, "#general", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "generalsettings");
                    break;
                }
                /* System page: */
                case MachineSettingsPageType_System:
                {
                    pSettingsPage = new UIMachineSettingsSystem;
                    addItem(":/chipset_32px.png", ":/chipset_24px.png", ":/chipset_16px.png",
                            iPageIndex, "#system", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "settings-system");
                    break;
                }
                /* Display page: */
                case MachineSettingsPageType_Display:
                {
                    pSettingsPage = new UIMachineSettingsDisplay;
                    addItem(":/vrdp_32px.png", ":/vrdp_24px.png", ":/vrdp_16px.png",
                            iPageIndex, "#display", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "settings-display");
                    break;
                }
                /* Storage page: */
                case MachineSettingsPageType_Storage:
                {
                    pSettingsPage = new UIMachineSettingsStorage(m_pActionPool);
                    addItem(":/hd_32px.png", ":/hd_24px.png", ":/hd_16px.png",
                            iPageIndex, "#storage", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "settings-storage");
                    break;
                }
                /* Audio page: */
                case MachineSettingsPageType_Audio:
                {
                    pSettingsPage = new UIMachineSettingsAudio;
                    addItem(":/sound_32px.png", ":/sound_24px.png", ":/sound_16px.png",
                            iPageIndex, "#audio", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "settings-audio");
                    break;
                }
                /* Network page: */
                case MachineSettingsPageType_Network:
                {
                    pSettingsPage = new UIMachineSettingsNetwork;
                    addItem(":/nw_32px.png", ":/nw_24px.png", ":/nw_16px.png",
                            iPageIndex, "#network", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "settings-network");
                    break;
                }
                /* Serial page: */
                case MachineSettingsPageType_Serial:
                {
                    pSettingsPage = new UIMachineSettingsSerial;
                    addItem(":/serial_port_32px.png", ":/serial_port_24px.png", ":/serial_port_16px.png",
                            iPageIndex, "#serialPorts", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "serialports");
                    break;
                }
                /* USB page: */
                case MachineSettingsPageType_USB:
                {
                    pSettingsPage = new UIMachineSettingsUSB;
                    addItem(":/usb_32px.png", ":/usb_24px.png", ":/usb_16px.png",
                            iPageIndex, "#usb", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "usb-support");
                    break;
                }
                /* Shared Folders page: */
                case MachineSettingsPageType_SF:
                {
                    pSettingsPage = new UIMachineSettingsSF;
                    addItem(":/sf_32px.png", ":/sf_24px.png", ":/sf_16px.png",
                            iPageIndex, "#sharedFolders", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "ct_shared-folders");
                    break;
                }
                /* Interface page: */
                case MachineSettingsPageType_Interface:
                {
                    pSettingsPage = new UIMachineSettingsInterface(m_machine.GetId());
                    addItem(":/interface_32px.png", ":/interface_24px.png", ":/interface_16px.png",
                            iPageIndex, "#userInterface", pSettingsPage);
                    addPageHelpKeyword(iPageIndex, "user-interface");
                    break;
                }
                default:
                    break;
            }
        }
    }

    /* Calculate initial configuration access level: */
    setConfigurationAccessLevel(::configurationAccessLevel(m_enmSessionState, m_enmMachineState));

    /* Define initial dialog platform, for now it's constant.
     * Who knows, maybe we will be able to change it dynamically one day: */
    const CPlatform comPlatform = m_machine.GetPlatform();
    if (comPlatform.isNotNull())
    {
        const KPlatformArchitecture enmArch = comPlatform.GetArchitecture();
        Assert(enmArch != KPlatformArchitecture_None);
        if (enmArch != KPlatformArchitecture_None)
        {
            /* Push platform flag through whole UIEditor hierarchy: */
            QMap<QString, QVariant> optFlags = optionalFlags();
            switch (enmArch)
            {
                /* For x86/ARM we'll set the flag: */
                case KPlatformArchitecture_x86:
                case KPlatformArchitecture_ARM:
                    optFlags["arch"] = QVariant::fromValue(enmArch);
                    break;
                /* For rest of platforms (+None) we'll remove it: */
                default:
                    optFlags.remove("arch");
                    break;
            }
            setOptionalFlags(optFlags);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}

bool UIAdvancedSettingsDialogMachine::isPageAvailable(int iPageId) const
{
    if (m_machine.isNull())
        return false;

    switch (iPageId)
    {
        case MachineSettingsPageType_USB:
        {
            /* Check if USB is implemented: */
            if (!m_machine.GetUSBProxyAvailable())
                return false;
            /* Get the USB controller object: */
            CUSBControllerVector controllerColl = m_machine.GetUSBControllers();
            /* Show the machine error message if any: */
            if (   !m_machine.isReallyOk()
                && controllerColl.size() > 0
                && !m_machine.GetUSBControllers().isEmpty())
                msgCenter().warnAboutUnaccessibleUSB(m_machine, parentWidget());
            break;
        }
        default:
            break;
    }
    return true;
}

void UIAdvancedSettingsDialogMachine::updateConfigurationAccessLevel()
{
    /* Determine new configuration access level: */
    const ConfigurationAccessLevel newConfigurationAccessLevel = ::configurationAccessLevel(m_enmSessionState, m_enmMachineState);

    /* Make sure someting changed: */
    if (configurationAccessLevel() == newConfigurationAccessLevel)
        return;

    /* Should we warn a user about access level decrease? */
    const bool fShouldWeWarn = configurationAccessLevel() == ConfigurationAccessLevel_Full;

    /* Apply new configuration access level: */
    setConfigurationAccessLevel(newConfigurationAccessLevel);

    /* Show a warning about access level decrease if we should: */
    if (isSettingsChanged() && fShouldWeWarn)
        msgCenter().warnAboutStateChange(this);
}
