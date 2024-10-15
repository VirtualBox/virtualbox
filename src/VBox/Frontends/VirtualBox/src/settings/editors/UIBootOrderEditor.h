/* $Id: UIBootOrderEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIBootListWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIBootOrderEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIBootOrderEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* COM includes: */
#include "KDeviceType.h"

/* Forward declarations: */
class QGridLayout;
class QLabel;
class QIToolBar;
class UIBootListWidget;
class CMachine;


/** Boot item data structure. */
struct UIBootItemData
{
    /** Constructs item data. */
    UIBootItemData()
        : m_enmType(KDeviceType_Null)
        , m_fEnabled(false)
    {}

    /** Returns whether @a another passed data is equal to this one. */
    bool operator==(const UIBootItemData &another) const
    {
        return true
               && (m_enmType == another.m_enmType)
               && (m_fEnabled == another.m_fEnabled)
               ;
    }

    /** Holds the device type. */
    KDeviceType  m_enmType;
    /** Holds whether the device enabled. */
    bool         m_fEnabled;
};
typedef QList<UIBootItemData> UIBootItemDataList;
Q_DECLARE_METATYPE(UIBootItemDataList);


/** Boot data tools namespace. */
namespace UIBootDataTools
{
    /** Loads item list for passed @a comMachine. */
    SHARED_LIBRARY_STUFF UIBootItemDataList loadBootItems(const CMachine &comMachine);
    /** Saves @a bootItems list to passed @a comMachine. */
    SHARED_LIBRARY_STUFF void saveBootItems(const UIBootItemDataList &bootItems, CMachine &comMachine);

    /** Converts passed @a bootItems list into human readable string. */
    SHARED_LIBRARY_STUFF QString bootItemsToReadableString(const UIBootItemDataList &bootItems);

    /** Performs serialization for passed @a bootItems list. */
    SHARED_LIBRARY_STUFF QString bootItemsToSerializedString(const UIBootItemDataList &bootItems);
    /** Performs deserialization for passed @a strBootItems string. */
    SHARED_LIBRARY_STUFF UIBootItemDataList bootItemsFromSerializedString(const QString &strBootItems);
}
using namespace UIBootDataTools;


/** UIEditor sub-class used as boot order editor. */
class SHARED_LIBRARY_STUFF UIBootOrderEditor : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIBootOrderEditor(QWidget *pParent = 0);

    /** Defines editor @a guiValue. */
    void setValue(const UIBootItemDataList &guiValue);
    /** Returns editor value. */
    UIBootItemDataList value() const;

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

protected:

    /** Preprocesses Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** Handles current item change. */
    void sltHandleCurrentBootItemChange();

private:

    /** Prepares all. */
    void prepare();

    /** Updates action availability: */
    void updateActionAvailability();

    /** Holds the main layout instance. */
    QGridLayout      *m_pLayout;
    /** Holds the label instance. */
    QLabel           *m_pLabel;
    /** Holds the table instance. */
    UIBootListWidget *m_pTable;
    /** Holds the toolbar instance. */
    QIToolBar        *m_pToolbar;
    /** Holds the move up action. */
    QAction          *m_pMoveUp;
    /** Holds the move down action. */
    QAction          *m_pMoveDown;
};


#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIBootOrderEditor_h */
