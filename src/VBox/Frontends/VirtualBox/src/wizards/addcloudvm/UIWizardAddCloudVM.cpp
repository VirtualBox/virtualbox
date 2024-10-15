/* $Id: UIWizardAddCloudVM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardAddCloudVM class implementation.
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
#include "UIWizardAddCloudVM.h"
#include "UIWizardAddCloudVMPageExpert.h"
#include "UIWizardAddCloudVMPageSource.h"

/* COM includes: */
#include "CCloudMachine.h"


UIWizardAddCloudVM::UIWizardAddCloudVM(QWidget *pParent,
                                       const QString &strFullGroupName /* = QString() */)
    : UINativeWizard(pParent, WizardType_AddCloudVM)
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

bool UIWizardAddCloudVM::addCloudVMs()
{
    /* Prepare result: */
    bool fResult = false;

    /* Acquire prepared client: */
    CCloudClient comClient = client();
    AssertReturn(comClient.isNotNull(), fResult);

    /* For each cloud instance name we have: */
    foreach (const QString &strInstanceName, instanceIds())
    {
        /* Initiate cloud VM add procedure: */
        CCloudMachine comMachine;

        /* Add cloud VM: */
        UINotificationProgressCloudMachineAdd *pNotification = new UINotificationProgressCloudMachineAdd(comClient,
                                                                                                         comMachine,
                                                                                                         strInstanceName,
                                                                                                         providerShortName(),
                                                                                                         profileName());
        connect(pNotification, &UINotificationProgressCloudMachineAdd::sigCloudMachineAdded,
                gpCloudMachineManager, &UICloudMachineManager::sltHandleCloudMachineAdded);
        gpNotificationCenter->append(pNotification);

        /* Positive: */
        fResult = true;
    }

    /* Return result: */
    return fResult;
}

void UIWizardAddCloudVM::populatePages()
{
    /* Create corresponding pages: */
    switch (mode())
    {
        case WizardMode_Basic:
        {
            addPage(new UIWizardAddCloudVMPageSource);
            break;
        }
        case WizardMode_Expert:
        {
            addPage(new UIWizardAddCloudVMPageExpert);
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid mode: %d", mode()));
            break;
        }
    }
}

void UIWizardAddCloudVM::sltRetranslateUI()
{
    /* Call to base-class: */
    UINativeWizard::sltRetranslateUI();

    /* Translate wizard: */
    setWindowTitle(tr("Add Cloud Virtual Machine"));
    /// @todo implement this?
    //setButtonText(QWizard::FinishButton, tr("Add"));
}
