/* $Id: UINetworkManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkManager class implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <QHeaderView>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QITabWidget.h"
#include "QITreeWidget.h"
#include "QIToolBar.h"
#include "UIActionPoolManager.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIDetailsWidgetCloudNetwork.h"
#include "UIDetailsWidgetHostNetwork.h"
#include "UIDetailsWidgetNATNetwork.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UINetworkManager.h"
#include "UINetworkManagerUtils.h"
#include "UINotificationCenter.h"
#include "UIShortcutPool.h"
#ifdef VBOX_WS_MAC
# include "UIWindowMenuManager.h"
#endif
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CCloudNetwork.h"
#include "CDHCPServer.h"
#include "CHost.h"
#ifdef VBOX_WS_MAC
# include "CHostOnlyNetwork.h"
#else
# include "CHostNetworkInterface.h"
#endif
#include "CNATNetwork.h"


/** Tab-widget indexes. */
enum TabWidgetIndex
{
    TabWidgetIndex_HostNetwork,
    TabWidgetIndex_NATNetwork,
    TabWidgetIndex_CloudNetwork,
};


#ifdef VBOX_WS_MAC
/** Host network tree-widget column indexes. */
enum HostNetworkColumn
{
    HostNetworkColumn_Name,
    HostNetworkColumn_Mask,
    HostNetworkColumn_LBnd,
    HostNetworkColumn_UBnd,
    HostNetworkColumn_Max,
};

#else /* !VBOX_WS_MAC */

/** Host network tree-widget column indexes. */
enum HostNetworkColumn
{
    HostNetworkColumn_Name,
    HostNetworkColumn_IPv4,
    HostNetworkColumn_IPv6,
    HostNetworkColumn_DHCP,
    HostNetworkColumn_Max,
};
#endif /* !VBOX_WS_MAC */


/** NAT network tree-widget column indexes. */
enum NATNetworkColumn
{
    NATNetworkColumn_Name,
    NATNetworkColumn_IPv4,
    NATNetworkColumn_IPv6,
    NATNetworkColumn_DHCP,
    NATNetworkColumn_Max,
};


/** Cloud network tree-widget column indexes. */
enum CloudNetworkColumn
{
    CloudNetworkColumn_Name,
    CloudNetworkColumn_Provider,
    CloudNetworkColumn_Profile,
    CloudNetworkColumn_Max,
};


/** Network Manager: Host Network tree-widget item. */
class UIItemHostNetwork : public QITreeWidgetItem, public UIDataHostNetwork
{
    Q_OBJECT;

public:

    /** Updates item fields from data. */
    void updateFields();

#ifdef VBOX_WS_MAC
    /** Returns item name. */
    QString name() const { return m_strName; }
#else
    /** Returns item name. */
    QString name() const { return m_interface.m_strName; }
#endif

private:

#ifndef VBOX_WS_MAC
    /** Returns CIDR for a passed @a strMask. */
    static int maskToCidr(const QString &strMask);
#endif
};


/** Network Manager: NAT Network tree-widget item. */
class UIItemNATNetwork : public QITreeWidgetItem, public UIDataNATNetwork
{
    Q_OBJECT;

public:

    /** Updates item fields from data. */
    void updateFields();

    /** Returns item name. */
    QString name() const { return m_strName; }
};


/** Network Manager: Cloud Network tree-widget item. */
class UIItemCloudNetwork : public QITreeWidgetItem, public UIDataCloudNetwork
{
    Q_OBJECT;

public:

    /** Updates item fields from data. */
    void updateFields();

    /** Returns item name. */
    QString name() const { return m_strName; }
};


/*********************************************************************************************************************************
*   Class UIItemHostNetwork implementation.                                                                                      *
*********************************************************************************************************************************/

void UIItemHostNetwork::updateFields()
{
#ifdef VBOX_WS_MAC
    /* Compose item fields: */
    setText(HostNetworkColumn_Name, m_strName);
    setText(HostNetworkColumn_Mask, m_strMask);
    setText(HostNetworkColumn_LBnd, m_strLBnd);
    setText(HostNetworkColumn_UBnd, m_strUBnd);

    /* Compose item tool-tip: */
    const QString strTable("<table cellspacing=5>%1</table>");
    const QString strHeader("<tr><td><nobr>%1:&nbsp;</nobr></td><td><nobr>%2</nobr></td></tr>");
    QString strToolTip;

    /* Network information: */
    strToolTip += strHeader.arg(UINetworkManager::tr("Name"), m_strName);
    strToolTip += strHeader.arg(UINetworkManager::tr("Mask"), m_strMask);
    strToolTip += strHeader.arg(UINetworkManager::tr("Lower Bound"), m_strLBnd);
    strToolTip += strHeader.arg(UINetworkManager::tr("Upper Bound"), m_strUBnd);

#else /* !VBOX_WS_MAC */

    /* Compose item fields: */
    setText(HostNetworkColumn_Name, m_interface.m_strName);
    setText(HostNetworkColumn_IPv4, m_interface.m_strAddress.isEmpty() ? QString() :
                                    QString("%1/%2").arg(m_interface.m_strAddress).arg(maskToCidr(m_interface.m_strMask)));
    setText(HostNetworkColumn_IPv6, m_interface.m_strAddress6.isEmpty() || !m_interface.m_fSupportedIPv6 ? QString() :
                                    QString("%1/%2").arg(m_interface.m_strAddress6).arg(m_interface.m_strPrefixLength6.toInt()));
    setText(HostNetworkColumn_DHCP, m_dhcpserver.m_fEnabled ? UINetworkManager::tr("Enabled", "DHCP Server")
                                                            : UINetworkManager::tr("Disabled", "DHCP Server"));

    /* Compose item tool-tip: */
    const QString strTable("<table cellspacing=5>%1</table>");
    const QString strHeader("<tr><td><nobr>%1:&nbsp;</nobr></td><td><nobr>%2</nobr></td></tr>");
    const QString strSubHeader("<tr><td><nobr>&nbsp;&nbsp;%1:&nbsp;</nobr></td><td><nobr>%2</nobr></td></tr>");
    QString strToolTip;

    /* Interface information: */
    strToolTip += strHeader.arg(UINetworkManager::tr("Adapter"))
                           .arg(m_interface.m_fDHCPEnabled ?
                                UINetworkManager::tr("Automatically configured", "interface") :
                                UINetworkManager::tr("Manually configured", "interface"));
    strToolTip += strSubHeader.arg(UINetworkManager::tr("IPv4 Address"))
                              .arg(m_interface.m_strAddress.isEmpty() ?
                                   UINetworkManager::tr ("Not set", "address") :
                                   m_interface.m_strAddress) +
                  strSubHeader.arg(UINetworkManager::tr("IPv4 Network Mask"))
                              .arg(m_interface.m_strMask.isEmpty() ?
                                   UINetworkManager::tr ("Not set", "mask") :
                                   m_interface.m_strMask);
    if (m_interface.m_fSupportedIPv6)
    {
        strToolTip += strSubHeader.arg(UINetworkManager::tr("IPv6 Address"))
                                  .arg(m_interface.m_strAddress6.isEmpty() ?
                                       UINetworkManager::tr("Not set", "address") :
                                       m_interface.m_strAddress6) +
                      strSubHeader.arg(UINetworkManager::tr("IPv6 Prefix Length"))
                                  .arg(m_interface.m_strPrefixLength6.isEmpty() ?
                                       UINetworkManager::tr("Not set", "length") :
                                       m_interface.m_strPrefixLength6);
    }

    /* DHCP server information: */
    strToolTip += strHeader.arg(UINetworkManager::tr("DHCP Server"))
                           .arg(m_dhcpserver.m_fEnabled ?
                                UINetworkManager::tr("Enabled", "server") :
                                UINetworkManager::tr("Disabled", "server"));
    if (m_dhcpserver.m_fEnabled)
    {
        strToolTip += strSubHeader.arg(UINetworkManager::tr("Address"))
                                  .arg(m_dhcpserver.m_strAddress.isEmpty() ?
                                       UINetworkManager::tr("Not set", "address") :
                                       m_dhcpserver.m_strAddress) +
                      strSubHeader.arg(UINetworkManager::tr("Network Mask"))
                                  .arg(m_dhcpserver.m_strMask.isEmpty() ?
                                       UINetworkManager::tr("Not set", "mask") :
                                       m_dhcpserver.m_strMask) +
                      strSubHeader.arg(UINetworkManager::tr("Lower Bound"))
                                  .arg(m_dhcpserver.m_strLowerAddress.isEmpty() ?
                                       UINetworkManager::tr("Not set", "bound") :
                                       m_dhcpserver.m_strLowerAddress) +
                      strSubHeader.arg(UINetworkManager::tr("Upper Bound"))
                                  .arg(m_dhcpserver.m_strUpperAddress.isEmpty() ?
                                       UINetworkManager::tr("Not set", "bound") :
                                       m_dhcpserver.m_strUpperAddress);
    }
#endif /* !VBOX_WS_MAC */

    /* Assign tool-tip finally: */
    setToolTip(HostNetworkColumn_Name, strTable.arg(strToolTip));
}

#ifndef VBOX_WS_MAC
/* static */
int UIItemHostNetwork::maskToCidr(const QString &strMask)
{
    /* Parse passed mask: */
    QList<int> address;
    foreach (const QString &strValue, strMask.split('.'))
        address << strValue.toInt();

    /* Calculate CIDR: */
    int iCidr = 0;
    for (int i = 0; i < 4 || i < address.size(); ++i)
    {
        switch(address.at(i))
        {
            case 0x80: iCidr += 1; break;
            case 0xC0: iCidr += 2; break;
            case 0xE0: iCidr += 3; break;
            case 0xF0: iCidr += 4; break;
            case 0xF8: iCidr += 5; break;
            case 0xFC: iCidr += 6; break;
            case 0xFE: iCidr += 7; break;
            case 0xFF: iCidr += 8; break;
            /* Return CIDR prematurelly: */
            default: return iCidr;
        }
    }

    /* Return CIDR: */
    return iCidr;
}
#endif /* !VBOX_WS_MAC */


/*********************************************************************************************************************************
*   Class UIItemNATNetwork implementation.                                                                                       *
*********************************************************************************************************************************/

void UIItemNATNetwork::updateFields()
{
    /* Compose item fields: */
    setText(NATNetworkColumn_Name, m_strName);
    setText(NATNetworkColumn_IPv4, m_strPrefixIPv4);
    setText(NATNetworkColumn_IPv6, m_strPrefixIPv6);
    setText(NATNetworkColumn_DHCP, m_fSupportsDHCP ? UINetworkManager::tr("Enabled", "DHCP Server")
                                                   : UINetworkManager::tr("Disabled", "DHCP Server"));

    /* Compose item tool-tip: */
    const QString strTable("<table cellspacing=5>%1</table>");
    const QString strHeader("<tr><td><nobr>%1:&nbsp;</nobr></td><td><nobr>%2</nobr></td></tr>");
    const QString strSubHeader("<tr><td><nobr>&nbsp;&nbsp;%1:&nbsp;</nobr></td><td><nobr>%2</nobr></td></tr>");
    QString strToolTip;

    /* Network information: */
    strToolTip += strHeader.arg(UINetworkManager::tr("Network Name"), m_strName);
    strToolTip += strHeader.arg(UINetworkManager::tr("Network IPv4 Prefix"), m_strPrefixIPv4);
    strToolTip += strHeader.arg(UINetworkManager::tr("Network IPv6 Prefix"), m_strPrefixIPv6);
    strToolTip += strHeader.arg(UINetworkManager::tr("Supports DHCP"), m_fSupportsDHCP ? UINetworkManager::tr("yes")
                                                                                       : UINetworkManager::tr("no"));
    strToolTip += strHeader.arg(UINetworkManager::tr("Supports IPv6"), m_fSupportsIPv6 ? UINetworkManager::tr("yes")
                                                                                       : UINetworkManager::tr("no"));
    if (m_fSupportsIPv6 && m_fAdvertiseDefaultIPv6Route)
        strToolTip += strSubHeader.arg(UINetworkManager::tr("Default IPv6 route"), UINetworkManager::tr("yes"));

    /* Assign tool-tip finally: */
    setToolTip(NATNetworkColumn_Name, strTable.arg(strToolTip));
}


/*********************************************************************************************************************************
*   Class UIItemCloudNetwork implementation.                                                                                     *
*********************************************************************************************************************************/

void UIItemCloudNetwork::updateFields()
{
    /* Compose item fields: */
    setText(CloudNetworkColumn_Name, m_strName);
    setText(CloudNetworkColumn_Provider, m_strProvider);
    setText(CloudNetworkColumn_Profile, m_strProfile);

    /* Compose item tool-tip: */
    const QString strTable("<table cellspacing=5>%1</table>");
    const QString strHeader("<tr><td><nobr>%1:&nbsp;</nobr></td><td><nobr>%2</nobr></td></tr>");
    QString strToolTip;

    /* Network information: */
    strToolTip += strHeader.arg(UINetworkManager::tr("Network Name"), m_strName);
    strToolTip += strHeader.arg(UINetworkManager::tr("Provider"), m_strProvider);
    strToolTip += strHeader.arg(UINetworkManager::tr("Profile"), m_strProfile);

    /* Assign tool-tip finally: */
    setToolTip(CloudNetworkColumn_Name, strTable.arg(strToolTip));
}


/*********************************************************************************************************************************
*   Class UINetworkManagerWidget implementation.                                                                                 *
*********************************************************************************************************************************/

