/* $Id: UIStatusBarEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIStatusBarEditor class implementation.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include <QApplication>
#include <QAccessibleWidget>
#include <QCheckBox>
#include <QDrag>
#include <QHBoxLayout>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QStatusBar>
#include <QStyleOption>
#include <QStylePainter>
#include <QWindow>

/* GUI includes: */
#include "QIToolButton.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UIMachineWindow.h"
#include "UIStatusBarEditor.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"

/* Forward declarations: */
class QAccessibleInterface;
class QMouseEvent;
class QObject;
class QPixmap;
class QPoint;
class QSize;


/** QWidget subclass used as status-bar editor button. */
class UIStatusBarEditorButton : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies about click. */
    void sigClick();

    /** Notifies about drag-object destruction. */
    void sigDragObjectDestroy();

public:

    /** Holds the mime-type for the D&D system. */
    static const QString MimeType;

    /** Constructs the button of passed @a enmType. */
    UIStatusBarEditorButton(IndicatorType enmType);

    /** Returns button type. */
    IndicatorType type() const { return m_enmType; }

    /** Returns button size-hint. */
    QSize sizeHint() const RT_OVERRIDE { return m_size; }

    /** Returns whether button is checked. */
    bool isChecked() const;
    /** Defines whether button is @a fChecked. */
    void setChecked(bool fChecked);

protected:

    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles mouse-press @a pEvent. */
    virtual void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles mouse-release @a pEvent. */
    virtual void mouseReleaseEvent(QMouseEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles mouse-enter @a pEvent. */
    virtual void enterEvent(QEnterEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles mouse-leave @a pEvent. */
    virtual void leaveEvent(QEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles mouse-move @a pEvent. */
    virtual void mouseMoveEvent(QMouseEvent *pEvent) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();

    /** Updates pixmap. */
    void updatePixmap();

    /** Holds the button type. */
    IndicatorType  m_enmType;
    /** Holds the button size. */
    QSize          m_size;
    /** Holds the button pixmap. */
    QPixmap        m_pixmap;
    /** Holds the button pixmap size. */
    QSize          m_pixmapSize;
    /** Holds whether button is checked. */
    bool           m_fChecked;
    /** Holds whether button is hovered. */
    bool           m_fHovered;
    /** Holds the last mouse-press position. */
    QPoint         m_mousePressPosition;
};


/** QAccessibleWidget extension used as an accessibility interface for UIStatusBarEditor buttons. */
class UIAccessibilityInterfaceForUIStatusBarEditorButton : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject);

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    UIAccessibilityInterfaceForUIStatusBarEditorButton(QWidget *pWidget);

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE RT_FINAL;

    /** Returns the state. */
    virtual QAccessible::State state() const RT_OVERRIDE RT_FINAL;

private:

    /** Returns corresponding toolbar button. */
    UIStatusBarEditorButton *button() const { return qobject_cast<UIStatusBarEditorButton*>(widget()); }
};


/*********************************************************************************************************************************
*   Class UIAccessibilityInterfaceForUIStatusBarEditorButton implementation.                                                     *
*********************************************************************************************************************************/

/* static */
QAccessibleInterface *UIAccessibilityInterfaceForUIStatusBarEditorButton::pFactory(const QString &strClassname, QObject *pObject)
{
    /* Creating toolbar button accessibility interface: */
    if (   pObject
        && strClassname == QLatin1String("UIStatusBarEditorButton"))
        return new UIAccessibilityInterfaceForUIStatusBarEditorButton(qobject_cast<QWidget*>(pObject));

    /* Null by default: */
    return 0;
}

UIAccessibilityInterfaceForUIStatusBarEditorButton::UIAccessibilityInterfaceForUIStatusBarEditorButton(QWidget *pWidget)
    : QAccessibleWidget(pWidget, QAccessible::CheckBox)
{
}

QString UIAccessibilityInterfaceForUIStatusBarEditorButton::text(QAccessible::Text /* enmTextRole */) const
{
    /* Make sure view still alive: */
    AssertPtrReturn(button(), QString());

    /* Return view tool-tip: */
    return gpConverter->toString(button()->type());
}

