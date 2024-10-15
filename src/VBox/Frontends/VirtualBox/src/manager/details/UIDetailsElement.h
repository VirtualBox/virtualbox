/* $Id: UIDetailsElement.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsElement class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_details_UIDetailsElement_h
#define FEQT_INCLUDED_SRC_manager_details_UIDetailsElement_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>

/* GUI includes: */
#include "UIDetailsItem.h"
#include "UIExtraDataDefs.h"
#include "UITextTable.h"

/* Forward declarations: */
class QPropertyAnimation;
class QStateMachine;
class QTextLayout;
class UIDetailsSet;
class UIGraphicsRotatorButton;
class UIGraphicsTextPane;
class CCloudMachine;
class CMachine;


/** UIDetailsItem extension implementing element item. */
class UIDetailsElement : public UIDetailsItem
{
    Q_OBJECT;
    Q_PROPERTY(int animatedValue READ animatedValue WRITE setAnimatedValue);
    Q_PROPERTY(int additionalHeight READ additionalHeight WRITE setAdditionalHeight);

signals:

    /** @name Item stuff.
      * @{ */
        /** Notifies about hover enter. */
        void sigHoverEnter();
        /** Notifies about hover leave. */
        void sigHoverLeave();

        /** Notifies about @a enmType element @a fToggled. */
        void sigToggleElement(DetailsElementType enmType, bool fToggled);
        /** Notifies about element toggle finished. */
        void sigToggleElementFinished();

        /** Notifies about element link clicked.
          * @param  strCategory  Brings the link category.
          * @param  strControl   Brings the wanted settings control.
          * @param  uId          Brings the ID. */
        void sigLinkClicked(const QString &strCategory, const QString &strControl, const QUuid &uId);
    /** @} */

public:

    /** RTTI item type. */
    enum { Type = UIDetailsItemType_Element };

    /** Constructs element item, passing pParent to the base-class.
      * @param  enmType  Brings element type.
      * @param  fOpened  Brings whether element is opened. */
    UIDetailsElement(UIDetailsSet *pParent, DetailsElementType enmType, bool fOpened);
    /** Destructs element item. */
    virtual ~UIDetailsElement() RT_OVERRIDE;

    /** @name Item stuff.
      * @{ */
        /** Returns element type. */
        DetailsElementType elementType() const { return m_enmType; }

        /** Defines the @a text table as the passed one. */
        void setText(const UITextTable &text);
        /** Returns the reference to the text table. */
        UITextTable &text() const;

        /** Closes group in @a fAnimated way if requested. */
        void close(bool fAnimated = true);
        /** Returns whether group is closed. */
        bool isClosed() const { return m_fClosed; }

        /** Opens group in @a fAnimated way if requested. */
        void open(bool fAnimated = true);
        /** Returns whether group is opened. */
        bool isOpened() const { return !m_fClosed; }

        /** Returns whether toggle animation is running. */
        bool isAnimationRunning() const { return m_fAnimationRunning; }
        /** Marks animation finished. */
        void markAnimationFinished();

        /** Updates element appearance. */
        virtual void updateAppearance();
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Updates layout. */
        virtual void updateLayout() RT_OVERRIDE;

        /** Returns minimum width-hint. */
        virtual int minimumWidthHint() const RT_OVERRIDE;
        /** Returns minimum height-hint. */
        virtual int minimumHeightHint() const RT_OVERRIDE;
    /** @} */

protected:

    /** Data field types. */
    enum ElementData
    {
        /* Hints: */
        ElementData_Margin,
        ElementData_Spacing
    };

    /** @name Event-handling stuff.
      * @{ */
        /** Handles show @a pEvent. */
        virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

        /** This event handler is delivered after the widget has been resized. */
        virtual void resizeEvent(QGraphicsSceneResizeEvent *pEvent) RT_OVERRIDE;

        /** Handles hover enter @a event. */
        virtual void hoverMoveEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE;
        /** Handles hover leave @a event. */
        virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE;

        /** Handles mouse press @a event. */
        virtual void mousePressEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;
        /** Handles mouse double-click @a event. */
        virtual void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;

