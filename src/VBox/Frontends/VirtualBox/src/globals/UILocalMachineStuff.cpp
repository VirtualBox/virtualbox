/* $Id: UILocalMachineStuff.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UILocalMachineStuff namespace implementation.
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
#include <QLocale>

/* GUI includes: */
#include "UICommon.h"
#include "UIGlobalSession.h"
#include "UILocalMachineStuff.h"
#include "UILoggingDefs.h"
#include "UIMessageCenter.h"
#include "UITranslator.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
# include "UIDesktopWidgetWatchdog.h"
#endif

/* COM includes: */
#include "CMachine.h"

/* Other VBox includes: */
#ifdef VBOX_WS_NIX
# include <iprt/env.h>
#endif

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h> /* For CLSID_Session. */


bool UILocalMachineStuff::switchToMachine(CMachine &comMachine)
{
#ifdef VBOX_WS_MAC
    const ULONG64 id = comMachine.ShowConsoleWindow();
#else
    const WId id = (WId)comMachine.ShowConsoleWindow();
#endif
    Assert(comMachine.isOk());
    if (!comMachine.isOk())
        return false;

    // WORKAROUND:
    // id == 0 means the console window has already done everything
    // necessary to implement the "show window" semantics.
    if (id == 0)
        return true;

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)

    return UIDesktopWidgetWatchdog::activateWindow(id, true);

#elif defined(VBOX_WS_MAC)

    // WORKAROUND:
    // This is just for the case were the other process cannot steal
    // the focus from us. It will send us a PSN so we can try.
    ProcessSerialNumber psn;
    psn.highLongOfPSN = id >> 32;
    psn.lowLongOfPSN = (UInt32)id;
# ifdef __clang__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    OSErr rc = ::SetFrontProcess(&psn);
#  pragma GCC diagnostic pop
# else
    OSErr rc = ::SetFrontProcess(&psn);
# endif
    if (!rc)
        Log(("GUI: %#RX64 couldn't do SetFrontProcess on itself, the selector (we) had to do it...\n", id));
    else
        Log(("GUI: Failed to bring %#RX64 to front. rc=%#x\n", id, rc));
    return !rc;

#else

    return false;

#endif
}

bool UILocalMachineStuff::launchMachine(CMachine &comMachine, UILaunchMode enmLaunchMode /* = UILaunchMode_Default */)
{
    /* Switch to machine window(s) if possible: */
    if (   comMachine.GetSessionState() == KSessionState_Locked /* precondition for CanShowConsoleWindow() */
        && comMachine.CanShowConsoleWindow())
    {
        switch (uiCommon().uiType())
        {
            /* For Selector UI: */
            case UIType_ManagerUI:
            {
                /* Just switch to existing VM window: */
                return switchToMachine(comMachine);
            }
            /* For Runtime UI: */
            case UIType_RuntimeUI:
            {
                /* Only separate UI process can reach that place.
                 * Switch to existing VM window and exit. */
                switchToMachine(comMachine);
                return false;
            }
        }
    }

    /* Not for separate UI (which can connect to machine in any state): */
    if (enmLaunchMode != UILaunchMode_Separate)
    {
        /* Make sure machine-state is one of required: */
        const KMachineState enmState = comMachine.GetState(); NOREF(enmState);
        AssertMsg(   enmState == KMachineState_PoweredOff
                  || enmState == KMachineState_Saved
                  || enmState == KMachineState_Teleported
                  || enmState == KMachineState_Aborted
                  || enmState == KMachineState_AbortedSaved
                  , ("Machine must be PoweredOff/Saved/Teleported/Aborted (%d)", enmState));
    }

    /* Create empty session instance: */
    CSession comSession;
    comSession.createInstance(CLSID_Session);
    if (comSession.isNull())
    {
        msgCenter().cannotOpenSession(comSession);
        return false;
    }

    /* Configure environment: */
    QVector<QString> astrEnv;
#ifdef VBOX_WS_WIN
    /* Allow started VM process to be foreground window: */
    AllowSetForegroundWindow(ASFW_ANY);
#endif
#ifdef VBOX_WS_NIX
    /* Make sure VM process will start on the same
     * display as window this wrapper is called from: */
    const char *pDisplay = RTEnvGet("DISPLAY");
    if (pDisplay)
        astrEnv.append(QString("DISPLAY=%1").arg(pDisplay));
    const char *pXauth = RTEnvGet("XAUTHORITY");
    if (pXauth)
        astrEnv.append(QString("XAUTHORITY=%1").arg(pXauth));
#endif
    QString strType;
    switch (enmLaunchMode)
    {
        case UILaunchMode_Default:  strType = ""; break;
        case UILaunchMode_Separate: strType = uiCommon().isSeparateProcess() ? "headless" : "separate"; break;
        case UILaunchMode_Headless: strType = "headless"; break;
        default: AssertFailedReturn(false);
    }

    /* Prepare "VM spawning" progress: */
    CProgress comProgress = comMachine.LaunchVMProcess(comSession, strType, astrEnv);
    if (!comMachine.isOk())
    {
        /* If the VM is started separately and the VM process is already running, then it is OK. */
        if (enmLaunchMode == UILaunchMode_Separate)
        {
            const KMachineState enmState = comMachine.GetState();
            if (   enmState >= KMachineState_FirstOnline
                && enmState <= KMachineState_LastOnline)
            {
                /* Already running: */
                return true;
            }
        }

        msgCenter().cannotOpenSession(comMachine);
        return false;
    }

    /* Show "VM spawning" progress: */
    msgCenter().showModalProgressDialog(comProgress, comMachine.GetName(),
                                        ":/progress_start_90px.png", 0, 0);
    if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
        msgCenter().cannotOpenSession(comProgress, comMachine.GetName());

    /* Unlock machine, close session: */
    comSession.UnlockMachine();

    /* True finally: */
    return true;
}