QAccessible::State UIAccessibilityInterfaceForUIStatusBarEditorButton::state() const /* override */
{
    /* Prepare the button state: */
    QAccessible::State state;

    /* Make sure button still alive: */
    AssertPtrReturn(button(), state);

    /* Compose the button state: */
    state.checkable = true;
    state.checked = button()->isChecked();

    /* Return the button state: */
    return state;
}


/*********************************************************************************************************************************
*   Class UIStatusBarEditorButton implementation.                                                                                *
*********************************************************************************************************************************/

/* static */
const QString UIStatusBarEditorButton::MimeType = QString("application/virtualbox;value=IndicatorType");

UIStatusBarEditorButton::UIStatusBarEditorButton(IndicatorType enmType)
    : m_enmType(enmType)
    , m_fChecked(false)
    , m_fHovered(false)
{
    prepare();
}

bool UIStatusBarEditorButton::isChecked() const
{
    return m_fChecked;
}

void UIStatusBarEditorButton::setChecked(bool fChecked)
{
    /* Update 'checked' state: */
    m_fChecked = fChecked;
    /* Update: */
    update();
}

bool UIStatusBarEditorButton::event(QEvent *pEvent)
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

void UIStatusBarEditorButton::sltRetranslateUI()
{
    /* Translate tool-tip: */
    setToolTip(UIStatusBarEditorWidget::tr("<nobr><b>Click</b> to toggle indicator presence.</nobr><br>"
                                           "<nobr><b>Drag&Drop</b> to change indicator position.</nobr>"));
}

void UIStatusBarEditorButton::paintEvent(QPaintEvent *)
{
    /* Create style-painter: */
    QStylePainter painter(this);
    /* Prepare option set for check-box: */
    QStyleOptionButton option;
    option.initFrom(this);
    /* Use the size of 'this': */
    option.rect = QRect(0, 0, width(), height());
    /* But do not use hover bit of 'this' since
     * we already have another hovered-state representation: */
    if (option.state & QStyle::State_MouseOver)
        option.state &= ~QStyle::State_MouseOver;
    /* Remember checked-state: */
    if (m_fChecked)
        option.state |= QStyle::State_On;
    /* Draw check-box for hovered-state: */
    if (m_fHovered)
        painter.drawControl(QStyle::CE_CheckBox, option);
    /* Draw pixmap for unhovered-state: */
    else
    {
        QRect pixmapRect = QRect(QPoint(0, 0), m_pixmapSize);
        pixmapRect.moveCenter(option.rect.center());
        painter.drawItemPixmap(pixmapRect, Qt::AlignCenter, m_pixmap);
    }
}

void UIStatusBarEditorButton::mousePressEvent(QMouseEvent *pEvent)
{
    /* We are interested in left button only: */
    if (pEvent->button() != Qt::LeftButton)
        return;

    /* Remember mouse-press position: */
    const QPoint gPos = pEvent->globalPosition().toPoint();
    m_mousePressPosition = gPos;
}

void UIStatusBarEditorButton::mouseReleaseEvent(QMouseEvent *pEvent)
{
    /* We are interested in left button only: */
    if (pEvent->button() != Qt::LeftButton)
        return;

    /* Forget mouse-press position: */
    m_mousePressPosition = QPoint();

    /* Notify about click: */
    emit sigClick();
}

void UIStatusBarEditorButton::enterEvent(QEnterEvent*)
{
    /* Make sure button isn't hovered: */
    if (m_fHovered)
        return;

    /* Invert hovered state: */
    m_fHovered = true;
    /* Update: */
    update();
}

void UIStatusBarEditorButton::leaveEvent(QEvent *)
{
    /* Make sure button is hovered: */
    if (!m_fHovered)
        return;

    /* Invert hovered state: */
    m_fHovered = false;
    /* Update: */
    update();
}