        /** Performs painting using passed @a pPainter, @a pOptions and optionally specified @a pWidget. */
        virtual void paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget *pWidget = 0) RT_OVERRIDE;
    /** @} */

    /** @name Item stuff.
      * @{ */
        /** Returns RTTI item type. */
        virtual int type() const RT_OVERRIDE { return Type; }

        /** Returns the description of the item. */
        virtual QString description() const RT_OVERRIDE;

        /** Returns cached machine reference. */
        const CMachine &machine();
        /** Returns cached cloud machine reference. */
        const CCloudMachine &cloudMachine();

        /** Returns whether element is of local type. */
        bool isLocal() const;

        /** Defines element @a strName. */
        void setName(const QString &strName);

        /** Defines @a iAdditionalHeight during toggle animation. */
        void setAdditionalHeight(int iAdditionalHeight);
        /** Returns additional height during toggle animation. */
        int additionalHeight() const { return m_iAdditionalHeight; }
        /** Returns toggle button instance. */
        UIGraphicsRotatorButton *button() const { return m_pButton; }

        /** Returns abstractly stored data value for certain @a iKey. */
        QVariant data(int iKey) const;
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Adds child @a pItem. */
        virtual void addItem(UIDetailsItem *pItem) RT_OVERRIDE;
        /** Removes child @a pItem. */
        virtual void removeItem(UIDetailsItem *pItem) RT_OVERRIDE;

        /** Returns children items of certain @a enmType. */
        virtual QList<UIDetailsItem*> items(UIDetailsItemType enmType) const RT_OVERRIDE;
        /** Returns whether there are children items of certain @a enmType. */
        virtual bool hasItems(UIDetailsItemType enmType) const RT_OVERRIDE;
        /** Clears children items of certain @a enmType. */
        virtual void clearItems(UIDetailsItemType enmType) RT_OVERRIDE;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Returns minimum width-hint for @a fClosed element. */
        virtual int minimumHeightHintForElement(bool fClosed) const;

        /** Returns minimum header width. */
        int minimumHeaderWidth() const { return m_iMinimumHeaderWidth; }
        /** Returns minimum header height. */
        int minimumHeaderHeight() const { return m_iMinimumHeaderHeight; }
    /** @} */

private slots:

    /** @name Item stuff.
      * @{ */
        /** Handles top-level window remaps. */
        void sltHandleWindowRemapped();

        /** Handles toggle button click. */
        void sltToggleButtonClicked();
        /** Handles toggle start. */
        void sltElementToggleStart();
        /** Handles toggle finish. */
        void sltElementToggleFinish(bool fToggled);

        /** Handles child anchor clicks. */
        void sltHandleAnchorClicked(const QString &strAnchor);
        /** Handles child copy request. */
        void sltHandleCopyRequest();
        /** Handles child edit request. */
        void sltHandleEditRequest();
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Handles children geometry changes. */
        void sltUpdateGeometry() { updateGeometry(); }
    /** @} */

    /** @name Move to sub-class.
      * @{ */
        /** Handles mount storage medium requests. */
        void sltMountStorageMedium();
    /** @} */

private:

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Prepares element. */
        void prepareElement();
        /** Prepares toggle button. */
        void prepareButton();
        /** Prepares text pane. */
        void prepareTextPane();
    /** @} */

    /** @name Item stuff.
      * @{ */
        /** Updates icon. */
        void updateIcon();

        /** Defines animated @a iValue. */
        void setAnimatedValue(int iValue) { m_iAnimatedValue = iValue; update(); }
        /** Returns animated value. */
        int animatedValue() const { return m_iAnimatedValue; }

        /** Handles any kind of hover @a pEvent. */
        void handleHoverEvent(QGraphicsSceneHoverEvent *pEvent);
        /** Updates hovered link. */
        void updateNameHoverLink();

        /** Updates animation parameters. */
        void updateAnimationParameters();
        /** Updates toggle button visibility.  */
        void updateButtonVisibility();

        /** Popups name & system editor. */
        void popupNameAndSystemEditor(bool fChooseName, bool fChoosePath, bool fChooseType, const QString &strValue);
        /** Popups base-memory editor. */
        void popupBaseMemoryEditor(const QString &strValue);
        /** Popups boot-order editor. */
        void popupBootOrderEditor(const QString &strValue);
        /** Popups video-memory editor. */
        void popupVideoMemoryEditor(const QString &strValue);
        /** Popups graphics controller type editor. */
        void popupGraphicsControllerTypeEditor(const QString &strValue);
        /** Popups storage editor. */
        void popupStorageEditor(const QString &strValue);
        /** Popups audio host-driver type editor. */
        void popupAudioHostDriverTypeEditor(const QString &strValue);
        /** Popups audio controller type editor. */
        void popupAudioControllerTypeEditor(const QString &strValue);
        /** Popups network attachment type editor. */
        void popupNetworkAttachmentTypeEditor(const QString &strValue);
        /** Popups USB controller type editor. */
        void popupUSBControllerTypeEditor(const QString &strValue);
        /** Popups visual-state type editor. */
        void popupVisualStateTypeEditor(const QString &strValue);
