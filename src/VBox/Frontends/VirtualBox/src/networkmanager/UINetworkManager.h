/* $Id: UINetworkManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkManager class declaration.
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

#ifndef FEQT_INCLUDED_SRC_networkmanager_UINetworkManager_h
#define FEQT_INCLUDED_SRC_networkmanager_UINetworkManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMainWindow>

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class CCloudNetwork;
#ifdef VBOX_WS_MAC
class CHostOnlyNetwork;
#else
class CHostNetworkInterface;
#endif
class CNATNetwork;
class QAbstractButton;
class QTreeWidgetItem;
class QVBoxLayout;
class QIDialogButtonBox;
class QITabWidget;
class QITreeWidget;
class UIActionPool;
class UIDetailsWidgetCloudNetwork;
class UIDetailsWidgetHostNetwork;
class UIDetailsWidgetNATNetwork;
class UIItemCloudNetwork;
class UIItemHostNetwork;
class UIItemNATNetwork;
class QIToolBar;
struct UIDataCloudNetwork;
struct UIDataHostNetwork;
struct UIDataNATNetwork;


/** QWidget extension providing GUI with the pane to control network related functionality. */
class UINetworkManagerWidget : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about details-widget @a fVisible. */
    void sigDetailsVisibilityChanged(bool fVisible);

    /** Notifies listeners about host network details data @a fDiffers. */
    void sigDetailsDataChangedHostNetwork(bool fDiffers);
    /** Notifies listeners about NAT network details data @a fDiffers. */
    void sigDetailsDataChangedNATNetwork(bool fDiffers);
    /** Notifies listeners about cloud network details data @a fDiffers. */
    void sigDetailsDataChangedCloudNetwork(bool fDiffers);

public:

    /** Constructs Network Manager widget.
      * @param  enmEmbedding  Brings the type of widget embedding.
      * @param  pActionPool   Brings the action-pool reference.
      * @param  fShowToolbar  Brings whether we should create/show toolbar. */
    UINetworkManagerWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                           bool fShowToolbar = true, QWidget *pParent = 0);

    /** Returns the menu. */
    QMenu *menu() const;

#ifdef VBOX_WS_MAC
    /** Returns the toolbar. */
    QIToolBar *toolbar() const { return m_pToolBar; }
#endif

protected:

    /** @name Event-handling stuff.
      * @{ */
        /** Handles resize @a pEvent. */
        virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

        /** Handles show @a pEvent. */
        virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    /** @} */

public slots:

    /** @name Details-widget stuff.
      * @{ */
        /** Handles command to reset details changes. */
        void sltResetDetailsChanges();
        /** Handles command to apply details changes. */
        void sltApplyDetailsChanges();
    /** @} */

private slots:

    /** @name Menu/action stuff.
      * @{ */
        /** Handles command to create host network. */
        void sltCreateHostNetwork();
#ifndef VBOX_WS_MAC
        /** Handles signal about host network @a comInterface created. */
        void sigHandleHostOnlyNetworkInterfaceCreated(const CHostNetworkInterface &comInterface);
#endif
        /** Handles command to remove host network. */
        void sltRemoveHostNetwork();
#ifndef VBOX_WS_MAC
        /** Handles signal about host network interface by the name of @a strInterfaceName was removed. */
        void sigHandleHostOnlyNetworkInterfaceRemoved(const QString &strInterfaceName);
#endif

        /** Handles command to create NAT network. */
        void sltCreateNATNetwork();
        /** Handles command to remove NAT network. */
        void sltRemoveNATNetwork();

        /** Handles command to create cloud network. */
        void sltCreateCloudNetwork();
        /** Handles command to remove cloud network. */
        void sltRemoveCloudNetwork();

        /** Handles command to make details @a fVisible. */
        void sltToggleDetailsVisibility(bool fVisible);
    /** @} */

    /** @name Tree-widget stuff.
      * @{ */
        /** Handles current tab-widget's index change. */
        void sltHandleCurrentTabWidgetIndexChange();

        /** Handles command to adjust tree-widget. */
        void sltAdjustTreeWidgets();

        /** Handles host network tree-widget current item change. */
        void sltHandleCurrentItemChangeHostNetwork();
        /** Handles host network context-menu request for tree-widget @a position. */
        void sltHandleContextMenuRequestHostNetwork(const QPoint &position);
        /** Handles command to apply host network details changes. */
        void sltApplyDetailsChangesHostNetwork();

        /** Handles NAT network tree-widget current item change.
          * @param  fHoldPosition  Holds whether we should try to keep
          *                        port forwarding rule position intact. */
        void sltHandleCurrentItemChangeNATNetworkHoldingPosition(bool fHoldPosition);
        /** Handles NAT network tree-widget current item change. */
        void sltHandleCurrentItemChangeNATNetwork();
        /** Handles NAT network context-menu request for tree-widget @a position. */
        void sltHandleContextMenuRequestNATNetwork(const QPoint &position);
        /** Handles command to apply NAT network details changes. */
        void sltApplyDetailsChangesNATNetwork();

        /** Handles cloud network tree-widget current item change. */
        void sltHandleCurrentItemChangeCloudNetwork();
        /** Handles cloud network context-menu request for tree-widget @a position. */
        void sltHandleContextMenuRequestCloudNetwork(const QPoint &position);
        /** Handles command to apply cloud network details changes. */
        void sltApplyDetailsChangesCloudNetwork();
    /** @} */

    /** @name Event-handling stuff.
      * @{ */
        /** Handles translation event. */
        void sltRetranslateUI();
    /** @} */