UINetworkManagerWidget::UINetworkManagerWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                                               bool fShowToolbar /* = true */, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_pActionPool(pActionPool)
    , m_fShowToolbar(fShowToolbar)
    , m_pToolBar(0)
    , m_pTabWidget(0)
    , m_pTabHostNetwork(0)
    , m_pLayoutHostNetwork(0)
    , m_pTreeWidgetHostNetwork(0)
    , m_pDetailsWidgetHostNetwork(0)
    , m_pTabNATNetwork(0)
    , m_pLayoutNATNetwork(0)
    , m_pTreeWidgetNATNetwork(0)
    , m_pDetailsWidgetNATNetwork(0)
    , m_pTabCloudNetwork(0)
    , m_pLayoutCloudNetwork(0)
    , m_pTreeWidgetCloudNetwork(0)
    , m_pDetailsWidgetCloudNetwork(0)
{
    prepare();
}

QMenu *UINetworkManagerWidget::menu() const
{
    AssertPtrReturn(m_pActionPool, 0);
    return m_pActionPool->action(UIActionIndexMN_M_NetworkWindow)->menu();
}

void UINetworkManagerWidget::sltRetranslateUI()
{
    /* Translate tab-widget: */
    if (m_pTabWidget)
    {
        m_pTabWidget->setTabText(0, UINetworkManager::tr("Host-only Networks"));
        m_pTabWidget->setTabText(1, UINetworkManager::tr("NAT Networks"));
        m_pTabWidget->setTabText(2, UINetworkManager::tr("Cloud Networks"));
    }

    /* Translate host network tree-widget: */
    if (m_pTreeWidgetHostNetwork)
    {
#ifdef VBOX_WS_MAC
        const QStringList fields = QStringList()
                                   << UINetworkManager::tr("Name")
                                   << UINetworkManager::tr("Mask")
                                   << UINetworkManager::tr("Lower Bound")
                                   << UINetworkManager::tr("Upper Bound");
#else /* !VBOX_WS_MAC */
        const QStringList fields = QStringList()
                                   << UINetworkManager::tr("Name")
                                   << UINetworkManager::tr("IPv4 Prefix")
                                   << UINetworkManager::tr("IPv6 Prefix")
                                   << UINetworkManager::tr("DHCP Server");
#endif /* !VBOX_WS_MAC */
        m_pTreeWidgetHostNetwork->setHeaderLabels(fields);
        m_pTreeWidgetHostNetwork->setWhatsThis(UINetworkManager::tr("Registered host-only networks"));
    }

    /* Translate NAT network tree-widget: */
    if (m_pTreeWidgetNATNetwork)
    {
        const QStringList fields = QStringList()
                                   << UINetworkManager::tr("Name")
                                   << UINetworkManager::tr("IPv4 Prefix")
                                   << UINetworkManager::tr("IPv6 Prefix")
                                   << UINetworkManager::tr("DHCP Server");
        m_pTreeWidgetNATNetwork->setHeaderLabels(fields);
        m_pTreeWidgetNATNetwork->setWhatsThis(UINetworkManager::tr("Registered NAT networks"));
    }

    /* Translate cloud network tree-widget: */
    if (m_pTreeWidgetCloudNetwork)
    {
        const QStringList fields = QStringList()
                                   << UINetworkManager::tr("Name")
                                   << UINetworkManager::tr("Provider")
                                   << UINetworkManager::tr("Profile");
        m_pTreeWidgetCloudNetwork->setHeaderLabels(fields);
        m_pTreeWidgetCloudNetwork->setWhatsThis(UINetworkManager::tr("Registered cloud networks"));
    }
}

void UINetworkManagerWidget::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QWidget::resizeEvent(pEvent);

    /* Adjust tree-widgets: */
    sltAdjustTreeWidgets();
}

void UINetworkManagerWidget::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QWidget::showEvent(pEvent);

    /* Adjust tree-widgets: */
    sltAdjustTreeWidgets();
}

void UINetworkManagerWidget::sltResetDetailsChanges()
{
    /* Check tab-widget: */
    AssertMsgReturnVoid(m_pTabWidget, ("This action should not be allowed!\n"));

    /* Just push current item data to details-widget again: */
    switch (m_pTabWidget->currentIndex())
    {
        case TabWidgetIndex_HostNetwork: sltHandleCurrentItemChangeHostNetwork(); break;
        case TabWidgetIndex_NATNetwork: sltHandleCurrentItemChangeNATNetwork(); break;
        case TabWidgetIndex_CloudNetwork: sltHandleCurrentItemChangeCloudNetwork(); break;
        default: break;
    }
}

void UINetworkManagerWidget::sltApplyDetailsChanges()
{
    /* Check tab-widget: */
    AssertMsgReturnVoid(m_pTabWidget, ("This action should not be allowed!\n"));

    /* Apply details-widget data changes: */
    switch (m_pTabWidget->currentIndex())
    {
        case TabWidgetIndex_HostNetwork: sltApplyDetailsChangesHostNetwork(); break;
        case TabWidgetIndex_NATNetwork: sltApplyDetailsChangesNATNetwork(); break;
        case TabWidgetIndex_CloudNetwork: sltApplyDetailsChangesCloudNetwork(); break;
        default: break;
    }
}

void UINetworkManagerWidget::sltCreateHostNetwork()
{
    /* For host networks only: */
    if (m_pTabWidget->currentIndex() != TabWidgetIndex_HostNetwork)
        return;

    /* Check host network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetHostNetwork, ("Host network tree-widget isn't created!\n"));

#ifdef VBOX_WS_MAC
    /* Compose a set of busy names: */
    QSet<QString> names;
    for (int i = 0; i < m_pTreeWidgetHostNetwork->topLevelItemCount(); ++i)
        names << qobject_cast<UIItemHostNetwork*>(m_pTreeWidgetHostNetwork->childItem(i))->name();
    /* Compose a map of busy indexes: */
    QMap<int, bool> presence;
    const QString strNameTemplate("HostNetwork%1");
    const QRegularExpression re(strNameTemplate.arg("([\\d]*)"));
    foreach (const QString &strName, names)
    {
        const QRegularExpressionMatch mt = re.match(strName);
        if (mt.hasMatch())
            presence[mt.captured(1).toInt()] = true;
    }
    /* Search for a minimum index: */
    int iMinimumIndex = 0;
    for (int i = 0; !presence.isEmpty() && i <= presence.lastKey() + 1; ++i)
        if (!presence.contains(i))
        {
            iMinimumIndex = i;
            break;
        }
    /* Compose resulting index and name: */
    const QString strNetworkName = strNameTemplate.arg(iMinimumIndex == 0 ? QString() : QString::number(iMinimumIndex));

    /* Compose new item data: */
    UIDataHostNetwork oldData;
    oldData.m_fExists = true;
    oldData.m_strName = strNetworkName;

    /* Get VirtualBox for further activities: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Create network: */
    CHostOnlyNetwork comNetwork = comVBox.CreateHostOnlyNetwork(oldData.m_strName);
    CHostOnlyNetwork comNetworkBase = comNetwork;

    /* Show error message if necessary: */
    if (!comVBox.isOk())
        UINotificationMessage::cannotCreateHostOnlyNetwork(comVBox);
    else
    {
        /* Save host network name: */
        if (comNetwork.isOk())
            comNetwork.SetNetworkName(oldData.m_strName);

        /* Show error message if necessary: */
        if (!comNetwork.isOk())
            UINotificationMessage::cannotChangeHostOnlyNetworkParameter(comNetwork);

        /* Add network to the tree: */
        UIDataHostNetwork newData;
        loadHostNetwork(comNetworkBase, newData);
        createItemForHostNetwork(newData, true);

        /* Adjust tree-widgets: */
        sltAdjustTreeWidgets();
    }

#else /* !VBOX_WS_MAC */

    /* Get host for further activities: */
    CHost comHost = gpGlobalSession->host();
    CHostNetworkInterface comInterface;

    /* Create interface: */
    UINotificationProgressHostOnlyNetworkInterfaceCreate *pNotification =
        new UINotificationProgressHostOnlyNetworkInterfaceCreate(comHost, comInterface);
    connect(pNotification, &UINotificationProgressHostOnlyNetworkInterfaceCreate::sigHostOnlyNetworkInterfaceCreated,
            this, &UINetworkManagerWidget::sigHandleHostOnlyNetworkInterfaceCreated);
    gpNotificationCenter->append(pNotification);
#endif /* !VBOX_WS_MAC */
}

#ifndef VBOX_WS_MAC
void UINetworkManagerWidget::sigHandleHostOnlyNetworkInterfaceCreated(const CHostNetworkInterface &comInterface)
{
    /* Get network name for further activities: */
    const QString strNetworkName = comInterface.GetNetworkName();

    /* Show error message if necessary: */
    if (!comInterface.isOk())
        UINotificationMessage::cannotAcquireHostNetworkInterfaceParameter(comInterface);
    else
    {
        /* Get VBox for further activities: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();

        /* Find corresponding DHCP server (create if necessary): */
        CDHCPServer comServer = comVBox.FindDHCPServerByNetworkName(strNetworkName);
        if (!comVBox.isOk() || comServer.isNull())
            comServer = comVBox.CreateDHCPServer(strNetworkName);

        /* Show error message if necessary: */
        if (!comVBox.isOk() || comServer.isNull())
            UINotificationMessage::cannotCreateDHCPServer(comVBox, strNetworkName);

        /* Add interface to the tree: */
        UIDataHostNetwork data;
        loadHostNetwork(comInterface, data);
        createItemForHostNetwork(data, true);

        /* Adjust tree-widgets: */
        sltAdjustTreeWidgets();
    }
}
#endif  /* !VBOX_WS_MAC */

void UINetworkManagerWidget::sltRemoveHostNetwork()
{
    /* For host networks only: */
    if (m_pTabWidget->currentIndex() != TabWidgetIndex_HostNetwork)
        return;

    /* Check host network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetHostNetwork, ("Host network tree-widget isn't created!\n"));

    /* Get network item: */
    UIItemHostNetwork *pItem = static_cast<UIItemHostNetwork*>(m_pTreeWidgetHostNetwork->currentItem());
    AssertMsgReturnVoid(pItem, ("Current item must not be null!\n"));

#ifdef VBOX_WS_MAC

    /* Get network name: */
    const QString strNetworkName(pItem->name());

    /* Confirm host network removal: */
    if (!msgCenter().confirmHostOnlyNetworkRemoval(strNetworkName, this))
        return;

    /* Get VirtualBox for further activities: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Find corresponding network: */
    const CHostOnlyNetwork &comNetwork = comVBox.FindHostOnlyNetworkByName(strNetworkName);

    /* Show error message if necessary: */
    if (!comVBox.isOk() || comNetwork.isNull())
        UINotificationMessage::cannotFindHostOnlyNetwork(comVBox, strNetworkName);
    else
    {
        /* Remove network finally: */
        comVBox.RemoveHostOnlyNetwork(comNetwork);

        /* Show error message if necessary: */
        if (!comVBox.isOk())
            UINotificationMessage::cannotRemoveHostOnlyNetwork(comVBox, strNetworkName);
        else
        {
            /* Move selection to somewhere else: */
            if (m_pTreeWidgetHostNetwork->itemBelow(pItem))
                m_pTreeWidgetHostNetwork->setCurrentItem(m_pTreeWidgetHostNetwork->itemBelow(pItem));
            else if (m_pTreeWidgetHostNetwork->itemAbove(pItem))
                m_pTreeWidgetHostNetwork->setCurrentItem(m_pTreeWidgetHostNetwork->itemAbove(pItem));
            else
                m_pTreeWidgetHostNetwork->setCurrentItem(0);

            /* Remove interface from the tree: */
            delete pItem;

            /* Adjust tree-widgets: */
            sltAdjustTreeWidgets();
        }
    }

#else /* !VBOX_WS_MAC */

    /* Get interface name: */
    const QString strInterfaceName(pItem->name());

    /* Confirm host network removal: */
    if (!msgCenter().confirmHostNetworkInterfaceRemoval(strInterfaceName, this))
        return;

    /* Get host for further activities: */
    CHost comHost = gpGlobalSession->host();

    /* Find corresponding interface: */
    const CHostNetworkInterface comInterface = comHost.FindHostNetworkInterfaceByName(strInterfaceName);

    /* Show error message if necessary: */
    if (!comHost.isOk() || comInterface.isNull())
        UINotificationMessage::cannotFindHostNetworkInterface(comHost, strInterfaceName);
    else
    {
        /* Get network name for further activities: */
        QString strNetworkName;
        if (comInterface.isOk())
            strNetworkName = comInterface.GetNetworkName();
        /* Get interface id for further activities: */
        QUuid uInterfaceId;
        if (comInterface.isOk())
            uInterfaceId = comInterface.GetId();

        /* Show error message if necessary: */
        if (!comInterface.isOk())
            UINotificationMessage::cannotAcquireHostNetworkInterfaceParameter(comInterface);
        else
        {
            /* Get VBox for further activities: */
            CVirtualBox comVBox = gpGlobalSession->virtualBox();

            /* Find corresponding DHCP server: */
            const CDHCPServer &comServer = comVBox.FindDHCPServerByNetworkName(strNetworkName);
            if (comVBox.isOk() && comServer.isNotNull())
            {
                /* Remove server if any: */
                comVBox.RemoveDHCPServer(comServer);

                /* Show error message if necessary: */
                if (!comVBox.isOk())
                    UINotificationMessage::cannotRemoveDHCPServer(comVBox, strInterfaceName);
            }

            /* Create interface: */
            UINotificationProgressHostOnlyNetworkInterfaceRemove *pNotification =
                new UINotificationProgressHostOnlyNetworkInterfaceRemove(comHost, uInterfaceId);
            connect(pNotification, &UINotificationProgressHostOnlyNetworkInterfaceRemove::sigHostOnlyNetworkInterfaceRemoved,
                    this, &UINetworkManagerWidget::sigHandleHostOnlyNetworkInterfaceRemoved);
            gpNotificationCenter->append(pNotification);
        }
    }
#endif /* !VBOX_WS_MAC */
}

