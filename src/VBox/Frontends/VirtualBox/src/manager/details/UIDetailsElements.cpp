/* $Id: UIDetailsElements.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsElement[Name] classes implementation.
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
#include <QDir>
#include <QGraphicsLinearLayout>
#include <QTimer>

/* GUI includes: */
#include "UICommon.h"
#include "UIConverter.h"
#include "UIDetailsElements.h"
#include "UIDetailsGenerator.h"
#include "UIDetailsModel.h"
#include "UIErrorString.h"
#include "UIGraphicsRotatorButton.h"
#include "UIGraphicsTextPane.h"
#include "UIIconPool.h"
#include "UIMachinePreview.h"
#include "UIThreadPool.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CAudioAdapter.h"
#include "CMedium.h"
#include "CMediumAttachment.h"
#include "CNetworkAdapter.h"
#include "CRecordingScreenSettings.h"
#include "CRecordingSettings.h"
#include "CSerialPort.h"
#include "CSharedFolder.h"
#include "CStorageController.h"
#include "CSystemProperties.h"
#include "CUSBController.h"
#include "CUSBDeviceFilter.h"
#include "CUSBDeviceFilters.h"
#include "CVRDEServer.h"

UIDetailsUpdateTask::UIDetailsUpdateTask(const CMachine &comMachine)
    : UITask(UITask::Type_DetailsPopulation)
    , m_comMachine(comMachine)
{
}

UIDetailsUpdateTask::UIDetailsUpdateTask(const CCloudMachine &comCloudMachine)
    : UITask(UITask::Type_DetailsPopulation)
    , m_comCloudMachine(comCloudMachine)
{
}

CMachine UIDetailsUpdateTask::machine() const
{
    /* Acquire copy under a proper lock: */
    m_machineMutex.lock();
    const CMachine comMachine = m_comMachine;
    m_machineMutex.unlock();
    return comMachine;
}

CCloudMachine UIDetailsUpdateTask::cloudMachine() const
{
    /* Acquire copy under a proper lock: */
    m_machineMutex.lock();
    const CCloudMachine comCloudMachine = m_comCloudMachine;
    m_machineMutex.unlock();
    return comCloudMachine;
}

UITextTable UIDetailsUpdateTask::table() const
{
    /* Acquire copy under a proper lock: */
    m_tableMutex.lock();
    const UITextTable guiTable = m_guiTable;
    m_tableMutex.unlock();
    return guiTable;
}

void UIDetailsUpdateTask::setTable(const UITextTable &guiTable)
{
    /* Assign under a proper lock: */
    m_tableMutex.lock();
    m_guiTable = guiTable;
    m_tableMutex.unlock();
}

UIDetailsElementInterface::UIDetailsElementInterface(UIDetailsSet *pParent, DetailsElementType type, bool fOpened)
    : UIDetailsElement(pParent, type, fOpened)
    , m_pTask(0)
{
    /* Listen for the global thread-pool: */
    connect(uiCommon().threadPool(), &UIThreadPool::sigTaskComplete,
            this, &UIDetailsElementInterface::sltUpdateAppearanceFinished);

    /* Translate finally: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIDetailsElementInterface::sltRetranslateUI);
}

void UIDetailsElementInterface::sltRetranslateUI()
{
    /* Assign corresponding name: */
    setName(gpConverter->toString(elementType()));
}

void UIDetailsElementInterface::updateAppearance()
{
    /* Call to base-class: */
    UIDetailsElement::updateAppearance();

    /* Prepare/start update task: */
    if (!m_pTask)
    {
        /* Prepare update task: */
        m_pTask = createUpdateTask();
        /* Post task into global thread-pool: */
        uiCommon().threadPool()->enqueueTask(m_pTask);
    }
}

void UIDetailsElementInterface::sltUpdateAppearanceFinished(UITask *pTask)
{
    /* Make sure that is one of our tasks: */
    if (pTask->type() != UITask::Type_DetailsPopulation)
        return;

    /* Skip unrelated tasks: */
    if (m_pTask != pTask)
        return;

    /* Assign new text if changed: */
    const UITextTable newText = qobject_cast<UIDetailsUpdateTask*>(pTask)->table();
    if (text() != newText)
        setText(newText);

    /* Mark task processed: */
    m_pTask = 0;

    /* Notify listeners about update task complete: */
    emit sigBuildDone();
}


UIDetailsElementPreview::UIDetailsElementPreview(UIDetailsSet *pParent, bool fOpened)
    : UIDetailsElement(pParent, DetailsElementType_Preview, fOpened)
{
    /* Create preview: */
    m_pPreview = new UIMachinePreview(this);
    AssertPtr(m_pPreview);
    {
        /* Configure preview: */
        connect(m_pPreview, &UIMachinePreview::sigSizeHintChanged,
                this, &UIDetailsElementPreview::sltPreviewSizeHintChanged);
    }

    /* Translate finally: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIDetailsElementPreview::sltRetranslateUI);
}

void UIDetailsElementPreview::updateLayout()
{
    /* Call to base-class: */
    UIDetailsElement::updateLayout();

    /* Show/hide preview: */
    if ((isClosed() || isAnimationRunning()) && m_pPreview->isVisible())
        m_pPreview->hide();
    if (!isClosed() && !isAnimationRunning() && !m_pPreview->isVisible())
        m_pPreview->show();

    /* Layout Preview: */
    const int iMargin = data(ElementData_Margin).toInt();
    m_pPreview->setPos(iMargin, 2 * iMargin + minimumHeaderHeight());
    m_pPreview->resize(m_pPreview->minimumSizeHint());
}