private:

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares actions. */
        void prepareActions();
        /** Prepares widgets. */
        void prepareWidgets();
        /** Prepares toolbar. */
        void prepareToolBar();
        /** Prepares tab-widget. */
        void prepareTabWidget();
        /** Prepares host network tab. */
        void prepareTabHostNetwork();
        /** Prepares host network tree-widget. */
        void prepareTreeWidgetHostNetwork();
        /** Prepares host network details-widget. */
        void prepareDetailsWidgetHostNetwork();
        /** Prepares NAT network tab. */
        void prepareTabNATNetwork();
        /** Prepares NAT network tree-widget. */
        void prepareTreeWidgetNATNetwork();
        /** Prepares NAT network details-widget. */
        void prepareDetailsWidgetNATNetwork();
        /** Prepares cloud network tab. */
        void prepareTabCloudNetwork();
        /** Prepares cloud network tree-widget. */
        void prepareTreeWidgetCloudNetwork();
        /** Prepares cloud network details-widget. */
        void prepareDetailsWidgetCloudNetwork();
        /** Load settings: */
        void loadSettings();
    /** @} */

    /** @name Loading stuff.
      * @{ */
        /** Loads host networks. */
        void loadHostNetworks();
#ifdef VBOX_WS_MAC
        /** Loads host @a comNetwork data to passed @a data container. */
        void loadHostNetwork(const CHostOnlyNetwork &comNetwork, UIDataHostNetwork &data);
#else
        /** Loads host @a comInterface data to passed @a data container. */
        void loadHostNetwork(const CHostNetworkInterface &comInterface, UIDataHostNetwork &data);
#endif

        /** Loads NAT networks. */
        void loadNATNetworks();
        /** Loads NAT @a comNetwork data to passed @a data container. */
        void loadNATNetwork(const CNATNetwork &comNetwork, UIDataNATNetwork &data);

        /** Loads cloud networks. */
        void loadCloudNetworks();
        /** Loads cloud @a comNetwork data to passed @a data container. */
        void loadCloudNetwork(const CCloudNetwork &comNetwork, UIDataCloudNetwork &data);
    /** @} */

    /** @name Actions stuff.
      * @{ */
        /** Updates action availability. */
        void updateActionAvailability();
    /** @} */

    /** @name Tree-widget stuff.
      * @{ */
        /** Creates a new host network tree-widget item on the basis of passed @a data, @a fChooseItem if requested. */
        void createItemForHostNetwork(const UIDataHostNetwork &data, bool fChooseItem);
        /** Updates passed host network tree-widget item on the basis of passed @a data, @a fChooseItem if requested. */
        void updateItemForHostNetwork(const UIDataHostNetwork &data, bool fChooseItem, UIItemHostNetwork *pItem);

        /** Creates a new NAT network tree-widget item on the basis of passed @a data, @a fChooseItem if requested. */
        void createItemForNATNetwork(const UIDataNATNetwork &data, bool fChooseItem);
        /** Updates passed NAT network tree-widget item on the basis of passed @a data, @a fChooseItem if requested. */
        void updateItemForNATNetwork(const UIDataNATNetwork &data, bool fChooseItem, UIItemNATNetwork *pItem);

        /** Creates a new cloud network tree-widget item on the basis of passed @a data, @a fChooseItem if requested. */
        void createItemForCloudNetwork(const UIDataCloudNetwork &data, bool fChooseItem);
        /** Updates passed cloud network tree-widget item on the basis of passed @a data, @a fChooseItem if requested. */
        void updateItemForCloudNetwork(const UIDataCloudNetwork &data, bool fChooseItem, UIItemCloudNetwork *pItem);

#ifdef VBOX_WS_MAC
        /** Returns a list of busy host network names. */
        QStringList busyNamesHost() const;