#ifndef VBOX_WS_MAC
        /** Popups menu-bar editor. */
        void popupMenuBarEditor(const QString &strValue);
#endif
        /** Popups status-bar editor. */
        void popupStatusBarEditor(const QString &strValue);
#ifndef VBOX_WS_MAC
        /** Popups mini-toolbar editor. */
        void popupMiniToolbarEditor(const QString &strValue);
#endif
        /** Popups cloud editor. */
        void popupCloudEditor(const QString &strValue);
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Updates minimum header width. */
        void updateMinimumHeaderWidth();
        /** Updates minimum header height. */
        void updateMinimumHeaderHeight();
    /** @} */

    /** @name Painting stuff.
      * @{ */
        /** Paints background using specified @a pPainter and certain @a pOptions. */
        void paintBackground(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions) const;
        /** Paints element info using specified @a pPainter and certain @a pOptions. */
        void paintElementInfo(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions) const;

        /** Paints @a pixmap using passed @a pPainter and spified @a rect. */
        static void paintPixmap(QPainter *pPainter, const QRect &rect, const QPixmap &pixmap);
        /** Paints @a strText using passed @a pPainter, @a font, @a color, @a pPaintDevice and spified @a point. */
        static void paintText(QPainter *pPainter, QPoint point,
                              const QFont &font, QPaintDevice *pPaintDevice,
                              const QString &strText, const QColor &color);
    /** @} */

    /** @name Item stuff.
      * @{ */
        /** Holds the parent reference. */
        UIDetailsSet       *m_pSet;
        /** Holds the element type. */
        DetailsElementType  m_enmType;

        /** Holds the element pixmap. */
        QPixmap  m_pixmap;
        /** Holds the element name. */
        QString  m_strName;

        /** Holds the name font. */
        QFont  m_nameFont;
        /** Holds the text font. */
        QFont  m_textFont;

        /** Holds the start default darkness. */
        int m_iDefaultDarknessStart;
        /** Holds the final default darkness. */
        int m_iDefaultDarknessFinal;

        /** Holds whether element is hovered. */
        bool                m_fHovered;
        /** Holds whether element name is hovered. */
        bool                m_fNameHovered;
        /** Holds the hovering animation machine instance. */
        QStateMachine      *m_pHoveringMachine;
        /** Holds the forward hovering animation instance. */
        QPropertyAnimation *m_pHoveringAnimationForward;
        /** Holds the backward hovering animation instance. */
        QPropertyAnimation *m_pHoveringAnimationBackward;
        /** Holds the animation duration. */
        int                 m_iAnimationDuration;
        /** Holds the default animation value. */
        int                 m_iDefaultValue;
        /** Holds the hovered animation value. */
        int                 m_iHoveredValue;
        /** Holds the animated value. */
        int                 m_iAnimatedValue;

        /** Holds the toggle button instance. */
        UIGraphicsRotatorButton *m_pButton;
        /** Holds whether element is closed. */
        bool  m_fClosed;
        /** Holds whether animation is running. */
        bool  m_fAnimationRunning;
        /** Holds the additional height. */
        int   m_iAdditionalHeight;

        /** Holds the graphics text pane instance. */
        UIGraphicsTextPane *m_pTextPane;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Holds the pixmap size. */
        QSize  m_pixmapSize;
        /** Holds the name size. */
        QSize  m_nameSize;
        /** Holds the button size. */
        QSize  m_buttonSize;

        /** Holds minimum header width. */
        int  m_iMinimumHeaderWidth;
        /** Holds minimum header height. */
        int  m_iMinimumHeaderHeight;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_details_UIDetailsElement_h */