void UIStatusBarEditorButton::mouseMoveEvent(QMouseEvent *pEvent)
{
    /* Make sure item isn't already dragged: */
    if (m_mousePressPosition.isNull())
        return QWidget::mouseMoveEvent(pEvent);

    /* Make sure item is really dragged: */
    const QPoint gPos = pEvent->globalPosition().toPoint();
    if (QLineF(gPos, m_mousePressPosition).length() <
        QApplication::startDragDistance())
        return QWidget::mouseMoveEvent(pEvent);

    /* Revoke hovered state: */
    m_fHovered = false;
    /* Update: */
    update();

    /* Initialize dragging: */
    m_mousePressPosition = QPoint();
    QDrag *pDrag = new QDrag(this);
    connect(pDrag, SIGNAL(destroyed(QObject*)), this, SIGNAL(sigDragObjectDestroy()));
    QMimeData *pMimeData = new QMimeData;
    pMimeData->setData(MimeType, gpConverter->toInternalString(m_enmType).toLatin1());
    pDrag->setMimeData(pMimeData);
    pDrag->setPixmap(m_pixmap);
    pDrag->exec();
}

void UIStatusBarEditorButton::prepare()
{
    /* Track mouse events: */
    setMouseTracking(true);

    /* Calculate icon size: */
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    m_pixmapSize = QSize(iIconMetric, iIconMetric);

    /* Cache button size-hint: */
    QStyleOptionButton option;
    option.initFrom(this);
    const QRect minRect = QApplication::style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option);
    m_size = m_pixmapSize.expandedTo(minRect.size());

    /* Update pixmap: */
    updatePixmap();

    /* Translate finally: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIStatusBarEditorButton::sltRetranslateUI);
}

void UIStatusBarEditorButton::updatePixmap()
{
    /* Recache pixmap for assigned type: */
    const QIcon icon = gpConverter->toIcon(m_enmType);
    const qreal fDevicePixelRatio = window() && window()->windowHandle() ? window()->windowHandle()->devicePixelRatio() : 1;
    m_pixmap = icon.pixmap(m_pixmapSize, fDevicePixelRatio);
}


/*********************************************************************************************************************************
*   Class UIStatusBarEditorWindow implementation.                                                                                *
*********************************************************************************************************************************/

UIStatusBarEditorWindow::UIStatusBarEditorWindow(UIMachineWindow *pParent)
    : UISlidingToolBar(pParent, pParent->statusBar(), new UIStatusBarEditorWidget(0, false, uiCommon().managedVMUuid()), UISlidingToolBar::Position_Bottom)
{
}


/*********************************************************************************************************************************
*   Class UIStatusBarEditorWidget implementation.                                                                                *
*********************************************************************************************************************************/

UIStatusBarEditorWidget::UIStatusBarEditorWidget(QWidget *pParent,
                                                 bool fStartedFromVMSettings /* = true */,
                                                 const QUuid &uMachineID /* = QString() */)
    : UIEditor(pParent)
    , m_fPrepared(false)
    , m_fStartedFromVMSettings(fStartedFromVMSettings)
    , m_uMachineID(uMachineID)
    , m_pMainLayout(0), m_pButtonLayout(0)
    , m_pButtonClose(0)
    , m_pCheckBoxEnable(0)
    , m_pButtonDropToken(0)
    , m_fDropAfterTokenButton(true)
{
    /* Prepare: */
    prepare();
}

void UIStatusBarEditorWidget::setMachineID(const QUuid &uMachineID)
{
    /* Remember new machine ID: */
    m_uMachineID = uMachineID;
    /* Prepare: */
    prepare();
}

bool UIStatusBarEditorWidget::isStatusBarEnabled() const
{
    /* For VM settings only: */
    AssertReturn(m_fStartedFromVMSettings, false);

    /* Acquire enable-checkbox if possible: */
    AssertPtrReturn(m_pCheckBoxEnable, false);
    return m_pCheckBoxEnable->isChecked();
}

void UIStatusBarEditorWidget::setStatusBarEnabled(bool fEnabled)
{
    /* For VM settings only: */
    AssertReturnVoid(m_fStartedFromVMSettings);

    /* Update enable-checkbox if possible: */
    AssertPtrReturnVoid(m_pCheckBoxEnable);
    m_pCheckBoxEnable->setChecked(fEnabled);
}

