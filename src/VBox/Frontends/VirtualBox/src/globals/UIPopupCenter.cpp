/* $Id: UIPopupCenter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupCenter class implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

/* GUI includes: */
#include "QIMessageBox.h"
#include "UIErrorString.h"
#include "UIExtraDataManager.h"
#include "UIHostComboEditor.h"
#include "UILoggingDefs.h"
#include "UIPopupCenter.h"
#include "UIPopupStack.h"

/* COM includes: */
#include "CAudioAdapter.h"
#include "CConsole.h"
#include "CEmulatedUSB.h"
#include "CMachine.h"
#include "CNetworkAdapter.h"
#include "CVRDEServer.h"

/* Other VBox includes: */
#include <VBox/sup.h>


/* static */
UIPopupCenter* UIPopupCenter::s_pInstance = 0;
UIPopupCenter* UIPopupCenter::instance() { return s_pInstance; }

/* static */
void UIPopupCenter::create()
{
    /* Make sure instance is NOT created yet: */
    if (s_pInstance)
        return;

    /* Create instance: */
    new UIPopupCenter;
    /* Prepare instance: */
    s_pInstance->prepare();
}

/* static */
void UIPopupCenter::destroy()
{
    /* Make sure instance is NOT destroyed yet: */
    if (!s_pInstance)
        return;

    /* Cleanup instance: */
    s_pInstance->cleanup();
    /* Destroy instance: */
    delete s_pInstance;
}

UIPopupCenter::UIPopupCenter()
{
    /* Assign instance: */
    s_pInstance = this;
}

UIPopupCenter::~UIPopupCenter()
{
    /* Unassign instance: */
    s_pInstance = 0;
}

void UIPopupCenter::prepare()
{
}

void UIPopupCenter::cleanup()
{
    /* Make sure all the popup-stack types destroyed: */
    foreach (const QString &strTypeID, m_stackTypes.keys())
        m_stackTypes.remove(strTypeID);
    /* Make sure all the popup-stacks destroyed: */
    foreach (const QString &strID, m_stacks.keys())
    {
        delete m_stacks[strID];
        m_stacks.remove(strID);
    }
}

void UIPopupCenter::showPopupStack(QWidget *pParent)
{
    /* Make sure parent is set! */
    AssertPtrReturnVoid(pParent);

    /* Make sure corresponding popup-stack *exists*: */
    const QString strPopupStackID(popupStackID(pParent));
    if (!m_stacks.contains(strPopupStackID))
        return;

    /* Assign stack with passed parent: */
    UIPopupStack *pPopupStack = m_stacks[strPopupStackID];
    assignPopupStackParent(pPopupStack, pParent, m_stackTypes[strPopupStackID]);
    pPopupStack->show();
}

void UIPopupCenter::hidePopupStack(QWidget *pParent)
{
    /* Make sure parent is set! */
    AssertPtrReturnVoid(pParent);

    /* Make sure corresponding popup-stack *exists*: */
    const QString strPopupStackID(popupStackID(pParent));
    if (!m_stacks.contains(strPopupStackID))
        return;

    /* Unassign stack with passed parent: */
    UIPopupStack *pPopupStack = m_stacks[strPopupStackID];
    pPopupStack->hide();
    unassignPopupStackParent(pPopupStack, pParent);
}

void UIPopupCenter::setPopupStackType(QWidget *pParent, UIPopupStackType enmType)
{
    /* Make sure parent is set! */
    AssertPtrReturnVoid(pParent);

    /* Composing corresponding popup-stack ID: */
    const QString strPopupStackID(popupStackID(pParent));

    /* Looking for current popup-stack type, create if it doesn't exists: */
    UIPopupStackType &enmCurrentType = m_stackTypes[strPopupStackID];

    /* Make sure stack-type has changed: */
    if (enmCurrentType == enmType)
        return;

    /* Remember new stack type: */
    LogRelFlow(("UIPopupCenter::setPopupStackType: Changing type of popup-stack with ID = '%s' from '%s' to '%s'.\n",
                strPopupStackID.toLatin1().constData(),
                enmCurrentType == UIPopupStackType_Separate ? "separate window" : "embedded widget",
                enmType == UIPopupStackType_Separate ? "separate window" : "embedded widget"));
    enmCurrentType = enmType;
}