#ifndef VBOX_WS_MAC
void UINetworkManagerWidget::sigHandleHostOnlyNetworkInterfaceRemoved(const QString &strInterfaceName)
{
    /* Check host network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetHostNetwork, ("Host network tree-widget isn't created!\n"));

    /* Search for required item: */
    QList<QTreeWidgetItem*> items = m_pTreeWidgetHostNetwork->findItems(strInterfaceName, Qt::MatchCaseSensitive);
    AssertReturnVoid(!items.isEmpty());
    QTreeWidgetItem *pItem = items.first();

    /* Move selection to somewhere else: */
    if (m_pTreeWidgetHostNetwork->itemBelow(pItem))
        m_pTreeWidgetHostNetwork->setCurrentItem(m_pTreeWidgetHostNetwork->itemBelow(pItem));
    else if (m_pTreeWidgetHostNetwork->itemAbove(pItem))
        m_pTreeWidgetHostNetwork->setCurrentItem(m_pTreeWidgetHostNetwork->itemAbove(pItem));
    else
        m_pTreeWidgetHostNetwork->setCurrentItem(0);

    /* Remove interface from the tree: */
    delete pItem;

    /* Adjust tree-widgets: */
    sltAdjustTreeWidgets();
}
#endif /* !VBOX_WS_MAC */

void UINetworkManagerWidget::sltCreateNATNetwork()
{
    /* For NAT networks only: */
    if (m_pTabWidget->currentIndex() != TabWidgetIndex_NATNetwork)
        return;

    /* Compose a set of busy names: */
    QSet<QString> names;
    for (int i = 0; i < m_pTreeWidgetNATNetwork->topLevelItemCount(); ++i)
        names << qobject_cast<UIItemNATNetwork*>(m_pTreeWidgetNATNetwork->childItem(i))->name();
    /* Compose a map of busy indexes: */
    QMap<int, bool> presence;
    const QString strNameTemplate("NatNetwork%1");
    const QRegularExpression re(strNameTemplate.arg("([\\d]*)"));
    foreach (const QString &strName, names)
    {
        const QRegularExpressionMatch mt = re.match(strName);
        if (mt.hasMatch())
            presence[mt.captured(1).toInt()] = true;
    }
    /* Search for a minimum index: */
    int iMinimumIndex = 0;
    for (int i = 0; !presence.isEmpty() && i <= presence.lastKey() + 1; ++i)
        if (!presence.contains(i))
        {
            iMinimumIndex = i;
            break;
        }
    /* Compose resulting index and name: */
    const QString strNetworkName = strNameTemplate.arg(iMinimumIndex == 0 ? QString() : QString::number(iMinimumIndex));

    /* Compose new item data: */
    UIDataNATNetwork oldData;
    oldData.m_fExists = true;
    oldData.m_strName = strNetworkName;
    oldData.m_strPrefixIPv4 = "10.0.2.0/24";
    oldData.m_strPrefixIPv6 = ""; // do we need something here?
    oldData.m_fSupportsDHCP = true;
    oldData.m_fSupportsIPv6 = false;
    oldData.m_fAdvertiseDefaultIPv6Route = false;

    /* Get VirtualBox for further activities: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Create network: */
    CNATNetwork comNetwork = comVBox.CreateNATNetwork(oldData.m_strName);
    CNATNetwork comNetworkBase = comNetwork;

    /* Show error message if necessary: */
    if (!comVBox.isOk())
        UINotificationMessage::cannotCreateNATNetwork(comVBox);
    else
    {
        /* Save NAT network name: */
        if (comNetwork.isOk())
            comNetwork.SetNetworkName(oldData.m_strName);
        /* Save NAT network IPv4 prefix: */
        if (comNetwork.isOk())
            comNetwork.SetNetwork(oldData.m_strPrefixIPv4);
        /* Save NAT network IPv6 prefix: */
        if (comNetwork.isOk())
            comNetwork.SetIPv6Prefix(oldData.m_strPrefixIPv6);
        /* Save whether NAT network needs DHCP server: */
        if (comNetwork.isOk())
            comNetwork.SetNeedDhcpServer(oldData.m_fSupportsDHCP);
        /* Save whether NAT network supports IPv6: */
        if (comNetwork.isOk())
            comNetwork.SetIPv6Enabled(oldData.m_fSupportsIPv6);
        /* Save whether NAT network should advertise default IPv6 route: */
        if (comNetwork.isOk())
            comNetwork.SetAdvertiseDefaultIPv6RouteEnabled(oldData.m_fAdvertiseDefaultIPv6Route);

        /* Show error message if necessary: */
        if (!comNetwork.isOk())
            UINotificationMessage::cannotChangeNATNetworkParameter(comNetwork);

        /* Add network to the tree: */
        UIDataNATNetwork newData;
        loadNATNetwork(comNetworkBase, newData);
        createItemForNATNetwork(newData, true);

        /* Adjust tree-widgets: */
        sltAdjustTreeWidgets();
    }
}

void UINetworkManagerWidget::sltRemoveNATNetwork()
{
    /* For NAT networks only: */
    if (m_pTabWidget->currentIndex() != TabWidgetIndex_NATNetwork)
        return;

    /* Check NAT network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetNATNetwork, ("NAT network tree-widget isn't created!\n"));

    /* Get network item: */
    UIItemNATNetwork *pItem = static_cast<UIItemNATNetwork*>(m_pTreeWidgetNATNetwork->currentItem());
    AssertMsgReturnVoid(pItem, ("Current item must not be null!\n"));

    /* Get network name: */
    const QString strNetworkName(pItem->name());

    /* Confirm host network removal: */
    if (!msgCenter().confirmNATNetworkRemoval(strNetworkName, this))
        return;

    /* Get VirtualBox for further activities: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Find corresponding network: */
    const CNATNetwork &comNetwork = comVBox.FindNATNetworkByName(strNetworkName);

    /* Show error message if necessary: */
    if (!comVBox.isOk() || comNetwork.isNull())
        UINotificationMessage::cannotFindNATNetwork(comVBox, strNetworkName);
    else
    {
        /* Remove network finally: */
        comVBox.RemoveNATNetwork(comNetwork);

        /* Show error message if necessary: */
        if (!comVBox.isOk())
            UINotificationMessage::cannotRemoveNATNetwork(comVBox, strNetworkName);
        else
        {
            /* Move selection to somewhere else: */
            if (m_pTreeWidgetNATNetwork->itemBelow(pItem))
                m_pTreeWidgetNATNetwork->setCurrentItem(m_pTreeWidgetNATNetwork->itemBelow(pItem));
            else if (m_pTreeWidgetNATNetwork->itemAbove(pItem))
                m_pTreeWidgetNATNetwork->setCurrentItem(m_pTreeWidgetNATNetwork->itemAbove(pItem));
            else
                m_pTreeWidgetNATNetwork->setCurrentItem(0);

            /* Remove interface from the tree: */
            delete pItem;

            /* Adjust tree-widgets: */
            sltAdjustTreeWidgets();
        }
    }
}

void UINetworkManagerWidget::sltCreateCloudNetwork()
{
    /* For cloud networks only: */
    if (m_pTabWidget->currentIndex() != TabWidgetIndex_CloudNetwork)
        return;

    /* Compose a set of busy names: */
    QSet<QString> names;
    for (int i = 0; i < m_pTreeWidgetCloudNetwork->topLevelItemCount(); ++i)
        names << qobject_cast<UIItemCloudNetwork*>(m_pTreeWidgetCloudNetwork->childItem(i))->name();
    /* Compose a map of busy indexes: */
    QMap<int, bool> presence;
    const QString strNameTemplate("CloudNetwork%1");
    const QRegularExpression re(strNameTemplate.arg("([\\d]*)"));
    foreach (const QString &strName, names)
    {
        const QRegularExpressionMatch mt = re.match(strName);
        if (mt.hasMatch())
            presence[mt.captured(1).toInt()] = true;
    }
    /* Search for a minimum index: */
    int iMinimumIndex = 0;
    for (int i = 0; !presence.isEmpty() && i <= presence.lastKey() + 1; ++i)
        if (!presence.contains(i))
        {
            iMinimumIndex = i;
            break;
        }
    /* Compose resulting index and name: */
    const QString strNetworkName = strNameTemplate.arg(iMinimumIndex == 0 ? QString() : QString::number(iMinimumIndex));

    /* Compose new item data: */
    UIDataCloudNetwork oldData;
    oldData.m_fEnabled = true;
    oldData.m_strName = strNetworkName;

    /* Get VirtualBox for further activities: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Create network: */
    CCloudNetwork comNetwork = comVBox.CreateCloudNetwork(oldData.m_strName);
    CCloudNetwork comNetworkBase = comNetwork;

    /* Show error message if necessary: */
    if (!comVBox.isOk())
        UINotificationMessage::cannotCreateCloudNetwork(comVBox);
    else
    {
        /* Save whether network enabled: */
        if (comNetwork.isOk())
            comNetwork.SetEnabled(oldData.m_fEnabled);
        /* Save cloud network name: */
        if (comNetwork.isOk())
            comNetwork.SetNetworkName(oldData.m_strName);

        /* Show error message if necessary: */
        if (!comNetwork.isOk())
            UINotificationMessage::cannotChangeCloudNetworkParameter(comNetwork);

        /* Add network to the tree: */
        UIDataCloudNetwork newData;
        loadCloudNetwork(comNetworkBase, newData);
        createItemForCloudNetwork(newData, true);

        /* Adjust tree-widgets: */
        sltAdjustTreeWidgets();
    }
}

void UINetworkManagerWidget::sltRemoveCloudNetwork()
{
    /* For cloud networks only: */
    if (m_pTabWidget->currentIndex() != TabWidgetIndex_CloudNetwork)
        return;

    /* Check cloud network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetCloudNetwork, ("Cloud network tree-widget isn't created!\n"));

    /* Get network item: */
    UIItemCloudNetwork *pItem = static_cast<UIItemCloudNetwork*>(m_pTreeWidgetCloudNetwork->currentItem());
    AssertMsgReturnVoid(pItem, ("Current item must not be null!\n"));

    /* Get network name: */
    const QString strNetworkName(pItem->name());

    /* Confirm host network removal: */
    if (!msgCenter().confirmCloudNetworkRemoval(strNetworkName, this))
        return;

    /* Get VirtualBox for further activities: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Find corresponding network: */
    const CCloudNetwork &comNetwork = comVBox.FindCloudNetworkByName(strNetworkName);

    /* Show error message if necessary: */
    if (!comVBox.isOk() || comNetwork.isNull())
        UINotificationMessage::cannotFindCloudNetwork(comVBox, strNetworkName);
    else
    {
        /* Remove network finally: */
        comVBox.RemoveCloudNetwork(comNetwork);

        /* Show error message if necessary: */
        if (!comVBox.isOk())
            UINotificationMessage::cannotRemoveCloudNetwork(comVBox, strNetworkName);
        else
        {
            /* Move selection to somewhere else: */
            if (m_pTreeWidgetCloudNetwork->itemBelow(pItem))
                m_pTreeWidgetCloudNetwork->setCurrentItem(m_pTreeWidgetCloudNetwork->itemBelow(pItem));
            else if (m_pTreeWidgetCloudNetwork->itemAbove(pItem))
                m_pTreeWidgetCloudNetwork->setCurrentItem(m_pTreeWidgetCloudNetwork->itemAbove(pItem));
            else
                m_pTreeWidgetCloudNetwork->setCurrentItem(0);

            /* Remove interface from the tree: */
            delete pItem;

            /* Adjust tree-widgets: */
            sltAdjustTreeWidgets();
        }
    }
}

void UINetworkManagerWidget::sltToggleDetailsVisibility(bool fVisible)
{
    /* Save the setting: */
    gEDataManager->setHostNetworkManagerDetailsExpanded(fVisible);
    /* Show/hide details area and Apply/Reset buttons: */
    switch (m_pTabWidget->currentIndex())
    {
        case TabWidgetIndex_HostNetwork:
        {
            if (m_pDetailsWidgetNATNetwork)
                m_pDetailsWidgetNATNetwork->setVisible(false);
            if (m_pDetailsWidgetCloudNetwork)
                m_pDetailsWidgetCloudNetwork->setVisible(false);
            if (m_pDetailsWidgetHostNetwork)
                m_pDetailsWidgetHostNetwork->setVisible(fVisible);
            break;
        }
        case TabWidgetIndex_NATNetwork:
        {
            if (m_pDetailsWidgetHostNetwork)
                m_pDetailsWidgetHostNetwork->setVisible(false);
            if (m_pDetailsWidgetCloudNetwork)
                m_pDetailsWidgetCloudNetwork->setVisible(false);
            if (m_pDetailsWidgetNATNetwork)
                m_pDetailsWidgetNATNetwork->setVisible(fVisible);
            break;
        }
        case TabWidgetIndex_CloudNetwork:
        {
            if (m_pDetailsWidgetHostNetwork)
                m_pDetailsWidgetHostNetwork->setVisible(false);
            if (m_pDetailsWidgetNATNetwork)
                m_pDetailsWidgetNATNetwork->setVisible(false);
            if (m_pDetailsWidgetCloudNetwork)
                m_pDetailsWidgetCloudNetwork->setVisible(fVisible);
            break;
        }
    }
    /* Notify external listeners: */
    emit sigDetailsVisibilityChanged(fVisible);
}