void UIStatusBarEditorWidget::setStatusBarConfiguration(const QList<IndicatorType> &restrictions,
                                                        const QList<IndicatorType> &order)
{
    /* Cache passed restrictions: */
    m_restrictions = restrictions;

    /* Cache passed order: */
    m_order = order;
    /* Append order with missed indicators: */
    for (int iType = IndicatorType_Invalid; iType < IndicatorType_Max; ++iType)
        if (iType != IndicatorType_Invalid && iType != IndicatorType_KeyboardExtension &&
            !m_order.contains((IndicatorType)iType))
            m_order << (IndicatorType)iType;

    /* Update configuration for all existing buttons: */
    foreach (const IndicatorType &enmType, m_order)
    {
        /* Get button: */
        UIStatusBarEditorButton *pButton = m_buttons.value(enmType);
        /* Make sure button exists: */
        if (!pButton)
            continue;
        /* Update button 'checked' state: */
        pButton->setChecked(!m_restrictions.contains(enmType));
        /* Make sure it have valid position: */
        const int iWantedIndex = position(enmType);
        const int iActualIndex = m_pButtonLayout->indexOf(pButton);
        if (iActualIndex != iWantedIndex)
        {
            /* Re-inject button into main-layout at proper position: */
            m_pButtonLayout->removeWidget(pButton);
            m_pButtonLayout->insertWidget(iWantedIndex, pButton);
        }
    }
}

void UIStatusBarEditorWidget::sltRetranslateUI()
{
    /* Translate widget itself: */
    setToolTip(tr("Allows to modify VM status-bar contents."));

    /* Translate close-button if necessary: */
    if (!m_fStartedFromVMSettings && m_pButtonClose)
        m_pButtonClose->setToolTip(tr("Close"));
    /* Translate enable-checkbox if necessary: */
    if (m_fStartedFromVMSettings && m_pCheckBoxEnable)
        m_pCheckBoxEnable->setToolTip(tr("Enable Status Bar"));
}

void UIStatusBarEditorWidget::paintEvent(QPaintEvent *)
{
    /* Prepare painter: */
    QPainter painter(this);

    /* Prepare palette colors: */
    const QPalette pal = QApplication::palette();
    QColor color0 = pal.color(QPalette::Window);
    QColor color1 = pal.color(QPalette::Window).lighter(110);
    color1.setAlpha(0);
    QColor color2 = pal.color(QPalette::Window).darker(200);
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    QColor color3 = pal.color(QPalette::Window).darker(120);
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

    /* Acquire metric: */
    const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4;

    /* Left corner: */
    QRadialGradient grad1(QPointF(iMetric, iMetric), iMetric);
    {
        grad1.setColorAt(0, color2);
        grad1.setColorAt(1, color1);
    }
    /* Right corner: */
    QRadialGradient grad2(QPointF(width() - iMetric, iMetric), iMetric);
    {
        grad2.setColorAt(0, color2);
        grad2.setColorAt(1, color1);
    }
    /* Top line: */
    QLinearGradient grad3(QPointF(iMetric, 0), QPointF(iMetric, iMetric));
    {
        grad3.setColorAt(0, color1);
        grad3.setColorAt(1, color2);
    }
    /* Left line: */
    QLinearGradient grad4(QPointF(0, iMetric), QPointF(iMetric, iMetric));
    {
        grad4.setColorAt(0, color1);
        grad4.setColorAt(1, color2);
    }
    /* Right line: */
    QLinearGradient grad5(QPointF(width(), iMetric), QPointF(width() - iMetric, iMetric));
    {
        grad5.setColorAt(0, color1);
        grad5.setColorAt(1, color2);
    }

    /* Paint shape/shadow: */
    painter.fillRect(QRect(iMetric, iMetric, width() - iMetric * 2, height() - iMetric), color0); // background
    painter.fillRect(QRect(0,                 0, iMetric, iMetric), grad1); // left corner
    painter.fillRect(QRect(width() - iMetric, 0, iMetric, iMetric), grad2); // right corner
    painter.fillRect(QRect(iMetric,           0, width() - iMetric * 2, iMetric), grad3); // bottom line
    painter.fillRect(QRect(0,                 iMetric, iMetric, height() - iMetric), grad4); // left line
    painter.fillRect(QRect(width() - iMetric, iMetric, iMetric, height() - iMetric), grad5); // right line

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /* Paint frames: */
    painter.save();
    painter.setPen(color3);
    painter.drawLine(QLine(QPoint(iMetric + 1,               iMetric + 1),
                           QPoint(width() - 1 - iMetric - 1, iMetric + 1)));
    painter.drawLine(QLine(QPoint(width() - 1 - iMetric - 1, iMetric + 1),
                           QPoint(width() - 1 - iMetric - 1, height() - 1)));
    painter.drawLine(QLine(QPoint(width() - 1 - iMetric - 1, height() - 1),
                           QPoint(iMetric + 1,               height() - 1)));
    painter.drawLine(QLine(QPoint(iMetric + 1,               height() - 1),
                           QPoint(iMetric + 1,               iMetric + 1)));
    painter.restore();
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

    /* Paint drop token: */
    if (m_pButtonDropToken)
    {
        QStyleOption option;
        option.state |= QStyle::State_Horizontal;
        const QRect geo = m_pButtonDropToken->geometry();
        option.rect = !m_fDropAfterTokenButton ?
                      QRect(geo.topLeft() - QPoint(iMetric, iMetric),
                            geo.bottomLeft() + QPoint(0, iMetric)) :
                      QRect(geo.topRight() - QPoint(0, iMetric),
                            geo.bottomRight() + QPoint(iMetric, iMetric));
        QApplication::style()->drawPrimitive(QStyle::PE_IndicatorToolBarSeparator,
                                             &option, &painter);
    }
}

