/* $Id: UITabBar.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITabBar class implementation.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include <QAction>
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QStyleOption>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QToolButton>
#include <QWindow>
#ifdef VBOX_WS_MAC
# include <QStackedLayout>
#endif

/* GUI includes: */
#include "UIIconPool.h"
#include "UITabBar.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"

/* Forward declarations: */
class QApplication;
class QDrag;
class QEvent;
class QHBoxLayout;
class QLabel;
class QMimeData;
class QMouseEvent;
#ifdef VBOX_WS_MAC
class QStackedLayout;
#endif
class QStyle;
class QStyleOption;
class QToolButton;


/** Our own skinnable implementation of tabs for tab-bar. */
class UITabBarItem : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies about item was clicked. */
    void sigClicked(UITabBarItem *pItem);

    /** Notifies about item close button was clicked. */
    void sigCloseClicked(UITabBarItem *pItem);

    /** Notifies about drag-object destruction. */
    void sigDragObjectDestroy();

public:

    /** Position styles. */
    enum PositionStyle { PositionStyle_Left, PositionStyle_Middle, PositionStyle_Right, PositionStyle_Single };

    /** Holds the mime-type for the D&D system. */
    static const QString MimeType;

    /** Creates tab-bar item on the basis of passed @a uuid and @a pAction. */
    UITabBarItem(const QUuid &uuid, const QAction *pAction);

    /** Returns item ID. */
    const QUuid uuid() const { return m_uuid; }

    /** Defines the item @a enmPositionStyle. */
    void setPositionStyle(PositionStyle enmPositionStyle);

    /** Marks item @a fCurrent. */
    void setCurrent(bool fCurrent);