void UINetworkManagerWidget::sltHandleCurrentTabWidgetIndexChange()
{
    /* Update actions: */
    updateActionAvailability();

    /* Adjust tree-widgets first of all: */
    sltAdjustTreeWidgets();

    /* Show/hide details area and Apply/Reset buttons: */
    const bool fVisible = m_pActionPool->action(UIActionIndexMN_M_Network_T_Details)->isChecked();
    switch (m_pTabWidget->currentIndex())
    {
        case TabWidgetIndex_HostNetwork:
        {
            if (m_pDetailsWidgetNATNetwork)
                m_pDetailsWidgetNATNetwork->setVisible(false);
            if (m_pDetailsWidgetCloudNetwork)
                m_pDetailsWidgetCloudNetwork->setVisible(false);
            if (m_pDetailsWidgetHostNetwork)
                m_pDetailsWidgetHostNetwork->setVisible(fVisible);
            break;
        }
        case TabWidgetIndex_NATNetwork:
        {
            if (m_pDetailsWidgetHostNetwork)
                m_pDetailsWidgetHostNetwork->setVisible(false);
            if (m_pDetailsWidgetCloudNetwork)
                m_pDetailsWidgetCloudNetwork->setVisible(false);
            if (m_pDetailsWidgetNATNetwork)
                m_pDetailsWidgetNATNetwork->setVisible(fVisible);
            break;
        }
        case TabWidgetIndex_CloudNetwork:
        {
            if (m_pDetailsWidgetHostNetwork)
                m_pDetailsWidgetHostNetwork->setVisible(false);
            if (m_pDetailsWidgetNATNetwork)
                m_pDetailsWidgetNATNetwork->setVisible(false);
            if (m_pDetailsWidgetCloudNetwork)
                m_pDetailsWidgetCloudNetwork->setVisible(fVisible);
            break;
        }
    }
}

void UINetworkManagerWidget::sltAdjustTreeWidgets()
{
    /* Check host network tree-widget: */
    if (m_pTreeWidgetHostNetwork)
    {
        /* Get the tree-widget abstract interface: */
        QAbstractItemView *pItemView = m_pTreeWidgetHostNetwork;
        /* Get the tree-widget header-view: */
        QHeaderView *pItemHeader = m_pTreeWidgetHostNetwork->header();

        /* Calculate the total tree-widget width: */
        const int iTotal = m_pTreeWidgetHostNetwork->viewport()->width();
#ifdef VBOX_WS_MAC
        /* Look for a minimum width hints for non-important columns: */
        const int iMinWidth1 = qMax(pItemView->sizeHintForColumn(HostNetworkColumn_Mask), pItemHeader->sectionSizeHint(HostNetworkColumn_Mask));
        const int iMinWidth2 = qMax(pItemView->sizeHintForColumn(HostNetworkColumn_LBnd), pItemHeader->sectionSizeHint(HostNetworkColumn_LBnd));
        const int iMinWidth3 = qMax(pItemView->sizeHintForColumn(HostNetworkColumn_UBnd), pItemHeader->sectionSizeHint(HostNetworkColumn_UBnd));
#else /* !VBOX_WS_MAC */
        /* Look for a minimum width hints for non-important columns: */
        const int iMinWidth1 = qMax(pItemView->sizeHintForColumn(HostNetworkColumn_IPv4), pItemHeader->sectionSizeHint(HostNetworkColumn_IPv4));
        const int iMinWidth2 = qMax(pItemView->sizeHintForColumn(HostNetworkColumn_IPv6), pItemHeader->sectionSizeHint(HostNetworkColumn_IPv6));
        const int iMinWidth3 = qMax(pItemView->sizeHintForColumn(HostNetworkColumn_DHCP), pItemHeader->sectionSizeHint(HostNetworkColumn_DHCP));
#endif /* !VBOX_WS_MAC */
        /* Propose suitable width hints for non-important columns: */
        const int iWidth1 = iMinWidth1 < iTotal / HostNetworkColumn_Max ? iMinWidth1 : iTotal / HostNetworkColumn_Max;
        const int iWidth2 = iMinWidth2 < iTotal / HostNetworkColumn_Max ? iMinWidth2 : iTotal / HostNetworkColumn_Max;
        const int iWidth3 = iMinWidth3 < iTotal / HostNetworkColumn_Max ? iMinWidth3 : iTotal / HostNetworkColumn_Max;
        /* Apply the proposal: */
#ifdef VBOX_WS_MAC
        m_pTreeWidgetHostNetwork->setColumnWidth(HostNetworkColumn_Mask, iWidth1);
        m_pTreeWidgetHostNetwork->setColumnWidth(HostNetworkColumn_LBnd, iWidth2);
        m_pTreeWidgetHostNetwork->setColumnWidth(HostNetworkColumn_UBnd, iWidth3);
#else /* !VBOX_WS_MAC */
        m_pTreeWidgetHostNetwork->setColumnWidth(HostNetworkColumn_IPv4, iWidth1);
        m_pTreeWidgetHostNetwork->setColumnWidth(HostNetworkColumn_IPv6, iWidth2);
        m_pTreeWidgetHostNetwork->setColumnWidth(HostNetworkColumn_DHCP, iWidth3);
#endif /* !VBOX_WS_MAC */
        m_pTreeWidgetHostNetwork->setColumnWidth(HostNetworkColumn_Name, iTotal - iWidth1 - iWidth2 - iWidth3);
    }

    /* Check NAT network tree-widget: */
    if (m_pTreeWidgetNATNetwork)
    {
        /* Get the tree-widget abstract interface: */
        QAbstractItemView *pItemView = m_pTreeWidgetNATNetwork;
        /* Get the tree-widget header-view: */
        QHeaderView *pItemHeader = m_pTreeWidgetNATNetwork->header();

        /* Calculate the total tree-widget width: */
        const int iTotal = m_pTreeWidgetNATNetwork->viewport()->width();
        /* Look for a minimum width hints for non-important columns: */
        const int iMinWidth1 = qMax(pItemView->sizeHintForColumn(NATNetworkColumn_IPv4), pItemHeader->sectionSizeHint(NATNetworkColumn_IPv4));
        const int iMinWidth2 = qMax(pItemView->sizeHintForColumn(NATNetworkColumn_IPv6), pItemHeader->sectionSizeHint(NATNetworkColumn_IPv6));
        const int iMinWidth3 = qMax(pItemView->sizeHintForColumn(NATNetworkColumn_DHCP), pItemHeader->sectionSizeHint(NATNetworkColumn_DHCP));
        /* Propose suitable width hints for non-important columns: */
        const int iWidth1 = iMinWidth1 < iTotal / NATNetworkColumn_Max ? iMinWidth1 : iTotal / NATNetworkColumn_Max;
        const int iWidth2 = iMinWidth2 < iTotal / NATNetworkColumn_Max ? iMinWidth2 : iTotal / NATNetworkColumn_Max;
        const int iWidth3 = iMinWidth3 < iTotal / NATNetworkColumn_Max ? iMinWidth3 : iTotal / NATNetworkColumn_Max;
        /* Apply the proposal: */
        m_pTreeWidgetNATNetwork->setColumnWidth(NATNetworkColumn_IPv4, iWidth1);
        m_pTreeWidgetNATNetwork->setColumnWidth(NATNetworkColumn_IPv6, iWidth2);
        m_pTreeWidgetNATNetwork->setColumnWidth(NATNetworkColumn_DHCP, iWidth3);
        m_pTreeWidgetNATNetwork->setColumnWidth(NATNetworkColumn_Name, iTotal - iWidth1 - iWidth2 - iWidth3);
    }

    /* Check cloud network tree-widget: */
    if (m_pTreeWidgetCloudNetwork)
    {
        /* Get the tree-widget abstract interface: */
        QAbstractItemView *pItemView = m_pTreeWidgetCloudNetwork;
        /* Get the tree-widget header-view: */
        QHeaderView *pItemHeader = m_pTreeWidgetCloudNetwork->header();

        /* Calculate the total tree-widget width: */
        const int iTotal = m_pTreeWidgetCloudNetwork->viewport()->width();
        /* Look for a minimum width hints for non-important columns: */
        const int iMinWidth1 = qMax(pItemView->sizeHintForColumn(CloudNetworkColumn_Provider), pItemHeader->sectionSizeHint(CloudNetworkColumn_Provider));
        const int iMinWidth2 = qMax(pItemView->sizeHintForColumn(CloudNetworkColumn_Profile), pItemHeader->sectionSizeHint(CloudNetworkColumn_Profile));
        /* Propose suitable width hints for non-important columns: */
        const int iWidth1 = iMinWidth1 < iTotal / CloudNetworkColumn_Max ? iMinWidth1 : iTotal / CloudNetworkColumn_Max;
        const int iWidth2 = iMinWidth2 < iTotal / CloudNetworkColumn_Max ? iMinWidth2 : iTotal / CloudNetworkColumn_Max;
        /* Apply the proposal: */
        m_pTreeWidgetCloudNetwork->setColumnWidth(CloudNetworkColumn_Provider, iWidth1);
        m_pTreeWidgetCloudNetwork->setColumnWidth(CloudNetworkColumn_Profile, iWidth2);
        m_pTreeWidgetCloudNetwork->setColumnWidth(CloudNetworkColumn_Name, iTotal - iWidth1 - iWidth2);
    }
}

void UINetworkManagerWidget::sltHandleCurrentItemChangeHostNetwork()
{
    /* Update actions: */
    updateActionAvailability();

    /* Check host network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetHostNetwork, ("Host network tree-widget isn't created!\n"));

    /* Get network item: */
    UIItemHostNetwork *pItem = static_cast<UIItemHostNetwork*>(m_pTreeWidgetHostNetwork->currentItem());

    /* Check host network details-widget: */
    AssertMsgReturnVoid(m_pDetailsWidgetHostNetwork, ("Host network details-widget isn't created!\n"));

    /* If there is an item => update details data: */
    if (pItem)
        m_pDetailsWidgetHostNetwork->setData(*pItem);
    /* Otherwise => clear details: */
    else
        m_pDetailsWidgetHostNetwork->setData(UIDataHostNetwork());
}

void UINetworkManagerWidget::sltHandleContextMenuRequestHostNetwork(const QPoint &position)
{
    /* Check host network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetHostNetwork, ("Host network tree-widget isn't created!\n"));

    /* Compose temporary context-menu: */
    QMenu menu;
    if (m_pTreeWidgetHostNetwork->itemAt(position))
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_T_Details));
    }
    else
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Create));
//        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Refresh));
    }
    /* And show it: */
    menu.exec(m_pTreeWidgetHostNetwork->mapToGlobal(position));
}