CSession UILocalMachineStuff::openSession(QUuid uId, KLockType enmLockType /* = KLockType_Write */)
{
    /* Prepare session: */
    CSession comSession;

    /* Make sure uId isn't null: */
    if (uId.isNull())
        uId = uiCommon().managedVMUuid();
    if (uId.isNull())
        return comSession;

    /* Simulate try-catch block: */
    bool fSuccess = false;
    do
    {
        /* Create empty session instance: */
        comSession.createInstance(CLSID_Session);
        if (comSession.isNull())
        {
            msgCenter().cannotOpenSession(comSession);
            break;
        }

        /* Search for the corresponding machine: */
        const CVirtualBox comVBox = gpGlobalSession->virtualBox();
        CMachine comMachine = comVBox.FindMachine(uId.toString());
        if (comMachine.isNull())
        {
            msgCenter().cannotFindMachineById(comVBox, uId);
            break;
        }

        if (enmLockType == KLockType_VM)
            comSession.SetName("GUI/Qt");

        /* Lock found machine to session: */
        comMachine.LockMachine(comSession, enmLockType);
        if (!comMachine.isOk())
        {
            msgCenter().cannotOpenSession(comMachine);
            break;
        }

        /* Pass the language ID as the property to the guest: */
        if (comSession.GetType() == KSessionType_Shared)
        {
            CMachine comStartedMachine = comSession.GetMachine();
            /* Make sure that the language is in two letter code.
             * Note: if languageId() returns an empty string lang.name() will
             * return "C" which is an valid language code. */
            QLocale lang(UITranslator::languageId());
            comStartedMachine.SetGuestPropertyValue("/VirtualBox/HostInfo/GUI/LanguageID", lang.name());
        }

        /* Success finally: */
        fSuccess = true;
    }
    while (0);
    /* Cleanup try-catch block: */
    if (!fSuccess)
        comSession.detach();

    /* Return session: */
    return comSession;
}

CSession UILocalMachineStuff::openSession(KLockType enmLockType /* = KLockType_Write */)
{
    /* Pass to function above: */
    return openSession(uiCommon().managedVMUuid(), enmLockType);
}

CSession UILocalMachineStuff::openExistingSession(const QUuid &uId)
{
    /* Pass to function above: */
    return openSession(uId, KLockType_Shared);
}

CSession UILocalMachineStuff::tryToOpenSessionFor(CMachine &comMachine)
{
    /* Prepare session: */
    CSession comSession;

    /* Session state unlocked? */
    if (comMachine.GetSessionState() == KSessionState_Unlocked)
    {
        /* Open own 'write' session: */
        comSession = openSession(comMachine.GetId());
        AssertReturn(!comSession.isNull(), CSession());
        comMachine = comSession.GetMachine();
    }
    /* Is this a Selector UI call? */
    else if (uiCommon().uiType() == UIType_ManagerUI)
    {
        /* Open existing 'shared' session: */
        comSession = openExistingSession(comMachine.GetId());
        AssertReturn(!comSession.isNull(), CSession());
        comMachine = comSession.GetMachine();
    }
    /* Else this is Runtime UI call
     * which has session locked for itself. */

    /* Return session: */
    return comSession;
}

