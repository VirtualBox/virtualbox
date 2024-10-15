/* $Id: UIMachineSettingsNetwork.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsNetwork class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QRegularExpression>
#include <QVBoxLayout>

/* GUI includes: */
#include "QITabWidget.h"
#include "UIErrorString.h"
#include "UIGlobalSession.h"
#include "UIMachineSettingsNetwork.h"
#include "UINetworkAttachmentEditor.h"
#include "UINetworkSettingsEditor.h"
#include "UITranslator.h"

/* COM includes: */
#include "CNATEngine.h"
#include "CNetworkAdapter.h"
#include "CPlatformProperties.h"


QString wipedOutString(const QString &strInputString)
{
    return strInputString.isEmpty() ? QString() : strInputString;
}


/** Machine settings: Network Adapter data structure. */
struct UIDataSettingsMachineNetworkAdapter
{
    /** Constructs data. */
    UIDataSettingsMachineNetworkAdapter()
        : m_iSlot(0)
        , m_fAdapterEnabled(false)
        , m_adapterType(KNetworkAdapterType_Null)
        , m_attachmentType(KNetworkAttachmentType_Null)
        , m_promiscuousMode(KNetworkAdapterPromiscModePolicy_Deny)
        , m_strBridgedAdapterName(QString())
        , m_strInternalNetworkName(QString())
        , m_strHostInterfaceName(QString())
        , m_strGenericDriverName(QString())
        , m_strGenericProperties(QString())
        , m_strNATNetworkName(QString())
#ifdef VBOX_WITH_CLOUD_NET
        , m_strCloudNetworkName(QString())
#endif
#ifdef VBOX_WITH_VMNET
        , m_strHostOnlyNetworkName(QString())
#endif
        , m_strMACAddress(QString())
        , m_fCableConnected(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsMachineNetworkAdapter &other) const
    {
        return true
               && (m_iSlot == other.m_iSlot)
               && (m_fAdapterEnabled == other.m_fAdapterEnabled)
               && (m_adapterType == other.m_adapterType)
               && (m_attachmentType == other.m_attachmentType)
               && (m_promiscuousMode == other.m_promiscuousMode)
               && (m_strBridgedAdapterName == other.m_strBridgedAdapterName)
               && (m_strInternalNetworkName == other.m_strInternalNetworkName)
               && (m_strHostInterfaceName == other.m_strHostInterfaceName)
               && (m_strGenericDriverName == other.m_strGenericDriverName)
               && (m_strGenericProperties == other.m_strGenericProperties)
               && (m_strNATNetworkName == other.m_strNATNetworkName)
#ifdef VBOX_WITH_CLOUD_NET
               && (m_strCloudNetworkName == other.m_strCloudNetworkName)
#endif
#ifdef VBOX_WITH_VMNET
               && (m_strHostOnlyNetworkName == other.m_strHostOnlyNetworkName)
#endif
               && (m_strMACAddress == other.m_strMACAddress)
               && (m_fCableConnected == other.m_fCableConnected)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineNetworkAdapter &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineNetworkAdapter &other) const { return !equal(other); }

    /** Holds the network adapter slot number. */
    int                               m_iSlot;
    /** Holds whether the network adapter is enabled. */
    bool                              m_fAdapterEnabled;
    /** Holds the network adapter type. */
    KNetworkAdapterType               m_adapterType;
    /** Holds the network attachment type. */
    KNetworkAttachmentType            m_attachmentType;
    /** Holds the network promiscuous mode policy. */
    KNetworkAdapterPromiscModePolicy  m_promiscuousMode;
    /** Holds the bridged adapter name. */
    QString                           m_strBridgedAdapterName;
    /** Holds the internal network name. */
    QString                           m_strInternalNetworkName;
    /** Holds the host interface name. */
    QString                           m_strHostInterfaceName;
    /** Holds the generic driver name. */
    QString                           m_strGenericDriverName;
    /** Holds the generic driver properties. */
    QString                           m_strGenericProperties;
    /** Holds the NAT network name. */
    QString                           m_strNATNetworkName;
#ifdef VBOX_WITH_CLOUD_NET
    /** Holds the cloud network name. */
    QString                           m_strCloudNetworkName;
#endif
#ifdef VBOX_WITH_VMNET
    /** Holds the host-only network name. */
    QString                           m_strHostOnlyNetworkName;
#endif
    /** Holds the network adapter MAC address. */
    QString                           m_strMACAddress;
    /** Holds whether the network adapter is connected. */
    bool                              m_fCableConnected;
};


/** Machine settings: Network page data structure. */
struct UIDataSettingsMachineNetwork
{
    /** Constructs data. */
    UIDataSettingsMachineNetwork() {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineNetwork & /* other */) const { return true; }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineNetwork & /* other */) const { return false; }
};


/*********************************************************************************************************************************
*   Class UIMachineSettingsNetwork implementation.                                                                               *
*********************************************************************************************************************************/