void UINetworkManagerWidget::sltApplyDetailsChangesHostNetwork()
{
    /* Check host network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetHostNetwork, ("Host network tree-widget isn't created!\n"));

    /* Get host network item: */
    UIItemHostNetwork *pItem = static_cast<UIItemHostNetwork*>(m_pTreeWidgetHostNetwork->currentItem());
    AssertMsgReturnVoid(pItem, ("Current item must not be null!\n"));

    /* Check host network details-widget: */
    AssertMsgReturnVoid(m_pDetailsWidgetHostNetwork, ("Host network details-widget isn't created!\n"));

    /* Revalidate host network details: */
    if (m_pDetailsWidgetHostNetwork->revalidate())
    {
        /* Get item data: */
        UIDataHostNetwork oldData = *pItem;
        UIDataHostNetwork newData = m_pDetailsWidgetHostNetwork->data();

#ifdef VBOX_WS_MAC
        /* Get VirtualBox for further activities: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();

        /* Find corresponding network: */
        CHostOnlyNetwork comNetwork = comVBox.FindHostOnlyNetworkByName(oldData.m_strName);
        CHostOnlyNetwork comNetworkBase = comNetwork;

        /* Show error message if necessary: */
        if (!comVBox.isOk() || comNetwork.isNull())
            UINotificationMessage::cannotFindHostOnlyNetwork(comVBox, oldData.m_strName);
        else
        {
            /* Save host network name: */
            if (comNetwork.isOk() && newData.m_strName != oldData.m_strName)
                comNetwork.SetNetworkName(newData.m_strName);
            /* Save host network mask: */
            if (comNetwork.isOk() && newData.m_strMask != oldData.m_strMask)
                comNetwork.SetNetworkMask(newData.m_strMask);
            /* Save host network lower bound: */
            if (comNetwork.isOk() && newData.m_strLBnd != oldData.m_strLBnd)
                comNetwork.SetLowerIP(newData.m_strLBnd);
            /* Save host network upper bound: */
            if (comNetwork.isOk() && newData.m_strUBnd != oldData.m_strUBnd)
                comNetwork.SetUpperIP(newData.m_strUBnd);

            /* Show error message if necessary: */
            if (!comNetwork.isOk())
                UINotificationMessage::cannotChangeHostOnlyNetworkParameter(comNetwork);

            /* Update network in the tree: */
            UIDataHostNetwork data;
            loadHostNetwork(comNetworkBase, data);
            updateItemForHostNetwork(data, true, pItem);

            /* Make sure current item fetched: */
            sltHandleCurrentItemChangeHostNetwork();

            /* Adjust tree-widgets: */
            sltAdjustTreeWidgets();
        }

#else /* !VBOX_WS_MAC */

        /* Get host for further activities: */
        CHost comHost = gpGlobalSession->host();

        /* Find corresponding interface: */
        CHostNetworkInterface comInterface = comHost.FindHostNetworkInterfaceByName(oldData.m_interface.m_strName);

        /* Show error message if necessary: */
        if (!comHost.isOk() || comInterface.isNull())
            UINotificationMessage::cannotFindHostNetworkInterface(comHost, oldData.m_interface.m_strName);
        else
        {
            /* Save automatic interface configuration: */
            if (newData.m_interface.m_fDHCPEnabled)
            {
                if (   comInterface.isOk()
                    && !oldData.m_interface.m_fDHCPEnabled)
                    comInterface.EnableDynamicIPConfig();
            }
            /* Save manual interface configuration: */
            else
            {
                /* Save IPv4 interface configuration: */
                if (   comInterface.isOk()
                    && (   oldData.m_interface.m_fDHCPEnabled
                        || newData.m_interface.m_strAddress != oldData.m_interface.m_strAddress
                        || newData.m_interface.m_strMask != oldData.m_interface.m_strMask))
                    comInterface.EnableStaticIPConfig(newData.m_interface.m_strAddress, newData.m_interface.m_strMask);
                /* Save IPv6 interface configuration: */
                if (   comInterface.isOk()
                    && newData.m_interface.m_fSupportedIPv6
                    && (   oldData.m_interface.m_fDHCPEnabled
                        || newData.m_interface.m_strAddress6 != oldData.m_interface.m_strAddress6
                        || newData.m_interface.m_strPrefixLength6 != oldData.m_interface.m_strPrefixLength6))
                    comInterface.EnableStaticIPConfigV6(newData.m_interface.m_strAddress6, newData.m_interface.m_strPrefixLength6.toULong());
            }

            /* Show error message if necessary: */
            if (!comInterface.isOk())
                UINotificationMessage::cannotChangeHostNetworkInterfaceParameter(comInterface);
            else
            {
                /* Get network name for further activities: */
                const QString strNetworkName = comInterface.GetNetworkName();

                /* Show error message if necessary: */
                if (!comInterface.isOk())
                    UINotificationMessage::cannotAcquireHostNetworkInterfaceParameter(comInterface);
                else
                {
                    /* Get VBox for further activities: */
                    CVirtualBox comVBox = gpGlobalSession->virtualBox();

                    /* Find corresponding DHCP server (create if necessary): */
                    CDHCPServer comServer = comVBox.FindDHCPServerByNetworkName(strNetworkName);
                    if (!comVBox.isOk() || comServer.isNull())
                        comServer = comVBox.CreateDHCPServer(strNetworkName);

                    /* Show error message if necessary: */
                    if (!comVBox.isOk() || comServer.isNull())
                        UINotificationMessage::cannotCreateDHCPServer(comVBox, strNetworkName);
                    else
                    {
                        /* Save whether DHCP server is enabled: */
                        if (   comServer.isOk()
                            && newData.m_dhcpserver.m_fEnabled != oldData.m_dhcpserver.m_fEnabled)
                            comServer.SetEnabled(newData.m_dhcpserver.m_fEnabled);
                        /* Save DHCP server configuration: */
                        if (   comServer.isOk()
                            && newData.m_dhcpserver.m_fEnabled
                            && (   newData.m_dhcpserver.m_strAddress != oldData.m_dhcpserver.m_strAddress
                                || newData.m_dhcpserver.m_strMask != oldData.m_dhcpserver.m_strMask
                                || newData.m_dhcpserver.m_strLowerAddress != oldData.m_dhcpserver.m_strLowerAddress
                                || newData.m_dhcpserver.m_strUpperAddress != oldData.m_dhcpserver.m_strUpperAddress))
                            comServer.SetConfiguration(newData.m_dhcpserver.m_strAddress, newData.m_dhcpserver.m_strMask,
                                                       newData.m_dhcpserver.m_strLowerAddress, newData.m_dhcpserver.m_strUpperAddress);

                        /* Show error message if necessary: */
                        if (!comServer.isOk())
                            UINotificationMessage::cannotChangeDHCPServerParameter(comServer);
                    }
                }
            }

            /* Find corresponding interface again (if necessary): */
            if (!comInterface.isOk())
            {
                comInterface = comHost.FindHostNetworkInterfaceByName(oldData.m_interface.m_strName);

                /* Show error message if necessary: */
                if (!comHost.isOk() || comInterface.isNull())
                    UINotificationMessage::cannotFindHostNetworkInterface(comHost, oldData.m_interface.m_strName);
            }

            /* If interface is Ok now: */
            if (comInterface.isNotNull() && comInterface.isOk())
            {
                /* Update interface in the tree: */
                UIDataHostNetwork data;
                loadHostNetwork(comInterface, data);
                updateItemForHostNetwork(data, true, pItem);

                /* Make sure current item fetched: */
                sltHandleCurrentItemChangeHostNetwork();

                /* Adjust tree-widgets: */
                sltAdjustTreeWidgets();
            }
        }
#endif /* !VBOX_WS_MAC */
    }

    /* Make sure button states updated: */
    m_pDetailsWidgetHostNetwork->updateButtonStates();
}

void UINetworkManagerWidget::sltHandleCurrentItemChangeNATNetworkHoldingPosition(bool fHoldPosition)
{
    /* Update actions: */
    updateActionAvailability();

    /* Check NAT network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetNATNetwork, ("NAT network tree-widget isn't created!\n"));

    /* Get network item: */
    UIItemNATNetwork *pItem = static_cast<UIItemNATNetwork*>(m_pTreeWidgetNATNetwork->currentItem());

    /* Check NAT network details-widget: */
    AssertMsgReturnVoid(m_pDetailsWidgetNATNetwork, ("NAT network details-widget isn't created!\n"));

    /* If there is an item => update details data: */
    if (pItem)
    {
        QStringList busyNamesForItem = busyNamesNAT();
        busyNamesForItem.removeAll(pItem->name());
        m_pDetailsWidgetNATNetwork->setData(*pItem, busyNamesForItem, fHoldPosition);
    }
    /* Otherwise => clear details: */
    else
        m_pDetailsWidgetNATNetwork->setData(UIDataNATNetwork());
}

void UINetworkManagerWidget::sltHandleCurrentItemChangeNATNetwork()
{
    sltHandleCurrentItemChangeNATNetworkHoldingPosition(false /* hold position? */);
}

void UINetworkManagerWidget::sltHandleContextMenuRequestNATNetwork(const QPoint &position)
{
    /* Check NAT network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetNATNetwork, ("NAT network tree-widget isn't created!\n"));

    /* Compose temporary context-menu: */
    QMenu menu;
    if (m_pTreeWidgetNATNetwork->itemAt(position))
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_T_Details));
    }
    else
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Create));
//        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Refresh));
    }
    /* And show it: */
    menu.exec(m_pTreeWidgetNATNetwork->mapToGlobal(position));
}

void UINetworkManagerWidget::sltApplyDetailsChangesNATNetwork()
{
    /* Check NAT network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetNATNetwork, ("NAT network tree-widget isn't created!\n"));

    /* Get NAT network item: */
    UIItemNATNetwork *pItem = static_cast<UIItemNATNetwork*>(m_pTreeWidgetNATNetwork->currentItem());
    AssertMsgReturnVoid(pItem, ("Current item must not be null!\n"));

    /* Check NAT network details-widget: */
    AssertMsgReturnVoid(m_pDetailsWidgetNATNetwork, ("NAT network details-widget isn't created!\n"));

    /* Revalidate NAT network details: */
    if (m_pDetailsWidgetNATNetwork->revalidate())
    {
        /* Get item data: */
        UIDataNATNetwork oldData = *pItem;
        UIDataNATNetwork newData = m_pDetailsWidgetNATNetwork->data();

        /* Get VirtualBox for further activities: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();

        /* Find corresponding network: */
        CNATNetwork comNetwork = comVBox.FindNATNetworkByName(oldData.m_strName);
        CNATNetwork comNetworkBase = comNetwork;

        /* Show error message if necessary: */
        if (!comVBox.isOk() || comNetwork.isNull())
            UINotificationMessage::cannotFindNATNetwork(comVBox, oldData.m_strName);
        else
        {
            /* Save NAT network name: */
            if (comNetwork.isOk() && newData.m_strName != oldData.m_strName)
                comNetwork.SetNetworkName(newData.m_strName);
            /* Save NAT network IPv4: */
            if (comNetwork.isOk() && newData.m_strPrefixIPv4 != oldData.m_strPrefixIPv4)
                comNetwork.SetNetwork(newData.m_strPrefixIPv4);
            /* Save NAT network IPv6: */
            if (comNetwork.isOk() && newData.m_strPrefixIPv6 != oldData.m_strPrefixIPv6)
                comNetwork.SetIPv6Prefix(newData.m_strPrefixIPv6);
            /* Save whether NAT network needs DHCP server: */
            if (comNetwork.isOk() && newData.m_fSupportsDHCP != oldData.m_fSupportsDHCP)
                comNetwork.SetNeedDhcpServer(newData.m_fSupportsDHCP);
            /* Save whether NAT network supports IPv6: */
            if (comNetwork.isOk() && newData.m_fSupportsIPv6 != oldData.m_fSupportsIPv6)
                comNetwork.SetIPv6Enabled(newData.m_fSupportsIPv6);
            /* Save whether NAT network should advertise default IPv6 route: */
            if (comNetwork.isOk() && newData.m_fAdvertiseDefaultIPv6Route != oldData.m_fAdvertiseDefaultIPv6Route)
                comNetwork.SetAdvertiseDefaultIPv6RouteEnabled(newData.m_fAdvertiseDefaultIPv6Route);

            /* Save IPv4 forwarding rules: */
            if (comNetwork.isOk() && newData.m_rules4 != oldData.m_rules4)
            {
                UIPortForwardingDataList oldRules = oldData.m_rules4;

                /* Remove rules to be removed: */
                foreach (const UIDataPortForwardingRule &oldRule, oldData.m_rules4)
                    if (comNetwork.isOk() && !newData.m_rules4.contains(oldRule))
                    {
                        comNetwork.RemovePortForwardRule(false /* IPv6? */, oldRule.name);
                        oldRules.removeAll(oldRule);
                    }
                /* Add rules to be added: */
                foreach (const UIDataPortForwardingRule &newRule, newData.m_rules4)
                    if (comNetwork.isOk() && !oldRules.contains(newRule))
                    {
                        comNetwork.AddPortForwardRule(false /* IPv6? */, newRule.name, newRule.protocol,
                                                      newRule.hostIp, newRule.hostPort.value(),
                                                      newRule.guestIp, newRule.guestPort.value());
                        oldRules.append(newRule);
                    }
            }
            /* Save IPv6 forwarding rules: */
            if (comNetwork.isOk() && newData.m_rules6 != oldData.m_rules6)
            {
                UIPortForwardingDataList oldRules = oldData.m_rules6;

                /* Remove rules to be removed: */
                foreach (const UIDataPortForwardingRule &oldRule, oldData.m_rules6)
                    if (comNetwork.isOk() && !newData.m_rules6.contains(oldRule))
                    {
                        comNetwork.RemovePortForwardRule(true /* IPv6? */, oldRule.name);
                        oldRules.removeAll(oldRule);
                    }
                /* Add rules to be added: */
                foreach (const UIDataPortForwardingRule &newRule, newData.m_rules6)
                    if (comNetwork.isOk() && !oldRules.contains(newRule))
                    {
                        comNetwork.AddPortForwardRule(true /* IPv6? */, newRule.name, newRule.protocol,
                                                      newRule.hostIp, newRule.hostPort.value(),
                                                      newRule.guestIp, newRule.guestPort.value());
                        oldRules.append(newRule);
                    }
            }

            /* Show error message if necessary: */
            if (!comNetwork.isOk())
                UINotificationMessage::cannotChangeNATNetworkParameter(comNetwork);

            /* Update network in the tree: */
            UIDataNATNetwork data;
            loadNATNetwork(comNetworkBase, data);
            updateItemForNATNetwork(data, true, pItem);

            /* Make sure current item fetched, trying to hold chosen position: */
            sltHandleCurrentItemChangeNATNetworkHoldingPosition(true /* hold position? */);

            /* Adjust tree-widgets: */
            sltAdjustTreeWidgets();
        }
    }

    /* Make sure button states updated: */
    m_pDetailsWidgetNATNetwork->updateButtonStates();
}

void UINetworkManagerWidget::sltHandleCurrentItemChangeCloudNetwork()
{
    /* Update actions: */
    updateActionAvailability();

    /* Check cloud network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetCloudNetwork, ("Cloud network tree-widget isn't created!\n"));

    /* Get network item: */
    UIItemCloudNetwork *pItem = static_cast<UIItemCloudNetwork*>(m_pTreeWidgetCloudNetwork->currentItem());

    /* Check Cloud network details-widget: */
    AssertMsgReturnVoid(m_pDetailsWidgetCloudNetwork, ("Cloud network details-widget isn't created!\n"));

    /* If there is an item => update details data: */
    if (pItem)
    {
        QStringList busyNamesForItem = busyNamesCloud();
        busyNamesForItem.removeAll(pItem->name());
        m_pDetailsWidgetCloudNetwork->setData(*pItem, busyNamesForItem);
    }
    /* Otherwise => clear details: */
    else
        m_pDetailsWidgetCloudNetwork->setData(UIDataCloudNetwork());
}

void UINetworkManagerWidget::sltHandleContextMenuRequestCloudNetwork(const QPoint &position)
{
    /* Check cloud network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetCloudNetwork, ("Cloud network tree-widget isn't created!\n"));

    /* Compose temporary context-menu: */
    QMenu menu;
    if (m_pTreeWidgetCloudNetwork->itemAt(position))
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_T_Details));
    }
    else
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Create));
//        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Refresh));
    }
    /* And show it: */
    menu.exec(m_pTreeWidgetCloudNetwork->mapToGlobal(position));
}

