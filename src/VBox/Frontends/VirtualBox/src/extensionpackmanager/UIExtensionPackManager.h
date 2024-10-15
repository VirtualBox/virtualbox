/* $Id: UIExtensionPackManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIExtensionPackManager class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensionpackmanager_UIExtensionPackManager_h
#define FEQT_INCLUDED_SRC_extensionpackmanager_UIExtensionPackManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class QIToolBar;
class QITreeWidget;
class UIActionPool;
struct UIDataExtensionPack;
class CExtPack;


/** QWidget extension providing GUI with the pane to control extension pack related functionality. */
class UIExtensionPackManagerWidget : public QWidget
{
    Q_OBJECT;

public:

    /** Constructs Extension Pack Manager widget.
      * @param  enmEmbedding  Brings the type of widget embedding.
      * @param  pActionPool   Brings the action-pool reference.
      * @param  fShowToolbar  Brings whether we should create/show toolbar. */
    UIExtensionPackManagerWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                                 bool fShowToolbar = true, QWidget *pParent = 0);

    /** Returns the menu. */
    QMenu *menu() const;

#ifdef VBOX_WS_MAC
    /** Returns the toolbar. */
    QIToolBar *toolbar() const { return m_pToolBar; }
#endif

private slots:

    /** @name Menu/action stuff.
      * @{ */
        /** Handles command to install extension pack. */
        void sltInstallExtensionPack();
        /** Handles command to uninstall extension pack. */
        void sltUninstallExtensionPack();
    /** @} */

    /** @name Tree-widget stuff.
      * @{ */
        /** Handles command to adjust tree-widget. */
        void sltAdjustTreeWidget();

        /** Handles tree-widget current item change. */
        void sltHandleCurrentItemChange();
        /** Handles context-menu request for tree-widget @a position. */
        void sltHandleContextMenuRequest(const QPoint &position);

        /** Handles signal about extension pack @a strName installed. */
        void sltHandleExtensionPackInstalled(const QString &strName);
        /** Handles signal about extension pack @a strName uninstalled. */
        void sltHandleExtensionPackUninstalled(const QString &strName);
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
        /** Prepare connections: */
        void prepareConnections();
    /** @} */

    /** @name Loading stuff.
      * @{ */
        /** Loads extension pack stuff. */
        void loadExtensionPacks();
        /** Loads extention @a comPackage data to passed @a extensionPackData container. */
        void loadExtensionPack(const CExtPack &comPackage, UIDataExtensionPack &extensionPackData);
    /** @} */

    /** @name Tree-widget stuff.
      * @{ */
        /** Creates a new tree-widget item
          * on the basis of passed @a extensionPackData, @a fChooseItem if requested. */
        void createItemForExtensionPack(const UIDataExtensionPack &extensionPackData, bool fChooseItem);
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

    /** @name Widget variables.
      * @{ */
        /** Holds the tree-widget instance. */
        QITreeWidget *m_pTreeWidget;
    /** @} */
};


/** QIManagerDialogFactory extension used as a factory for Extension Pack Manager dialog. */
class UIExtensionPackManagerFactory : public QIManagerDialogFactory
{
public:

    /** Constructs Extension Pack Manager factory acquiring additional arguments.
      * @param  pActionPool  Brings the action-pool reference. */
    UIExtensionPackManagerFactory(UIActionPool *pActionPool = 0);

protected:

    /** Creates derived @a pDialog instance.
      * @param  pCenterWidget  Brings the widget reference to center according to. */
    virtual void create(QIManagerDialog *&pDialog, QWidget *pCenterWidget) RT_OVERRIDE;

    /** Holds the action-pool reference. */
    UIActionPool *m_pActionPool;
};


/** QIManagerDialog extension providing GUI with the dialog to control extension pack related functionality. */
class UIExtensionPackManager : public QIManagerDialog
{
    Q_OBJECT;

private slots:

    /** @name Event-handling stuff.
      * @{ */
        /** Handles translation event. */
        void sltRetranslateUI();
    /** @} */

private:

    /** Constructs Extension Pack Manager dialog.
      * @param  pCenterWidget  Brings the widget reference to center according to.
      * @param  pActionPool    Brings the action-pool reference. */
    UIExtensionPackManager(QWidget *pCenterWidget, UIActionPool *pActionPool);

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Configures all. */
        virtual void configure() RT_OVERRIDE;
        /** Configures central-widget. */
        virtual void configureCentralWidget() RT_OVERRIDE;
        /** Perform final preparations. */
        virtual void finalize() RT_OVERRIDE;
    /** @} */

    /** @name Widget stuff.
      * @{ */
        /** Returns the widget. */
        virtual UIExtensionPackManagerWidget *widget() RT_OVERRIDE;
    /** @} */

    /** @name Action related variables.
      * @{ */
        /** Holds the action-pool reference. */
        UIActionPool *m_pActionPool;
    /** @} */

    /** Allow factory access to private/protected members: */
    friend class UIExtensionPackManagerFactory;
};

#endif /* !FEQT_INCLUDED_SRC_extensionpackmanager_UIExtensionPackManager_h */