UIMachineSettingsNetwork::UIMachineSettingsNetwork()
    : m_pCache(0)
    , m_pTabWidget(0)
{
    prepare();
}

UIMachineSettingsNetwork::~UIMachineSettingsNetwork()
{
    cleanup();
}

bool UIMachineSettingsNetwork::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIMachineSettingsNetwork::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return;

    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache name lists: */
    refreshBridgedAdapterList();
    refreshInternalNetworkList(true);
    refreshHostInterfaceList();
    refreshGenericDriverList(true);
    refreshNATNetworkList();
#ifdef VBOX_WITH_CLOUD_NET
    refreshCloudNetworkList();
#endif
#ifdef VBOX_WITH_VMNET
    refreshHostOnlyNetworkList();
#endif

    /* Prepare old data: */
    UIDataSettingsMachineNetwork oldNetworkData;

    /* For each network adapter: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
    {
        /* Prepare old data: */
        UIDataSettingsMachineNetworkAdapter oldAdapterData;

        /* Check whether adapter is valid: */
        const CNetworkAdapter &comAdapter = m_machine.GetNetworkAdapter(iSlot);
        if (!comAdapter.isNull())
        {
            /* Gather old data: */
            oldAdapterData.m_iSlot = iSlot;
            oldAdapterData.m_fAdapterEnabled = comAdapter.GetEnabled();
            oldAdapterData.m_attachmentType = comAdapter.GetAttachmentType();
            oldAdapterData.m_strBridgedAdapterName = wipedOutString(comAdapter.GetBridgedInterface());
            oldAdapterData.m_strInternalNetworkName = wipedOutString(comAdapter.GetInternalNetwork());
            oldAdapterData.m_strHostInterfaceName = wipedOutString(comAdapter.GetHostOnlyInterface());
            oldAdapterData.m_strGenericDriverName = wipedOutString(comAdapter.GetGenericDriver());
            oldAdapterData.m_strNATNetworkName = wipedOutString(comAdapter.GetNATNetwork());
#ifdef VBOX_WITH_CLOUD_NET
            oldAdapterData.m_strCloudNetworkName = wipedOutString(comAdapter.GetCloudNetwork());
#endif
#ifdef VBOX_WITH_VMNET
            oldAdapterData.m_strHostOnlyNetworkName = wipedOutString(comAdapter.GetHostOnlyNetwork());
#endif
            oldAdapterData.m_adapterType = comAdapter.GetAdapterType();
            oldAdapterData.m_promiscuousMode = comAdapter.GetPromiscModePolicy();
            oldAdapterData.m_strMACAddress = comAdapter.GetMACAddress();
            oldAdapterData.m_strGenericProperties = loadGenericProperties(comAdapter);
            oldAdapterData.m_fCableConnected = comAdapter.GetCableConnected();
            foreach (const QString &strRedirect, comAdapter.GetNATEngine().GetRedirects())
            {
                /* Gather old data & cache key: */
                const QStringList &forwardingData = strRedirect.split(',');
                AssertMsg(forwardingData.size() == 6, ("Redirect rule should be composed of 6 parts!\n"));
                const UIDataPortForwardingRule oldForwardingData(forwardingData.at(0),
                                                                 (KNATProtocol)forwardingData.at(1).toUInt(),
                                                                 forwardingData.at(2),
                                                                 forwardingData.at(3).toUInt(),
                                                                 forwardingData.at(4),
                                                                 forwardingData.at(5).toUInt());
                const QString &strForwardingKey = forwardingData.at(0);
                /* Cache old data: */
                m_pCache->child(iSlot).child(strForwardingKey).cacheInitialData(oldForwardingData);
            }
        }

        /* Cache old data: */
        m_pCache->child(iSlot).cacheInitialData(oldAdapterData);
    }

    /* Cache old data: */
    m_pCache->cacheInitialData(oldNetworkData);

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsNetwork::getFromCache()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return;

    /* Load old data from cache: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
        getFromCache(iSlot, m_pCache->child(iSlot));

    /* Apply language settings: */
    sltRetranslateUI();

    /* Polish page finally: */
    polishPage();

    /* Revalidate: */
    revalidate();
}

void UIMachineSettingsNetwork::putToCache()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return;

    /* Prepare new data: */
    UIDataSettingsMachineNetwork newNetworkData;

    /* Gather new data to cache: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
        putToCache(iSlot, m_pCache->child(iSlot));

    /* Cache new data: */
    m_pCache->cacheCurrentData(newNetworkData);
}

void UIMachineSettingsNetwork::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

bool UIMachineSettingsNetwork::validate(QList<UIValidationMessage> &messages)
{
    /* Sanity check: */
    if (!m_pTabWidget)
        return false;

    /* Pass by default: */
    bool fValid = true;

    /* Delegate validation to particular tab-editor: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
        if (!validate(iSlot, messages))
            fValid = false;

    /* Return result: */
    return fValid;
}

