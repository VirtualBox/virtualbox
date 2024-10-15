/* $Id: UIUSBFiltersEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUSBFiltersEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIUSBFiltersEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIUSBFiltersEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"
#include "UIExtraDataDefs.h"

/* Forward declartions: */
class QHBoxLayout;
class QTreeWidgetItem;
class QILabelSeparator;
class QIToolBar;
class QITreeWidget;
class UIUSBMenu;

/** USB Filter data. */
struct UIDataUSBFilter
{
    /** Constructs data. */
    UIDataUSBFilter()
        : m_fActive(true)
        , m_enmRemoteMode(UIRemoteMode_Any)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataUSBFilter &other) const
    {
        return true
               && m_fActive == other.m_fActive
               && m_strName == other.m_strName
               && m_strVendorId == other.m_strVendorId
               && m_strProductId == other.m_strProductId
               && m_strRevision == other.m_strRevision
               && m_strManufacturer == other.m_strManufacturer
               && m_strProduct == other.m_strProduct
               && m_strSerialNumber == other.m_strSerialNumber
               && m_strPort == other.m_strPort
               && m_enmRemoteMode == other.m_enmRemoteMode
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataUSBFilter &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataUSBFilter &other) const { return !equal(other); }

    /** Holds whether USB filter is active. */
    bool          m_fActive;
    /** Holds the USB filter name. */
    QString       m_strName;
    /** Holds the USB filter vendor ID. */
    QString       m_strVendorId;
    /** Holds the USB filter product ID. */
    QString       m_strProductId;
    /** Holds the USB filter revision. */
    QString       m_strRevision;
    /** Holds the USB filter manufacturer. */
    QString       m_strManufacturer;
    /** Holds the USB filter product. */
    QString       m_strProduct;
    /** Holds the USB filter serial number. */
    QString       m_strSerialNumber;
    /** Holds the USB filter port. */
    QString       m_strPort;
    /** Holds the USB filter remote. */
    UIRemoteMode  m_enmRemoteMode;
};

/** UIEditor sub-class used as a USB filters editor. */
class SHARED_LIBRARY_STUFF UIUSBFiltersEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about value change. */
    void sigValueChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIUSBFiltersEditor(QWidget *pParent = 0);

    /** Defines editor @a strValue. */
    void setValue(const QList<UIDataUSBFilter> &guiValue);
    /** Returns editor value. */
    QList<UIDataUSBFilter> value() const;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;
    /** Handles @a pCurrentItem change. */
    void sltHandleCurrentItemChange(QTreeWidgetItem *pCurrentItem);
    /** Handles @a pItem double-click. */
    void sltHandleDoubleClick(QTreeWidgetItem *pItem);
    /** Handles context menu request for @a position. */
    void sltHandleContextMenuRequest(const QPoint &position);

    /** Handles command to create USB filter. */
    void sltCreateFilter();
    /** Handles command to add USB filter. */
    void sltAddFilter();
    /** Handles command to confirm add of existing USB filter defined by @a pAction. */
    void sltAddFilterConfirmed(QAction *pAction);
    /** Handles command to edit USB filter. */
    void sltEditFilter();
    /** Handles command to remove USB filter. */
    void sltRemoveFilter();
    /** Handles command to move chosen USB filter up. */
    void sltMoveFilterUp();
    /** Handles command to move chosen USB filter down. */
    void sltMoveFilterDown();

    /** Handles USB filter tree activity state change for @a pChangedItem. */
    void sltHandleActivityStateChange(QTreeWidgetItem *pChangedItem);

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepare tree-widget. */
    void prepareTreeWidget();
    /** Prepare tool-bar. */
    void prepareToolbar();
    /** Prepares connections. */
    void prepareConnections();

    /** Creates USB filter item based on passed @a data. */
    void addUSBFilterItem(const UIDataUSBFilter &data, bool fChoose);
    /** Reloads tree. */
    void reloadTree();

    /** Holds the value to be set. */
    QList<UIDataUSBFilter>  m_guiValue;

    /** @name Internal
     * @{ */
        /** Holds the "New Filter %1" translation tag. */
        QString  m_strTrUSBFilterName;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the widget separator instance. */
        QILabelSeparator *m_pLabelSeparator;
        /** Holds the tree layout instance. */
        QHBoxLayout      *m_pLayoutTree;
        /** Holds the tree-widget instance. */
        QITreeWidget     *m_pTreeWidget;
        /** Holds the toolbar instance. */
        QIToolBar        *m_pToolbar;
        /** Holds the 'new USB filter' action instance. */
        QAction          *m_pActionNew;
        /** Holds the 'add USB filter' action instance. */
        QAction          *m_pActionAdd;
        /** Holds the 'edit USB filter' action instance. */
        QAction          *m_pActionEdit;
        /** Holds the 'remove USB filter' action instance. */
        QAction          *m_pActionRemove;
        /** Holds the Move Up action instance. */
        QAction          *m_pActionMoveUp;
        /** Holds the Move Down action instance. */
        QAction          *m_pActionMoveDown;
        /** Holds the USB devices menu instance. */
        UIUSBMenu        *m_pMenuUSBDevices;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIUSBFiltersEditor_h */
