/* $Id: UICloudConsoleManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudConsoleManager class declaration.
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

#ifndef FEQT_INCLUDED_SRC_cloud_consolemanager_UICloudConsoleManager_h
#define FEQT_INCLUDED_SRC_cloud_consolemanager_UICloudConsoleManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class QAbstractButton;
class QTreeWidgetItem;
class QITreeWidget;
class QITreeWidgetItem;
class UIActionPool;
class UICloudConsoleDetailsWidget;
class UIItemCloudConsoleApplication;
class UIItemCloudConsoleProfile;
class QIToolBar;
struct UIDataCloudConsoleApplication;
struct UIDataCloudConsoleProfile;


/** QWidget extension providing GUI with the pane to control cloud console related functionality. */
class UICloudConsoleManagerWidget : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about cloud console details-widget @a fVisible. */
    void sigCloudConsoleDetailsVisibilityChanged(bool fVisible);
    /** Notifies listeners about cloud console details data @a fDiffers. */
    void sigCloudConsoleDetailsDataChanged(bool fDiffers);

public:

    /** Constructs Cloud Console Manager widget.
      * @param  enmEmbedding  Brings the type of widget embedding.
      * @param  pActionPool   Brings the action-pool reference.
      * @param  fShowToolbar  Brings whether we should create/show toolbar. */
    UICloudConsoleManagerWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                                bool fShowToolbar = true, QWidget *pParent = 0);

    /** Returns the menu. */
    QMenu *menu() const;

#ifdef VBOX_WS_MAC
    /** Returns the toolbar. */
    QIToolBar *toolbar() const { return m_pToolBar; }
#endif

public slots:

    /** @name Details-widget stuff.
      * @{ */
        /** Handles command to reset cloud console details changes. */
        void sltResetCloudConsoleDetailsChanges();
        /** Handles command to apply cloud console details changes. */
        void sltApplyCloudConsoleDetailsChanges();
    /** @} */