void UIMachineSettingsNetwork::sltRetranslateUI()
{
    /* Sanity check: */
    if (!m_pTabWidget)
        return;

    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
    {
        m_pTabWidget->setTabText(iSlot, tabTitle(iSlot));
        reloadAlternatives(iSlot);
    }
}

void UIMachineSettingsNetwork::handleFilterChange()
{
    /* Show tabs from 2nd to 4th in expert mode only: */
    if (m_pTabWidget)
        for (int i = 1; i < m_pTabWidget->count(); ++i)
            m_pTabWidget->setTabVisible(i, m_fInExpertMode);
}

void UIMachineSettingsNetwork::polishPage()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return;

    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
    {
        m_pTabWidget->setTabEnabled(iSlot,
                                    isMachineOffline() ||
                                    (isMachineInValidMode() &&
                                     m_pCache->childCount() > iSlot &&
                                     m_pCache->child(iSlot).base().m_fAdapterEnabled));
        polishTab(iSlot);
    }
}

void UIMachineSettingsNetwork::sltHandleAlternativeNameChange()
{
    /* Sanity check: */
    if (!m_pTabWidget)
        return;

    /* Determine the sender tab-editor: */
    UINetworkSettingsEditor *pSender = qobject_cast<UINetworkSettingsEditor*>(sender());
    AssertPtrReturnVoid(pSender);

    /* Determine the sender slot: */
    const int iSenderSlot = m_tabEditors.indexOf(pSender);

    /* Check if we need to update other adapter tabs if
     * alternative name for certain type is changed: */
    bool fUpdateOthers = false;
    switch (attachmentType(iSenderSlot))
    {
        case KNetworkAttachmentType_Internal:
        {
            if (!pSender->valueName(attachmentType(iSenderSlot)).isNull())
            {
                refreshInternalNetworkList();
                fUpdateOthers = true;
            }
            break;
        }
        case KNetworkAttachmentType_Generic:
        {
            if (!pSender->valueName(attachmentType(iSenderSlot)).isNull())
            {
                refreshGenericDriverList();
                fUpdateOthers = true;
            }
            break;
        }
        default:
            break;
    }

    /* Update alternatives for all the tabs besides the sender: */
    if (fUpdateOthers)
        for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
            if (iSlot != iSenderSlot)
                reloadAlternatives(iSlot);

    /* Revalidate full page: */
    revalidate();
}

void UIMachineSettingsNetwork::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheMachineNetwork;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIMachineSettingsNetwork::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        /* Prepare tab-widget: */
        m_pTabWidget = new QITabWidget(this);
        if (m_pTabWidget)
        {
            /* How many adapters to display: */
            /** @todo r=klaus this needs to be done based on the actual chipset type of the VM,
              * but in this place the m_machine field isn't set yet. My observation (on Linux)
              * is that the limitation to 4 isn't necessary any more, but this needs to be checked
              * on all platforms to be certain that it's usable everywhere. */
            const ulong uCount = qMin((ULONG)4, gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxNetworkAdapters(KChipsetType_PIIX3));

            /* Create corresponding adapter tabs: */
            for (ulong uSlot = 0; uSlot < uCount; ++uSlot)
                prepareTab();

            pLayoutMain->addWidget(m_pTabWidget);
        }
    }
}

void UIMachineSettingsNetwork::prepareTab()
{
    /* Prepare tab: */
    UIEditor *pTab = new UIEditor(m_pTabWidget);
    if (pTab)
    {
        /* Prepare tab layout: */
        QVBoxLayout *pLayout = new QVBoxLayout(pTab);
        if (pLayout)
        {
#ifdef VBOX_WS_MAC
            /* On Mac OS X we can do a bit of smoothness: */
            int iLeft, iTop, iRight, iBottom;
            pLayout->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
            pLayout->setContentsMargins(iLeft / 2, iTop / 2, iRight / 2, iBottom / 2);
#endif

            /* Prepare settings editor: */
            UINetworkSettingsEditor *pEditor = new UINetworkSettingsEditor(this);
            if (pEditor)
            {
                m_tabEditors << pEditor;
                prepareConnections(pEditor);
                pTab->addEditor(pEditor);
                pLayout->addWidget(pEditor);
            }

            pLayout->addStretch();
        }

        addEditor(pTab);
        m_pTabWidget->addTab(pTab, QString());
    }
}

void UIMachineSettingsNetwork::prepareConnections(UINetworkSettingsEditor *pTabEditor)
{
    /* Attachment connections: */
    connect(pTabEditor, &UINetworkSettingsEditor::sigFeatureStateChanged,
            this, &UIMachineSettingsNetwork::revalidate);
    connect(pTabEditor, &UINetworkSettingsEditor::sigAttachmentTypeChanged,
            this, &UIMachineSettingsNetwork::revalidate);
    connect(pTabEditor, &UINetworkSettingsEditor::sigAlternativeNameChanged,
            this, &UIMachineSettingsNetwork::sltHandleAlternativeNameChange);

    /* Advanced connections: */
    connect(pTabEditor, &UINetworkSettingsEditor::sigMACAddressChanged,
            this, &UIMachineSettingsNetwork::revalidate);
}

