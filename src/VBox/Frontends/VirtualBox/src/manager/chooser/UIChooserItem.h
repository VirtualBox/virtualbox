/* $Id: UIChooserItem.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserItem class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_chooser_UIChooserItem_h
#define FEQT_INCLUDED_SRC_manager_chooser_UIChooserItem_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QGraphicsEffect>
#include <QMimeData>
#include <QRectF>
#include <QString>
#include <QUuid>

/* GUI includes: */
#include "QIGraphicsWidget.h"
#include "UIChooserDefs.h"

/* Forward declaration: */
class QGraphicsSceneHoverEvent;
class QGraphicsSceneMouseEvent;
class QGraphicsSceneDragDropEvent;
class QPropertyAnimation;
class QStateMachine;
class UIActionPool;
class UIChooserItemGroup;
class UIChooserItemGlobal;
class UIChooserItemMachine;
class UIChooserModel;
class UIChooserNode;


/** A simple QGraphicsEffect extension to mark disabled UIChooserItem.
  * @note Applies blur and gray scale filters. */
class UIChooserDisabledItemEffect : public QGraphicsEffect
{
    Q_OBJECT;

public:

    /** Constructs blur effect passing @a pParent to the base-class.
      * @param  iBlurRadius  Brings the blur effect radius. */
    UIChooserDisabledItemEffect(int iBlurRadius, QObject *pParent = 0);

protected:

    /** Draws effect with passed @a pPainter. */
    virtual void draw(QPainter *pPainter) RT_OVERRIDE RT_FINAL;;

private:

    /** Holds the blur effect radius. */
    int m_iBlurRadius;
};


/** QIGraphicsWidget extension used as interface
  * for graphics chooser model/view architecture. */
class UIChooserItem : public QIGraphicsWidget
{
    Q_OBJECT;
    Q_PROPERTY(int animatedValue READ animatedValue WRITE setAnimatedValue);

signals:

    /** @name Item stuff.
      * @{ */
        /** Notifies listeners about hover enter. */
        void sigHoverEnter();
        /** Notifies listeners about hover leave. */
        void sigHoverLeave();
    /** @} */

public:

    /** Constructs item passing @a pParent to the base-class.
      * @param  pNode          Brings the node this item is built for.
      * @param  iDefaultValue  Brings default value for hovering animation.
      * @param  iHoveredValue  Brings hovered value for hovering animation. */
    UIChooserItem(UIChooserItem *pParent, UIChooserNode *pNode,
                  int iDefaultValue = 0, int iHoveredValue = 100);

    /** @name Item stuff.
      * @{ */
        /** Returns parent reference. */
        UIChooserItem *parentItem() const {  return m_pParent; }
        /** Returns node reference. */
        UIChooserNode *node() const { return m_pNode; }

        /** Casts item to group one. */
        UIChooserItemGroup *toGroupItem();
        /** Casts item to global one. */
        UIChooserItemGlobal *toGlobalItem();
        /** Casts item to machine one. */
        UIChooserItemMachine *toMachineItem();

        /** Returns model reference. */
        UIChooserModel *model() const;

        /** Returns whether item is root. */
        bool isRoot() const;

        /** Returns item name. */
        QString name() const;
        /** Returns item full-name. */
        QString fullName() const;
        /** Returns item description. */
        QString description() const;
        /** Returns item definition. */
        QString definition() const;

        /** Returns whether item is favorite. */
        bool isFavorite() const;
        /** Defines whether item is @a fFavorite. */
        virtual void setFavorite(bool fFavorite);

        /** Returns item position. */
        int position() const;

        /** Returns whether item is hovered. */
        bool isHovered() const;

        /** Returns whether item is selected.
          * @note Sometimes it's useful to know whether item is selected in model above. */
        virtual bool isSelected() const;
        /** Defines item as @a fSelected.
          * @note Don't forget to call for base-class method when reimplementing it. */
        virtual void setSelected(bool fSelected);

        /** Starts item editing. */
        virtual void startEditing() = 0;

        /** Updates item. */
        virtual void updateItem() = 0;
        /** Updates item tool-tip. */
        virtual void updateToolTip() = 0;