void UIDetailsElementPreview::sltPreviewSizeHintChanged()
{
    /* Recursively update size-hints: */
    updateGeometry();
    /* Update whole model layout: */
    model()->updateLayout();
}

void UIDetailsElementPreview::sltRetranslateUI()
{
    /* Assign corresponding name: */
    setName(gpConverter->toString(elementType()));
}

int UIDetailsElementPreview::minimumWidthHint() const
{
    /* Prepare variables: */
    int iMargin = data(ElementData_Margin).toInt();

    /* Calculating proposed width: */
    int iProposedWidth = 0;

    /* Maximum between header width and preview width: */
    iProposedWidth += qMax(minimumHeaderWidth(), m_pPreview->minimumSizeHint().toSize().width());

    /* Two margins: */
    iProposedWidth += 2 * iMargin;

    /* Return result: */
    return iProposedWidth;
}

int UIDetailsElementPreview::minimumHeightHintForElement(bool fClosed) const
{
    /* Prepare variables: */
    int iMargin = data(ElementData_Margin).toInt();

    /* Calculating proposed height: */
    int iProposedHeight = 0;

    /* Two margins: */
    iProposedHeight += 2 * iMargin;

    /* Header height: */
    iProposedHeight += minimumHeaderHeight();

    /* Element is opened? */
    if (!fClosed)
    {
        iProposedHeight += iMargin;
        iProposedHeight += m_pPreview->minimumSizeHint().toSize().height();
    }
    else
    {
        /* Additional height during animation: */
        if (button()->isAnimationRunning())
            iProposedHeight += additionalHeight();
    }

    /* Return result: */
    return iProposedHeight;
}

void UIDetailsElementPreview::updateAppearance()
{
    /* Call to base-class: */
    UIDetailsElement::updateAppearance();

    /* Set new machine attribute directly: */
    m_pPreview->setMachine(machine());
    m_pPreview->resize(m_pPreview->minimumSizeHint());
    emit sigBuildDone();
}


void UIDetailsUpdateTaskGeneral::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationGeneral(comMachine, m_fOptions));
}

void UIDetailsUpdateTaskGeneralCloud::run()
{
    /* Acquire corresponding machine: */
    CCloudMachine comCloudMachine = cloudMachine();
    if (comCloudMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationGeneral(comCloudMachine, m_fOptions));
}

UITask *UIDetailsElementGeneral::createUpdateTask()
{
    return   isLocal()
           ? static_cast<UITask*>(new UIDetailsUpdateTaskGeneral(machine(), model()->optionsGeneral()))
           : static_cast<UITask*>(new UIDetailsUpdateTaskGeneralCloud(cloudMachine(), model()->optionsGeneral()));
}


void UIDetailsUpdateTaskSystem::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationSystem(comMachine, m_fOptions));
}

UITask *UIDetailsElementSystem::createUpdateTask()
{
    return new UIDetailsUpdateTaskSystem(machine(), model()->optionsSystem());
}


void UIDetailsUpdateTaskDisplay::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationDisplay(comMachine, m_fOptions));
}

UITask *UIDetailsElementDisplay::createUpdateTask()
{
    return new UIDetailsUpdateTaskDisplay(machine(), model()->optionsDisplay());
}


void UIDetailsUpdateTaskStorage::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationStorage(comMachine, m_fOptions));
}

UITask *UIDetailsElementStorage::createUpdateTask()
{
    return new UIDetailsUpdateTaskStorage(machine(), model()->optionsStorage());
}


void UIDetailsUpdateTaskAudio::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationAudio(comMachine, m_fOptions));
}

UITask *UIDetailsElementAudio::createUpdateTask()
{
    return new UIDetailsUpdateTaskAudio(machine(), model()->optionsAudio());
}

void UIDetailsUpdateTaskNetwork::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationNetwork(comMachine, m_fOptions));
}

UITask *UIDetailsElementNetwork::createUpdateTask()
{
    return new UIDetailsUpdateTaskNetwork(machine(), model()->optionsNetwork());
}

void UIDetailsUpdateTaskSerial::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationSerial(comMachine, m_fOptions));
}

UITask *UIDetailsElementSerial::createUpdateTask()
{
    return new UIDetailsUpdateTaskSerial(machine(), model()->optionsSerial());
}

void UIDetailsUpdateTaskUSB::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationUSB(comMachine, m_fOptions));
}

UITask *UIDetailsElementUSB::createUpdateTask()
{
    return new UIDetailsUpdateTaskUSB(machine(), model()->optionsUsb());
}


void UIDetailsUpdateTaskSF::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationSharedFolders(comMachine, m_fOptions));
}

UITask *UIDetailsElementSF::createUpdateTask()
{
    return new UIDetailsUpdateTaskSF(machine(), model()->optionsSharedFolders());
}


void UIDetailsUpdateTaskUI::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationUI(comMachine, m_fOptions));
}

UITask *UIDetailsElementUI::createUpdateTask()
{
    return new UIDetailsUpdateTaskUI(machine(), model()->optionsUserInterface());
}


void UIDetailsUpdateTaskDescription::run()
{
    /* Acquire corresponding machine: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;

    /* Generate details table: */
    setTable(UIDetailsGenerator::generateMachineInformationDescription(comMachine, m_fOptions));
}

UITask *UIDetailsElementDescription::createUpdateTask()
{
    return new UIDetailsUpdateTaskDescription(machine(), model()->optionsDescription());
}