void UIMachineSettingsNetwork::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

void UIMachineSettingsNetwork::polishTab(int iSlot)
{
    /* Acquire tab-editor: */
    UINetworkSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturnVoid(pTabEditor);

    /* General stuff: */
    pTabEditor->setFeatureAvailable(isMachineOffline());

    /* Attachment stuff: */
    pTabEditor->setAttachmentOptionsAvailable(isMachineInValidMode());

    /* Advanced stuff: */
    pTabEditor->setAdapterOptionsAvailable(isMachineOffline());
    pTabEditor->setPromiscuousOptionsAvailable(   attachmentType(iSlot) != KNetworkAttachmentType_Null
                                               && attachmentType(iSlot) != KNetworkAttachmentType_Generic
                                               && attachmentType(iSlot) != KNetworkAttachmentType_NAT);
    pTabEditor->setMACOptionsAvailable(isMachineOffline());
    pTabEditor->setGenericPropertiesAvailable(attachmentType(iSlot) == KNetworkAttachmentType_Generic);
    pTabEditor->setCableOptionsAvailable(isMachineInValidMode());
    pTabEditor->setForwardingOptionsAvailable(attachmentType(iSlot) == KNetworkAttachmentType_NAT);
}

void UIMachineSettingsNetwork::getFromCache(int iSlot, const UISettingsCacheMachineNetworkAdapter &adapterCache)
{
    /* Acquire tab-editor: */
    UINetworkSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturnVoid(pTabEditor);

    /* Get old data: */
    const UIDataSettingsMachineNetworkAdapter &oldAdapterData = adapterCache.base();

    /* Load adapter activity state: */
    pTabEditor->setFeatureEnabled(oldAdapterData.m_fAdapterEnabled);

    /* Load attachment type: */
    pTabEditor->setValueType(oldAdapterData.m_attachmentType);
    /* Load alternative names: */
    pTabEditor->setValueName(KNetworkAttachmentType_Bridged, wipedOutString(oldAdapterData.m_strBridgedAdapterName));
    pTabEditor->setValueName(KNetworkAttachmentType_Internal, wipedOutString(oldAdapterData.m_strInternalNetworkName));
    pTabEditor->setValueName(KNetworkAttachmentType_HostOnly, wipedOutString(oldAdapterData.m_strHostInterfaceName));
    pTabEditor->setValueName(KNetworkAttachmentType_Generic, wipedOutString(oldAdapterData.m_strGenericDriverName));
    pTabEditor->setValueName(KNetworkAttachmentType_NATNetwork, wipedOutString(oldAdapterData.m_strNATNetworkName));
#ifdef VBOX_WITH_CLOUD_NET
    pTabEditor->setValueName(KNetworkAttachmentType_Cloud, wipedOutString(oldAdapterData.m_strCloudNetworkName));
#endif
#ifdef VBOX_WITH_VMNET
    pTabEditor->setValueName(KNetworkAttachmentType_HostOnlyNetwork, wipedOutString(oldAdapterData.m_strHostOnlyNetworkName));
#endif

    /* Load settings: */
    pTabEditor->setAdapterType(oldAdapterData.m_adapterType);
    pTabEditor->setPromiscuousMode(oldAdapterData.m_promiscuousMode);
    pTabEditor->setMACAddress(oldAdapterData.m_strMACAddress);
    pTabEditor->setGenericProperties(oldAdapterData.m_strGenericProperties);
    pTabEditor->setCableConnected(oldAdapterData.m_fCableConnected);

    /* Load port forwarding rules: */
    UIPortForwardingDataList portForwardingRules;
    for (int i = 0; i < adapterCache.childCount(); ++i)
        portForwardingRules << adapterCache.child(i).base();
    pTabEditor->setPortForwardingRules(portForwardingRules);

    /* Reload alternatives: */
    reloadAlternatives(iSlot);
}

