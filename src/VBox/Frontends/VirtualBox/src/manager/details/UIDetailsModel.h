/* $Id: UIDetailsModel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsModel class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_details_UIDetailsModel_h
#define FEQT_INCLUDED_SRC_manager_details_UIDetailsModel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* COM includes: */

/* Forward declaration: */
class QGraphicsItem;
class QGraphicsScene;
class QGraphicsSceneContextMenuEvent;
class QGraphicsView;
class UIVirtualMachineItem;
class UIDetails;
class UIDetailsContextMenu;
class UIDetailsElement;
class UIDetailsElementAnimationCallback;
class UIDetailsGroup;
class UIDetailsItem;
class UIDetailsView;


/** QObject sub-class used as graphics details model. */
class UIDetailsModel : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about model root item @a iMinimumWidthHint changed. */
    void sigRootItemMinimumWidthHintChanged(int iMinimumWidthHint);

    /** Notifies listeners about element link clicked.
      * @param  strCategory  Brings details element category.
      * @param  strControl   Brings settings control to select.
      * @param  uId          Brings ID of the machine details referring. */
    void sigLinkClicked(const QString &strCategory, const QString &strControl, const QUuid &uId);

public:

    /** Constructs a details model passing @a pParent to the base-class.
      * @param  pParent  Brings the details container to embed into. */
    UIDetailsModel(UIDetails *pParent);
    /** Destructs a details model. */
    virtual ~UIDetailsModel() RT_OVERRIDE;

    /** Inits model. */
    void init();

    /** Returns graphics scene this model belongs to. */
    QGraphicsScene *scene() const;
    /** Returns the reference of the first view of the scene(). */
    UIDetailsView *view() const;
    /** Returns paint device this model belongs to. */
    QGraphicsView *paintDevice() const;

    /** Returns graphics item as certain @a position. */
    QGraphicsItem *itemAt(const QPointF &position) const;

    /** Returns the details pane reference. */
    UIDetails *details() const { return m_pDetails; }

    /** Returns the root item instance. */
    UIDetailsItem *root() const;

    /** Updates layout by positioning items manually. */
    void updateLayout();

    /** Defines virtual machine @a items for this model to reflect. */
    void setItems(const QList<UIVirtualMachineItem*> &items);

    /** Returns the details categories. */
    const QMap<DetailsElementType, bool> &categories() const { return m_categories; }
    /** Defines the details @a categories. */
    void setCategories(const QMap<DetailsElementType, bool> &categories);

    /** Returns options for General element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral optionsGeneral() const { return m_fOptionsGeneral; }
    /** Defines @a fOptions for General element. */
    void setOptionsGeneral(UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral fOptions);

    /** Returns options for System element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeSystem optionsSystem() const { return m_fOptionsSystem; }
    /** Defines @a fOptions for System element. */
    void setOptionsSystem(UIExtraDataMetaDefs::DetailsElementOptionTypeSystem fOptions);

    /** Returns options for Display element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay optionsDisplay() const { return m_fOptionsDisplay; }
    /** Defines @a fOptions for Display element. */
    void setOptionsDisplay(UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay fOptions);

    /** Returns options for Storage element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeStorage optionsStorage() const { return m_fOptionsStorage; }
    /** Defines @a fOptions for Storage element. */
    void setOptionsStorage(UIExtraDataMetaDefs::DetailsElementOptionTypeStorage fOptions);

    /** Returns options for Audio element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeAudio optionsAudio() const { return m_fOptionsAudio; }
    /** Defines @a fOptions for Audio element. */
    void setOptionsAudio(UIExtraDataMetaDefs::DetailsElementOptionTypeAudio fOptions);

    /** Returns options for Network element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork optionsNetwork() const { return m_fOptionsNetwork; }
    /** Defines @a fOptions for Network element. */
    void setOptionsNetwork(UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork fOptions);

    /** Returns options for Serial element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeSerial optionsSerial() const { return m_fOptionsSerial; }
    /** Defines @a fOptions for Serial element. */
    void setOptionsSerial(UIExtraDataMetaDefs::DetailsElementOptionTypeSerial fOptions);

    /** Returns options for Usb element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeUsb optionsUsb() const { return m_fOptionsUsb; }
    /** Defines @a fOptions for Usb element. */
    void setOptionsUsb(UIExtraDataMetaDefs::DetailsElementOptionTypeUsb fOptions);

    /** Returns options for Shared Folders element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders optionsSharedFolders() const { return m_fOptionsSharedFolders; }
    /** Defines @a fOptions for Shared Folders element. */
    void setOptionsSharedFolders(UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders fOptions);

    /** Returns options for User Interface element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface optionsUserInterface() const { return m_fOptionsUserInterface; }
    /** Defines @a fOptions for User Interface element. */
    void setOptionsUserInterface(UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface fOptions);

    /** Returns options for Description element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeDescription optionsDescription() const { return m_fOptionsDescription; }
    /** Defines @a fOptions for Description element. */
    void setOptionsDescription(UIExtraDataMetaDefs::DetailsElementOptionTypeDescription fOptions);