        /** Installs event-filter for @a pSource object.
          * @note  Base-class implementation does nothing. */
        virtual void installEventFilterHelper(QObject *pSource) { Q_UNUSED(pSource); }
        /** Defines whether visual effect for disabled item is @a fOn. */
        void setDisabledEffect(bool fOn);
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Returns children items of certain @a enmType. */
        virtual QList<UIChooserItem*> items(UIChooserNodeType enmType = UIChooserNodeType_Any) const = 0;

        /** Adds possible @a fFavorite child @a pItem to certain @a iPosition. */
        virtual void addItem(UIChooserItem *pItem, bool fFavorite, int iPosition) = 0;
        /** Removes child @a pItem. */
        virtual void removeItem(UIChooserItem *pItem) = 0;

        /** Searches for a first child item answering to specified @a strSearchTag and @a iSearchFlags. */
        virtual UIChooserItem *searchForItem(const QString &strSearchTag, int iSearchFlags) = 0;

        /** Searches for a first machine child item. */
        virtual UIChooserItem *firstMachineItem() = 0;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Updates geometry. */
        virtual void updateGeometry() RT_OVERRIDE;

        /** Updates layout. */
        virtual void updateLayout() = 0;

        /** Returns minimum width-hint. */
        virtual int minimumWidthHint() const = 0;
        /** Returns minimum height-hint. */
        virtual int minimumHeightHint() const = 0;
    /** @} */

    /** @name Navigation stuff.
      * @{ */
        /** Makes sure item is visible. */
        virtual void makeSureItsVisible();

        /** Returns pixmap item representation. */
        virtual QPixmap toPixmap() = 0;

        /** Returns whether item drop is allowed.
          * @param  pEvent    Brings information about drop event.
          * @param  enmPlace  Brings the place of drag token to the drop moment. */
        virtual bool isDropAllowed(QGraphicsSceneDragDropEvent *pEvent, UIChooserItemDragToken enmPlace = UIChooserItemDragToken_Off) const = 0;
        /** Processes item drop.
          * @param  pEvent    Brings information about drop event.
          * @param  pFromWho  Brings the item according to which we choose drop position.
          * @param  enmPlace  Brings the place of drag token to the drop moment (according to item mentioned above). */
        virtual void processDrop(QGraphicsSceneDragDropEvent *pEvent, UIChooserItem *pFromWho = 0, UIChooserItemDragToken enmPlace = UIChooserItemDragToken_Off) = 0;
        /** Reset drag token. */
        virtual void resetDragToken() = 0;

        /** Returns drag token place. */
        UIChooserItemDragToken dragTokenPlace() const;
        /** Defines drag token @a enmPlace. */
        void setDragTokenPlace(UIChooserItemDragToken enmPlace);
    /** @} */

protected:

    /** @name Event-handling stuff.
      * @{ */
        /** Handles hover enter @a event. */
        virtual void hoverMoveEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE;
        /** Handles hover leave @a event. */
        virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE;

        /** Handles mouse press @a event. */
        virtual void mousePressEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;
        /** Handles mouse move @a event. */
        virtual void mouseMoveEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;

        /** Handles drag move @a event. */
        virtual void dragMoveEvent(QGraphicsSceneDragDropEvent *pEvent) RT_OVERRIDE;
        /** Handles drag leave @a event. */
        virtual void dragLeaveEvent(QGraphicsSceneDragDropEvent *pEvent) RT_OVERRIDE;
        /** Handles drop @a event. */
        virtual void dropEvent(QGraphicsSceneDragDropEvent *pEvent) RT_OVERRIDE;
    /** @} */

    /** @name Item stuff.
      * @{ */
        /** Returns item's default animation value. */
        int defaultValue() const { return m_iDefaultValue; }
        /** Defines item's default animation @a iValue. */
        void setDefaultValue(int iValue) { m_iDefaultValue = iValue; update(); }

        /** Returns item's hovered animation value. */
        int hoveredValue() const { return m_iHoveredValue; }
        /** Defines item's hovered animation @a iValue. */
        void setHoveredValue(int iValue) { m_iHoveredValue = iValue; update(); }

        /** Returns item's animated value. */
        int animatedValue() const { return m_iAnimatedValue; }
        /** Defines item's animated @a iValue. */
        void setAnimatedValue(int iValue) { m_iAnimatedValue = iValue; update(); }
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Returns previous geometry. */
        QRectF previousGeometry() const { return m_previousGeometry; }
        /** Defines previous @a geometry. */
        void setPreviousGeometry(const QRectF &geometry) { m_previousGeometry = geometry; }