void UIStatusBarEditorWidget::dragEnterEvent(QDragEnterEvent *pEvent)
{
    /* Make sure event is valid: */
    AssertPtrReturnVoid(pEvent);
    /* And mime-data is set: */
    const QMimeData *pMimeData = pEvent->mimeData();
    AssertPtrReturnVoid(pMimeData);
    /* Make sure mime-data format is valid: */
    if (!pMimeData->hasFormat(UIStatusBarEditorButton::MimeType))
        return;

    /* Accept drag-enter event: */
    pEvent->acceptProposedAction();
}

void UIStatusBarEditorWidget::dragMoveEvent(QDragMoveEvent *pEvent)
{
    /* Make sure event is valid: */
    AssertPtrReturnVoid(pEvent);
    /* And mime-data is set: */
    const QMimeData *pMimeData = pEvent->mimeData();
    AssertPtrReturnVoid(pMimeData);
    /* Make sure mime-data format is valid: */
    if (!pMimeData->hasFormat(UIStatusBarEditorButton::MimeType))
        return;

    /* Reset token: */
    m_pButtonDropToken = 0;
    m_fDropAfterTokenButton = true;

    /* Get event position: */
    const QPoint lPos = pEvent->position().toPoint();
    /* Search for most suitable button: */
    foreach (const IndicatorType &enmType, m_order)
    {
        m_pButtonDropToken = m_buttons.value(enmType);
        const QRect geo = m_pButtonDropToken->geometry();
        if (lPos.x() < geo.center().x())
        {
            m_fDropAfterTokenButton = false;
            break;
        }
    }
    /* Update: */
    update();
}

void UIStatusBarEditorWidget::dragLeaveEvent(QDragLeaveEvent *)
{
    /* Reset token: */
    m_pButtonDropToken = 0;
    m_fDropAfterTokenButton = true;
    /* Update: */
    update();
}