void UIPopupCenter::setPopupStackOrientation(QWidget *pParent, UIPopupStackOrientation newStackOrientation)
{
    /* Make sure parent is set! */
    AssertPtrReturnVoid(pParent);

    /* Composing corresponding popup-stack ID: */
    const QString strPopupStackID(popupStackID(pParent));

    /* Looking for current popup-stack orientation, create if it doesn't exists: */
    UIPopupStackOrientation &stackOrientation = m_stackOrientations[strPopupStackID];

    /* Make sure stack-orientation has changed: */
    if (stackOrientation == newStackOrientation)
        return;

    /* Remember new stack orientation: */
    LogRelFlow(("UIPopupCenter::setPopupStackType: Changing orientation of popup-stack with ID = '%s' from '%s' to '%s'.\n",
                strPopupStackID.toLatin1().constData(),
                stackOrientation == UIPopupStackOrientation_Top ? "top oriented" : "bottom oriented",
                newStackOrientation == UIPopupStackOrientation_Top ? "top oriented" : "bottom oriented"));
    stackOrientation = newStackOrientation;

    /* Update orientation for popup-stack if it currently exists: */
    if (m_stacks.contains(strPopupStackID))
        m_stacks[strPopupStackID]->setOrientation(stackOrientation);
}

void UIPopupCenter::message(QWidget *pParent, const QString &strID,
                            const QString &strMessage, const QString &strDetails,
                            const QString &strButtonText1 /* = QString() */,
                            const QString &strButtonText2 /* = QString() */,
                            bool fProposeAutoConfirmation /* = false */)
{
    showPopupPane(pParent, strID,
                  strMessage, strDetails,
                  strButtonText1, strButtonText2,
                  fProposeAutoConfirmation);
}

void UIPopupCenter::popup(QWidget *pParent, const QString &strID,
                          const QString &strMessage)
{
    message(pParent, strID, strMessage, QString());
}

void UIPopupCenter::alert(QWidget *pParent, const QString &strID,
                          const QString &strMessage,
                          bool fProposeAutoConfirmation /* = false */)
{
    message(pParent, strID, strMessage, QString(),
            QApplication::translate("UIMessageCenter", "Close") /* 1st button text */,
            QString() /* 2nd button text */,
            fProposeAutoConfirmation);
}

void UIPopupCenter::alertWithDetails(QWidget *pParent, const QString &strID,
                                     const QString &strMessage,
                                     const QString &strDetails,
                                     bool fProposeAutoConfirmation /* = false */)
{
    message(pParent, strID, strMessage, strDetails,
            QApplication::translate("UIMessageCenter", "Close") /* 1st button text */,
            QString() /* 2nd button text */,
            fProposeAutoConfirmation);
}

void UIPopupCenter::question(QWidget *pParent, const QString &strID,
                             const QString &strMessage,
                             const QString &strButtonText1 /* = QString() */,
                             const QString &strButtonText2 /* = QString() */,
                             bool fProposeAutoConfirmation /* = false */)
{
    message(pParent, strID, strMessage, QString(),
            strButtonText1, strButtonText2,
            fProposeAutoConfirmation);
}

void UIPopupCenter::recall(QWidget *pParent, const QString &strID)
{
    hidePopupPane(pParent, strID);
}

void UIPopupCenter::showPopupPane(QWidget *pParent, const QString &strID,
                                  const QString &strMessage, const QString &strDetails,
                                  QString strButtonText1 /* = QString() */, QString strButtonText2 /* = QString() */,
                                  bool fProposeAutoConfirmation /* = false */)
{
    /* Make sure parent is set! */
    AssertPtrReturnVoid(pParent);

    /* Prepare buttons: */
    int iButton1 = 0;
    int iButton2 = 0;
    /* Make sure single button is properly configured: */
    if (!strButtonText1.isEmpty() && strButtonText2.isEmpty())
        iButton1 = AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape;
    else if (strButtonText1.isEmpty() && !strButtonText2.isEmpty())
        iButton2 = AlertButton_Cancel | AlertButtonOption_Default | AlertButtonOption_Escape;
    /* Make sure buttons are unique if set both: */
    else if (!strButtonText1.isEmpty() && !strButtonText2.isEmpty())
    {
        iButton1 = AlertButton_Ok | AlertButtonOption_Default;
        iButton2 = AlertButton_Cancel | AlertButtonOption_Escape;
        /* If user made a mistake in button names, we will fix that: */
        if (strButtonText1 == strButtonText2)
        {
            strButtonText1 = QApplication::translate("UIMessageCenter", "Ok");
            strButtonText1 = QApplication::translate("UIMessageCenter", "Cancel");
        }
    }

    /* Check if popup-pane was auto-confirmed before: */
    if ((iButton1 || iButton2) && fProposeAutoConfirmation)
    {
        const QStringList confirmedPopupList = gEDataManager->suppressedMessages();
        if (   confirmedPopupList.contains(strID)
            || confirmedPopupList.contains("allPopupPanes")
            || confirmedPopupList.contains("all") )
        {
            int iResultCode = AlertOption_AutoConfirmed;
            if (iButton1 & AlertButtonOption_Default)
                iResultCode |= (iButton1 & AlertButtonMask);
            else if (iButton2 & AlertButtonOption_Default)
                iResultCode |= (iButton2 & AlertButtonMask);
            emit sigPopupPaneDone(strID, iResultCode);
            return;
        }
    }

    /* Looking for corresponding popup-stack: */
    const QString strPopupStackID(popupStackID(pParent));
    UIPopupStack *pPopupStack = 0;
    /* If there is already popup-stack with such ID: */
    if (m_stacks.contains(strPopupStackID))
    {
        /* Just get existing one: */
        pPopupStack = m_stacks[strPopupStackID];
    }
    /* If there is no popup-stack with such ID: */
    else
    {
        /* Create new one: */
        pPopupStack = m_stacks[strPopupStackID] = new UIPopupStack(strPopupStackID, m_stackOrientations[strPopupStackID]);
        /* Attach popup-stack connections: */
        connect(pPopupStack, &UIPopupStack::sigPopupPaneDone, this, &UIPopupCenter::sltPopupPaneDone);
        connect(pPopupStack, &UIPopupStack::sigRemove,        this, &UIPopupCenter::sltRemovePopupStack);
    }

    /* If there is already popup-pane with such ID: */
    if (pPopupStack->exists(strID))
    {
        /* Just update existing one: */
        pPopupStack->updatePopupPane(strID, strMessage, strDetails);
    }
    /* If there is no popup-pane with such ID: */
    else
    {
        /* Compose button description map: */
        QMap<int, QString> buttonDescriptions;
        if (iButton1 != 0)
            buttonDescriptions[iButton1] = strButtonText1;
        if (iButton2 != 0)
            buttonDescriptions[iButton2] = strButtonText2;
        if (fProposeAutoConfirmation)
            buttonDescriptions[AlertButton_Cancel | AlertOption_AutoConfirmed] = QString();
        /* Create new one: */
        pPopupStack->createPopupPane(strID, strMessage, strDetails, buttonDescriptions);
    }

    /* Show popup-stack: */
    showPopupStack(pParent);
}