void UIMachineSettingsNetwork::putToCache(int iSlot, UISettingsCacheMachineNetworkAdapter &adapterCache)
{
    /* Acquire tab-editor: */
    UINetworkSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturnVoid(pTabEditor);

    /* Prepare new data: */
    UIDataSettingsMachineNetworkAdapter newAdapterData;

    /* Save slot number: */
    newAdapterData.m_iSlot = iSlot;

    /* Save adapter activity state: */
    newAdapterData.m_fAdapterEnabled = pTabEditor->isFeatureEnabled();

    /* Save attachment type & alternative name: */
    newAdapterData.m_attachmentType = attachmentType(iSlot);
    newAdapterData.m_strBridgedAdapterName = pTabEditor->valueName(KNetworkAttachmentType_Bridged);
    newAdapterData.m_strInternalNetworkName = pTabEditor->valueName(KNetworkAttachmentType_Internal);
    newAdapterData.m_strHostInterfaceName = pTabEditor->valueName(KNetworkAttachmentType_HostOnly);
    newAdapterData.m_strGenericDriverName = pTabEditor->valueName(KNetworkAttachmentType_Generic);
    newAdapterData.m_strNATNetworkName = pTabEditor->valueName(KNetworkAttachmentType_NATNetwork);
#ifdef VBOX_WITH_CLOUD_NET
    newAdapterData.m_strCloudNetworkName = pTabEditor->valueName(KNetworkAttachmentType_Cloud);
#endif
#ifdef VBOX_WITH_VMNET
    newAdapterData.m_strHostOnlyNetworkName = pTabEditor->valueName(KNetworkAttachmentType_HostOnlyNetwork);
#endif

    /* Save settings: */
    newAdapterData.m_adapterType = pTabEditor->adapterType();
    newAdapterData.m_promiscuousMode = pTabEditor->promiscuousMode();
    newAdapterData.m_strMACAddress = pTabEditor->macAddress();
    newAdapterData.m_strGenericProperties = pTabEditor->genericProperties();
    newAdapterData.m_fCableConnected = pTabEditor->cableConnected();

    /* Save port forwarding rules: */
    foreach (const UIDataPortForwardingRule &rule, pTabEditor->portForwardingRules())
        adapterCache.child(rule.name).cacheCurrentData(rule);

    /* Cache new data: */
    adapterCache.cacheCurrentData(newAdapterData);
}

void UIMachineSettingsNetwork::reloadAlternatives(int iSlot)
{
    /* Acquire tab-editor: */
    UINetworkSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturnVoid(pTabEditor);

    pTabEditor->setValueNames(KNetworkAttachmentType_Bridged, bridgedAdapterList());
    pTabEditor->setValueNames(KNetworkAttachmentType_Internal, internalNetworkList());
    pTabEditor->setValueNames(KNetworkAttachmentType_HostOnly, hostInterfaceList());
    pTabEditor->setValueNames(KNetworkAttachmentType_Generic, genericDriverList());
    pTabEditor->setValueNames(KNetworkAttachmentType_NATNetwork, natNetworkList());
#ifdef VBOX_WITH_CLOUD_NET
    pTabEditor->setValueNames(KNetworkAttachmentType_Cloud, cloudNetworkList());
#endif
#ifdef VBOX_WITH_VMNET
    pTabEditor->setValueNames(KNetworkAttachmentType_HostOnlyNetwork, hostOnlyNetworkList());
#endif
}

KNetworkAttachmentType UIMachineSettingsNetwork::attachmentType(int iSlot) const
{
    /* Acquire tab-editor: */
    UINetworkSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturn(pTabEditor, KNetworkAttachmentType_Null);
    return pTabEditor->valueType();
}

QString UIMachineSettingsNetwork::alternativeName(int iSlot,
                                                  KNetworkAttachmentType enmType /* = KNetworkAttachmentType_Null */) const
{
    /* Acquire tab-editor: */
    UINetworkSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturn(pTabEditor, QString());
    if (enmType == KNetworkAttachmentType_Null)
        enmType = attachmentType(iSlot);
    return pTabEditor->valueName(enmType);
}

bool UIMachineSettingsNetwork::validate(int iSlot, QList<UIValidationMessage> &messages)
{
    /* Acquire tab-editor: */
    UINetworkSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturn(pTabEditor, false);

    /* Pass by default: */
    bool fPass = true;

    /* Prepare message: */
    UIValidationMessage message;
    message.first = UITranslator::removeAccelMark(tabTitle(iSlot));

    /* Validate enabled adapter only: */
    if (pTabEditor->isFeatureEnabled())
    {
        /* Validate alternatives: */
        switch (attachmentType(iSlot))
        {
            case KNetworkAttachmentType_Bridged:
            {
                if (alternativeName(iSlot).isNull())
                {
                    message.second << tr("No bridged network adapter is currently selected.");
                    fPass = false;
                }
                break;
            }
            case KNetworkAttachmentType_Internal:
            {
                if (alternativeName(iSlot).isNull())
                {
                    message.second << tr("No internal network name is currently specified.");
                    fPass = false;
                }
                break;
            }
#ifndef VBOX_WITH_VMNET
            case KNetworkAttachmentType_HostOnly:
            {
                if (alternativeName(iSlot).isNull())
                {
                    message.second << tr("No host-only network adapter is currently selected.");
                    fPass = false;
                }
                break;
            }
#else /* VBOX_WITH_VMNET */
            case KNetworkAttachmentType_HostOnly:
            {
                message.second << tr("Host-only adapters are no longer supported, use host-only networks instead.");
                fPass = false;
                break;
            }
#endif /* VBOX_WITH_VMNET */
            case KNetworkAttachmentType_Generic:
            {
                if (alternativeName(iSlot).isNull())
                {
                    message.second << tr("No generic driver is currently selected.");
                    fPass = false;
                }
                break;
            }
            case KNetworkAttachmentType_NATNetwork:
            {
                if (alternativeName(iSlot).isNull())
                {
                    message.second << tr("No NAT network name is currently specified.");
                    fPass = false;
                }
                break;
            }
#ifdef VBOX_WITH_CLOUD_NET
            case KNetworkAttachmentType_Cloud:
            {
                if (alternativeName(iSlot).isNull())
                {
                    message.second << tr("No cloud network name is currently specified.");
                    fPass = false;
                }
                break;
            }
#endif /* VBOX_WITH_CLOUD_NET */
#ifdef VBOX_WITH_VMNET
            case KNetworkAttachmentType_HostOnlyNetwork:
            {
                if (alternativeName(iSlot).isNull())
                {
                    message.second << tr("No host-only network name is currently specified.");
                    fPass = false;
                }
                break;
            }
#endif /* VBOX_WITH_VMNET */
            default:
                break;
        }

        /* Validate MAC-address length: */
        if (pTabEditor->macAddress().size() < 12)
        {
            message.second << tr("The MAC address must be 12 hexadecimal digits long.");
            fPass = false;
        }

        /* Make sure MAC-address is unicast: */
        if (pTabEditor->macAddress().size() >= 2)
        {
            if (pTabEditor->macAddress().indexOf(QRegularExpression("^[0-9A-Fa-f][02468ACEace]")) != 0)
            {
                message.second << tr("The second digit in the MAC address may not be odd as only unicast addresses are allowed.");
                fPass = false;
            }
        }
    }

    /* Serialize message: */
    if (!message.second.isEmpty())
        messages << message;

    /* Return result: */
    return fPass;
}

