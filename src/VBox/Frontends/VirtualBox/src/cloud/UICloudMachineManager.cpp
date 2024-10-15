/* $Id: UICloudMachineManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudMachineManager class implementation.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

/* GUI includes: */
#include "UICloudMachineManager.h"

/* COM includes: */
#include "CCloudMachine.h"

/* Other VBox includes: */

/* External includes: */


/* static */
UICloudMachineManager *UICloudMachineManager::s_pInstance = 0;

/* static */
void UICloudMachineManager::create()
{
    /* Make sure instance is NOT created yet: */
    AssertReturnVoid(!s_pInstance);

    /* Create instance: */
    new UICloudMachineManager;
    /* Prepare instance: */
    s_pInstance->prepare();
}

/* static */
void UICloudMachineManager::destroy()
{
    /* Make sure instance is NOT destroyed yet: */
    AssertPtrReturnVoid(s_pInstance);

    s_pInstance->cleanup();
    /* Destroy instance: */
    delete s_pInstance;
}

UICloudMachineManager::UICloudMachineManager()
{
    /* Assign instance: */
    s_pInstance = this;
}

UICloudMachineManager::~UICloudMachineManager()
{
    /* Unassign instance: */
    s_pInstance = 0;
}

void UICloudMachineManager::prepare()
{
}

void UICloudMachineManager::cleanup()
{
}

void UICloudMachineManager::notifyCloudMachineUnregistered(const QString &strProviderShortName,
                                                           const QString &strProfileName,
                                                           const QUuid &uId)
{
    emit sigCloudMachineUnregistered(strProviderShortName, strProfileName, uId);
}

void UICloudMachineManager::notifyCloudMachineRegistered(const QString &strProviderShortName,
                                                         const QString &strProfileName,
                                                         const CCloudMachine &comMachine)
{
    emit sigCloudMachineRegistered(strProviderShortName, strProfileName, comMachine);
}

void UICloudMachineManager::sltHandleCloudMachineAdded(const QString &strProviderShortName,
                                                       const QString &strProfileName,
                                                       const CCloudMachine &comMachine)
{
    /* Make sure we cached added cloud VM in GUI: */
    notifyCloudMachineRegistered(strProviderShortName,
                                 strProfileName,
                                 comMachine);
}
