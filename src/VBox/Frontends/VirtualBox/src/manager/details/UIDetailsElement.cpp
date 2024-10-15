/* $Id: UIDetailsElement.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsElement class implementation.
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

/* Qt includes: */
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPropertyAnimation>
#include <QSignalTransition>
#include <QStateMachine>
#include <QStyleOptionGraphicsItem>
#include <QWindow>

/* GUI includes: */
#include "QIDialogContainer.h"
#include "UIActionPool.h"
#include "UIAudioControllerEditor.h"
#include "UIAudioHostDriverEditor.h"
#include "UIBaseMemoryEditor.h"
#include "UIBootOrderEditor.h"
#include "UICloudMachineSettingsDialogPage.h"
#include "UICloudNetworkingStuff.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIDetailsElement.h"
#include "UIDetailsGenerator.h"
#include "UIDetailsSet.h"
#include "UIDetailsModel.h"
#include "UIExtraDataManager.h"
#include "UIGraphicsControllerEditor.h"
#include "UIGraphicsRotatorButton.h"
#include "UIGraphicsTextPane.h"
#include "UIIconPool.h"
#include "UIMachineAttributeSetter.h"
#include "UIMediumTools.h"
#include "UINameAndSystemEditor.h"
#include "UINetworkAttachmentEditor.h"
#include "UITaskCloudGetSettingsForm.h"
#include "UIThreadPool.h"
#include "UIVideoMemoryEditor.h"
#include "UIVirtualBoxManager.h"
#include "UIVisualStateEditor.h"


/** Known anchor roles. */
enum AnchorRole
{
    AnchorRole_Invalid,
    AnchorRole_MachineName,
    AnchorRole_MachineLocation,
    AnchorRole_OSType,
    AnchorRole_BaseMemory,
    AnchorRole_BootOrder,
    AnchorRole_VideoMemory,
    AnchorRole_GraphicsControllerType,
    AnchorRole_Storage,
    AnchorRole_AudioHostDriverType,
    AnchorRole_AudioControllerType,
    AnchorRole_NetworkAttachmentType,
    AnchorRole_USBControllerType,
    AnchorRole_VisualStateType,
#ifndef VBOX_WS_MAC
    AnchorRole_MenuBar,
#endif
    AnchorRole_StatusBar,
#ifndef VBOX_WS_MAC
    AnchorRole_MiniToolbar,
#endif
    AnchorRole_Cloud,
};


UIDetailsElement::UIDetailsElement(UIDetailsSet *pParent, DetailsElementType enmType, bool fOpened)
    : UIDetailsItem(pParent)
    , m_pSet(pParent)
    , m_enmType(enmType)
    , m_iDefaultDarknessStart(100)
    , m_iDefaultDarknessFinal(105)
    , m_fHovered(false)
    , m_fNameHovered(false)
    , m_pHoveringMachine(0)
    , m_pHoveringAnimationForward(0)
    , m_pHoveringAnimationBackward(0)
    , m_iAnimationDuration(300)
    , m_iDefaultValue(0)
    , m_iHoveredValue(100)
    , m_iAnimatedValue(m_iDefaultValue)
    , m_pButton(0)
    , m_fClosed(!fOpened)
    , m_fAnimationRunning(false)
    , m_iAdditionalHeight(0)
    , m_pTextPane(0)
    , m_iMinimumHeaderWidth(0)
    , m_iMinimumHeaderHeight(0)
{
    /* Prepare element: */
    prepareElement();
    /* Prepare button: */
    prepareButton();
    /* Prepare text-pane: */
    prepareTextPane();

    /* Setup size-policy: */
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    /* Add item to the parent: */
    AssertMsg(parentItem(), ("No parent set for details element!"));
    parentItem()->addItem(this);
}

UIDetailsElement::~UIDetailsElement()
{
    /* Remove item from the parent: */
    AssertMsg(parentItem(), ("No parent set for details element!"));
    parentItem()->removeItem(this);
}

void UIDetailsElement::setText(const UITextTable &text)
{
    /* Pass text to text-pane: */
    m_pTextPane->setText(text);
}

UITextTable &UIDetailsElement::text() const
{
    /* Retrieve text from text-pane: */
    return m_pTextPane->text();
}

void UIDetailsElement::close(bool fAnimated /* = true */)
{
    m_pButton->setToggled(false, fAnimated);
}

void UIDetailsElement::open(bool fAnimated /* = true */)
{
    m_pButton->setToggled(true, fAnimated);
}

void UIDetailsElement::markAnimationFinished()
{
    /* Mark animation as non-running: */
    m_fAnimationRunning = false;

    /* Recursively update size-hint: */
    updateGeometry();
    /* Repaint: */
    update();
}

void UIDetailsElement::updateAppearance()
{
    /* Reset name hover state: */
    m_fNameHovered = false;
    updateNameHoverLink();

    /* Update anchor role restrictions: */
    const ConfigurationAccessLevel enmCal = m_pSet->configurationAccessLevel();
    m_pTextPane->setAnchorRoleRestricted("#machine_name",    enmCal != ConfigurationAccessLevel_Full
                                                          && enmCal != ConfigurationAccessLevel_Partial_Saved);
    m_pTextPane->setAnchorRoleRestricted("#machine_location", enmCal != ConfigurationAccessLevel_Full);
    m_pTextPane->setAnchorRoleRestricted("#os_type", enmCal != ConfigurationAccessLevel_Full);
    m_pTextPane->setAnchorRoleRestricted("#base_memory", enmCal != ConfigurationAccessLevel_Full);
    m_pTextPane->setAnchorRoleRestricted("#boot_order", enmCal != ConfigurationAccessLevel_Full);
    m_pTextPane->setAnchorRoleRestricted("#video_memory", enmCal != ConfigurationAccessLevel_Full);
    m_pTextPane->setAnchorRoleRestricted("#graphics_controller_type", enmCal != ConfigurationAccessLevel_Full);
    m_pTextPane->setAnchorRoleRestricted("#mount", enmCal == ConfigurationAccessLevel_Null);
    m_pTextPane->setAnchorRoleRestricted("#attach", enmCal != ConfigurationAccessLevel_Full);
    m_pTextPane->setAnchorRoleRestricted("#audio_host_driver_type",    enmCal != ConfigurationAccessLevel_Full
                                                                    && enmCal != ConfigurationAccessLevel_Partial_Saved);
    m_pTextPane->setAnchorRoleRestricted("#audio_controller_type", enmCal != ConfigurationAccessLevel_Full);
    m_pTextPane->setAnchorRoleRestricted("#network_attachment_type", enmCal == ConfigurationAccessLevel_Null);
    m_pTextPane->setAnchorRoleRestricted("#usb_controller_type", enmCal != ConfigurationAccessLevel_Full);
    m_pTextPane->setAnchorRoleRestricted("#visual_state", enmCal == ConfigurationAccessLevel_Null);
#ifndef VBOX_WS_MAC
    m_pTextPane->setAnchorRoleRestricted("#menu_bar", enmCal == ConfigurationAccessLevel_Null);
#endif
    m_pTextPane->setAnchorRoleRestricted("#status_bar", enmCal == ConfigurationAccessLevel_Null);
#ifndef VBOX_WS_MAC
    m_pTextPane->setAnchorRoleRestricted("#mini_toolbar", enmCal == ConfigurationAccessLevel_Null);
#endif
}