void UIPopupCenter::hidePopupPane(QWidget *pParent, const QString &strID)
{
    /* Make sure parent is set! */
    AssertPtrReturnVoid(pParent);

    /* Make sure corresponding popup-stack *exists*: */
    const QString strPopupStackID(popupStackID(pParent));
    if (!m_stacks.contains(strPopupStackID))
        return;

    /* Make sure corresponding popup-pane *exists*: */
    UIPopupStack *pPopupStack = m_stacks[strPopupStackID];
    if (!pPopupStack->exists(strID))
        return;

    /* Recall corresponding popup-pane: */
    pPopupStack->recallPopupPane(strID);
}

void UIPopupCenter::sltPopupPaneDone(QString strID, int iResultCode)
{
    /* Remember auto-confirmation fact (if necessary): */
    if (iResultCode & AlertOption_AutoConfirmed)
        gEDataManager->setSuppressedMessages(gEDataManager->suppressedMessages() << strID);

    /* Notify listeners: */
    emit sigPopupPaneDone(strID, iResultCode);
}

void UIPopupCenter::sltRemovePopupStack(QString strID)
{
    /* Make sure corresponding popup-stack *exists*: */
    if (!m_stacks.contains(strID))
    {
        AssertMsgFailed(("Popup-stack already destroyed!\n"));
        return;
    }

    /* Delete popup-stack asyncronously.
     * To avoid issues with events which already posted: */
    UIPopupStack *pPopupStack = m_stacks[strID];
    m_stacks.remove(strID);
    pPopupStack->deleteLater();
}

/* static */
QString UIPopupCenter::popupStackID(QWidget *pParent)
{
    /* Make sure parent is set! */
    AssertPtrReturn(pParent, QString());

    /* Special handling for Runtime UI: */
    if (pParent->inherits("UIMachineWindow"))
        return QString("UIMachineWindow");

    /* Common handling for other cases: */
    return pParent->metaObject()->className();
}

/* static */
void UIPopupCenter::assignPopupStackParent(UIPopupStack *pPopupStack, QWidget *pParent, UIPopupStackType enmStackType)
{
    /* Make sure parent is set! */
    AssertPtrReturnVoid(pParent);

    /* Assign event-filter: */
    pParent->window()->installEventFilter(pPopupStack);

    /* Assign parent depending on passed *stack* type: */
    switch (enmStackType)
    {
        case UIPopupStackType_Embedded:
        {
            pPopupStack->setParent(pParent);
            break;
        }
        case UIPopupStackType_Separate:
        {
            pPopupStack->setParent(pParent, Qt::Tool | Qt::FramelessWindowHint);
            break;
        }
        default: break;
    }
}

/* static */
void UIPopupCenter::unassignPopupStackParent(UIPopupStack *pPopupStack, QWidget *pParent)
{
    /* Make sure parent is set! */
    AssertPtrReturnVoid(pParent);

    /* Unassign parent: */
    pPopupStack->setParent(0);

    /* Unassign event-filter: */
    pParent->window()->removeEventFilter(pPopupStack);
}
