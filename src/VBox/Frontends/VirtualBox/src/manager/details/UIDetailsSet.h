/* $Id: UIDetailsSet.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsSet class declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_details_UIDetailsSet_h
#define FEQT_INCLUDED_SRC_manager_details_UIDetailsSet_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIDetailsItem.h"
#include "UIExtraDataDefs.h"
#include "UISettingsDefs.h"

/* COM includes: */
#include "CCloudMachine.h"
#include "CMachine.h"

/* Forward declarations: */
class UIVirtualMachineItem;

/* Using declarations: */
using namespace UISettingsDefs;

/** UIDetailsItem extension implementing set item. */
class UIDetailsSet : public UIDetailsItem
{
    Q_OBJECT;

public:

    /** RTTI item type. */
    enum { Type = UIDetailsItemType_Set };

    /** Constructs set item, passing pParent to the base-class. */
    UIDetailsSet(UIDetailsItem *pParent);
    /** Destructs set item. */
    virtual ~UIDetailsSet() RT_OVERRIDE;

    /** @name Item stuff.
      * @{ */
        /** Cleanups set, wiping out machine-item and [cloud]machine information for good. */
        void clearSet();
        /** Builds set based on passed @a pMachineItem.
          * @param  fFullSet  Brings whether full set should be built.
          * @param  settings  Brings details related settings. */
        void buildSet(UIVirtualMachineItem *pMachineItem, bool fFullSet, const QMap<DetailsElementType, bool> &settings);

        /** Returns cached machine. */
        const CMachine &machine() const { return m_comMachine; }
        /** Returns cached cloud machine. */
        const CCloudMachine &cloudMachine() const { return m_comCloudMachine; }

        /** Returns whether set is of local type. */
        bool isLocal() const { return m_fIsLocal; }
        /** Returns whether set has cached details. */
        bool hasDetails() const { return m_fHasDetails; }
        /** Returns configuration access level. */
        ConfigurationAccessLevel configurationAccessLevel() const { return m_configurationAccessLevel; }
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Updates layout. */
        virtual void updateLayout() RT_OVERRIDE;
    /** @} */

protected slots:

    /** @name Item stuff.
      * @{ */
        /** Handles request about starting step build.
          * @param  uStepId      Brings the step ID.
          * @param  iStepNumber  Brings the step number. */
        virtual void sltBuildStep(const QUuid &uStepId, int iStepNumber) RT_OVERRIDE;
    /** @} */

protected:

    /** @name Event-handling stuff.
      * @{ */
        /** Performs painting using passed @a pPainter, @a pOptions and optionally specified @a pWidget. */
        virtual void paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget *pWidget = 0) RT_OVERRIDE;
    /** @} */

    /** @name Item stuff.
      * @{ */
        /** Returns RTTI item type. */
        virtual int type() const RT_OVERRIDE { return Type; }

        /** Returns the description of the item. */
        virtual QString description() const RT_OVERRIDE;
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Adds child @a pItem. */
        virtual void addItem(UIDetailsItem *pItem) RT_OVERRIDE;
        /** Removes child @a pItem. */
        virtual void removeItem(UIDetailsItem *pItem) RT_OVERRIDE;

        /** Returns children items of certain @a enmType. */
        virtual QList<UIDetailsItem*> items(UIDetailsItemType type = UIDetailsItemType_Element) const RT_OVERRIDE;
        /** Returns whether there are children items of certain @a enmType. */
        virtual bool hasItems(UIDetailsItemType type = UIDetailsItemType_Element) const RT_OVERRIDE;
        /** Clears children items of certain @a enmType. */
        virtual void clearItems(UIDetailsItemType type = UIDetailsItemType_Element) RT_OVERRIDE;

        /** Returns details element of certain @a enmElementType. */
        UIDetailsElement *element(DetailsElementType enmElementType) const;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Returns minimum width-hint. */
        virtual int minimumWidthHint() const RT_OVERRIDE;
        /** Returns minimum height-hint. */
        virtual int minimumHeightHint() const RT_OVERRIDE;
    /** @} */

private slots:

    /** @name Event-handling stuff.
      * @{ */
        /** Handles machine-state change for item with @a uId. */
        void sltMachineStateChange(const QUuid &uId);
        /** Handles machine-attribute change for item with @a uId. */
        void sltMachineAttributesChange(const QUuid &uId);
    /** @} */

    /** @name Item stuff.
      * @{ */
        /** Handles enumerated signal for medium with @a uId. */
        void sltMediumEnumerated(const QUuid &uId);
    /** @} */

private:

    /** Data field types. */
    enum SetItemData
    {
        /* Layout hints: */
        SetData_Margin,
        SetData_Spacing
    };

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Prepares set. */
        void prepareSet();
        /** Prepares connections. */
        void prepareConnections();
    /** @} */

    /** @name Item stuff.
      * @{ */
        /** Returns abstractly stored data value for certain @a iKey. */
        QVariant data(int iKey) const;

        /** Rebuilds set based on cached machine item. */
        void rebuildSet();

        /** Creates element of specified @a enmElementType in @a fOpen state. */
        UIDetailsElement *createElement(DetailsElementType enmElementType, bool fOpen);
    /** @} */

    /** @name Painting stuff.
      * @{ */
        /** Paints background using specified @a pPainter and certain @a pOptions. */
        void paintBackground(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions) const;
    /** @} */

    /** @name Item stuff.
      * @{ */
        /** Holds the machine-item this set is built for. */
        UIVirtualMachineItem           *m_pMachineItem;
        /** Holds whether whether full set should be built. */
        bool                            m_fFullSet;
        /** Holds the details related settings. */
        QMap<DetailsElementType, bool>  m_settings;

        /** Holds the machine reference. */
        CMachine       m_comMachine;
        /** Holds the cloud machine reference. */
        CCloudMachine  m_comCloudMachine;

        /** Holds whether set is of local type. */
        bool                      m_fIsLocal;
        /** Holds whether set has details. */
        bool                      m_fHasDetails;
        /** Holds configuration access level. */
        ConfigurationAccessLevel  m_configurationAccessLevel;

        /** Holds the build step instance. */
        UIPrepareStep             *m_pBuildStep;
        /** Holds the list of types to build steps for. */
        QList<DetailsElementType>  m_types;
        /** Holds the generated set ID. */
        QUuid                      m_uSetId;

        /** Holds the start background darkness. */
        int  m_iBackgroundDarknessStart;
        /** Holds the final background darkness. */
        int  m_iBackgroundDarknessFinal;
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Holds the map of generated detail elements. */
        QMap<int, UIDetailsItem*>  m_elements;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Holds the list of elements in the Preview group. */
        QList<DetailsElementType>  m_listPreviewGroup;
        /** Holds the list of elements in the Outside group. */
        QList<DetailsElementType>  m_listOutsideGroup;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_details_UIDetailsSet_h */