void UIDetailsElement::updateLayout()
{
    /* Prepare variables: */
    QSize size = geometry().size().toSize();
    int iMargin = data(ElementData_Margin).toInt();

    /* Layout button: */
    int iButtonWidth = m_buttonSize.width();
    int iButtonHeight = m_buttonSize.height();
    int iButtonX = size.width() - 2 * iMargin - iButtonWidth;
    int iButtonY = iButtonHeight == m_iMinimumHeaderHeight ? iMargin :
                   iMargin + (m_iMinimumHeaderHeight - iButtonHeight) / 2;
    m_pButton->setPos(iButtonX, iButtonY);

    /* If closed or animation running => hide: */
    if ((isClosed() || isAnimationRunning()) && m_pTextPane->isVisible())
        m_pTextPane->hide();
    /* If opened and animation isn't running => show: */
    else if (!isClosed() && !isAnimationRunning() && !m_pTextPane->isVisible())
        m_pTextPane->show();

    /* Layout text-pane: */
    int iTextPaneX = 2 * iMargin;
    int iTextPaneY = iMargin + m_iMinimumHeaderHeight + 2 * iMargin;
    m_pTextPane->setPos(iTextPaneX, iTextPaneY);
    m_pTextPane->resize(size.width() - 4 * iMargin,
                        size.height() - 4 * iMargin - m_iMinimumHeaderHeight);
}

int UIDetailsElement::minimumWidthHint() const
{
    /* Prepare variables: */
    int iMargin = data(ElementData_Margin).toInt();
    int iMinimumWidthHint = 0;

    /* Maximum width: */
    iMinimumWidthHint = qMax(m_iMinimumHeaderWidth, (int)m_pTextPane->minimumSizeHint().width());

    /* And 4 margins: 2 left and 2 right: */
    iMinimumWidthHint += 4 * iMargin;

    /* Return result: */
    return iMinimumWidthHint;
}

int UIDetailsElement::minimumHeightHint() const
{
    return minimumHeightHintForElement(m_fClosed);
}

void UIDetailsElement::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    UIDetailsItem::showEvent(pEvent);

    /* Update icon: */
    updateIcon();
}

void UIDetailsElement::resizeEvent(QGraphicsSceneResizeEvent*)
{
    /* Update layout: */
    updateLayout();
}

void UIDetailsElement::hoverMoveEvent(QGraphicsSceneHoverEvent *pEvent)
{
    /* Update hover state: */
    if (!m_fHovered)
    {
        m_fHovered = true;
        emit sigHoverEnter();
    }

    /* Update name-hover state: */
    handleHoverEvent(pEvent);
}

void UIDetailsElement::hoverLeaveEvent(QGraphicsSceneHoverEvent *pEvent)
{
    /* Update hover state: */
    if (m_fHovered)
    {
        m_fHovered = false;
        emit sigHoverLeave();
    }

    /* Update name-hover state: */
    handleHoverEvent(pEvent);
}

void UIDetailsElement::mousePressEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Only for hovered header: */
    if (!m_fNameHovered)
        return;

    /* Process link click: */
    pEvent->accept();
    QString strCategory;
    if (m_enmType >= DetailsElementType_General &&
        m_enmType < DetailsElementType_Description)
        strCategory = QString("#%1").arg(gpConverter->toInternalString(m_enmType));
    else if (m_enmType == DetailsElementType_Description)
        strCategory = QString("#%1%%m_pEditorDescription").arg(gpConverter->toInternalString(m_enmType));
    emit sigLinkClicked(strCategory, QString(), machine().GetId());
}

void UIDetailsElement::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Only for left-button: */
    if (pEvent->button() != Qt::LeftButton)
        return;

    /* Process left-button double-click: */
    emit sigToggleElement(m_enmType, isClosed());
}

void UIDetailsElement::paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget *)
{
    /* Update button visibility: */
    updateButtonVisibility();

    /* Paint background: */
    paintBackground(pPainter, pOptions);
    /* Paint element info: */
    paintElementInfo(pPainter, pOptions);
}

QString UIDetailsElement::description() const
{
    return tr("%1 details", "like 'General details' or 'Storage details'").arg(m_strName);
}

const CMachine &UIDetailsElement::machine()
{
    return m_pSet->machine();
}

const CCloudMachine &UIDetailsElement::cloudMachine()
{
    return m_pSet->cloudMachine();
}

bool UIDetailsElement::isLocal() const
{
    return m_pSet->isLocal();
}

void UIDetailsElement::setName(const QString &strName)
{
    /* Cache name: */
    m_strName = strName;
    const QFontMetrics fm(m_nameFont, model()->paintDevice());
    m_nameSize = QSize(fm.horizontalAdvance(m_strName), fm.height());

    /* Update linked values: */
    updateMinimumHeaderWidth();
    updateMinimumHeaderHeight();
}

void UIDetailsElement::setAdditionalHeight(int iAdditionalHeight)
{
    /* Cache new value: */
    m_iAdditionalHeight = iAdditionalHeight;
    /* Update layout: */
    updateLayout();
    /* Repaint: */
    update();
}

QVariant UIDetailsElement::data(int iKey) const
{
    /* Provide other members with required data: */
    switch (iKey)
    {
        /* Hints: */
        case ElementData_Margin: return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 4;
        case ElementData_Spacing: return QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 2;
        /* Default: */
        default: break;
    }
    return QVariant();
}

void UIDetailsElement::addItem(UIDetailsItem*)
{
    AssertMsgFailed(("Details element do NOT support children!"));
}

void UIDetailsElement::removeItem(UIDetailsItem*)
{
    AssertMsgFailed(("Details element do NOT support children!"));
}

QList<UIDetailsItem*> UIDetailsElement::items(UIDetailsItemType) const
{
    AssertMsgFailed(("Details element do NOT support children!"));
    return QList<UIDetailsItem*>();
}