void UINetworkManagerWidget::sltApplyDetailsChangesCloudNetwork()
{
    /* Check cloud network tree-widget: */
    AssertMsgReturnVoid(m_pTreeWidgetCloudNetwork, ("Cloud network tree-widget isn't created!\n"));

    /* Get Cloud network item: */
    UIItemCloudNetwork *pItem = static_cast<UIItemCloudNetwork*>(m_pTreeWidgetCloudNetwork->currentItem());
    AssertMsgReturnVoid(pItem, ("Current item must not be null!\n"));

    /* Check Cloud network details-widget: */
    AssertMsgReturnVoid(m_pDetailsWidgetCloudNetwork, ("Cloud network details-widget isn't created!\n"));

    /* Revalidate Cloud network details: */
    if (m_pDetailsWidgetCloudNetwork->revalidate())
    {
        /* Get item data: */
        UIDataCloudNetwork oldData = *pItem;
        UIDataCloudNetwork newData = m_pDetailsWidgetCloudNetwork->data();

        /* Get VirtualBox for further activities: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();

        /* Find corresponding network: */
        CCloudNetwork comNetwork = comVBox.FindCloudNetworkByName(oldData.m_strName);
        CCloudNetwork comNetworkBase = comNetwork;

        /* Show error message if necessary: */
        if (!comVBox.isOk() || comNetwork.isNull())
            UINotificationMessage::cannotFindCloudNetwork(comVBox, oldData.m_strName);
        else
        {
            /* Save whether cloud network enabled: */
            if (comNetwork.isOk() && newData.m_fEnabled != oldData.m_fEnabled)
                comNetwork.SetEnabled(newData.m_fEnabled);
            /* Save cloud network name: */
            if (comNetwork.isOk() && newData.m_strName != oldData.m_strName)
                comNetwork.SetNetworkName(newData.m_strName);
            /* Save cloud provider: */
            if (comNetwork.isOk() && newData.m_strProvider != oldData.m_strProvider)
                comNetwork.SetProvider(newData.m_strProvider);
            /* Save cloud profile: */
            if (comNetwork.isOk() && newData.m_strProfile != oldData.m_strProfile)
                comNetwork.SetProfile(newData.m_strProfile);
            /* Save cloud network id: */
            if (comNetwork.isOk() && newData.m_strId != oldData.m_strId)
                comNetwork.SetNetworkId(newData.m_strId);

            /* Show error message if necessary: */
            if (!comNetwork.isOk())
                UINotificationMessage::cannotChangeCloudNetworkParameter(comNetwork);

            /* Update network in the tree: */
            UIDataCloudNetwork data;
            loadCloudNetwork(comNetworkBase, data);
            updateItemForCloudNetwork(data, true, pItem);

            /* Make sure current item fetched: */
            sltHandleCurrentItemChangeCloudNetwork();

            /* Adjust tree-widgets: */
            sltAdjustTreeWidgets();
        }
    }

    /* Make sure button states updated: */
    m_pDetailsWidgetNATNetwork->updateButtonStates();
}

void UINetworkManagerWidget::prepare()
{
    /* Prepare self: */
    uiCommon().setHelpKeyword(this, "network-manager");

    /* Prepare stuff: */
    prepareActions();
    prepareWidgets();

    /* Load settings: */
    loadSettings();

    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UINetworkManagerWidget::sltRetranslateUI);

    /* Load networks: */
    loadHostNetworks();
    loadNATNetworks();
    loadCloudNetworks();
}

void UINetworkManagerWidget::prepareActions()
{
    /* First of all, add actions which has smaller shortcut scope: */
    addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Create));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Network_T_Details));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Refresh));

    /* Connect actions: */
    connect(m_pActionPool->action(UIActionIndexMN_M_Network_S_Create), &QAction::triggered,
            this, &UINetworkManagerWidget::sltCreateHostNetwork);
    connect(m_pActionPool->action(UIActionIndexMN_M_Network_S_Create), &QAction::triggered,
            this, &UINetworkManagerWidget::sltCreateNATNetwork);
    connect(m_pActionPool->action(UIActionIndexMN_M_Network_S_Create), &QAction::triggered,
            this, &UINetworkManagerWidget::sltCreateCloudNetwork);
    connect(m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove), &QAction::triggered,
            this, &UINetworkManagerWidget::sltRemoveHostNetwork);
    connect(m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove), &QAction::triggered,
            this, &UINetworkManagerWidget::sltRemoveNATNetwork);
    connect(m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove), &QAction::triggered,
            this, &UINetworkManagerWidget::sltRemoveCloudNetwork);
    connect(m_pActionPool->action(UIActionIndexMN_M_Network_T_Details), &QAction::toggled,
            this, &UINetworkManagerWidget::sltToggleDetailsVisibility);
}

void UINetworkManagerWidget::prepareWidgets()
{
    /* Create main-layout: */
    new QVBoxLayout(this);
    if (layout())
    {
        /* Configure layout: */
        layout()->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
        layout()->setSpacing(10);
#else
        layout()->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif

        /* Prepare toolbar, if requested: */
        if (m_fShowToolbar)
            prepareToolBar();

        /* Prepare tab-widget: */
        prepareTabWidget();

        /* Prepare details widgets: */
        prepareDetailsWidgetHostNetwork();
        prepareDetailsWidgetNATNetwork();
        prepareDetailsWidgetCloudNetwork();
    }
}

void UINetworkManagerWidget::prepareToolBar()
{
    /* Prepare toolbar: */
    m_pToolBar = new QIToolBar(parentWidget());
    if (m_pToolBar)
    {
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Create));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Network_T_Details));

#ifdef VBOX_WS_MAC
        /* Check whether we are embedded into a stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Add into layout: */
            layout()->addWidget(m_pToolBar);
        }
#else /* !VBOX_WS_MAC */
        /* Add into layout: */
        layout()->addWidget(m_pToolBar);
#endif /* !VBOX_WS_MAC */
    }
}

void UINetworkManagerWidget::prepareTabWidget()
{
    /* Create tab-widget: */
    m_pTabWidget = new QITabWidget(this);
    if (m_pTabWidget)
    {
        connect(m_pTabWidget, &QITabWidget::currentChanged,
                this, &UINetworkManagerWidget::sltHandleCurrentTabWidgetIndexChange);

        prepareTabHostNetwork();
        prepareTabNATNetwork();
        prepareTabCloudNetwork();

        /* Add into layout: */
        layout()->addWidget(m_pTabWidget);
    }
}

void UINetworkManagerWidget::prepareTabHostNetwork()
{
    /* Prepare host network tab: */
    m_pTabHostNetwork = new QWidget(m_pTabWidget);
    if (m_pTabHostNetwork)
    {
        /* Prepare host network layout: */
        m_pLayoutHostNetwork = new QVBoxLayout(m_pTabHostNetwork);
        if (m_pLayoutHostNetwork)
            prepareTreeWidgetHostNetwork();

        /* Add into tab-widget: */
        m_pTabWidget->insertTab(TabWidgetIndex_HostNetwork, m_pTabHostNetwork, QString());
    }
}

void UINetworkManagerWidget::prepareTreeWidgetHostNetwork()
{
    /* Prepare host network tree-widget: */
    m_pTreeWidgetHostNetwork = new QITreeWidget(m_pTabHostNetwork);
    if (m_pTreeWidgetHostNetwork)
    {
        m_pTreeWidgetHostNetwork->setRootIsDecorated(false);
        m_pTreeWidgetHostNetwork->setAlternatingRowColors(true);
        m_pTreeWidgetHostNetwork->setContextMenuPolicy(Qt::CustomContextMenu);
        m_pTreeWidgetHostNetwork->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_pTreeWidgetHostNetwork->setColumnCount(HostNetworkColumn_Max);
        m_pTreeWidgetHostNetwork->setSortingEnabled(true);
        m_pTreeWidgetHostNetwork->sortByColumn(HostNetworkColumn_Name, Qt::AscendingOrder);
        m_pTreeWidgetHostNetwork->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
        connect(m_pTreeWidgetHostNetwork, &QITreeWidget::currentItemChanged,
                this, &UINetworkManagerWidget::sltHandleCurrentItemChangeHostNetwork);
        connect(m_pTreeWidgetHostNetwork, &QITreeWidget::customContextMenuRequested,
                this, &UINetworkManagerWidget::sltHandleContextMenuRequestHostNetwork);
        connect(m_pTreeWidgetHostNetwork, &QITreeWidget::itemDoubleClicked,
                m_pActionPool->action(UIActionIndexMN_M_Network_T_Details), &QAction::setChecked);

        /* Add into layout: */
        m_pLayoutHostNetwork->addWidget(m_pTreeWidgetHostNetwork);
    }
}

void UINetworkManagerWidget::prepareDetailsWidgetHostNetwork()
{
    /* Prepare host network details-widget: */
    m_pDetailsWidgetHostNetwork = new UIDetailsWidgetHostNetwork(m_enmEmbedding, this);
    if (m_pDetailsWidgetHostNetwork)
    {
        m_pDetailsWidgetHostNetwork->setVisible(false);
        m_pDetailsWidgetHostNetwork->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        connect(m_pDetailsWidgetHostNetwork, &UIDetailsWidgetHostNetwork::sigDataChanged,
                this, &UINetworkManagerWidget::sigDetailsDataChangedHostNetwork);
        connect(m_pDetailsWidgetHostNetwork, &UIDetailsWidgetHostNetwork::sigDataChangeRejected,
                this, &UINetworkManagerWidget::sltHandleCurrentItemChangeHostNetwork);
        connect(m_pDetailsWidgetHostNetwork, &UIDetailsWidgetHostNetwork::sigDataChangeAccepted,
                this, &UINetworkManagerWidget::sltApplyDetailsChangesHostNetwork);

        /* Add into layout: */
        layout()->addWidget(m_pDetailsWidgetHostNetwork);
    }
}

void UINetworkManagerWidget::prepareTabNATNetwork()
{
    /* Prepare NAT network tab: */
    m_pTabNATNetwork = new QWidget(m_pTabWidget);
    if (m_pTabNATNetwork)
    {
        /* Prepare NAT network layout: */
        m_pLayoutNATNetwork = new QVBoxLayout(m_pTabNATNetwork);
        if (m_pLayoutNATNetwork)
            prepareTreeWidgetNATNetwork();

        /* Add into tab-widget: */
        m_pTabWidget->insertTab(TabWidgetIndex_NATNetwork, m_pTabNATNetwork, QString());
    }
}

void UINetworkManagerWidget::prepareTreeWidgetNATNetwork()
{
    /* Prepare NAT network tree-widget: */
    m_pTreeWidgetNATNetwork = new QITreeWidget(m_pTabNATNetwork);
    if (m_pTreeWidgetNATNetwork)
    {
        m_pTreeWidgetNATNetwork->setRootIsDecorated(false);
        m_pTreeWidgetNATNetwork->setAlternatingRowColors(true);
        m_pTreeWidgetNATNetwork->setContextMenuPolicy(Qt::CustomContextMenu);
        m_pTreeWidgetNATNetwork->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_pTreeWidgetNATNetwork->setColumnCount(NATNetworkColumn_Max);
        m_pTreeWidgetNATNetwork->setSortingEnabled(true);
        m_pTreeWidgetNATNetwork->sortByColumn(NATNetworkColumn_Name, Qt::AscendingOrder);
        m_pTreeWidgetNATNetwork->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
        connect(m_pTreeWidgetNATNetwork, &QITreeWidget::currentItemChanged,
                this, &UINetworkManagerWidget::sltHandleCurrentItemChangeNATNetwork);
        connect(m_pTreeWidgetNATNetwork, &QITreeWidget::customContextMenuRequested,
                this, &UINetworkManagerWidget::sltHandleContextMenuRequestNATNetwork);
        connect(m_pTreeWidgetNATNetwork, &QITreeWidget::itemDoubleClicked,
                m_pActionPool->action(UIActionIndexMN_M_Network_T_Details), &QAction::setChecked);

        /* Add into layout: */
        m_pLayoutNATNetwork->addWidget(m_pTreeWidgetNATNetwork);
    }
}

void UINetworkManagerWidget::prepareDetailsWidgetNATNetwork()
{
    /* Prepare NAT network details-widget: */
    m_pDetailsWidgetNATNetwork = new UIDetailsWidgetNATNetwork(m_enmEmbedding, this);
    if (m_pDetailsWidgetNATNetwork)
    {
        m_pDetailsWidgetNATNetwork->setVisible(false);
        m_pDetailsWidgetNATNetwork->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        connect(m_pDetailsWidgetNATNetwork, &UIDetailsWidgetNATNetwork::sigDataChanged,
                this, &UINetworkManagerWidget::sigDetailsDataChangedNATNetwork);
        connect(m_pDetailsWidgetNATNetwork, &UIDetailsWidgetNATNetwork::sigDataChangeRejected,
                this, &UINetworkManagerWidget::sltHandleCurrentItemChangeNATNetwork);
        connect(m_pDetailsWidgetNATNetwork, &UIDetailsWidgetNATNetwork::sigDataChangeAccepted,
                this, &UINetworkManagerWidget::sltApplyDetailsChangesNATNetwork);

        /* Add into layout: */
        layout()->addWidget(m_pDetailsWidgetNATNetwork);
    }
}

void UINetworkManagerWidget::prepareTabCloudNetwork()
{
    /* Prepare cloud network tab: */
    m_pTabCloudNetwork = new QWidget(m_pTabWidget);
    if (m_pTabCloudNetwork)
    {
        /* Prepare cloud network layout: */
        m_pLayoutCloudNetwork = new QVBoxLayout(m_pTabCloudNetwork);
        if (m_pLayoutCloudNetwork)
            prepareTreeWidgetCloudNetwork();

        /* Add into tab-widget: */
        m_pTabWidget->insertTab(TabWidgetIndex_CloudNetwork, m_pTabCloudNetwork, QString());
    }
}