void UIStatusBarEditorWidget::dropEvent(QDropEvent *pEvent)
{
    /* Make sure event is valid: */
    AssertPtrReturnVoid(pEvent);
    /* And mime-data is set: */
    const QMimeData *pMimeData = pEvent->mimeData();
    AssertPtrReturnVoid(pMimeData);
    /* Make sure mime-data format is valid: */
    if (!pMimeData->hasFormat(UIStatusBarEditorButton::MimeType))
        return;

    /* Make sure token-button set: */
    if (!m_pButtonDropToken)
        return;

    /* Determine type of token-button: */
    const IndicatorType tokenType = m_pButtonDropToken->type();
    /* Determine type of dropped-button: */
    const QString strDroppedType =
        QString::fromLatin1(pMimeData->data(UIStatusBarEditorButton::MimeType));
    const IndicatorType droppedType =
        gpConverter->fromInternalString<IndicatorType>(strDroppedType);

    /* Make sure these types are different: */
    if (droppedType == tokenType)
        return;

    /* Remove type of dropped-button: */
    m_order.removeAll(droppedType);
    /* Insert type of dropped-button into position of token-button: */
    int iPosition = m_order.indexOf(tokenType);
    if (m_fDropAfterTokenButton)
        ++iPosition;
    m_order.insert(iPosition, droppedType);

    if (m_fStartedFromVMSettings)
    {
        /* Reapply status-bar configuration from cache: */
        setStatusBarConfiguration(m_restrictions, m_order);
    }
    else
    {
        /* Save updated status-bar indicator order: */
        gEDataManager->setStatusBarIndicatorOrder(m_order, machineID());
    }
}

void UIStatusBarEditorWidget::sltHandleConfigurationChange(const QUuid &uMachineID)
{
    /* Skip unrelated machine IDs: */
    if (machineID() != uMachineID)
        return;

    /* Recache status-bar configuration: */
    setStatusBarConfiguration(gEDataManager->restrictedStatusBarIndicators(machineID()),
                              gEDataManager->statusBarIndicatorOrder(machineID()));
}

void UIStatusBarEditorWidget::sltHandleButtonClick()
{
    /* Make sure sender is valid: */
    UIStatusBarEditorButton *pButton = qobject_cast<UIStatusBarEditorButton*>(sender());
    AssertPtrReturnVoid(pButton);

    /* Get sender type: */
    const IndicatorType enmType = pButton->type();

    /* Invert restriction for sender type: */
    if (m_restrictions.contains(enmType))
        m_restrictions.removeAll(enmType);
    else
        m_restrictions.append(enmType);

    if (m_fStartedFromVMSettings)
    {
        /* Reapply status-bar configuration from cache: */
        setStatusBarConfiguration(m_restrictions, m_order);
    }
    else
    {
        /* Save updated status-bar indicator restrictions: */
        gEDataManager->setRestrictedStatusBarIndicators(m_restrictions, machineID());
    }
}

void UIStatusBarEditorWidget::sltHandleDragObjectDestroy()
{
    /* Reset token: */
    m_pButtonDropToken = 0;
    m_fDropAfterTokenButton = true;
    /* Update: */
    update();
}