bool UIDetailsElement::hasItems(UIDetailsItemType) const
{
    AssertMsgFailed(("Details element do NOT support children!"));
    return false;
}

void UIDetailsElement::clearItems(UIDetailsItemType)
{
    AssertMsgFailed(("Details element do NOT support children!"));
}

int UIDetailsElement::minimumHeightHintForElement(bool fClosed) const
{
    /* Prepare variables: */
    int iMargin = data(ElementData_Margin).toInt();
    int iMinimumHeightHint = 0;

    /* Two margins: */
    iMinimumHeightHint += 2 * iMargin;

    /* Header height: */
    iMinimumHeightHint += m_iMinimumHeaderHeight;

    /* Element is opened? */
    if (!fClosed)
    {
        /* Add text height: */
        if (!m_pTextPane->isEmpty())
            iMinimumHeightHint += 2 * iMargin + (int)m_pTextPane->minimumSizeHint().height();
    }

    /* Additional height during animation: */
    if (m_fAnimationRunning && isClosed())
        iMinimumHeightHint += m_iAdditionalHeight;

    /* Return value: */
    return iMinimumHeightHint;
}

void UIDetailsElement::sltHandleWindowRemapped()
{
    /* Update icon: */
    updateIcon();
}

void UIDetailsElement::sltToggleButtonClicked()
{
    emit sigToggleElement(m_enmType, isClosed());
}

void UIDetailsElement::sltElementToggleStart()
{
    /* Mark animation running: */
    m_fAnimationRunning = true;

    /* Setup animation: */
    updateAnimationParameters();

    /* Invert toggle-state instantly only for closed elements.
     * Opened element being closed should remain opened
     * until animation is fully finished. */
    if (m_fClosed)
        m_fClosed = !m_fClosed;
}

void UIDetailsElement::sltElementToggleFinish(bool fToggled)
{
    /* Update toggle-state: */
    m_fClosed = !fToggled;

    /* Notify about finishing: */
    emit sigToggleElementFinished();
}