void UINetworkManagerWidget::prepareTreeWidgetCloudNetwork()
{
    /* Prepare cloud network tree-widget: */
    m_pTreeWidgetCloudNetwork = new QITreeWidget(m_pTabCloudNetwork);
    if (m_pTreeWidgetCloudNetwork)
    {
        m_pTreeWidgetCloudNetwork->setRootIsDecorated(false);
        m_pTreeWidgetCloudNetwork->setAlternatingRowColors(true);
        m_pTreeWidgetCloudNetwork->setContextMenuPolicy(Qt::CustomContextMenu);
        m_pTreeWidgetCloudNetwork->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_pTreeWidgetCloudNetwork->setColumnCount(CloudNetworkColumn_Max);
        m_pTreeWidgetCloudNetwork->setSortingEnabled(true);
        m_pTreeWidgetCloudNetwork->sortByColumn(CloudNetworkColumn_Name, Qt::AscendingOrder);
        m_pTreeWidgetCloudNetwork->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
        connect(m_pTreeWidgetCloudNetwork, &QITreeWidget::currentItemChanged,
                this, &UINetworkManagerWidget::sltHandleCurrentItemChangeCloudNetwork);
        connect(m_pTreeWidgetCloudNetwork, &QITreeWidget::customContextMenuRequested,
                this, &UINetworkManagerWidget::sltHandleContextMenuRequestCloudNetwork);
        connect(m_pTreeWidgetCloudNetwork, &QITreeWidget::itemDoubleClicked,
                m_pActionPool->action(UIActionIndexMN_M_Network_T_Details), &QAction::setChecked);

        /* Add into layout: */
        m_pLayoutCloudNetwork->addWidget(m_pTreeWidgetCloudNetwork);
    }
}

void UINetworkManagerWidget::prepareDetailsWidgetCloudNetwork()
{
    /* Prepare cloud network details-widget: */
    m_pDetailsWidgetCloudNetwork = new UIDetailsWidgetCloudNetwork(m_enmEmbedding, this);
    if (m_pDetailsWidgetCloudNetwork)
    {
        m_pDetailsWidgetCloudNetwork->setVisible(false);
        m_pDetailsWidgetCloudNetwork->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        connect(m_pDetailsWidgetCloudNetwork, &UIDetailsWidgetCloudNetwork::sigDataChanged,
                this, &UINetworkManagerWidget::sigDetailsDataChangedCloudNetwork);
        connect(m_pDetailsWidgetCloudNetwork, &UIDetailsWidgetCloudNetwork::sigDataChangeRejected,
                this, &UINetworkManagerWidget::sltHandleCurrentItemChangeCloudNetwork);
        connect(m_pDetailsWidgetCloudNetwork, &UIDetailsWidgetCloudNetwork::sigDataChangeAccepted,
                this, &UINetworkManagerWidget::sltApplyDetailsChangesCloudNetwork);

        /* Add into layout: */
        layout()->addWidget(m_pDetailsWidgetCloudNetwork);
    }
}

void UINetworkManagerWidget::loadSettings()
{
    /* Details action/widget: */
    if (m_pActionPool)
    {
        m_pActionPool->action(UIActionIndexMN_M_Network_T_Details)->setChecked(gEDataManager->hostNetworkManagerDetailsExpanded());
        sltToggleDetailsVisibility(m_pActionPool->action(UIActionIndexMN_M_Network_T_Details)->isChecked());
    }
}

void UINetworkManagerWidget::loadHostNetworks()
{
    /* Check host network tree-widget: */
    if (!m_pTreeWidgetHostNetwork)
        return;

    /* Clear tree first of all: */
    m_pTreeWidgetHostNetwork->clear();

#ifdef VBOX_WS_MAC
    /* Get VirtualBox for further activities: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Get networks for further activities: */
    const QVector<CHostOnlyNetwork> networks = comVBox.GetHostOnlyNetworks();

    /* Show error message if necessary: */
    if (!comVBox.isOk())
        UINotificationMessage::cannotAcquireVirtualBoxParameter(comVBox);
    else
    {
        /* For each host network => load it to the tree: */
        foreach (const CHostOnlyNetwork &comNetwork, networks)
        {
            UIDataHostNetwork data;
            loadHostNetwork(comNetwork, data);
            createItemForHostNetwork(data, false);
        }

        /* Choose the 1st item as current initially: */
        m_pTreeWidgetHostNetwork->setCurrentItem(m_pTreeWidgetHostNetwork->topLevelItem(0));
        sltHandleCurrentItemChangeHostNetwork();

        /* Adjust tree-widgets: */
        sltAdjustTreeWidgets();
    }

#else /* !VBOX_WS_MAC */

    /* Get host for further activities: */
    const CHost comHost = gpGlobalSession->host();

    /* Get interfaces for further activities: */
    const QVector<CHostNetworkInterface> interfaces = comHost.GetNetworkInterfaces();

    /* Show error message if necessary: */
    if (!comHost.isOk())
        UINotificationMessage::cannotAcquireHostParameter(comHost);
    else
    {
        /* For each host-only interface => load it to the tree: */
        foreach (const CHostNetworkInterface &comInterface, interfaces)
            if (comInterface.GetInterfaceType() == KHostNetworkInterfaceType_HostOnly)
            {
                UIDataHostNetwork data;
                loadHostNetwork(comInterface, data);
                createItemForHostNetwork(data, false);
            }

        /* Choose the 1st item as current initially: */
        m_pTreeWidgetHostNetwork->setCurrentItem(m_pTreeWidgetHostNetwork->topLevelItem(0));
        sltHandleCurrentItemChangeHostNetwork();

        /* Adjust tree-widgets: */
        sltAdjustTreeWidgets();
    }
#endif /* !VBOX_WS_MAC */
}

#ifdef VBOX_WS_MAC
void UINetworkManagerWidget::loadHostNetwork(const CHostOnlyNetwork &comNetwork, UIDataHostNetwork &data)
{
    /* Gather network settings: */
    if (comNetwork.isNotNull())
        data.m_fExists = true;
    if (comNetwork.isOk())
        data.m_strName = comNetwork.GetNetworkName();
    if (comNetwork.isOk())
        data.m_strMask = comNetwork.GetNetworkMask();
    if (comNetwork.isOk())
        data.m_strLBnd = comNetwork.GetLowerIP();
    if (comNetwork.isOk())
        data.m_strUBnd = comNetwork.GetUpperIP();

    /* Show error message if necessary: */
    if (!comNetwork.isOk())
        UINotificationMessage::cannotAcquireHostOnlyNetworkParameter(comNetwork);
}

#else /* !VBOX_WS_MAC */

void UINetworkManagerWidget::loadHostNetwork(const CHostNetworkInterface &comInterface, UIDataHostNetwork &data)
{
    /* Gather interface settings: */
    if (comInterface.isNotNull())
        data.m_interface.m_fExists = true;
    if (comInterface.isOk())
        data.m_interface.m_strName = comInterface.GetName();
    if (comInterface.isOk())
        data.m_interface.m_fDHCPEnabled = comInterface.GetDHCPEnabled();
    if (comInterface.isOk())
        data.m_interface.m_strAddress = comInterface.GetIPAddress();
    if (comInterface.isOk())
        data.m_interface.m_strMask = comInterface.GetNetworkMask();
    if (comInterface.isOk())
        data.m_interface.m_fSupportedIPv6 = comInterface.GetIPV6Supported();
    if (comInterface.isOk())
        data.m_interface.m_strAddress6 = comInterface.GetIPV6Address();
    if (comInterface.isOk())
        data.m_interface.m_strPrefixLength6 = QString::number(comInterface.GetIPV6NetworkMaskPrefixLength());

    /* Get host interface network name for further activities: */
    QString strNetworkName;
    if (comInterface.isOk())
        strNetworkName = comInterface.GetNetworkName();

    /* Show error message if necessary: */
    if (!comInterface.isOk())
        UINotificationMessage::cannotAcquireHostNetworkInterfaceParameter(comInterface);

    /* Get VBox for further activities: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Find corresponding DHCP server (create if necessary): */
    CDHCPServer comServer = comVBox.FindDHCPServerByNetworkName(strNetworkName);
    if (!comVBox.isOk() || comServer.isNull())
        comServer = comVBox.CreateDHCPServer(strNetworkName);

    /* Show error message if necessary: */
    if (!comVBox.isOk() || comServer.isNull())
        UINotificationMessage::cannotCreateDHCPServer(comVBox, strNetworkName);
    else
    {
        /* Gather DHCP server settings: */
        if (comServer.isOk())
            data.m_dhcpserver.m_fEnabled = comServer.GetEnabled();
        if (comServer.isOk())
            data.m_dhcpserver.m_strAddress = comServer.GetIPAddress();
        if (comServer.isOk())
            data.m_dhcpserver.m_strMask = comServer.GetNetworkMask();
        if (comServer.isOk())
            data.m_dhcpserver.m_strLowerAddress = comServer.GetLowerIP();
        if (comServer.isOk())
            data.m_dhcpserver.m_strUpperAddress = comServer.GetUpperIP();

        /* Show error message if necessary: */
        if (!comServer.isOk())
            return UINotificationMessage::cannotAcquireDHCPServerParameter(comServer);
    }
}
#endif /* !VBOX_WS_MAC */

void UINetworkManagerWidget::loadNATNetworks()
{
    /* Check NAT network tree-widget: */
    if (!m_pTreeWidgetNATNetwork)
        return;

    /* Clear tree first of all: */
    m_pTreeWidgetNATNetwork->clear();

    /* Get VirtualBox for further activities: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Get interfaces for further activities: */
    const QVector<CNATNetwork> networks = comVBox.GetNATNetworks();

    /* Show error message if necessary: */
    if (!comVBox.isOk())
        UINotificationMessage::cannotAcquireVirtualBoxParameter(comVBox);
    else
    {
        /* For each NAT network => load it to the tree: */
        foreach (const CNATNetwork &comNetwork, networks)
        {
            UIDataNATNetwork data;
            loadNATNetwork(comNetwork, data);
            createItemForNATNetwork(data, false);
        }

        /* Choose the 1st item as current initially: */
        m_pTreeWidgetNATNetwork->setCurrentItem(m_pTreeWidgetNATNetwork->topLevelItem(0));
        sltHandleCurrentItemChangeNATNetwork();

        /* Adjust tree-widgets: */
        sltAdjustTreeWidgets();
    }
}

void UINetworkManagerWidget::loadNATNetwork(const CNATNetwork &comNetwork, UIDataNATNetwork &data)
{
    /* Gather network settings: */
    if (comNetwork.isNotNull())
        data.m_fExists = true;
    if (comNetwork.isOk())
        data.m_strName = comNetwork.GetNetworkName();
    if (comNetwork.isOk())
        data.m_strPrefixIPv4 = comNetwork.GetNetwork();
    if (comNetwork.isOk())
        data.m_strPrefixIPv6 = comNetwork.GetIPv6Prefix();
    if (comNetwork.isOk())
        data.m_fSupportsDHCP = comNetwork.GetNeedDhcpServer();
    if (comNetwork.isOk())
        data.m_fSupportsIPv6 = comNetwork.GetIPv6Enabled();
    if (comNetwork.isOk())
        data.m_fAdvertiseDefaultIPv6Route = comNetwork.GetAdvertiseDefaultIPv6RouteEnabled();

    /* Gather forwarding rules: */
    if (comNetwork.isOk())
    {
        /* Load IPv4 rules: */
        foreach (QString strIPv4Rule, comNetwork.GetPortForwardRules4())
        {
            /* Replace all ':' with ',' first: */
            strIPv4Rule.replace(':', ',');
            /* Parse rules: */
            QStringList rules = strIPv4Rule.split(',');
            Assert(rules.size() == 6);
            if (rules.size() != 6)
                continue;
            data.m_rules4 << UIDataPortForwardingRule(rules.at(0),
                                                      gpConverter->fromInternalString<KNATProtocol>(rules.at(1)),
                                                      QString(rules.at(2)).remove('[').remove(']'),
                                                      rules.at(3).toUInt(),
                                                      QString(rules.at(4)).remove('[').remove(']'),
                                                      rules.at(5).toUInt());
        }

        /* Load IPv6 rules: */
        foreach (QString strIPv6Rule, comNetwork.GetPortForwardRules6())
        {
            /* Replace all ':' with ',' first: */
            strIPv6Rule.replace(':', ',');
            /* But replace ',' back with ':' for addresses: */
            QRegularExpression re("\\[[0-9a-fA-F,]*,[0-9a-fA-F,]*\\]");
            re.setPatternOptions(QRegularExpression::InvertedGreedinessOption);
            QRegularExpressionMatch mt = re.match(strIPv6Rule);
            while (mt.hasMatch())
            {
                QString strCapOld = mt.captured();
                QString strCapNew = strCapOld;
                strCapNew.replace(',', ':');
                strIPv6Rule.replace(strCapOld, strCapNew);
                mt = re.match(strIPv6Rule);
            }
            /* Parse rules: */
            QStringList rules = strIPv6Rule.split(',');
            Assert(rules.size() == 6);
            if (rules.size() != 6)
                continue;
            data.m_rules6 << UIDataPortForwardingRule(rules.at(0),
                                                      gpConverter->fromInternalString<KNATProtocol>(rules.at(1)),
                                                      QString(rules.at(2)).remove('[').remove(']'),
                                                      rules.at(3).toUInt(),
                                                      QString(rules.at(4)).remove('[').remove(']'),
                                                      rules.at(5).toUInt());
        }
    }

    /* Show error message if necessary: */
    if (!comNetwork.isOk())
        UINotificationMessage::cannotAcquireNATNetworkParameter(comNetwork);
}

void UINetworkManagerWidget::loadCloudNetworks()
{
    /* Check cloud network tree-widget: */
    if (!m_pTreeWidgetCloudNetwork)
        return;

    /* Clear tree first of all: */
    m_pTreeWidgetCloudNetwork->clear();

    /* Get VirtualBox for further activities: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Get interfaces for further activities: */
    const QVector<CCloudNetwork> networks = comVBox.GetCloudNetworks();

    /* Show error message if necessary: */
    if (!comVBox.isOk())
        UINotificationMessage::cannotAcquireVirtualBoxParameter(comVBox);
    else
    {
        /* For each cloud network => load it to the tree: */
        foreach (const CCloudNetwork &comNetwork, networks)
        {
            UIDataCloudNetwork data;
            loadCloudNetwork(comNetwork, data);
            createItemForCloudNetwork(data, false);
        }

        /* Choose the 1st item as current initially: */
        m_pTreeWidgetCloudNetwork->setCurrentItem(m_pTreeWidgetCloudNetwork->topLevelItem(0));
        sltHandleCurrentItemChangeCloudNetwork();

        /* Adjust tree-widgets: */
        sltAdjustTreeWidgets();
    }
}