void UIMachineSettingsNetwork::refreshBridgedAdapterList()
{
    /* Reload bridged adapters: */
    m_bridgedAdapterList = UINetworkAttachmentEditor::bridgedAdapters();
}

void UIMachineSettingsNetwork::refreshInternalNetworkList(bool fFullRefresh /* = false */)
{
    /* Sanity check: */
    if (!m_pTabWidget)
        return;

    /* Reload internal network list: */
    m_internalNetworkList.clear();
    /* Get internal network names from other VMs: */
    if (fFullRefresh)
        m_internalNetworkListSaved = UINetworkAttachmentEditor::internalNetworks();
    m_internalNetworkList << m_internalNetworkListSaved;
    /* Append internal network list with names from all the tabs: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
    {
        const QString strName = alternativeName(iSlot, KNetworkAttachmentType_Internal);
        if (!strName.isEmpty() && !m_internalNetworkList.contains(strName))
            m_internalNetworkList << strName;
    }
}

#ifdef VBOX_WITH_CLOUD_NET
void UIMachineSettingsNetwork::refreshCloudNetworkList()
{
    /* Reload cloud network list: */
    m_cloudNetworkList = UINetworkAttachmentEditor::cloudNetworks();
}
#endif /* VBOX_WITH_CLOUD_NET */

#ifdef VBOX_WITH_VMNET
void UIMachineSettingsNetwork::refreshHostOnlyNetworkList()
{
    /* Reload host-only network list: */
    m_hostOnlyNetworkList = UINetworkAttachmentEditor::hostOnlyNetworks();
}
#endif /* VBOX_WITH_VMNET */

void UIMachineSettingsNetwork::refreshHostInterfaceList()
{
    /* Reload host interfaces: */
    m_hostInterfaceList = UINetworkAttachmentEditor::hostInterfaces();
}

void UIMachineSettingsNetwork::refreshGenericDriverList(bool fFullRefresh /* = false */)
{
    /* Sanity check: */
    if (!m_pTabWidget)
        return;

    /* Load generic driver list: */
    m_genericDriverList.clear();
    /* Get generic driver names from other VMs: */
    if (fFullRefresh)
        m_genericDriverListSaved = UINetworkAttachmentEditor::genericDrivers();
    m_genericDriverList << m_genericDriverListSaved;
    /* Append generic driver list with names from all the tabs: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
    {
        const QString strName = alternativeName(iSlot, KNetworkAttachmentType_Generic);
        if (!strName.isEmpty() && !m_genericDriverList.contains(strName))
            m_genericDriverList << strName;
    }
}

void UIMachineSettingsNetwork::refreshNATNetworkList()
{
    /* Reload nat networks: */
    m_natNetworkList = UINetworkAttachmentEditor::natNetworks();
}

/* static */
QString UIMachineSettingsNetwork::tabTitle(int iSlot)
{
    return QApplication::translate("UICommon", "Adapter %1").arg(QString("&%1").arg(iSlot + 1));
}

/* static */
QString UIMachineSettingsNetwork::loadGenericProperties(const CNetworkAdapter &adapter)
{
    /* Prepare formatted string: */
    QVector<QString> names;
    QVector<QString> props;
    props = adapter.GetProperties(QString(), names);
    QString strResult;
    /* Load generic properties: */
    for (int i = 0; i < names.size(); ++i)
    {
        strResult += names[i] + "=" + props[i];
        if (i < names.size() - 1)
          strResult += "\n";
    }
    /* Return formatted string: */
    return strResult;
}