void UIDetailsElement::sltHandleAnchorClicked(const QString &strAnchor)
{
    /* Compose a map of known anchor roles: */
    QMap<QString, AnchorRole> roles;
    roles["#machine_name"] = AnchorRole_MachineName;
    roles["#machine_location"] = AnchorRole_MachineLocation;
    roles["#os_type"] = AnchorRole_OSType;
    roles["#base_memory"] = AnchorRole_BaseMemory;
    roles["#boot_order"] = AnchorRole_BootOrder;
    roles["#video_memory"] = AnchorRole_VideoMemory;
    roles["#graphics_controller_type"] = AnchorRole_GraphicsControllerType;
    roles["#mount"] = AnchorRole_Storage;
    roles["#attach"] = AnchorRole_Storage;
    roles["#audio_host_driver_type"] = AnchorRole_AudioHostDriverType;
    roles["#audio_controller_type"] = AnchorRole_AudioControllerType;
    roles["#network_attachment_type"] = AnchorRole_NetworkAttachmentType;
    roles["#usb_controller_type"] = AnchorRole_USBControllerType;
    roles["#visual_state"] = AnchorRole_VisualStateType;
#ifndef VBOX_WS_MAC
    roles["#menu_bar"] = AnchorRole_MenuBar;
#endif
    roles["#status_bar"] = AnchorRole_StatusBar;
#ifndef VBOX_WS_MAC
    roles["#mini_toolbar"] = AnchorRole_MiniToolbar;
#endif
    roles["#cloud"] = AnchorRole_Cloud;

    /* Current anchor role: */
    const QString strRole = strAnchor.section(',', 0, 0);
    const QString strData = strAnchor.section(',', 1);

    /* Handle known anchor roles: */
    const AnchorRole enmRole = roles.value(strRole, AnchorRole_Invalid);
    switch (enmRole)
    {
        case AnchorRole_MachineName:
        case AnchorRole_MachineLocation:
        case AnchorRole_OSType:
        {
            popupNameAndSystemEditor(enmRole == AnchorRole_MachineName /* choose name? */,
                                     enmRole == AnchorRole_MachineLocation /* choose path? */,
                                     enmRole == AnchorRole_OSType /* choose type? */,
                                     strData.section(',', 0, 0) /* value */);
            break;
        }
        case AnchorRole_BaseMemory:
        {
            popupBaseMemoryEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
        case AnchorRole_BootOrder:
        {
            popupBootOrderEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
        case AnchorRole_VideoMemory:
        {
            popupVideoMemoryEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
        case AnchorRole_GraphicsControllerType:
        {
            popupGraphicsControllerTypeEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
        case AnchorRole_Storage:
        {
            popupStorageEditor(strData /* complex value */);
            break;
        }
        case AnchorRole_AudioHostDriverType:
        {
            popupAudioHostDriverTypeEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
        case AnchorRole_AudioControllerType:
        {
            popupAudioControllerTypeEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
        case AnchorRole_NetworkAttachmentType:
        {
            popupNetworkAttachmentTypeEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
        case AnchorRole_USBControllerType:
        {
            popupUSBControllerTypeEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
        case AnchorRole_VisualStateType:
        {
            popupVisualStateTypeEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
#ifndef VBOX_WS_MAC
        case AnchorRole_MenuBar:
        {
            popupMenuBarEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
#endif
        case AnchorRole_StatusBar:
        {
            popupStatusBarEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
#ifndef VBOX_WS_MAC
        case AnchorRole_MiniToolbar:
        {
            popupMiniToolbarEditor(strData.section(',', 0, 0) /* value */);
            break;
        }
#endif
        case AnchorRole_Cloud:
        {
            popupCloudEditor(strData /* complex value */);
            break;
        }
        default:
            break;
    }
}

void UIDetailsElement::sltHandleCopyRequest()
{
    /* Acquire sender: */
    QObject *pSender = sender();
    AssertPtrReturnVoid(pSender);

    /* Acquire clipboard: */
    QClipboard *pClipboard = QGuiApplication::clipboard();
    AssertPtrReturnVoid(pClipboard);
    pClipboard->setText(pSender->property("contents").toString());
}

void UIDetailsElement::sltHandleEditRequest()
{
    /* Acquire sender: */
    QObject *pSender = sender();
    AssertPtrReturnVoid(pSender);

    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Acquire cloud machine: */
        CCloudMachine comCloudMachine = cloudMachine();

        /* Prepare editor: */
        UISafePointerCloudMachineSettingsDialogPage pEditor = new UICloudMachineSettingsDialogPage(pPopup,
                                                                                                   false /* full-scale? */);
        if (pEditor)
        {
            /* Configure editor: */
            connect(pEditor.data(), &UICloudMachineSettingsDialogPage::sigValidChanged,
                    pPopup.data(), &QIDialogContainer::setProgressBarHidden);
            connect(pEditor.data(), &UICloudMachineSettingsDialogPage::sigValidChanged,
                    pPopup.data(), &QIDialogContainer::setOkButtonEnabled);
            pEditor->setFilter(pSender->property("filter").toString());
            /* Create get settings form task: */
            UITaskCloudGetSettingsForm *pTask = new UITaskCloudGetSettingsForm(comCloudMachine);
            /* Create get settings form receiver: */
            UIReceiverCloudGetSettingsForm *pReceiver = new UIReceiverCloudGetSettingsForm(pEditor);
            if (pReceiver)
            {
                connect(pReceiver, &UIReceiverCloudGetSettingsForm::sigTaskComplete,
                        pEditor.data(), &UICloudMachineSettingsDialogPage::setForm);
                connect(pReceiver, &UIReceiverCloudGetSettingsForm::sigTaskFailed,
                        pPopup.data(), &QIDialogContainer::close);
            }
            /* Start task: */
            if (pTask && pReceiver)
                uiCommon().threadPoolCloud()->enqueueTask(pTask);
            /* Embed editor: */
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->resize(pPopup->minimumSizeHint());

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
        {
            /* Makes sure page data committed: */
            if (pEditor)
                pEditor->makeSureDataCommitted();

            /* Apply form: */
            CForm comForm = pEditor->form();
            applyCloudMachineSettingsForm(comCloudMachine, comForm, gpNotificationCenter);
        }

        /* Delete popup: */
        delete pPopup;
    }
}

void UIDetailsElement::sltMountStorageMedium()
{
    /* Sender action: */
    QAction *pAction = qobject_cast<QAction*>(sender());
    AssertMsgReturnVoid(pAction, ("This slot should only be called by menu action!\n"));

    /* Current mount-target: */
    const UIMediumTarget target = pAction->data().value<UIMediumTarget>();

    /* Update current machine mount-target: */
    UIMediumTools::updateMachineStorage(machine(), target, gpManager->actionPool());
}

void UIDetailsElement::prepareElement()
{
    /* Initialization: */
    m_nameFont = font();
    m_nameFont.setWeight(QFont::Bold);
    m_textFont = font();

    /* Update icon: */
    updateIcon();

    /* Create hovering animation machine: */
    m_pHoveringMachine = new QStateMachine(this);
    if (m_pHoveringMachine)
    {
        /* Create 'default' state: */
        QState *pStateDefault = new QState(m_pHoveringMachine);
        /* Create 'hovered' state: */
        QState *pStateHovered = new QState(m_pHoveringMachine);

        /* Configure 'default' state: */
        if (pStateDefault)
        {
            /* When we entering default state => we assigning animatedValue to m_iDefaultValue: */
            pStateDefault->assignProperty(this, "animatedValue", m_iDefaultValue);

            /* Add state transition: */
            QSignalTransition *pDefaultToHovered = pStateDefault->addTransition(this, SIGNAL(sigHoverEnter()), pStateHovered);
            if (pDefaultToHovered)
            {
                /* Create forward animation: */
                m_pHoveringAnimationForward = new QPropertyAnimation(this, "animatedValue", this);
                if (m_pHoveringAnimationForward)
                {
                    m_pHoveringAnimationForward->setDuration(m_iAnimationDuration);
                    m_pHoveringAnimationForward->setStartValue(m_iDefaultValue);
                    m_pHoveringAnimationForward->setEndValue(m_iHoveredValue);

                    /* Add to transition: */
                    pDefaultToHovered->addAnimation(m_pHoveringAnimationForward);
                }
            }
        }

        /* Configure 'hovered' state: */
        if (pStateHovered)
        {
            /* When we entering hovered state => we assigning animatedValue to m_iHoveredValue: */
            pStateHovered->assignProperty(this, "animatedValue", m_iHoveredValue);

            /* Add state transition: */
            QSignalTransition *pHoveredToDefault = pStateHovered->addTransition(this, SIGNAL(sigHoverLeave()), pStateDefault);
            if (pHoveredToDefault)
            {
                /* Create backward animation: */
                m_pHoveringAnimationBackward = new QPropertyAnimation(this, "animatedValue", this);
                if (m_pHoveringAnimationBackward)
                {
                    m_pHoveringAnimationBackward->setDuration(m_iAnimationDuration);
                    m_pHoveringAnimationBackward->setStartValue(m_iHoveredValue);
                    m_pHoveringAnimationBackward->setEndValue(m_iDefaultValue);

                    /* Add to transition: */
                    pHoveredToDefault->addAnimation(m_pHoveringAnimationBackward);
                }
            }
        }

        /* Initial state is 'default': */
        m_pHoveringMachine->setInitialState(pStateDefault);
        /* Start state-machine: */
        m_pHoveringMachine->start();
    }

    /* Configure connections: */
    connect(gpManager, &UIVirtualBoxManager::sigWindowRemapped,
            this, &UIDetailsElement::sltHandleWindowRemapped);
    connect(this, &UIDetailsElement::sigToggleElement,
            model(), &UIDetailsModel::sltToggleElements);
    connect(this, &UIDetailsElement::sigLinkClicked,
            model(), &UIDetailsModel::sigLinkClicked);
}

void UIDetailsElement::prepareButton()
{
    /* Setup toggle-button: */
    m_pButton = new UIGraphicsRotatorButton(this, "additionalHeight", !m_fClosed, true /* reflected */);
    m_pButton->setAutoHandleButtonClick(false);
    connect(m_pButton, &UIGraphicsRotatorButton::sigButtonClicked, this, &UIDetailsElement::sltToggleButtonClicked);
    connect(m_pButton, &UIGraphicsRotatorButton::sigRotationStart, this, &UIDetailsElement::sltElementToggleStart);
    connect(m_pButton, &UIGraphicsRotatorButton::sigRotationFinish, this, &UIDetailsElement::sltElementToggleFinish);
    m_buttonSize = m_pButton->minimumSizeHint().toSize();
}

void UIDetailsElement::prepareTextPane()
{
    /* Create text-pane: */
    m_pTextPane = new UIGraphicsTextPane(this, model()->paintDevice());
    connect(m_pTextPane, &UIGraphicsTextPane::sigGeometryChanged, this, &UIDetailsElement::sltUpdateGeometry);
    connect(m_pTextPane, &UIGraphicsTextPane::sigAnchorClicked, this, &UIDetailsElement::sltHandleAnchorClicked);
}

void UIDetailsElement::updateIcon()
{
    /* Prepare whole icon first of all: */
    const QIcon icon = gpConverter->toIcon(elementType());

    /* Cache icon: */
    if (icon.isNull())
    {
        /* No icon provided: */
        m_pixmapSize = QSize();
        m_pixmap = QPixmap();
    }
    else
    {
        /* Determine default icon size: */
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_pixmapSize = QSize(iIconMetric, iIconMetric);
        /* Acquire the icon of corresponding size (taking top-level widget DPI into account): */
        const qreal fDevicePixelRatio = gpManager->windowHandle() ? gpManager->windowHandle()->devicePixelRatio() : 1;
        m_pixmap = icon.pixmap(m_pixmapSize, fDevicePixelRatio);
    }

    /* Update linked values: */
    updateMinimumHeaderWidth();
    updateMinimumHeaderHeight();
}

void UIDetailsElement::handleHoverEvent(QGraphicsSceneHoverEvent *pEvent)
{
    /* Not for 'preview' element type: */
    if (m_enmType == DetailsElementType_Preview)
        return;

    /* Prepare variables: */
    int iMargin = data(ElementData_Margin).toInt();
    int iSpacing = data(ElementData_Spacing).toInt();
    int iNameHeight = m_nameSize.height();
    int iElementNameX = 2 * iMargin + m_pixmapSize.width() + iSpacing;
    int iElementNameY = iNameHeight == m_iMinimumHeaderHeight ?
                        iMargin : iMargin + (m_iMinimumHeaderHeight - iNameHeight) / 2;

    /* Simulate hyperlink hovering: */
    QPoint point = pEvent->pos().toPoint();
    bool fNameHovered = QRect(QPoint(iElementNameX, iElementNameY), m_nameSize).contains(point);
    if (   m_pSet->configurationAccessLevel() != ConfigurationAccessLevel_Null
        && m_fNameHovered != fNameHovered)
    {
        m_fNameHovered = fNameHovered;
        updateNameHoverLink();
    }
}

void UIDetailsElement::updateNameHoverLink()
{
    if (m_fNameHovered)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
    update();
}

void UIDetailsElement::updateAnimationParameters()
{
    /* Recalculate animation parameters: */
    int iOpenedHeight = minimumHeightHintForElement(false);
    int iClosedHeight = minimumHeightHintForElement(true);
    int iAdditionalHeight = iOpenedHeight - iClosedHeight;
    if (m_fClosed)
        m_iAdditionalHeight = 0;
    else
        m_iAdditionalHeight = iAdditionalHeight;
    m_pButton->setAnimationRange(0, iAdditionalHeight);
}

void UIDetailsElement::updateButtonVisibility()
{
    if (m_fHovered && !m_pButton->isVisible())
        m_pButton->show();
    else if (!m_fHovered && m_pButton->isVisible())
        m_pButton->hide();
}

void UIDetailsElement::popupNameAndSystemEditor(bool fChooseName, bool fChoosePath, bool fChooseType, const QString &strValue)
{
    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Prepare editor: */
        UINameAndSystemEditor *pEditor = new UINameAndSystemEditor(pPopup,
                                                                   fChooseName,
                                                                   fChoosePath,
                                                                   false /* edition? */,
                                                                   false /* image? */,
                                                                   fChooseType);
        if (pEditor)
        {
            if (fChooseName)
                pEditor->setName(strValue);
            else if (fChoosePath)
                pEditor->setPath(strValue);
            else if (fChooseType)
                pEditor->setGuestOSTypeByTypeId(strValue);

            /* Add to popup: */
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->adjustSize();

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
        {
            if (fChooseName)
                setMachineAttribute(machine(), MachineAttribute_Name, QVariant::fromValue(pEditor->name()));
            else if (fChooseType)
                setMachineAttribute(machine(), MachineAttribute_OSType, QVariant::fromValue(pEditor->typeId()));
            else if (fChoosePath)
                setMachineLocation(machine().GetId(), pEditor->path());
        }

        /* Delete popup: */
        delete pPopup;
    }
}

void UIDetailsElement::popupBaseMemoryEditor(const QString &strValue)
{
    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Prepare editor: */
        UIBaseMemoryEditor *pEditor = new UIBaseMemoryEditor(pPopup);
        if (pEditor)
        {
            pEditor->setValue(strValue.toInt());
            connect(pEditor, &UIBaseMemoryEditor::sigValidChanged,
                    pPopup.data(), &QIDialogContainer::setOkButtonEnabled);
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->adjustSize();

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
            setMachineAttribute(machine(), MachineAttribute_BaseMemory, QVariant::fromValue(pEditor->value()));

        /* Delete popup: */
        delete pPopup;
    }
}

void UIDetailsElement::popupBootOrderEditor(const QString &strValue)
{
    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Prepare editor: */
        UIBootOrderEditor *pEditor = new UIBootOrderEditor(pPopup);
        if (pEditor)
        {
            pEditor->setValue(bootItemsFromSerializedString(strValue));
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->adjustSize();

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
            setMachineAttribute(machine(), MachineAttribute_BootOrder, QVariant::fromValue(pEditor->value()));

        /* Delete popup: */
        delete pPopup;
    }
}

void UIDetailsElement::popupVideoMemoryEditor(const QString &strValue)
{
    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Prepare editor: */
        UIVideoMemoryEditor *pEditor = new UIVideoMemoryEditor(pPopup);
        if (pEditor)
        {
            pEditor->setValue(strValue.toInt());
            connect(pEditor, &UIVideoMemoryEditor::sigValidChanged,
                    pPopup.data(), &QIDialogContainer::setOkButtonEnabled);
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->adjustSize();

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
            setMachineAttribute(machine(), MachineAttribute_VideoMemory, QVariant::fromValue(pEditor->value()));

        /* Delete popup: */
        delete pPopup;
    }
}

void UIDetailsElement::popupGraphicsControllerTypeEditor(const QString &strValue)
{
    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Prepare editor: */
        UIGraphicsControllerEditor *pEditor = new UIGraphicsControllerEditor(pPopup);
        if (pEditor)
        {
            pEditor->setValue(static_cast<KGraphicsControllerType>(strValue.toInt()));
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->adjustSize();

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
            setMachineAttribute(machine(), MachineAttribute_GraphicsControllerType, QVariant::fromValue(pEditor->value()));

        /* Delete popup: */
        delete pPopup;
    }
}

void UIDetailsElement::popupStorageEditor(const QString &strValue)
{
    /* Prepare storage-menu: */
    UIMenu menu;
    menu.setShowToolTip(true);

    /* Storage-controller name: */
    QString strControllerName = strValue.section(',', 0, 0);
    /* Storage-slot: */
    StorageSlot storageSlot = gpConverter->fromString<StorageSlot>(strValue.section(',', 1));

    /* Fill storage-menu: */
    UIMediumTools::prepareStorageMenu(&menu, this, SLOT(sltMountStorageMedium()),
                                      machine(), strControllerName, storageSlot);

    /* Exec menu: */
    menu.exec(QCursor::pos());
}

void UIDetailsElement::popupAudioHostDriverTypeEditor(const QString &strValue)
{
    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Prepare editor: */
        UIAudioHostDriverEditor *pEditor = new UIAudioHostDriverEditor(pPopup);
        if (pEditor)
        {
            pEditor->setValue(static_cast<KAudioDriverType>(strValue.toInt()));
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->adjustSize();

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
            setMachineAttribute(machine(), MachineAttribute_AudioHostDriverType, QVariant::fromValue(pEditor->value()));

        /* Delete popup: */
        delete pPopup;
    }
}

void UIDetailsElement::popupAudioControllerTypeEditor(const QString &strValue)
{
    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Prepare editor: */
        UIAudioControllerEditor *pEditor = new UIAudioControllerEditor(pPopup);
        if (pEditor)
        {
            pEditor->setValue(static_cast<KAudioControllerType>(strValue.toInt()));
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->adjustSize();

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
            setMachineAttribute(machine(), MachineAttribute_AudioControllerType, QVariant::fromValue(pEditor->value()));

        /* Delete popup: */
        delete pPopup;
    }
}

void UIDetailsElement::popupNetworkAttachmentTypeEditor(const QString &strValue)
{
    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Prepare editor: */
        UINetworkAttachmentEditor *pEditor = new UINetworkAttachmentEditor(pPopup);
        if (pEditor)
        {
            pEditor->setValueNames(KNetworkAttachmentType_Bridged, UINetworkAttachmentEditor::bridgedAdapters());
            pEditor->setValueNames(KNetworkAttachmentType_Internal, UINetworkAttachmentEditor::internalNetworks());
            pEditor->setValueNames(KNetworkAttachmentType_HostOnly, UINetworkAttachmentEditor::hostInterfaces());
            pEditor->setValueNames(KNetworkAttachmentType_Generic, UINetworkAttachmentEditor::genericDrivers());
            pEditor->setValueNames(KNetworkAttachmentType_NATNetwork, UINetworkAttachmentEditor::natNetworks());
#ifdef VBOX_WITH_CLOUD_NET
            pEditor->setValueNames(KNetworkAttachmentType_Cloud, UINetworkAttachmentEditor::cloudNetworks());
#endif
#ifdef VBOX_WITH_VMNET
            pEditor->setValueNames(KNetworkAttachmentType_HostOnlyNetwork, UINetworkAttachmentEditor::hostOnlyNetworks());
#endif
            pEditor->setValueType(static_cast<KNetworkAttachmentType>(strValue.section(';', 1, 1).toInt()));
            pEditor->setValueName(pEditor->valueType(), strValue.section(';', 2, 2));
            connect(pEditor, &UINetworkAttachmentEditor::sigValidChanged,
                    pPopup.data(), &QIDialogContainer::setOkButtonEnabled);
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->adjustSize();

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
        {
            UINetworkAdapterDescriptor nad(strValue.section(';', 0, 0).toInt(),
                                           pEditor->valueType(), pEditor->valueName(pEditor->valueType()));
            setMachineAttribute(machine(), MachineAttribute_NetworkAttachmentType, QVariant::fromValue(nad));
        }

        /* Delete popup: */
        delete pPopup;
    }
}

void UIDetailsElement::popupUSBControllerTypeEditor(const QString &strValue)
{
    /* Parse controller type list: */
    UIUSBControllerTypeSet controllerSet;
    const QStringList controllerInternals = strValue.split(';');
    foreach (const QString &strControllerType, controllerInternals)
    {
        /* Parse each internal controller description: */
        bool fParsed = false;
        KUSBControllerType enmType = static_cast<KUSBControllerType>(strControllerType.toInt(&fParsed));
        if (!fParsed)
            enmType = KUSBControllerType_Null;
        controllerSet << enmType;
    }

    /* Prepare existing controller sets: */
    QMap<int, UIUSBControllerTypeSet> controllerSets;
    controllerSets[0] = UIUSBControllerTypeSet();
    controllerSets[1] = UIUSBControllerTypeSet() << KUSBControllerType_OHCI;
    controllerSets[2] = UIUSBControllerTypeSet() << KUSBControllerType_OHCI << KUSBControllerType_EHCI;
    controllerSets[3] = UIUSBControllerTypeSet() << KUSBControllerType_XHCI;

    /* Fill menu with actions: */
    UIMenu menu;
    QActionGroup group(&menu);
    QMap<int, QAction*> actions;
    actions[0] = menu.addAction(QApplication::translate("UIDetails", "Disabled", "details (usb)"));
    group.addAction(actions.value(0));
    actions.value(0)->setCheckable(true);
    actions[1] = menu.addAction(QApplication::translate("UIDetails", "USB 1.1 (OHCI) Controller", "details (usb)"));
    group.addAction(actions.value(1));
    actions.value(1)->setCheckable(true);
    actions[2] = menu.addAction(QApplication::translate("UIDetails", "USB 2.0 (OHCI + EHCI) Controller", "details (usb)"));
    group.addAction(actions.value(2));
    actions.value(2)->setCheckable(true);
    actions[3] = menu.addAction(QApplication::translate("UIDetails", "USB 3.0 (xHCI) Controller", "details (usb)"));
    group.addAction(actions.value(3));
    actions.value(3)->setCheckable(true);

    /* Mark current one: */
    for (int i = 0; i <= 3; ++i)
        actions.value(i)->setChecked(controllerSets.key(controllerSet) == i);

    /* Execute menu, look for result: */
    QAction *pTriggeredAction = menu.exec(QCursor::pos());
    if (pTriggeredAction)
    {
        const int iTriggeredIndex = actions.key(pTriggeredAction);
        if (controllerSets.key(controllerSet) != iTriggeredIndex)
            setMachineAttribute(machine(), MachineAttribute_USBControllerType, QVariant::fromValue(controllerSets.value(iTriggeredIndex)));
    }
}

void UIDetailsElement::popupVisualStateTypeEditor(const QString &strValue)
{
    /* Prepare popup: */
    QPointer<QIDialogContainer> pPopup = new QIDialogContainer(0, Qt::Tool);
    if (pPopup)
    {
        /* Prepare editor: */
        UIVisualStateEditor *pEditor = new UIVisualStateEditor(pPopup);
        if (pEditor)
        {
            pEditor->setMachineId(machine().GetId());
            pEditor->setValue(static_cast<UIVisualStateType>(strValue.toInt()));
            pPopup->setWidget(pEditor);
        }

        /* Adjust popup geometry: */
        pPopup->move(QCursor::pos());
        pPopup->adjustSize();

        // WORKAROUND:
        // On Windows, Tool dialogs aren't activated by default by some reason.
        // So we have created sltActivateWindow wrapping actual activateWindow
        // to fix that annoying issue.
        QMetaObject::invokeMethod(pPopup, "sltActivateWindow", Qt::QueuedConnection);
        /* Execute popup, change machine name if confirmed: */
        if (pPopup->exec() == QDialog::Accepted)
            gEDataManager->setRequestedVisualState(pEditor->value(), machine().GetId());

        /* Delete popup: */
        delete pPopup;
    }
}

#ifndef VBOX_WS_MAC
void UIDetailsElement::popupMenuBarEditor(const QString &strValue)
{
    /* Parse whether we have it enabled, true if unable to parse: */
    bool fParsed = false;
    bool fEnabled = strValue.toInt(&fParsed);
    if (!fParsed)
        fEnabled = true;

    /* Fill menu with actions: */
    UIMenu menu;
    QActionGroup group(&menu);
    QAction *pActionDisable = menu.addAction(QApplication::translate("UIDetails", "Disabled", "details (user interface/menu-bar)"));
    group.addAction(pActionDisable);
    pActionDisable->setCheckable(true);
    pActionDisable->setChecked(!fEnabled);
    QAction *pActionEnable = menu.addAction(QApplication::translate("UIDetails", "Enabled", "details (user interface/menu-bar)"));
    group.addAction(pActionEnable);
    pActionEnable->setCheckable(true);
    pActionEnable->setChecked(fEnabled);

    /* Execute menu, look for result: */
    QAction *pTriggeredAction = menu.exec(QCursor::pos());
    if (   pTriggeredAction
        && (   (fEnabled && pTriggeredAction == pActionDisable)
            || (!fEnabled && pTriggeredAction == pActionEnable)))
    {
        gEDataManager->setMenuBarEnabled(!fEnabled, machine().GetId());
    }
}
#endif

void UIDetailsElement::popupStatusBarEditor(const QString &strValue)
{
    /* Parse whether we have it enabled, true if unable to parse: */
    bool fParsed = false;
    bool fEnabled = strValue.toInt(&fParsed);
    if (!fParsed)
        fEnabled = true;

    /* Fill menu with actions: */
    UIMenu menu;
    QActionGroup group(&menu);
    QAction *pActionDisable = menu.addAction(QApplication::translate("UIDetails", "Disabled", "details (user interface/status-bar)"));
    group.addAction(pActionDisable);
    pActionDisable->setCheckable(true);
    pActionDisable->setChecked(!fEnabled);
    QAction *pActionEnable = menu.addAction(QApplication::translate("UIDetails", "Enabled", "details (user interface/status-bar)"));
    group.addAction(pActionEnable);
    pActionEnable->setCheckable(true);
    pActionEnable->setChecked(fEnabled);

    /* Execute menu, look for result: */
    QAction *pTriggeredAction = menu.exec(QCursor::pos());
    if (   pTriggeredAction
        && (   (fEnabled && pTriggeredAction == pActionDisable)
            || (!fEnabled && pTriggeredAction == pActionEnable)))
    {
        gEDataManager->setStatusBarEnabled(!fEnabled, machine().GetId());
    }
}

#ifndef VBOX_WS_MAC
void UIDetailsElement::popupMiniToolbarEditor(const QString &strValue)
{
    /* Parse whether we have it enabled: */
    bool fParsed = false;
    MiniToolbarAlignment enmAlignment = static_cast<MiniToolbarAlignment>(strValue.toInt(&fParsed));
    if (!fParsed)
        enmAlignment = MiniToolbarAlignment_Disabled;

    /* Fill menu with actions: */
    UIMenu menu;
    QActionGroup group(&menu);
    QAction *pActionDisabled = menu.addAction(QApplication::translate("UIDetails", "Disabled", "details (user interface/mini-toolbar)"));
    group.addAction(pActionDisabled);
    pActionDisabled->setCheckable(true);
    pActionDisabled->setChecked(enmAlignment == MiniToolbarAlignment_Disabled);
    QAction *pActionTop = menu.addAction(QApplication::translate("UIDetails", "Top", "details (user interface/mini-toolbar position)"));
    group.addAction(pActionTop);
    pActionTop->setCheckable(true);
    pActionTop->setChecked(enmAlignment == MiniToolbarAlignment_Top);
    QAction *pActionBottom = menu.addAction(QApplication::translate("UIDetails", "Bottom", "details (user interface/mini-toolbar position)"));
    group.addAction(pActionBottom);
    pActionBottom->setCheckable(true);
    pActionBottom->setChecked(enmAlignment == MiniToolbarAlignment_Bottom);

    /* Execute menu, look for result: */
    QAction *pTriggeredAction = menu.exec(QCursor::pos());
    if (pTriggeredAction)
    {
        const QUuid uMachineId = machine().GetId();
        if (pTriggeredAction == pActionDisabled)
            gEDataManager->setMiniToolbarEnabled(false, uMachineId);
        else if (pTriggeredAction == pActionTop)
        {
            gEDataManager->setMiniToolbarEnabled(true, uMachineId);
            gEDataManager->setMiniToolbarAlignment(Qt::AlignTop, uMachineId);
        }
        else if (pTriggeredAction == pActionBottom)
        {
            gEDataManager->setMiniToolbarEnabled(true, uMachineId);
            gEDataManager->setMiniToolbarAlignment(Qt::AlignBottom, uMachineId);
        }
    }
}
#endif

void UIDetailsElement::popupCloudEditor(const QString &strValue)
{
    /* Prepare cloud-menu: */
    UIMenu menu;
    menu.setShowToolTip(true);

    /* Acquire cloud machine: */
    CCloudMachine comCloudMachine = cloudMachine();
    /* Acquire details form: */
    CForm comForm = comCloudMachine.GetDetailsForm();
    /* Ignore cloud machine errors: */
    if (comCloudMachine.isOk())
    {
        /* For each form value: */
        foreach (const CFormValue &comIteratedValue, comForm.GetValues())
        {
            /* Acquire label: */
            const QString &strIteratedLabel = comIteratedValue.GetLabel();
            if (strIteratedLabel != strValue)
                continue;

            /* Acquire resulting value in short and full form: */
            const QString strIteratedResultShort = UIDetailsGenerator::generateFormValueInformation(comIteratedValue);
            const QString strIteratedResultFull = UIDetailsGenerator::generateFormValueInformation(comIteratedValue, true /* full */);

            /* Add 'Copy' action: */
            QAction *pAction = menu.addAction(tr("Copy value (%1)").arg(strIteratedResultShort),
                                              this, &UIDetailsElement::sltHandleCopyRequest);
            if (pAction)
            {
                pAction->setToolTip(strIteratedResultFull);
                pAction->setProperty("contents", strIteratedResultFull);
            }

            /* Add 'Edit' action: */
            if (comIteratedValue.GetEnabled())
            {
                QAction *pAction = menu.addAction(tr("Edit value..."),
                                                  this, &UIDetailsElement::sltHandleEditRequest);
                if (pAction)
                    pAction->setProperty("filter", strIteratedLabel);
            }

            /* Quit prematurely: */
            break;
        }
    }

    /* Exec menu: */
    menu.exec(QCursor::pos());
}

void UIDetailsElement::updateMinimumHeaderWidth()
{
    /* Prepare variables: */
    int iSpacing = data(ElementData_Spacing).toInt();

    /* Update minimum-header-width: */
    m_iMinimumHeaderWidth = m_pixmapSize.width() +
                            iSpacing + m_nameSize.width() +
                            iSpacing + m_buttonSize.width();
}

void UIDetailsElement::updateMinimumHeaderHeight()
{
    /* Update minimum-header-height: */
    m_iMinimumHeaderHeight = qMax(m_pixmapSize.height(), m_nameSize.height());
    m_iMinimumHeaderHeight = qMax(m_iMinimumHeaderHeight, m_buttonSize.height());
}

void UIDetailsElement::paintBackground(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions) const
{
    /* Save painter: */
    pPainter->save();

    /* Prepare variables: */
    const int iMargin = data(ElementData_Margin).toInt();
    const int iHeadHeight = 2 * iMargin + m_iMinimumHeaderHeight;
    const QRect optionRect = pOptions->rect;
    const QRect headRect = QRect(optionRect.topLeft(), QSize(optionRect.width(), iHeadHeight));
    const QRect fullRect = m_fAnimationRunning
                         ? QRect(optionRect.topLeft(), QSize(optionRect.width(), iHeadHeight + m_iAdditionalHeight))
                         : optionRect;

    /* Acquire background color: */
    QColor backgroundColor = QApplication::palette().color(QPalette::Active, QPalette::Window);

    /* Paint default background: */
    QLinearGradient gradientDefault(fullRect.topLeft(), fullRect.bottomRight());
    gradientDefault.setColorAt(0, backgroundColor.darker(m_iDefaultDarknessStart));
    gradientDefault.setColorAt(1, backgroundColor.darker(m_iDefaultDarknessFinal));
    pPainter->fillRect(fullRect, gradientDefault);

    /* If element is hovered: */
    if (animatedValue())
    {
        /* Acquire header color: */
        QColor headColor = backgroundColor.lighter(130);

        /* Paint hovered background: */
        QColor hcTone1 = headColor;
        QColor hcTone2 = headColor;
        hcTone1.setAlpha(255 * animatedValue() / 100);
        hcTone2.setAlpha(0);
        QLinearGradient gradientHovered(headRect.topLeft(), headRect.bottomLeft());
        gradientHovered.setColorAt(0, hcTone1);
        gradientHovered.setColorAt(1, hcTone2);
        pPainter->fillRect(headRect, gradientHovered);
    }

    /* Restore painter: */
    pPainter->restore();
}

void UIDetailsElement::paintElementInfo(QPainter *pPainter, const QStyleOptionGraphicsItem *) const
{
    /* Initialize some necessary variables: */
    const int iMargin = data(ElementData_Margin).toInt();
    const int iSpacing = data(ElementData_Spacing).toInt();

    /* Calculate attributes: */
    const int iPixmapHeight = m_pixmapSize.height();
    const int iNameHeight = m_nameSize.height();
    const int iMaximumHeight = qMax(iPixmapHeight, iNameHeight);

    /* Prepare color: */
    const QPalette pal = QApplication::palette();
    const QColor buttonTextColor = pal.color(QPalette::Active, QPalette::Text);
    const QColor linkTextColor = pal.color(QPalette::Active, QPalette::Link);

    /* Paint pixmap: */
    int iElementPixmapX = 2 * iMargin;
    int iElementPixmapY = iPixmapHeight == iMaximumHeight ?
                          iMargin : iMargin + (iMaximumHeight - iPixmapHeight) / 2;
    paintPixmap(/* Painter: */
                pPainter,
                /* Rectangle to paint in: */
                QRect(QPoint(iElementPixmapX, iElementPixmapY), m_pixmapSize),
                /* Pixmap to paint: */
                m_pixmap);

    /* Paint name: */
    int iMachineNameX = iElementPixmapX +
                        m_pixmapSize.width() +
                        iSpacing;
    int iMachineNameY = iNameHeight == iMaximumHeight ?
                        iMargin : iMargin + (iMaximumHeight - iNameHeight) / 2;
    paintText(/* Painter: */
              pPainter,
              /* Rectangle to paint in: */
              QPoint(iMachineNameX, iMachineNameY),
              /* Font to paint text: */
              m_nameFont,
              /* Paint device: */
              model()->paintDevice(),
              /* Text to paint: */
              m_strName,
              /* Name hovered? */
              m_fNameHovered ? linkTextColor : buttonTextColor);
}

/* static */
void UIDetailsElement::paintPixmap(QPainter *pPainter, const QRect &rect, const QPixmap &pixmap)
{
    pPainter->drawPixmap(rect, pixmap);
}

/* static */
void UIDetailsElement::paintText(QPainter *pPainter, QPoint point,
                                 const QFont &font, QPaintDevice *pPaintDevice,
                                 const QString &strText, const QColor &color)
{
    /* Prepare variables: */
    QFontMetrics fm(font, pPaintDevice);
    point += QPoint(0, fm.ascent());

    /* Draw text: */
    pPainter->save();
    pPainter->setFont(font);
    pPainter->setPen(color);
    pPainter->drawText(point, strText);
    pPainter->restore();
}