        /** Returns @a strText size calculated on the basis of certain @a font and @a pPaintDevice. */
        static QSize textSize(const QFont &font, QPaintDevice *pPaintDevice, const QString &strText);
        /** Returns a width of line containing @a iCount of chars calculated on the basis of certain @a font and @a pPaintDevice. */
        static int textWidth(const QFont &font, QPaintDevice *pPaintDevice, int iCount);
        /** Compresses @a strText to @a iWidth on the basis of certain @a font and @a pPaintDevice. */
        static QString compressText(const QFont &font, QPaintDevice *pPaintDevice, QString strText, int iWidth);
    /** @} */

    /** @name Navigation stuff.
      * @{ */
        /** Returns D&D mime data. */
        virtual QMimeData *createMimeData() = 0;

        /** Returns drag token darkness. */
        int dragTokenDarkness() const { return m_iDragTokenDarkness; }
    /** @} */

    /** @name Painting stuff.
      * @{ */
        /** Paints frame @a rectangle using passed @a pPainter.
          * @param  fIsSelected  Brings whether this rectangle should be filled.
          * @param  iRadius      Brings the radius of rounded corners. */
        static void paintFrameRect(QPainter *pPainter, bool fIsSelected, int iRadius,
                                   const QRect &rectangle);
        /** Paints @a pixmap using passed @a pPainter putting its upper-left corner to specified @a point. */
        static void paintPixmap(QPainter *pPainter, const QPoint &point,
                                const QPixmap &pixmap);
        /** Paints @a strText using passed @a pPainter putting its upper-left corner to specified @a point.
          * @param  font          Brings the text font.
          * @param  pPaintDevice  Brings the paint-device reference to initilize painting from. */
        static void paintText(QPainter *pPainter, QPoint point,
                              const QFont &font, QPaintDevice *pPaintDevice,
                              const QString &strText);
        /** Paints flat button @a rectangle using passed @a pPainter moving light focus according to passed @a cursorPosition. */
        static void paintFlatButton(QPainter *pPainter, const QRect &rectangle, const QPoint &cursorPosition);
    /** @} */

private:

    /** @name Item stuff.
      * @{ */
        /** Holds the item's parent item. */
        UIChooserItem *m_pParent;
        /** Holds the node this item is built for. */
        UIChooserNode *m_pNode;

        /** Holds whether item is hovered. */
        bool                         m_fHovered;
        /** Holds whether item is selected. */
        bool                         m_fSelected;
        /** Holds the hovering animation machine instance. */
        QStateMachine               *m_pHoveringMachine;
        /** Holds the forward hovering animation instance. */
        QPropertyAnimation          *m_pHoveringAnimationForward;
        /** Holds the backward hovering animation instance. */
        QPropertyAnimation          *m_pHoveringAnimationBackward;
        /** Holds the animation duration. */
        int                          m_iAnimationDuration;
        /** Holds the default animation value. */
        int                          m_iDefaultValue;
        /** Holds the hovered animation value. */
        int                          m_iHoveredValue;
        /** Holds the animated value. */
        int                          m_iAnimatedValue;
        /** Holds the blur effect instance. */
        UIChooserDisabledItemEffect *m_pDisabledEffect;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Holds previous geometry. */
        QRectF  m_previousGeometry;
    /** @} */

    /** @name Navigation stuff.
      * @{ */
        /** Holds drag token place. */
        UIChooserItemDragToken  m_enmDragTokenPlace;

        /** Holds drag token darkness. */
        int  m_iDragTokenDarkness;
    /** @} */
};


/** QMimeData for graphics item interface. */
class UIChooserItemMimeData : public QMimeData
{
    Q_OBJECT;

public:

    /** Constructs mime-data on the basis of passed @a pItem. */
    UIChooserItemMimeData(UIChooserItem *pItem);

    /** Returns cached item. */
    UIChooserItem *item() const { return m_pItem; }

    /** Constructs mime-data on the basis of passed @a pItem. */
    virtual bool hasFormat(const QString &strMimeType) const RT_OVERRIDE;

private:

    /** Holds the cached item. */
    UIChooserItem *m_pItem;
};


#endif /* !FEQT_INCLUDED_SRC_manager_chooser_UIChooserItem_h */