/* static */
bool UIMachineSettingsNetwork::saveGenericProperties(CNetworkAdapter &comAdapter, const QString &strProperties)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Save generic properties: */
    if (fSuccess)
    {
        /* Acquire 'added' properties: */
        const QStringList newProps = strProperties.split("\n");

        /* Insert 'added' properties: */
        QHash<QString, QString> hash;
        for (int i = 0; fSuccess && i < newProps.size(); ++i)
        {
            /* Parse property line: */
            const QString strLine = newProps.at(i);
            const QString strKey = strLine.section('=', 0, 0);
            const QString strVal = strLine.section('=', 1, -1);
            if (strKey.isEmpty() || strVal.isEmpty())
                continue;
            /* Save property in the adapter and the hash: */
            comAdapter.SetProperty(strKey, strVal);
            fSuccess = comAdapter.isOk();
            hash[strKey] = strVal;
        }

        /* Acquire actual properties ('added' and 'removed'): */
        QVector<QString> names;
        QVector<QString> props;
        if (fSuccess)
        {
            props = comAdapter.GetProperties(QString(), names);
            fSuccess = comAdapter.isOk();
        }

        /* Exclude 'removed' properties: */
        for (int i = 0; fSuccess && i < names.size(); ++i)
        {
            /* Get property name and value: */
            const QString strKey = names.at(i);
            const QString strVal = props.at(i);
            if (strVal == hash.value(strKey))
                continue;
            /* Remove property from the adapter: */
            // Actually we are _replacing_ property value,
            // not _removing_ it at all, but we are replacing it
            // with default constructed value, which is QString().
            comAdapter.SetProperty(strKey, hash.value(strKey));
            fSuccess = comAdapter.isOk();
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsNetwork::saveData()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save network settings from cache: */
    if (fSuccess && isMachineInValidMode() && m_pCache->wasChanged())
    {
        /* For each adapter: */
        for (int iSlot = 0; fSuccess && iSlot < m_pTabWidget->count(); ++iSlot)
            fSuccess = saveAdapterData(iSlot);
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsNetwork::saveAdapterData(int iSlot)
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save adapter settings from cache: */
    if (fSuccess && m_pCache->child(iSlot).wasChanged())
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineNetworkAdapter &oldAdapterData = m_pCache->child(iSlot).base();
        /* Get new data from cache: */
        const UIDataSettingsMachineNetworkAdapter &newAdapterData = m_pCache->child(iSlot).data();

        /* Get network adapter for further activities: */
        CNetworkAdapter comAdapter = m_machine.GetNetworkAdapter(iSlot);
        fSuccess = m_machine.isOk() && comAdapter.isNotNull();

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
        else
        {
            /* Save whether the adapter is enabled: */
            if (fSuccess && isMachineOffline() && newAdapterData.m_fAdapterEnabled != oldAdapterData.m_fAdapterEnabled)
            {
                comAdapter.SetEnabled(newAdapterData.m_fAdapterEnabled);
                fSuccess = comAdapter.isOk();
            }
            /* Save adapter type: */
            if (fSuccess && isMachineOffline() && newAdapterData.m_adapterType != oldAdapterData.m_adapterType)
            {
                comAdapter.SetAdapterType(newAdapterData.m_adapterType);
                fSuccess = comAdapter.isOk();
            }
            /* Save adapter MAC address: */
            if (fSuccess && isMachineOffline() && newAdapterData.m_strMACAddress != oldAdapterData.m_strMACAddress)
            {
                comAdapter.SetMACAddress(newAdapterData.m_strMACAddress);
                fSuccess = comAdapter.isOk();
            }
            /* Save adapter attachment type: */
            switch (newAdapterData.m_attachmentType)
            {
                case KNetworkAttachmentType_Bridged:
                {
                    if (fSuccess && newAdapterData.m_strBridgedAdapterName != oldAdapterData.m_strBridgedAdapterName)
                    {
                        comAdapter.SetBridgedInterface(newAdapterData.m_strBridgedAdapterName);
                        fSuccess = comAdapter.isOk();
                    }
                    break;
                }
                case KNetworkAttachmentType_Internal:
                {
                    if (fSuccess && newAdapterData.m_strInternalNetworkName != oldAdapterData.m_strInternalNetworkName)
                    {
                        comAdapter.SetInternalNetwork(newAdapterData.m_strInternalNetworkName);
                        fSuccess = comAdapter.isOk();
                    }
                    break;
                }
                case KNetworkAttachmentType_HostOnly:
                {
                    if (fSuccess && newAdapterData.m_strHostInterfaceName != oldAdapterData.m_strHostInterfaceName)
                    {
                        comAdapter.SetHostOnlyInterface(newAdapterData.m_strHostInterfaceName);
                        fSuccess = comAdapter.isOk();
                    }
                    break;
                }
                case KNetworkAttachmentType_Generic:
                {
                    if (fSuccess && newAdapterData.m_strGenericDriverName != oldAdapterData.m_strGenericDriverName)
                    {
                        comAdapter.SetGenericDriver(newAdapterData.m_strGenericDriverName);
                        fSuccess = comAdapter.isOk();
                    }
                    if (fSuccess && newAdapterData.m_strGenericProperties != oldAdapterData.m_strGenericProperties)
                        fSuccess = saveGenericProperties(comAdapter, newAdapterData.m_strGenericProperties);
                    break;
                }
                case KNetworkAttachmentType_NATNetwork:
                {
                    if (fSuccess && newAdapterData.m_strNATNetworkName != oldAdapterData.m_strNATNetworkName)
                    {
                        comAdapter.SetNATNetwork(newAdapterData.m_strNATNetworkName);
                        fSuccess = comAdapter.isOk();
                    }
                    break;
                }
#ifdef VBOX_WITH_CLOUD_NET
                case KNetworkAttachmentType_Cloud:
                {
                    if (fSuccess && newAdapterData.m_strCloudNetworkName != oldAdapterData.m_strCloudNetworkName)
                    {
                        comAdapter.SetCloudNetwork(newAdapterData.m_strCloudNetworkName);
                        fSuccess = comAdapter.isOk();
                    }
                    break;
                }
#endif /* VBOX_WITH_CLOUD_NET */
#ifdef VBOX_WITH_VMNET
                case KNetworkAttachmentType_HostOnlyNetwork:
                {
                    if (fSuccess && newAdapterData.m_strHostOnlyNetworkName != oldAdapterData.m_strHostOnlyNetworkName)
                    {
                        comAdapter.SetHostOnlyNetwork(newAdapterData.m_strHostOnlyNetworkName);
                        fSuccess = comAdapter.isOk();
                    }
                    break;
                }
#endif /* VBOX_WITH_VMNET */
                default:
                    break;
            }
            if (fSuccess && newAdapterData.m_attachmentType != oldAdapterData.m_attachmentType)
            {
                comAdapter.SetAttachmentType(newAdapterData.m_attachmentType);
                fSuccess = comAdapter.isOk();
            }
            /* Save adapter promiscuous mode: */
            if (fSuccess && newAdapterData.m_promiscuousMode != oldAdapterData.m_promiscuousMode)
            {
                comAdapter.SetPromiscModePolicy(newAdapterData.m_promiscuousMode);
                fSuccess = comAdapter.isOk();
            }
            /* Save whether the adapter cable connected: */
            if (fSuccess && newAdapterData.m_fCableConnected != oldAdapterData.m_fCableConnected)
            {
                comAdapter.SetCableConnected(newAdapterData.m_fCableConnected);
                fSuccess = comAdapter.isOk();
            }

            /* Get NAT engine for further activities: */
            CNATEngine comEngine;
            if (fSuccess)
            {
                comEngine = comAdapter.GetNATEngine();
                fSuccess = comAdapter.isOk() && comEngine.isNotNull();
            }

            /* Show error message if necessary: */
            if (!fSuccess)
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comAdapter));
            else
            {
                /* Save adapter port forwarding rules: */
                if (   oldAdapterData.m_attachmentType == KNetworkAttachmentType_NAT
                    || newAdapterData.m_attachmentType == KNetworkAttachmentType_NAT)
                {
                    /* For each rule: */
                    for (int iRule = 0; fSuccess && iRule < m_pCache->child(iSlot).childCount(); ++iRule)
                    {
                        /* Get rule cache: */
                        const UISettingsCachePortForwardingRule &ruleCache = m_pCache->child(iSlot).child(iRule);

                        /* Remove rule marked for 'remove' or 'update': */
                        if (ruleCache.wasRemoved() || ruleCache.wasUpdated())
                        {
                            comEngine.RemoveRedirect(ruleCache.base().name);
                            fSuccess = comEngine.isOk();
                        }
                    }
                    for (int iRule = 0; fSuccess && iRule < m_pCache->child(iSlot).childCount(); ++iRule)
                    {
                        /* Get rule cache: */
                        const UISettingsCachePortForwardingRule &ruleCache = m_pCache->child(iSlot).child(iRule);

                        /* Create rule marked for 'create' or 'update': */
                        if (ruleCache.wasCreated() || ruleCache.wasUpdated())
                        {
                            comEngine.AddRedirect(ruleCache.data().name, ruleCache.data().protocol,
                                                  ruleCache.data().hostIp, ruleCache.data().hostPort.value(),
                                                  ruleCache.data().guestIp, ruleCache.data().guestPort.value());
                            fSuccess = comEngine.isOk();
                        }
                    }

                    /* Show error message if necessary: */
                    if (!fSuccess)
                        notifyOperationProgressError(UIErrorString::formatErrorInfo(comEngine));
                }
            }
        }
    }
    /* Return result: */
    return fSuccess;
}