protected:

    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

    /** Handles mouse-press @a pEvent. */
    virtual void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-release @a pEvent. */
    virtual void mouseReleaseEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-move @a pEvent. */
    virtual void mouseMoveEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-enter @a pEvent. */
    virtual void enterEvent(QEnterEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-leave @a pEvent. */
    virtual void leaveEvent(QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles close button click. */
    void sltCloseClicked() { emit sigCloseClicked(this); }

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();
    /** Update pixmap. */
    void updatePixmap();

    /** Holds the item ID. */
    const QUuid    m_uuid;
    /** Holds the item action reference. */
    const QAction *m_pAction;

    /** Holds the item position style. */
    PositionStyle m_enmPosition;

    /** Holds whether the item is current. */
    bool  m_fCurrent;
    /** Holds whether the item is hovered. */
    bool  m_fHovered;

    /** Holds the main layout instance. */
    QHBoxLayout    *m_pLayout;
#ifdef VBOX_WS_MAC
    /** Holds the stacked layout instance. */
    QStackedLayout *m_pLayoutStacked;
#endif

    /** Holds the icon label instance. */
    QLabel      *m_pLabelIcon;
    /** Holds the name label instance. */
    QLabel      *m_pLabelName;
    /** Holds the close button instance. */
    QToolButton *m_pButtonClose;

    /** Holds the last mouse-press position. */
    QPoint m_mousePressPosition;
};


/*********************************************************************************************************************************
*   Class UITabBarItem implementation.                                                                                           *
*********************************************************************************************************************************/

/* static */
const QString UITabBarItem::MimeType = QString("application/virtualbox;value=TabID");

UITabBarItem::UITabBarItem(const QUuid &uuid, const QAction *pAction)
    : m_uuid(uuid)
    , m_pAction(pAction)
    , m_enmPosition(PositionStyle_Single)
    , m_fCurrent(false)
    , m_fHovered(false)
    , m_pLayout(0)
#ifdef VBOX_WS_MAC
    , m_pLayoutStacked(0)
#endif
    , m_pLabelIcon(0)
    , m_pLabelName(0)
    , m_pButtonClose(0)
{
    /* Prepare: */
    prepare();
}

void UITabBarItem::setPositionStyle(PositionStyle enmPosition)
{
    /* Remember the position: */
    m_enmPosition = enmPosition;

    /* And call for repaint: */
    update();
}

void UITabBarItem::setCurrent(bool fCurrent)
{
    /* Remember the state: */
    m_fCurrent = fCurrent;

#ifdef VBOX_WS_MAC
    /* Adjust name color: */
    QPalette pal = qApp->palette();
    if (m_fCurrent)
        pal.setColor(QPalette::ButtonText, pal.color(QPalette::BrightText));
    m_pLabelName->setPalette(pal);
#endif

    /* And call for repaint: */
    update();
}

bool UITabBarItem::event(QEvent *pEvent)
{
    /* Handle know event types: */
    switch (pEvent->type())
    {
        case QEvent::Show:
        case QEvent::ScreenChangeInternal:
        {
            /* Update pixmap: */
            updatePixmap();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QWidget::event(pEvent);
}

void UITabBarItem::sltRetranslateUI()
{
    /* Translate label: */
    m_pLabelName->setText(m_pAction->text().remove('&'));
}

void UITabBarItem::paintEvent(QPaintEvent * /* pEvent */)
{
#ifdef VBOX_WS_MAC

    /* Prepare painter: */
    QPainter painter(this);

    /* Prepare palette colors: */
    const QPalette pal = QApplication::palette();
    const QColor color0 = m_fCurrent
                        ? pal.color(QPalette::Shadow).darker(110)
                        : pal.color(QPalette::Window).lighter(105);
    const QColor color1 = pal.color(QPalette::Window);
    const QColor color2 = color0.darker(120);
    const QColor color3 = color0.darker(130);

    /* Invent pixel metric: */
    const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4;

    /* Top-left corner: */
    QRadialGradient grad1(QPointF(iMetric, iMetric), iMetric);
    {
        grad1.setColorAt(0, color0);
        grad1.setColorAt(.8, color0);
        grad1.setColorAt(.81, color2);
        grad1.setColorAt(1, color1);
    }
    /* Top-right corner: */
    QRadialGradient grad2(QPointF(width() - iMetric, iMetric), iMetric);
    {
        grad2.setColorAt(0, color0);
        grad2.setColorAt(.8, color0);
        grad2.setColorAt(.81, color2);
        grad2.setColorAt(1, color1);
    }
    /* Bottom-left corner: */
    QRadialGradient grad3(QPointF(iMetric, height() - iMetric), iMetric);
    {
        grad3.setColorAt(0, color0);
        grad3.setColorAt(.8, color0);
        grad3.setColorAt(.81, color3);
        grad3.setColorAt(1, color1);
    }
    /* Botom-right corner: */
    QRadialGradient grad4(QPointF(width() - iMetric, height() - iMetric), iMetric);
    {
        grad4.setColorAt(0, color0);
        grad4.setColorAt(.8, color0);
        grad4.setColorAt(.81, color3);
        grad4.setColorAt(1, color1);
    }

    /* Top line: */
    QLinearGradient grad5(QPointF(iMetric, 0), QPointF(iMetric, iMetric));
    {
        grad5.setColorAt(0, color1);
        grad5.setColorAt(.19, color2);
        grad5.setColorAt(.2, color0);
        grad5.setColorAt(1, color0);
    }
    /* Bottom line: */
    QLinearGradient grad6(QPointF(iMetric, height()), QPointF(iMetric, height() - iMetric));
    {
        grad6.setColorAt(0, color1);
        grad6.setColorAt(.19, color3);
        grad6.setColorAt(.2, color0);
        grad6.setColorAt(1, color0);
    }
    /* Left line: */
    QLinearGradient grad7(QPointF(0, height() - iMetric), QPointF(iMetric, height() - iMetric));
    {
        grad7.setColorAt(0, color1);
        grad7.setColorAt(.19, color2);
        grad7.setColorAt(.2, color0);
        grad7.setColorAt(1, color0);
    }
    /* Right line: */
    QLinearGradient grad8(QPointF(width(), height() - iMetric), QPointF(width() - iMetric, height() - iMetric));
    {
        grad8.setColorAt(0, color1);
        grad8.setColorAt(.19, color2);
        grad8.setColorAt(.2, color0);
        grad8.setColorAt(1, color0);
    }

    /* Paint: */
    painter.fillRect(QRect(iMetric, iMetric, width() - iMetric * 2, height() - iMetric * 2), color0);

    if (m_enmPosition == PositionStyle_Left || m_enmPosition == PositionStyle_Single)
    {
        painter.fillRect(QRect(0,                  0,                  iMetric,            iMetric),           grad1);
        painter.fillRect(QRect(0,                  height() - iMetric, iMetric,            iMetric),           grad3);
    }
    if (m_enmPosition == PositionStyle_Right || m_enmPosition == PositionStyle_Single)
    {
        painter.fillRect(QRect(width() - iMetric,  0,                  iMetric,            iMetric),           grad2);
        painter.fillRect(QRect(width() - iMetric,  height() - iMetric, iMetric,            iMetric),           grad4);
    }

    int iX = 0;
    int iYL = 0;
    int iYR = 0;
    int iWid = width();
    int iHeiL = height();
    int iHeiR = height();
    if (m_enmPosition == PositionStyle_Left || m_enmPosition == PositionStyle_Single)
    {
        iX = iMetric;
        iYL = iMetric;
        iWid -= iMetric;
        iHeiL -= iMetric * 2;
    }
    if (m_enmPosition == PositionStyle_Right || m_enmPosition == PositionStyle_Single)
    {
        iYR = iMetric;
        iWid -= iMetric;
        iHeiR -= iMetric * 2;
    }
    painter.fillRect(QRect(0,                  iYL,                iMetric,            iHeiL),             grad7);
    painter.fillRect(QRect(width() - iMetric,  iYR,                iMetric,            iHeiR),             grad8);
    painter.fillRect(QRect(iX,                 0,                  iWid,               iMetric),           grad5);
    painter.fillRect(QRect(iX,                 height() - iMetric, iWid,               iMetric),           grad6);

#else /* !VBOX_WS_MAC */

    /* Prepare painter: */
    QPainter painter(this);

    /* Prepare palette colors: */
    const QPalette pal = QApplication::palette();
    const QColor color0 = m_fCurrent ? pal.color(QPalette::Base)
                        : m_fHovered ? pal.color(QPalette::Base).darker(102)
                        :              pal.color(QPalette::Button).darker(102);
    QColor color1 = color0;
    color1.setAlpha(0);
    QColor color2 = pal.color(QPalette::Shadow);

    /* Invent pixel metric: */
    const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 2;

    /* Top-left corner: */
    QRadialGradient grad1(QPointF(iMetric, iMetric), iMetric);
    {
        grad1.setColorAt(0, color0);
        grad1.setColorAt(.8, color1);
        grad1.setColorAt(.9, color2);
        grad1.setColorAt(1, color1);
    }
    /* Bottom-left corner: */
    QRadialGradient grad2(QPointF(iMetric, height() - iMetric), iMetric);
    {
        grad2.setColorAt(0, color0);
        grad2.setColorAt(.8, color1);
        grad2.setColorAt(.9, color2);
        grad2.setColorAt(1, color1);
    }
    /* Top-right corner: */
    QRadialGradient grad3(QPointF(width() - iMetric, iMetric), iMetric);
    {
        grad3.setColorAt(0, color0);
        grad3.setColorAt(.8, color1);
        grad3.setColorAt(.9, color2);
        grad3.setColorAt(1, color1);
    }
    /* Botom-right corner: */
    QRadialGradient grad4(QPointF(width() - iMetric, height() - iMetric), iMetric);
    {
        grad4.setColorAt(0, color0);
        grad4.setColorAt(.8, color1);
        grad4.setColorAt(.9, color2);
        grad4.setColorAt(1, color1);
    }

    /* Left line: */
    QLinearGradient grad5(QPointF(0, height() - iMetric), QPointF(iMetric, height() - iMetric));
    {
        grad5.setColorAt(0, color1);
        grad5.setColorAt(.1, color2);
        grad5.setColorAt(.2, color1);
        grad5.setColorAt(1, color0);
    }
    /* Right line: */
    QLinearGradient grad6(QPointF(width(), height() - iMetric), QPointF(width() - iMetric, height() - iMetric));
    {
        grad6.setColorAt(0, color1);
        grad6.setColorAt(.1, color2);
        grad6.setColorAt(.2, color1);
        grad6.setColorAt(1, color0);
    }
    /* Top line: */
    QLinearGradient grad7(QPointF(iMetric, 0), QPointF(iMetric, iMetric));
    {
        grad7.setColorAt(0, color1);
        grad7.setColorAt(.1, color2);
        grad7.setColorAt(.2, color1);
        grad7.setColorAt(1, color0);
    }
    /* Bottom line: */
    QLinearGradient grad8(QPointF(iMetric, height()), QPointF(iMetric, height() - iMetric));
    {
        grad8.setColorAt(0, color1);
        grad8.setColorAt(.1, color2);
        grad8.setColorAt(.2, color1);
        grad8.setColorAt(1, color0);
    }

    /* Paint: */
    painter.fillRect(QRect(iMetric, iMetric, width() - iMetric * 2, height() - iMetric * 2), color0);

    if (m_enmPosition == PositionStyle_Left || m_enmPosition == PositionStyle_Single)
    {
        painter.fillRect(QRect(0,                  0,                  iMetric,            iMetric),           grad1);
        painter.fillRect(QRect(0,                  height() - iMetric, iMetric,            iMetric),           grad2);
    }
    if (m_enmPosition == PositionStyle_Right || m_enmPosition == PositionStyle_Single)
    {
        painter.fillRect(QRect(width() - iMetric,  0,                  iMetric,            iMetric),           grad3);
        painter.fillRect(QRect(width() - iMetric,  height() - iMetric, iMetric,            iMetric),           grad4);
    }

    int iX = 0;
    int iYL = 0;
    int iYR = 0;
    int iWid = width();
    int iHeiL = height();
    int iHeiR = height();
    if (m_enmPosition == PositionStyle_Left || m_enmPosition == PositionStyle_Single)
    {
        iX = iMetric;
        iYL = iMetric;
        iWid -= iMetric;
        iHeiL -= iMetric * 2;
    }
    if (m_enmPosition == PositionStyle_Right || m_enmPosition == PositionStyle_Single)
    {
        iYR = iMetric;
        iWid -= iMetric;
        iHeiR -= iMetric * 2;
    }

    QPainterPath path5;
    path5.moveTo(0, 0);
    path5.lineTo(iMetric, iMetric);
    path5.lineTo(iMetric, height() - iMetric);
    path5.lineTo(0, height());
    path5.closeSubpath();
    painter.setClipPath(path5);
    painter.fillRect(QRect(0,                  iYL,                iMetric,            iHeiL),             grad5);
    painter.setClipping(false);

    QPainterPath path6;
    path6.moveTo(width(), 0);
    path6.lineTo(width() - iMetric, iMetric);
    path6.lineTo(width() - iMetric, height() - iMetric);
    path6.lineTo(width(), height());
    path6.closeSubpath();
    painter.setClipPath(path6);
    painter.fillRect(QRect(width() - iMetric,  iYR,                iMetric,            iHeiR),             grad6);
    painter.setClipping(false);

    QPainterPath path7;
    path7.moveTo(0, 0);
    path7.lineTo(iMetric, iMetric);
    path7.lineTo(width() - iMetric, iMetric);
    path7.lineTo(width(), 0);
    path7.closeSubpath();
    painter.setClipPath(path7);
    painter.fillRect(QRect(iX,                 0,                  iWid,               iMetric),           grad7);
    painter.setClipping(false);

    QPainterPath path8;
    path8.moveTo(0, height());
    path8.lineTo(iMetric, height() - iMetric);
    path8.lineTo(width() - iMetric, height() - iMetric);
    path8.lineTo(width(), height());
    path8.closeSubpath();
    painter.setClipPath(path8);
    painter.fillRect(QRect(iX,                 height() - iMetric, iWid,               iMetric),           grad8);
    painter.setClipping(false);

#endif /* !VBOX_WS_MAC */
}

void UITabBarItem::mousePressEvent(QMouseEvent *pEvent)
{
    /* We are interested in left button only: */
    if (pEvent->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(pEvent);

    /* Remember mouse-press position: */
    m_mousePressPosition = pEvent->globalPosition().toPoint();
}

void UITabBarItem::mouseReleaseEvent(QMouseEvent *pEvent)
{
    /* We are interested in left button only: */
    if (pEvent->button() != Qt::LeftButton)
        return QWidget::mouseReleaseEvent(pEvent);

    /* Forget mouse-press position: */
    m_mousePressPosition = QPoint();

    /* Notify listeners about the item was clicked: */
    emit sigClicked(this);
}

void UITabBarItem::mouseMoveEvent(QMouseEvent *pEvent)
{
    /* Make sure item isn't already dragged: */
    if (m_mousePressPosition.isNull())
        return QWidget::mouseMoveEvent(pEvent);

    /* Make sure item is now being dragged: */
    const QPoint gPos = pEvent->globalPosition().toPoint();
    if (QLineF(gPos, m_mousePressPosition).length() < QApplication::startDragDistance())
        return QWidget::mouseMoveEvent(pEvent);

    /* Revoke hovered state: */
#ifdef VBOX_WS_MAC
    m_pLayoutStacked->setCurrentWidget(m_pLabelIcon);
#endif
    m_fHovered = false;
    /* And call for repaint: */
    update();

    /* Initialize dragging: */
    m_mousePressPosition = QPoint();
    QDrag *pDrag = new QDrag(this);
    connect(pDrag, &QObject::destroyed, this, &UITabBarItem::sigDragObjectDestroy);
    QMimeData *pMimeData = new QMimeData;
    pMimeData->setData(MimeType, uuid().toByteArray());
    pDrag->setMimeData(pMimeData);
    const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    const qreal fDevicePixelRatio = window() && window()->windowHandle() ? window()->windowHandle()->devicePixelRatio() : 1;
    pDrag->setPixmap(m_pAction->icon().pixmap(QSize(iMetric, iMetric), fDevicePixelRatio));
    pDrag->exec();
}

void UITabBarItem::enterEvent(QEnterEvent *pEvent)
{
    /* Make sure button isn't hovered: */
    if (m_fHovered)
        return QWidget::enterEvent(pEvent);

    /* Invert hovered state: */
#ifdef VBOX_WS_MAC
    m_pLayoutStacked->setCurrentWidget(m_pButtonClose);
#endif
    m_fHovered = true;
    /* And call for repaint: */
    update();
}

void UITabBarItem::leaveEvent(QEvent *pEvent)
{
    /* Make sure button is hovered: */
    if (!m_fHovered)
        return QWidget::leaveEvent(pEvent);

    /* Invert hovered state: */
#ifdef VBOX_WS_MAC
    m_pLayoutStacked->setCurrentWidget(m_pLabelIcon);
#endif
    m_fHovered = false;
    /* And call for repaint: */
    update();
}

void UITabBarItem::prepare()
{
    /* Configure self: */
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    /* Create main layout: */
    m_pLayout = new QHBoxLayout(this);
    if (m_pLayout)
    {
        /* Invent pixel metric: */
        const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
#ifdef VBOX_WS_MAC
        const int iMargin = iMetric / 4;
#else
        const int iMargin = iMetric / 2;
#endif
        const int iSpacing = iMargin / 2;
#ifdef VBOX_WS_MAC
        const int iMetricCloseButton = iMetric * 3 / 4;
#else
        const int iMetricCloseButton = iMetric * 2 / 3;
#endif

        /* Configure layout: */
#ifdef VBOX_WS_MAC
        m_pLayout->setContentsMargins(iMargin + iSpacing, iMargin, iMargin + iSpacing, iMargin);
#else
        m_pLayout->setContentsMargins(iMargin + iSpacing, iMargin, iMargin, iMargin);
#endif
        m_pLayout->setSpacing(iSpacing);

        /* Create icon label: */
        m_pLabelIcon = new QLabel;
        if (m_pLabelIcon)
        {
            /* Configure label: */
            m_pLabelIcon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }

        /* Create name label: */
        m_pLabelName = new QLabel;
        if (m_pLabelName)
        {
            /* Configure label: */
            m_pLabelName->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }

        /* Create close button: */
        m_pButtonClose = new QToolButton;
        if (m_pButtonClose)
        {
            /* Configure button: */
            m_pButtonClose->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            m_pButtonClose->setIconSize(QSize(iMetricCloseButton, iMetricCloseButton));
            m_pButtonClose->setIcon(UIIconPool::iconSet(":/close_16px.png"));
#ifdef VBOX_WS_MAC
            m_pButtonClose->setStyleSheet("QToolButton { border: 0px }");
#else
            m_pButtonClose->setAutoRaise(true);
#endif
            connect(m_pButtonClose, &QToolButton::clicked, this, &UITabBarItem::sltCloseClicked);
        }

#ifdef VBOX_WS_MAC
        /* Create stacked-layout: */
        m_pLayoutStacked = new QStackedLayout(m_pLayout);
        if (m_pLayoutStacked)
        {
            m_pLayoutStacked->setAlignment(Qt::AlignCenter);

            /* Add icon-label and close-button into stacked-layout: */
            m_pLayoutStacked->addWidget(m_pLabelIcon);
            m_pLayoutStacked->addWidget(m_pButtonClose);
            m_pLayoutStacked->setAlignment(m_pLabelIcon, Qt::AlignCenter);
            m_pLayoutStacked->setAlignment(m_pButtonClose, Qt::AlignCenter);

            /* Add stacked-layout into main-layout: */
            m_pLayout->addLayout(m_pLayoutStacked);
        }

        /* Add name-label into main-layout: */
        m_pLayout->addWidget(m_pLabelName);
#else /* !VBOX_WS_MAC */
        /* Add everything into main-layout: */
        m_pLayout->addWidget(m_pLabelIcon);
        m_pLayout->addWidget(m_pLabelName);
        m_pLayout->addWidget(m_pButtonClose);
#endif /* !VBOX_WS_MAC */
    }

    /* Update pixmap: */
    updatePixmap();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UITabBarItem::sltRetranslateUI);
}

void UITabBarItem::updatePixmap()
{
    /* Configure label icon: */
    const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    const qreal fDevicePixelRatio = window() && window()->windowHandle() ? window()->windowHandle()->devicePixelRatio() : 1;
    m_pLabelIcon->setPixmap(m_pAction->icon().pixmap(QSize(iMetric, iMetric), fDevicePixelRatio));
}


/*********************************************************************************************************************************
*   Class UITabBar implementation.                                                                                               *
*********************************************************************************************************************************/

UITabBar::UITabBar(Alignment enmAlignment, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmAlignment(enmAlignment)
    , m_pLayoutMain(0)
    , m_pLayoutTab(0)
    , m_pCurrentItem(0)
    , m_pItemToken(0)
    , m_fDropAfterTokenItem(0)
{
    /* Prepare: */
    prepare();
}

QUuid UITabBar::addTab(const QAction *pAction)
{
    /* Generate unique ID: */
    const QUuid uuid = QUuid::createUuid();
    /* Create new tab item: */
    UITabBarItem *pItem = new UITabBarItem(uuid, pAction);
    AssertPtrReturn(pItem, QUuid());
    {
        /* Configure item: */
        connect(pItem, &UITabBarItem::sigClicked,           this, &UITabBar::sltHandleMakeChildCurrent);
        connect(pItem, &UITabBarItem::sigCloseClicked,      this, &UITabBar::sltHandleChildClose);
        connect(pItem, &UITabBarItem::sigDragObjectDestroy, this, &UITabBar::sltHandleDragObjectDestroy);
        /* Add item into layout and list: */
        switch (m_enmAlignment)
        {
            case Align_Left:
                m_pLayoutTab->addWidget(pItem);
                m_aItems.append(pItem);
                break;
            case Align_Right:
                m_pLayoutTab->insertWidget(0, pItem);
                m_aItems.prepend(pItem);
                break;
        }
        /* Update children styles: */
        updateChildrenStyles();
        /* Return unique ID: */
        return uuid;
    }
}

bool UITabBar::removeTab(const QUuid &uuid)
{
    /* Prepare result: */
    bool fSuccess = false;

    /* Do we need to bother about current item? */
    bool fMoveCurrent = m_pCurrentItem->uuid() == uuid;

    /* Search through all the items we have: */
    for (int i = 0; i < m_aItems.size(); ++i)
    {
        /* Get iterated item: */
        UITabBarItem *pItem = m_aItems.at(i);
        /* If that item is what we are looking for: */
        if (pItem->uuid() == uuid)
        {
            /* Delete it and wipe it from the list: */
            delete pItem;
            m_aItems[i] = 0;
            fSuccess = true;
        }
    }
    /* Flush wiped out items: */
    m_aItems.removeAll((UITabBarItem *)0);

    /* If we had removed current item: */
    if (fMoveCurrent)
    {
        /* Mark it null initially: */
        m_pCurrentItem = 0;
        /* But choose something suitable if we have: */
        if (!m_aItems.isEmpty())
            sltHandleMakeChildCurrent(m_aItems.first());
    }

    /* Update children styles: */
    updateChildrenStyles();

    /* Return result: */
    return fSuccess;
}

bool UITabBar::setCurrent(const QUuid &uuid)
{
    /* Prepare result: */
    bool fSuccess = false;

    /* Search through all the items we have: */
    for (int i = 0; i < m_aItems.size(); ++i)
    {
        /* Get iterated item: */
        UITabBarItem *pItem = m_aItems.at(i);
        /* If that item is what we are looking for: */
        if (pItem->uuid() == uuid)
        {
            /* Make it current: */
            sltHandleMakeChildCurrent(pItem);
            fSuccess = true;
            break;
        }
    }

    /* Return result: */
    return fSuccess;
}

QList<QUuid> UITabBar::tabOrder() const
{
    QList<QUuid> list;
    foreach (UITabBarItem *pItem, m_aItems)
        list << pItem->uuid();
    return list;
}

void UITabBar::paintEvent(QPaintEvent *pEvent)
{
    /* Call to base-class: */
    QWidget::paintEvent(pEvent);

    /* If we have a token item: */
    if (m_pItemToken)
    {
        /* Prepare painter: */
        QPainter painter(this);

        /* Paint drop token: */
        QStyleOption option;
        option.state |= QStyle::State_Horizontal;
        const QRect geo = m_pItemToken->geometry();
        option.rect = !m_fDropAfterTokenItem
                    ? QRect(geo.topLeft() - QPoint(5, 5),
                            geo.bottomLeft() + QPoint(0, 5))
                    : QRect(geo.topRight() - QPoint(0, 5),
                            geo.bottomRight() + QPoint(5, 5));
        QApplication::style()->drawPrimitive(QStyle::PE_IndicatorToolBarSeparator,
                                             &option, &painter);
    }
}

void UITabBar::dragEnterEvent(QDragEnterEvent *pEvent)
{
    /* Make sure event is valid: */
    AssertPtrReturnVoid(pEvent);
    /* And mime-data is set: */
    const QMimeData *pMimeData = pEvent->mimeData();
    AssertPtrReturnVoid(pMimeData);

    /* Make sure mime-data format is valid: */
    if (!pMimeData->hasFormat(UITabBarItem::MimeType))
        return;

    /* Accept drag-enter event: */
    pEvent->acceptProposedAction();
}

void UITabBar::dragMoveEvent(QDragMoveEvent *pEvent)
{
    /* Make sure event is valid: */
    AssertPtrReturnVoid(pEvent);
    const QPoint lPos = pEvent->position().toPoint();

    /* And mime-data is set: */
    const QMimeData *pMimeData = pEvent->mimeData();
    AssertPtrReturnVoid(pMimeData);

    /* Make sure mime-data format is valid: */
    if (!pMimeData->hasFormat(UITabBarItem::MimeType))
        return;

    /* Reset token: */
    m_pItemToken = 0;
    m_fDropAfterTokenItem = true;

    /* Search for most suitable item: */
    foreach (UITabBarItem *pItem, m_aItems)
    {
        /* Advance token: */
        m_pItemToken = pItem;
        const QRect geo = m_pItemToken->geometry();
        if (lPos.x() < geo.center().x())
        {
            m_fDropAfterTokenItem = false;
            break;
        }
    }

    /* Update: */
    update();
}

void UITabBar::dragLeaveEvent(QDragLeaveEvent * /* pEvent */)
{
    /* Reset token: */
    m_pItemToken = 0;
    m_fDropAfterTokenItem = true;

    /* Update: */
    update();
}

void UITabBar::dropEvent(QDropEvent *pEvent)
{
    /* Make sure event is valid: */
    AssertPtrReturnVoid(pEvent);
    /* And mime-data is set: */
    const QMimeData *pMimeData = pEvent->mimeData();
    AssertPtrReturnVoid(pMimeData);

    /* Make sure mime-data format is valid: */
    if (!pMimeData->hasFormat(UITabBarItem::MimeType))
        return;

    /* Make sure token-item set: */
    if (!m_pItemToken)
        return;

    /* Determine ID of token-item: */
    const QUuid tokenUuid = m_pItemToken->uuid();
    /* Determine ID of dropped-item: */
    const QUuid droppedUuid(pMimeData->data(UITabBarItem::MimeType));

    /* Make sure these uuids are different: */
    if (droppedUuid == tokenUuid)
        return;

    /* Search for an item with dropped ID: */
    UITabBarItem *pItemDropped = 0;
    foreach (UITabBarItem *pItem, m_aItems)
    {
        if (pItem->uuid() == droppedUuid)
        {
            pItemDropped = pItem;
            break;
        }
    }

    /* Make sure dropped-item found: */
    if (!pItemDropped)
        return;

    /* Remove dropped-item: */
    m_aItems.removeAll(pItemDropped);
    m_pLayoutTab->removeWidget(pItemDropped);
    /* Insert dropped-item into position of token-item: */
    int iPosition = m_aItems.indexOf(m_pItemToken);
    AssertReturnVoid(iPosition != -1);
    if (m_fDropAfterTokenItem)
        ++iPosition;
    m_aItems.insert(iPosition, pItemDropped);
    m_pLayoutTab->insertWidget(iPosition, pItemDropped);

    /* Update children styles: */
    updateChildrenStyles();
}

void UITabBar::sltHandleMakeChildCurrent(UITabBarItem *pItem)
{
    /* Make sure item exists: */
    AssertPtrReturnVoid(pItem);

    /* Remove current mark from current item if exists: */
    if (m_pCurrentItem)
        m_pCurrentItem->setCurrent(false);

    /* Assign new current item: */
    m_pCurrentItem = pItem;

    /* Place current mark onto current item if exists: */
    if (m_pCurrentItem)
        m_pCurrentItem->setCurrent(true);

    /* Notify listeners: */
    emit sigCurrentTabChanged(pItem->uuid());
}

void UITabBar::sltHandleChildClose(UITabBarItem *pItem)
{
    /* Make sure item exists: */
    AssertPtrReturnVoid(pItem);

    /* Notify listeners: */
    emit sigTabRequestForClosing(pItem->uuid());
}

void UITabBar::sltHandleDragObjectDestroy()
{
    /* Reset token: */
    m_pItemToken = 0;
    m_fDropAfterTokenItem = true;

    /* Update: */
    update();
}

void UITabBar::prepare()
{
    /* Track D&D events: */
    setAcceptDrops(true);

    /* Create main layout: */
    m_pLayoutMain = new QHBoxLayout(this);
    AssertPtrReturnVoid(m_pLayoutMain);
    {
        /* Configure layout: */
        m_pLayoutMain->setSpacing(0);
        m_pLayoutMain->setContentsMargins(0, 0, 0, 0);

        /* Add strech to beginning: */
        if (m_enmAlignment == Align_Right)
            m_pLayoutMain->addStretch();

        /* Create tab layout: */
        m_pLayoutTab = new QHBoxLayout;
        AssertPtrReturnVoid(m_pLayoutTab);
        {
            /* Add into layout: */
            m_pLayoutMain->addLayout(m_pLayoutTab);
        }

        /* Add strech to end: */
        if (m_enmAlignment == Align_Left)
            m_pLayoutMain->addStretch();
    }
}

void UITabBar::updateChildrenStyles()
{
    /* Single child has corresponding (rounded) style: */
    if (m_aItems.size() == 1)
        m_aItems.first()->setPositionStyle(UITabBarItem::PositionStyle_Single);
    /* If there are more than one child: */
    else if (m_aItems.size() > 1)
    {
        /* First make all children have no rounded sides: */
        foreach (UITabBarItem *pItem, m_aItems)
            pItem->setPositionStyle(UITabBarItem::PositionStyle_Middle);
        /* Then make first child rounded left, while last rounded right: */
        m_aItems.first()->setPositionStyle(UITabBarItem::PositionStyle_Left);
        m_aItems.last()->setPositionStyle(UITabBarItem::PositionStyle_Right);
    }
    /* Repaint: */
    update();
}

#include "UITabBar.moc"