void UIStatusBarEditorWidget::prepare()
{
    /* Do nothing if already prepared: */
    if (m_fPrepared)
        return;

    /* Do not prepare if machine ID is not set: */
    if (m_uMachineID.isNull())
        return;

    /* Install tool-bar button accessibility interface factory: */
    QAccessible::installFactory(UIAccessibilityInterfaceForUIStatusBarEditorButton::pFactory);

    /* Track D&D events: */
    setAcceptDrops(true);

    /* Create main-layout: */
    m_pMainLayout = new QHBoxLayout(this);
    AssertPtrReturnVoid(m_pMainLayout);
    {
        /* Configure main-layout: */
        int iLeft, iTop, iRight, iBottom;
        m_pMainLayout->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
        /* Acquire metric: */
        const int iStandardMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 2;
        const int iMinimumMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4;
        /* Standard margins should not be too small/large: */
        iLeft   = iStandardMetric;
        iTop    = iStandardMetric;
        iRight  = iStandardMetric;
        iBottom = iStandardMetric;
        /* Bottom margin should be smaller for the common case: */
        if (iBottom >= iMinimumMetric)
            iBottom -= iMinimumMetric;
        /* Left margin should be bigger for the settings case: */
        if (m_fStartedFromVMSettings)
            iLeft += iMinimumMetric;
        /* Apply margins/spacing finally: */
        m_pMainLayout->setContentsMargins(iLeft, iTop, iRight, iBottom);
        m_pMainLayout->setSpacing(0);
        /* Create close-button if necessary: */
        if (!m_fStartedFromVMSettings)
        {
            m_pButtonClose = new QIToolButton;
            AssertPtrReturnVoid(m_pButtonClose);
            {
                /* Configure close-button: */
                m_pButtonClose->setFocusPolicy(Qt::StrongFocus);
                m_pButtonClose->setShortcut(Qt::Key_Escape);
                m_pButtonClose->setIcon(UIIconPool::iconSet(":/ok_16px.png"));
                connect(m_pButtonClose, SIGNAL(clicked(bool)), this, SIGNAL(sigCancelClicked()));
                /* Add close-button into main-layout: */
                m_pMainLayout->addWidget(m_pButtonClose);
            }
        }
        /* Create enable-checkbox if necessary: */
        else
        {
            m_pCheckBoxEnable = new QCheckBox;
            AssertPtrReturnVoid(m_pCheckBoxEnable);
            {
                /* Configure enable-checkbox: */
                m_pCheckBoxEnable->setFocusPolicy(Qt::StrongFocus);
                /* Add enable-checkbox into main-layout: */
                m_pMainLayout->addWidget(m_pCheckBoxEnable);
            }
        }
        /* Insert stretch: */
        m_pMainLayout->addStretch();
        /* Create button-layout: */
        m_pButtonLayout = new QHBoxLayout;
        AssertPtrReturnVoid(m_pButtonLayout);
        {
            /* Configure button-layout: */
            m_pButtonLayout->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
            m_pButtonLayout->setSpacing(5);
#else
            m_pButtonLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing) / 2);
#endif
            /* Add button-layout into main-layout: */
            m_pMainLayout->addLayout(m_pButtonLayout);
        }
        /* Prepare status buttons: */
        prepareStatusButtons();
    }

    /* Mark as prepared: */
    m_fPrepared = true;

    /* Translate contents: */
    sltRetranslateUI();
}

void UIStatusBarEditorWidget::prepareStatusButtons()
{
    /* Create status buttons: */
    for (int i = IndicatorType_Invalid; i < IndicatorType_Max; ++i)
    {
        /* Get current type: */
        const IndicatorType enmType = (IndicatorType)i;
        /* Skip inappropriate types: */
        if (enmType == IndicatorType_Invalid || enmType == IndicatorType_KeyboardExtension)
            continue;
        /* Create status button: */
        prepareStatusButton(enmType);
    }

    if (!m_fStartedFromVMSettings)
    {
        /* Cache status-bar configuration: */
        setStatusBarConfiguration(gEDataManager->restrictedStatusBarIndicators(machineID()),
                                  gEDataManager->statusBarIndicatorOrder(machineID()));
        /* And listen for the status-bar configuration changes after that: */
        connect(gEDataManager, &UIExtraDataManager::sigStatusBarConfigurationChange,
                this, &UIStatusBarEditorWidget::sltHandleConfigurationChange);
    }
}

void UIStatusBarEditorWidget::prepareStatusButton(IndicatorType enmType)
{
    /* Create status button: */
    UIStatusBarEditorButton *pButton = new UIStatusBarEditorButton(enmType);
    AssertPtrReturnVoid(pButton);
    {
        /* Configure status button: */
        connect(pButton, &UIStatusBarEditorButton::sigClick, this, &UIStatusBarEditorWidget::sltHandleButtonClick);
        connect(pButton, &UIStatusBarEditorButton::sigDragObjectDestroy, this, &UIStatusBarEditorWidget::sltHandleDragObjectDestroy);
        /* Add status button into button-layout: */
        m_pButtonLayout->addWidget(pButton);
        /* Insert status button into map: */
        m_buttons.insert(enmType, pButton);
    }
}

int UIStatusBarEditorWidget::position(IndicatorType enmType) const
{
    int iPosition = 0;
    foreach (const IndicatorType &iteratedType, m_order)
        if (iteratedType == enmType)
            return iPosition;
        else
            ++iPosition;
    return iPosition;
}


#include "UIStatusBarEditor.moc"