void UINetworkManagerWidget::loadCloudNetwork(const CCloudNetwork &comNetwork, UIDataCloudNetwork &data)
{
    /* Gather network settings: */
    if (comNetwork.isNotNull())
        data.m_fExists = true;
    if (comNetwork.isNotNull())
        data.m_fEnabled = comNetwork.GetEnabled();
    if (comNetwork.isOk())
        data.m_strName = comNetwork.GetNetworkName();
    if (comNetwork.isOk())
        data.m_strProvider = comNetwork.GetProvider();
    if (comNetwork.isOk())
        data.m_strProfile = comNetwork.GetProfile();
    if (comNetwork.isOk())
        data.m_strId = comNetwork.GetNetworkId();

    /* Show error message if necessary: */
    if (!comNetwork.isOk())
        UINotificationMessage::cannotAcquireCloudNetworkParameter(comNetwork);
}

void UINetworkManagerWidget::updateActionAvailability()
{
    /* Check which tab we have currently: */
    switch (m_pTabWidget->currentIndex())
    {
        case TabWidgetIndex_HostNetwork:
        {
            AssertMsgReturnVoid(m_pTreeWidgetHostNetwork, ("Host network tree-widget isn't created!\n"));
            UIItemHostNetwork *pItem = static_cast<UIItemHostNetwork*>(m_pTreeWidgetHostNetwork->currentItem());
            m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove)->setEnabled(pItem);
            break;
        }
        case TabWidgetIndex_NATNetwork:
        {
            AssertMsgReturnVoid(m_pTreeWidgetNATNetwork, ("NAT network tree-widget isn't created!\n"));
            UIItemNATNetwork *pItem = static_cast<UIItemNATNetwork*>(m_pTreeWidgetNATNetwork->currentItem());
            m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove)->setEnabled(pItem);
            break;
        }
        case TabWidgetIndex_CloudNetwork:
        {
            AssertMsgReturnVoid(m_pTreeWidgetCloudNetwork, ("Cloud network tree-widget isn't created!\n"));
            UIItemCloudNetwork *pItem = static_cast<UIItemCloudNetwork*>(m_pTreeWidgetCloudNetwork->currentItem());
            m_pActionPool->action(UIActionIndexMN_M_Network_S_Remove)->setEnabled(pItem);
            break;
        }
        default:
            break;
    }
}

void UINetworkManagerWidget::createItemForHostNetwork(const UIDataHostNetwork &data, bool fChooseItem)
{
    /* Prepare new item: */
    UIItemHostNetwork *pItem = new UIItemHostNetwork;
    if (pItem)
    {
        pItem->UIDataHostNetwork::operator=(data);
        pItem->updateFields();

        /* Add item to the tree: */
        m_pTreeWidgetHostNetwork->addTopLevelItem(pItem);

        /* And choose it as current if necessary: */
        if (fChooseItem)
            m_pTreeWidgetHostNetwork->setCurrentItem(pItem);
    }
}

void UINetworkManagerWidget::updateItemForHostNetwork(const UIDataHostNetwork &data, bool fChooseItem, UIItemHostNetwork *pItem)
{
    /* Update passed item: */
    if (pItem)
    {
        /* Configure item: */
        pItem->UIDataHostNetwork::operator=(data);
        pItem->updateFields();
        /* And choose it as current if necessary: */
        if (fChooseItem)
            m_pTreeWidgetHostNetwork->setCurrentItem(pItem);
    }
}

void UINetworkManagerWidget::createItemForNATNetwork(const UIDataNATNetwork &data, bool fChooseItem)
{
    /* Create new item: */
    UIItemNATNetwork *pItem = new UIItemNATNetwork;
    if (pItem)
    {
        /* Configure item: */
        pItem->UIDataNATNetwork::operator=(data);
        pItem->updateFields();
        /* Add item to the tree: */
        m_pTreeWidgetNATNetwork->addTopLevelItem(pItem);
        /* And choose it as current if necessary: */
        if (fChooseItem)
            m_pTreeWidgetNATNetwork->setCurrentItem(pItem);
    }
}

void UINetworkManagerWidget::updateItemForNATNetwork(const UIDataNATNetwork &data, bool fChooseItem, UIItemNATNetwork *pItem)
{
    /* Update passed item: */
    if (pItem)
    {
        /* Configure item: */
        pItem->UIDataNATNetwork::operator=(data);
        pItem->updateFields();
        /* And choose it as current if necessary: */
        if (fChooseItem)
            m_pTreeWidgetNATNetwork->setCurrentItem(pItem);
    }
}

void UINetworkManagerWidget::createItemForCloudNetwork(const UIDataCloudNetwork &data, bool fChooseItem)
{
    /* Create new item: */
    UIItemCloudNetwork *pItem = new UIItemCloudNetwork;
    if (pItem)
    {
        /* Configure item: */
        pItem->UIDataCloudNetwork::operator=(data);
        pItem->updateFields();
        /* Add item to the tree: */
        m_pTreeWidgetCloudNetwork->addTopLevelItem(pItem);
        /* And choose it as current if necessary: */
        if (fChooseItem)
            m_pTreeWidgetCloudNetwork->setCurrentItem(pItem);
    }
}

void UINetworkManagerWidget::updateItemForCloudNetwork(const UIDataCloudNetwork &data, bool fChooseItem, UIItemCloudNetwork *pItem)
{
    /* Update passed item: */
    if (pItem)
    {
        /* Configure item: */
        pItem->UIDataCloudNetwork::operator=(data);
        pItem->updateFields();
        /* And choose it as current if necessary: */
        if (fChooseItem)
            m_pTreeWidgetCloudNetwork->setCurrentItem(pItem);
    }
}

#ifdef VBOX_WS_MAC
QStringList UINetworkManagerWidget::busyNamesHost() const
{
    QStringList names;
    for (int i = 0; i < m_pTreeWidgetHostNetwork->topLevelItemCount(); ++i)
    {
        UIItemHostNetwork *pItem = qobject_cast<UIItemHostNetwork*>(m_pTreeWidgetHostNetwork->childItem(i));
        const QString strItemName(pItem->name());
        if (!strItemName.isEmpty() && !names.contains(strItemName))
            names << strItemName;
    }
    return names;
}
#endif /* VBOX_WS_MAC */

QStringList UINetworkManagerWidget::busyNamesNAT() const
{
    QStringList names;
    for (int i = 0; i < m_pTreeWidgetNATNetwork->topLevelItemCount(); ++i)
    {
        UIItemNATNetwork *pItem = qobject_cast<UIItemNATNetwork*>(m_pTreeWidgetNATNetwork->childItem(i));
        const QString strItemName(pItem->name());
        if (!strItemName.isEmpty() && !names.contains(strItemName))
            names << strItemName;
    }
    return names;
}

QStringList UINetworkManagerWidget::busyNamesCloud() const
{
    QStringList names;
    for (int i = 0; i < m_pTreeWidgetCloudNetwork->topLevelItemCount(); ++i)
    {
        UIItemCloudNetwork *pItem = qobject_cast<UIItemCloudNetwork*>(m_pTreeWidgetCloudNetwork->childItem(i));
        const QString strItemName(pItem->name());
        if (!strItemName.isEmpty() && !names.contains(strItemName))
            names << strItemName;
    }
    return names;
}


/*********************************************************************************************************************************
*   Class UINetworkManagerFactory implementation.                                                                                *
*********************************************************************************************************************************/

UINetworkManagerFactory::UINetworkManagerFactory(UIActionPool *pActionPool /* = 0 */)
    : m_pActionPool(pActionPool)
{
}

void UINetworkManagerFactory::create(QIManagerDialog *&pDialog, QWidget *pCenterWidget)
{
    pDialog = new UINetworkManager(pCenterWidget, m_pActionPool);
}


/*********************************************************************************************************************************
*   Class UINetworkManager implementation.                                                                                       *
*********************************************************************************************************************************/

UINetworkManager::UINetworkManager(QWidget *pCenterWidget, UIActionPool *pActionPool)
    : QIManagerDialog(pCenterWidget)
    , m_pActionPool(pActionPool)
{
}

void UINetworkManager::sltHandleButtonBoxClick(QAbstractButton *pButton)
{
    /* Disable buttons first of all: */
    button(ButtonType_Reset)->setEnabled(false);
    button(ButtonType_Apply)->setEnabled(false);

    /* Compare with known buttons: */
    if (pButton == button(ButtonType_Reset))
        emit sigDataChangeRejected();
    else
    if (pButton == button(ButtonType_Apply))
        emit sigDataChangeAccepted();
}

void UINetworkManager::sltRetranslateUI()
{
    /* Translate window title: */
    setWindowTitle(tr("Network Manager"));

    /* Translate buttons: */
    button(ButtonType_Reset)->setText(tr("Reset"));
    button(ButtonType_Apply)->setText(tr("Apply"));
    button(ButtonType_Close)->setText(tr("Close"));
    button(ButtonType_Help)->setText(tr("Help"));
    button(ButtonType_Reset)->setStatusTip(tr("Reset changes in current network details"));
    button(ButtonType_Apply)->setStatusTip(tr("Apply changes in current network details"));
    button(ButtonType_Close)->setStatusTip(tr("Close dialog without saving"));
    button(ButtonType_Help)->setStatusTip(tr("Show dialog help"));
    button(ButtonType_Reset)->setShortcut(QString("Ctrl+Backspace"));
    button(ButtonType_Apply)->setShortcut(QString("Ctrl+Return"));
    button(ButtonType_Close)->setShortcut(Qt::Key_Escape);
    button(ButtonType_Help)->setShortcut(UIShortcutPool::standardSequence(QKeySequence::HelpContents));
    button(ButtonType_Reset)->setToolTip(tr("Reset Changes (%1)").arg(button(ButtonType_Reset)->shortcut().toString()));
    button(ButtonType_Apply)->setToolTip(tr("Apply Changes (%1)").arg(button(ButtonType_Apply)->shortcut().toString()));
    button(ButtonType_Close)->setToolTip(tr("Close Window (%1)").arg(button(ButtonType_Close)->shortcut().toString()));
    button(ButtonType_Help)->setToolTip(tr("Show Help (%1)").arg(button(ButtonType_Help)->shortcut().toString()));
}

void UINetworkManager::configure()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/host_iface_manager_32px.png", ":/host_iface_manager_16px.png"));
#endif
}

void UINetworkManager::configureCentralWidget()
{
    /* Prepare widget: */
    UINetworkManagerWidget *pWidget = new UINetworkManagerWidget(EmbedTo_Dialog, m_pActionPool, true, this);
    if (pWidget)
    {
        setWidget(pWidget);
        setWidgetMenu(pWidget->menu());
#ifdef VBOX_WS_MAC
        setWidgetToolbar(pWidget->toolbar());
#endif
        connect(this, &UINetworkManager::sigDataChangeRejected,
                pWidget, &UINetworkManagerWidget::sltResetDetailsChanges);
        connect(this, &UINetworkManager::sigDataChangeAccepted,
                pWidget, &UINetworkManagerWidget::sltApplyDetailsChanges);

        /* Add into layout: */
        centralWidget()->layout()->addWidget(pWidget);
    }
}

void UINetworkManager::configureButtonBox()
{
    /* Configure button-box: */
    connect(widget(), &UINetworkManagerWidget::sigDetailsVisibilityChanged,
            button(ButtonType_Apply), &QPushButton::setVisible);
    connect(widget(), &UINetworkManagerWidget::sigDetailsVisibilityChanged,
            button(ButtonType_Reset), &QPushButton::setVisible);
    connect(widget(), &UINetworkManagerWidget::sigDetailsDataChangedHostNetwork,
            button(ButtonType_Apply), &QPushButton::setEnabled);
    connect(widget(), &UINetworkManagerWidget::sigDetailsDataChangedHostNetwork,
            button(ButtonType_Reset), &QPushButton::setEnabled);
    connect(widget(), &UINetworkManagerWidget::sigDetailsDataChangedNATNetwork,
            button(ButtonType_Apply), &QPushButton::setEnabled);
    connect(widget(), &UINetworkManagerWidget::sigDetailsDataChangedNATNetwork,
            button(ButtonType_Reset), &QPushButton::setEnabled);
    connect(widget(), &UINetworkManagerWidget::sigDetailsDataChangedCloudNetwork,
            button(ButtonType_Apply), &QPushButton::setEnabled);
    connect(widget(), &UINetworkManagerWidget::sigDetailsDataChangedCloudNetwork,
            button(ButtonType_Reset), &QPushButton::setEnabled);
    connect(buttonBox(), &QIDialogButtonBox::clicked,
            this, &UINetworkManager::sltHandleButtonBoxClick);
    // WORKAROUND:
    // Since we connected signals later than extra-data loaded
    // for signals above, we should handle that stuff here again:
    button(ButtonType_Apply)->setVisible(gEDataManager->hostNetworkManagerDetailsExpanded());
    button(ButtonType_Reset)->setVisible(gEDataManager->hostNetworkManagerDetailsExpanded());
}

void UINetworkManager::finalize()
{
    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UINetworkManager::sltRetranslateUI);
}

UINetworkManagerWidget *UINetworkManager::widget()
{
    return qobject_cast<UINetworkManagerWidget*>(QIManagerDialog::widget());
}


#include "UINetworkManager.moc"