private slots:

    /** @name Menu/action stuff.
      * @{ */
        /** Handles command to add cloud console application. */
        void sltAddCloudConsoleApplication();
        /** Handles command to remove cloud console application. */
        void sltRemoveCloudConsoleApplication();
        /** Handles command to add cloud console profile. */
        void sltAddCloudConsoleProfile();
        /** Handles command to remove cloud console profile. */
        void sltRemoveCloudConsoleProfile();
        /** Handles command to make cloud console details @a fVisible. */
        void sltToggleCloudConsoleDetailsVisibility(bool fVisible);
    /** @} */

    /** @name Tree-widget stuff.
      * @{ */
        /** Handles request to load cloud console stuff. */
        void sltLoadCloudConsoleStuff() { loadCloudConsoleStuff(); }
        /** Adjusts tree-widget according content. */
        void sltPerformTableAdjustment();
        /** Handles tree-widget current item change. */
        void sltHandleCurrentItemChange();
        /** Handles context-menu request for tree-widget @a position. */
        void sltHandleContextMenuRequest(const QPoint &position);
        /** Handles tree-widget @a pItem change. */
        void sltHandleItemChange(QTreeWidgetItem *pItem);
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
        /** Prepares tree-widget. */
        void prepareTreeWidget();
        /** Prepares details-widget. */
        void prepareDetailsWidget();
        /** Prepares connections. */
        void prepareConnections();
        /** Load settings: */
        void loadSettings();
    /** @} */

    /** @name Loading stuff.
      * @{ */
        /** Loads cloud console stuff. */
        void loadCloudConsoleStuff();
        /** Loads cloud console @a strSuperset data to passed @a applicationData container. */
        void loadCloudConsoleApplication(const QString &strSuperset,
                                         UIDataCloudConsoleApplication &applicationData);
        /** Loads cloud console @a strSuperset data to passed @a profileData container, using @a applicationData as hint. */
        void loadCloudConsoleProfile(const QString &strSuperset,
                                     const UIDataCloudConsoleApplication &applicationData,
                                     UIDataCloudConsoleProfile &profileData);
    /** @} */

    /** @name Tree-widget stuff.
      * @{ */
        /** Searches an application item with specified @a strApplicationId. */
        UIItemCloudConsoleApplication *searchApplicationItem(const QString &strApplicationId) const;
        /** Searches a profile child of application item with specified @a strApplicationId and @a strProfileId. */
        UIItemCloudConsoleProfile *searchProfileItem(const QString &strApplicationId,
                                                     const QString &strProfileId) const;
        /** Searches an item with specified @a strDefinition. */
        QITreeWidgetItem *searchItemByDefinition(const QString &strDefinition) const;

        /** Creates a new tree-widget item
          * on the basis of passed @a applicationData, @a fChooseItem if requested. */
        void createItemForCloudConsoleApplication(const UIDataCloudConsoleApplication &applicationData,
                                                  bool fChooseItem);

        /** Creates a new tree-widget item as a child of certain @a pParent,
          * on the basis of passed @a profileData, @a fChooseItem if requested. */
        void createItemForCloudConsoleProfile(QTreeWidgetItem *pParent,
                                              const UIDataCloudConsoleProfile &profileData,
                                              bool fChooseItem);

        /* Gathers a list of Cloud Console Manager restrictions starting from @a pParentItem. */
        QStringList gatherCloudConsoleManagerRestrictions(QTreeWidgetItem *pParentItem);
    /** @} */

    /** @name General variables.
      * @{ */
        /** Holds the widget embedding type. */
        const EmbedTo m_enmEmbedding;
        /** Holds the action-pool reference. */
        UIActionPool *m_pActionPool;
        /** Holds whether we should create/show toolbar. */
        const bool    m_fShowToolbar;
        /** Holds current item definition. */
        QString       m_strDefinition;
    /** @} */

    /** @name Toolbar and menu variables.
      * @{ */
        /** Holds the toolbar instance. */
        QIToolBar *m_pToolBar;
    /** @} */

    /** @name Splitter variables.
      * @{ */
        /** Holds the tree-widget instance. */
        QITreeWidget                *m_pTreeWidget;
        /** Holds the details-widget instance. */
        UICloudConsoleDetailsWidget *m_pDetailsWidget;
    /** @} */
};


/** QIManagerDialogFactory extension used as a factory for Cloud Console Manager dialog. */
class UICloudConsoleManagerFactory : public QIManagerDialogFactory
{
public:

    /** Constructs Cloud Console Manager actory acquiring additional arguments.
      * @param  pActionPool  Brings the action-pool reference. */
    UICloudConsoleManagerFactory(UIActionPool *pActionPool = 0);

protected:

    /** Creates derived @a pDialog instance.
      * @param  pCenterWidget  Brings the widget reference to center according to. */
    virtual void create(QIManagerDialog *&pDialog, QWidget *pCenterWidget) RT_OVERRIDE;

    /** Holds the action-pool reference. */
    UIActionPool *m_pActionPool;
};


/** QIManagerDialog extension providing GUI with the dialog to control cloud console related functionality. */
class UICloudConsoleManager : public QIManagerDialog
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

    /** Constructs Cloud Console Manager dialog.
      * @param  pCenterWidget  Brings the widget reference to center according to.
      * @param  pActionPool    Brings the action-pool reference. */
    UICloudConsoleManager(QWidget *pCenterWidget, UIActionPool *pActionPool);

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
        virtual UICloudConsoleManagerWidget *widget() RT_OVERRIDE;
    /** @} */

    /** @name Action related variables.
      * @{ */
        /** Holds the action-pool reference. */
        UIActionPool *m_pActionPool;
    /** @} */

    /** Allow factory access to private/protected members: */
    friend class UICloudConsoleManagerFactory;
};

#endif /* !FEQT_INCLUDED_SRC_cloud_consolemanager_UICloudConsoleManager_h */