public slots:

    /** Handle details view resize. */
    void sltHandleViewResize();

    /** Handles chooser pane signal about group toggle started. */
    void sltHandleToggleStarted();
    /** Handles chooser pane signal about group toggle finished. */
    void sltHandleToggleFinished();

    /** Handle extra-data categories change. */
    void sltHandleExtraDataCategoriesChange();
    /** Handle extra-data options change for category of certain @a enmType. */
    void sltHandleExtraDataOptionsChange(DetailsElementType enmType);

    /** Handles request to start toggle details element of certain @a enmType, making element @a fToggled. */
    void sltToggleElements(DetailsElementType type, bool fToggled);

protected:

    /** Preprocesses any Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles command to detach COM. */
    void sltDetachCOM();

    /** Handles sigal about details element of certain @a enmType toggling finished, making element @a fToggled. */
    void sltToggleAnimationFinished(DetailsElementType type, bool fToggled);

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();

        /** Loads details categories. */
        void loadDetailsCategories();
        /** Loads details options for certain category @a enmType.
          * @note enmType equal to DetailsElementType_Invalid means load everything. */
        void loadDetailsOptions(DetailsElementType enmType = DetailsElementType_Invalid);

        /** Cleanups all. */
        void cleanup();
    /** @} */

    /** Performs handling for allowed context menu @a pEvent. */
    bool processContextMenuEvent(QGraphicsSceneContextMenuEvent *pEvent);

    /** Holds the details reference. */
    UIDetails *m_pDetails;

    /** Holds the graphics scene reference. */
    QGraphicsScene                    *m_pScene;
    /** Holds the root element instance. */
    UIDetailsGroup                    *m_pRoot;
    /** Holds the element animation callback instance. */
    UIDetailsElementAnimationCallback *m_pAnimationCallback;

    /** Holds the details categories. */
    QMap<DetailsElementType, bool>  m_categories;

    /** Holds the options for General element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral        m_fOptionsGeneral;
    /** Holds the options for System element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeSystem         m_fOptionsSystem;
    /** Holds the options for Display element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay        m_fOptionsDisplay;
    /** Holds the options for Storage element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeStorage        m_fOptionsStorage;
    /** Holds the options for Audio element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeAudio          m_fOptionsAudio;
    /** Holds the options for Network element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork        m_fOptionsNetwork;
    /** Holds the options for Serial element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeSerial         m_fOptionsSerial;
    /** Holds the options for Usb element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeUsb            m_fOptionsUsb;
    /** Holds the options for Shared Folders element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders  m_fOptionsSharedFolders;
    /** Holds the options for User Interface element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface  m_fOptionsUserInterface;
    /** Holds the options for Description element. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeDescription    m_fOptionsDescription;

    /** Holds the context-menu instance. */
    UIDetailsContextMenu *m_pContextMenu;
};


/** QObject sub-class used as details element animation callback. */
class UIDetailsElementAnimationCallback : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about all animations finished.
      * @param  enmType   Brings the type of element item which was animated.
      * @param  fToggled  Brings whether elements being toggled to be closed or opened. */
    void sigAllAnimationFinished(DetailsElementType enmType, bool fToggled);

public:

    /** Constructors details element animation callback passing @a pParent to the base-class.
      * @param  enmType   Brings the type of element item which was animated.
      * @param  fToggled  Brings whether elements being toggled to be closed or opened. */
    UIDetailsElementAnimationCallback(QObject *pParent, DetailsElementType enmType, bool fToggled);

    /** Adds notifier for a certain details @a pItem. */
    void addNotifier(UIDetailsElement *pItem);

private slots:

    /** Handles a signal about animation finnished. */
    void sltAnimationFinished();

private:

    /** Holds the list of elements which notifies this callback about completion. */
    QList<UIDetailsElement*>  m_notifiers;
    /** Holds the type of element item which was animated. */
    DetailsElementType        m_enmType;
    /** Holds whether elements being toggled to be closed or opened. */
    bool                      m_fToggled;
};


#endif /* !FEQT_INCLUDED_SRC_manager_details_UIDetailsModel_h */