#endif
        /** Returns a list of busy NAT network names. */
        QStringList busyNamesNAT() const;
        /** Returns a list of busy cloud network names. */
        QStringList busyNamesCloud() const;
    /** @} */

    /** @name General variables.
      * @{ */
        /** Holds the widget embedding type. */
        const EmbedTo  m_enmEmbedding;
        /** Holds the action-pool reference. */
        UIActionPool  *m_pActionPool;
        /** Holds whether we should create/show toolbar. */
        const bool     m_fShowToolbar;
    /** @} */

    /** @name Toolbar and menu variables.
      * @{ */
        /** Holds the toolbar instance. */
        QIToolBar *m_pToolBar;
    /** @} */

    /** @name Splitter variables.
      * @{ */
        /** Holds the tab-widget instance. */
        QITabWidget            *m_pTabWidget;

        /** Holds the host network tab. */
        QWidget                    *m_pTabHostNetwork;
        /** Holds the host network layout. */
        QVBoxLayout                *m_pLayoutHostNetwork;
        /** Holds the host network tree-widget instance. */
        QITreeWidget               *m_pTreeWidgetHostNetwork;
        /** Holds the host network details-widget instance. */
        UIDetailsWidgetHostNetwork *m_pDetailsWidgetHostNetwork;

        /** Holds the NAT network tab. */
        QWidget                   *m_pTabNATNetwork;
        /** Holds the NAT network tab layout. */
        QVBoxLayout               *m_pLayoutNATNetwork;
        /** Holds the NAT network tree-widget instance. */
        QITreeWidget              *m_pTreeWidgetNATNetwork;
        /** Holds the NAT network details-widget instance. */
        UIDetailsWidgetNATNetwork *m_pDetailsWidgetNATNetwork;

        /** Holds the cloud network tab. */
        QWidget                     *m_pTabCloudNetwork;
        /** Holds the cloud network tab layout. */
        QVBoxLayout                 *m_pLayoutCloudNetwork;
        /** Holds the cloud network tree-widget instance. */
        QITreeWidget                *m_pTreeWidgetCloudNetwork;
        /** Holds the cloud network details-widget instance. */
        UIDetailsWidgetCloudNetwork *m_pDetailsWidgetCloudNetwork;
    /** @} */
};


/** QIManagerDialogFactory extension used as a factory for Network Manager dialog. */
class UINetworkManagerFactory : public QIManagerDialogFactory
{
public:

    /** Constructs Media Manager factory acquiring additional arguments.
      * @param  pActionPool  Brings the action-pool reference. */
    UINetworkManagerFactory(UIActionPool *pActionPool = 0);

protected:

    /** Creates derived @a pDialog instance.
      * @param  pCenterWidget  Brings the widget reference to center according to. */
    virtual void create(QIManagerDialog *&pDialog, QWidget *pCenterWidget) RT_OVERRIDE;

    /** Holds the action-pool reference. */
    UIActionPool *m_pActionPool;
};


/** QIManagerDialog extension providing GUI with the dialog to control network related functionality. */
class UINetworkManager : public QIManagerDialog
{
    Q_OBJECT;

signals:

    /** Notifies listeners about data change rejected and should be reseted. */
    void sigDataChangeRejected();
    /** Notifies listeners about data change accepted and should be applied. */
    void sigDataChangeAccepted();

private slots:

    /** @name Button-box stuff.
      * @{ */
        /** Handles button-box button click. */
        void sltHandleButtonBoxClick(QAbstractButton *pButton);
    /** @} */

    /** @name Event-handling stuff.
      * @{ */
        /** Handles translation event. */
        void sltRetranslateUI();
    /** @} */

private:

    /** Constructs Network Manager dialog.
      * @param  pCenterWidget  Brings the widget reference to center according to.
      * @param  pActionPool    Brings the action-pool reference. */
    UINetworkManager(QWidget *pCenterWidget, UIActionPool *pActionPool);

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Configures all. */
        virtual void configure() RT_OVERRIDE;
        /** Configures central-widget. */
        virtual void configureCentralWidget() RT_OVERRIDE;
        /** Configures button-box. */
        virtual void configureButtonBox() RT_OVERRIDE;
        /** Perform final preparations. */
        virtual void finalize() RT_OVERRIDE;
    /** @} */

    /** @name Widget stuff.
      * @{ */
        /** Returns the widget. */
        virtual UINetworkManagerWidget *widget() RT_OVERRIDE;
    /** @} */

    /** @name Action related variables.
      * @{ */
        /** Holds the action-pool reference. */
        UIActionPool *m_pActionPool;
    /** @} */

    /** Allow factory access to private/protected members: */
    friend class UINetworkManagerFactory;
};

#endif /* !FEQT_INCLUDED_SRC_networkmanager_UINetworkManager_h */
