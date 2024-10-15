/* $Id: UIWizardNewCloudVM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewCloudVM class implementation.
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

/* GUI includes: */
#include "UICloudMachineManager.h"
#include "UINotificationCenter.h"
#include "UIWizardNewCloudVM.h"
#include "UIWizardNewCloudVMPageSource.h"
#include "UIWizardNewCloudVMPageProperties.h"
#include "UIWizardNewCloudVMPageExpert.h"

/* COM includes: */
#include "CCloudMachine.h"


UIWizardNewCloudVM::UIWizardNewCloudVM(QWidget *pParent,
                                       const QString &strFullGroupName /* = QString() */)
    : UINativeWizard(pParent, WizardType_NewCloudVM)
{
#ifndef VBOX_WS_MAC
    /* Assign watermark: */
    setPixmapName(":/wizard_new_cloud_vm.png");
#else
    /* Assign background image: */
    setPixmapName(":/wizard_new_cloud_vm_bg.png");
#endif

    /* Parse passed full group name: */
    const QString strProviderShortName = strFullGroupName.section('/', 1, 1);
    const QString strProfileName = strFullGroupName.section('/', 2, 2);
    if (!strProviderShortName.isEmpty() && !strProfileName.isEmpty())
    {
        m_strProviderShortName = strProviderShortName;
        m_strProfileName = strProfileName;
    }
}

void UIWizardNewCloudVM::createVSDForm()
{
    /* Acquire prepared client and description: */
    CCloudClient comClient = client();
    CVirtualSystemDescription comVSD = vsd();
    AssertReturnVoid(comClient.isNotNull() && comVSD.isNotNull());

    /* Create launch VSD form: */
    UINotificationProgressLaunchVSDFormCreate *pNotification = new UINotificationProgressLaunchVSDFormCreate(comClient,
                                                                                                             comVSD,
                                                                                                             providerShortName(),
                                                                                                             profileName());
    connect(pNotification, &UINotificationProgressLaunchVSDFormCreate::sigVSDFormCreated,
            this, &UIWizardNewCloudVM::setVSDForm);
    handleNotificationProgressNow(pNotification);
}

bool UIWizardNewCloudVM::createCloudVM()
{
    /* Prepare result: */
    bool fResult = false;

    /* Acquire prepared client and description: */
    CCloudClient comClient = client();
    CVirtualSystemDescription comVSD = vsd();
    AssertReturn(comClient.isNotNull() && comVSD.isNotNull(), false);

    /* Initiate cloud VM creation procedure: */
    CCloudMachine comMachine;

    /* Create cloud VM: */
    UINotificationProgressCloudMachineCreate *pNotification = new UINotificationProgressCloudMachineCreate(comClient,
                                                                                                           comMachine,
                                                                                                           comVSD,
                                                                                                           providerShortName(),
                                                                                                           profileName());
    connect(pNotification, &UINotificationProgressCloudMachineCreate::sigCloudMachineCreated,
            gpCloudMachineManager, &UICloudMachineManager::sltHandleCloudMachineAdded);
    gpNotificationCenter->append(pNotification);

    /* Positive: */
    fResult = true;

    /* Return result: */
    return fResult;
}

void UIWizardNewCloudVM::populatePages()
{
    /* Create corresponding pages: */
    switch (mode())
    {
        case WizardMode_Basic:
        {
            addPage(new UIWizardNewCloudVMPageSource);
            addPage(new UIWizardNewCloudVMPageProperties);
            break;
        }
        case WizardMode_Expert:
        {
            addPage(new UIWizardNewCloudVMPageExpert);
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid mode: %d", mode()));
            break;
        }
    }
}

void UIWizardNewCloudVM::sltRetranslateUI()
{
    /* Call to base-class: */
    UINativeWizard::sltRetranslateUI();

    /* Translate wizard: */
    setWindowTitle(tr("Create Cloud Virtual Machine"));
    /// @todo implement this?
    //setButtonText(QWizard::FinishButton, tr("Create"));
}
