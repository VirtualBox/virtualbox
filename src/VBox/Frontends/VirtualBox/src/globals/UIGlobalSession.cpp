/* $Id: UIGlobalSession.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSession class implementation.
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
#include <QApplication>

/* GUI includes: */
#include "UIGlobalSession.h"
#include "UIGuestOSType.h"
#include "UIMessageCenter.h"
#include "UIVirtualBoxClientEventHandler.h"

/* COM includes: */
#include "CSystemProperties.h"

/* Other VBox includes: */
#ifdef VBOX_WITH_XPCOM
# include <iprt/path.h>
#endif

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h> // for CLSID_VirtualBoxClient


/* static */
UIGlobalSession *UIGlobalSession::s_pInstance = 0;

/* static */
void UIGlobalSession::create()
{
    /* Make sure instance is NOT created yet: */
    AssertReturnVoid(!s_pInstance);

    /* Create instance: */
    new UIGlobalSession;
}

/* static */
void UIGlobalSession::destroy()
{
    /* Make sure instance is NOT destroyed yet: */
    AssertPtrReturnVoid(s_pInstance);

    /* Destroy instance: */
    delete s_pInstance;
    s_pInstance = 0;
}

bool UIGlobalSession::prepare()
{
    /* Prepare guest OS type manager before COM stuff: */
    m_pGuestOSTypeManager = new UIGuestOSTypeManager;

    /* Init COM: */
    HRESULT rc = COMBase::InitializeCOM(true);
    if (FAILED(rc))
    {
#ifdef VBOX_WITH_XPCOM
        if (rc == NS_ERROR_FILE_ACCESS_DENIED)
        {
            char szHome[RTPATH_MAX] = "";
            com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome));
            msgCenter().cannotInitUserHome(QString(szHome));
        }
        else
#endif
            msgCenter().cannotInitCOM(rc);
        return false;
    }

    /* Make sure VirtualBoxClient instance created: */
    m_comVBoxClient.createInstance(CLSID_VirtualBoxClient);
    if (!m_comVBoxClient.isOk())
    {
        msgCenter().cannotCreateVirtualBoxClient(m_comVBoxClient);
        return false;
    }

    /* Init wrappers: */
    comWrappersReinit();

    /* Watch for the VBoxSVC availability changes: */
    connect(gVBoxClientEvents, &UIVirtualBoxClientEventHandler::sigVBoxSVCAvailabilityChange,
            this, &UIGlobalSession::sltHandleVBoxSVCAvailabilityChange);

    /* Success finally: */
    return true;
}

void UIGlobalSession::cleanup()
{
    /* Cleanup guest OS type manager before COM stuff: */
    delete m_pGuestOSTypeManager;
    m_pGuestOSTypeManager = 0;

    /* Starting COM cleanup: */
    m_comCleanupProtectionToken.lockForWrite();

    /* Detach COM wrappers: */
    m_comHost.detach();
    m_comVBox.detach();
    m_comVBoxClient.detach();

    /* There may be COM related event instances still in the message queue
     * which reference COM objects. Remove them to release those objects
     * before uninitializing the COM subsystem. */
    QApplication::removePostedEvents(this);

    /* Finally cleanup COM itself: */
    COMBase::CleanupCOM();

    /* Finishing COM cleanup: */
    m_comCleanupProtectionToken.unlock();
}

const UIGuestOSTypeManager &UIGlobalSession::guestOSTypeManager()
{
    /* Handle exceptional and undesired case!
     * This object is created and destroyed within own timeframe.
     * If pointer isn't yet initialized or already cleaned up,
     * something is definitely wrong. */
    AssertPtr(m_pGuestOSTypeManager);
    if (!m_pGuestOSTypeManager)
    {
        m_pGuestOSTypeManager = new UIGuestOSTypeManager;
        m_pGuestOSTypeManager->reCacheGuestOSTypes();
    }

    /* Return an object instance: */
    return *m_pGuestOSTypeManager;
}

int UIGlobalSession::supportedRecordingFeatures() const
{
    int iSupportedFlag = 0;
    CSystemProperties comProperties = virtualBox().GetSystemProperties();
    foreach (const KRecordingFeature &enmFeature, comProperties.GetSupportedRecordingFeatures())
        iSupportedFlag |= enmFeature;
    return iSupportedFlag;
}

void UIGlobalSession::sltHandleVBoxSVCAvailabilityChange(bool fAvailable)
{
    /* Make sure the VBoxSVC availability changed: */
    if (m_fVBoxSVCAvailable == fAvailable)
        return;

    /* Cache the new VBoxSVC availability value: */
    m_fVBoxSVCAvailable = fAvailable;

    /* If VBoxSVC is not available: */
    if (!m_fVBoxSVCAvailable)
    {
        /* Mark wrappers invalid: */
        m_fWrappersValid = false;

        /* Re-fetch corresponding CVirtualBox to restart VBoxSVC: */
        CVirtualBoxClient comVBoxClient = virtualBoxClient();
        m_comVBox = comVBoxClient.GetVirtualBox();
        if (!comVBoxClient.isOk())
        {
            // The proper behavior would be to show the message and to exit the app, e.g.:
            // msgCenter().cannotAcquireVirtualBox(m_comVBoxClient);
            // return QApplication::quit();
            // But CVirtualBox is still NULL in current Main implementation,
            // and this call do not restart anything, so we are waiting
            // for subsequent event about VBoxSVC is available again.
        }
    }
    /* If VBoxSVC is available: */
    else
    {
        /* Try to re-init wrappers, quit if failed: */
        if (   !m_fWrappersValid
            && !comWrappersReinit())
            return QApplication::quit();
    }

    /* Notify listeners about the VBoxSVC availability change: */
    emit sigVBoxSVCAvailabilityChange(m_fVBoxSVCAvailable);
}

UIGlobalSession::UIGlobalSession()
    : m_fWrappersValid(false)
    , m_fVBoxSVCAvailable(true)
    , m_pGuestOSTypeManager(0)
{
    /* Assign instance: */
    s_pInstance = this;
}

UIGlobalSession::~UIGlobalSession()
{
    /* Unassign instance: */
    s_pInstance = 0;
}

bool UIGlobalSession::comWrappersReinit()
{
    /* Make sure VirtualBox instance acquired: */
    CVirtualBoxClient comVBoxClient = virtualBoxClient();
    m_comVBox = comVBoxClient.GetVirtualBox();
    if (!comVBoxClient.isOk())
    {
        msgCenter().cannotAcquireVirtualBox(comVBoxClient);
        return false;
    }

    /* Acquire host: */
    CVirtualBox comVBox = virtualBox();
    m_comHost = comVBox.GetHost();
//    if (!comVBox.isOk())
//    {
//        msgCenter().cannotAcquireVirtualBoxParameter(comVBoxClient);
//        return false;
//    }

    /* Acquire home folder: */
    m_strHomeFolder = comVBox.GetHomeFolder();
//    if (!comVBox.isOk())
//    {
//        msgCenter().cannotAcquireVirtualBoxParameter(comVBoxClient);
//        return false;
//    }

    /* Re-initialize guest OS type database: */
    if (m_pGuestOSTypeManager)
        m_pGuestOSTypeManager->reCacheGuestOSTypes();

    /* Mark wrappers valid: */
    m_fWrappersValid = true;

    /* Success finally: */
    return true;
}
