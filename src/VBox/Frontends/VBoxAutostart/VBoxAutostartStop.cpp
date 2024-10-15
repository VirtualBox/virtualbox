/* $Id: VBoxAutostartStop.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxAutostart - VirtualBox Autostart service, stop machines during system shutdown.
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

#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <list>

#include "VBoxAutostart.h"

using namespace com;

/**
 * VM list entry.
 */
typedef struct AUTOSTOPVM
{
    /** ID of the VM to start. */
    Bstr           strId;
    /** Action to do with the VM. */
    AutostopType_T enmAutostopType;
} AUTOSTOPVM;

static HRESULT autostartSaveVMState(ComPtr<IConsole> &console)
{
    HRESULT hrc = S_OK;
    ComPtr<IMachine> machine;
    ComPtr<IProgress> progress;

    do
    {
        /* first pause so we don't trigger a live save which needs more time/resources */
        bool fPaused = false;
        hrc = console->Pause();
        if (FAILED(hrc))
        {
            bool fError = true;
            if (hrc == VBOX_E_INVALID_VM_STATE)
            {
                /* check if we are already paused */
                MachineState_T machineState;
                CHECK_ERROR_BREAK(console, COMGETTER(State)(&machineState));
                /* the error code was lost by the previous instruction */
                hrc = VBOX_E_INVALID_VM_STATE;
                if (machineState != MachineState_Paused)
                {
                    RTMsgError("Machine in invalid state %d -- %s\n",
                               machineState, machineStateToName(machineState, false));
                }
                else
                {
                    fError = false;
                    fPaused = true;
                }
            }
            if (fError)
                break;
        }

        CHECK_ERROR(console, COMGETTER(Machine)(machine.asOutParam()));
        CHECK_ERROR(machine, SaveState(progress.asOutParam()));
        if (FAILED(hrc))
        {
            if (!fPaused)
                console->Resume();
            break;
        }

        hrc = showProgress(progress);
        CHECK_PROGRESS_ERROR(progress, ("Failed to save machine state"));
        if (FAILED(hrc))
        {
            if (!fPaused)
                console->Resume();
        }
    } while (0);

    return hrc;
}

DECLHIDDEN(int) autostartStopMain(PCFGAST pCfgAst)
{
    RT_NOREF(pCfgAst);
    std::list<AUTOSTOPVM> listVM;

    autostartSvcLogVerbose(1, "Stopping machines ...\n");

    /*
     * Build a list of all VMs we need to autostop first, apply the overrides
     * from the configuration and start the VMs afterwards.
     */
    com::SafeIfaceArray<IMachine> machines;
    HRESULT hrc = g_pVirtualBox->COMGETTER(Machines)(ComSafeArrayAsOutParam(machines));
    if (SUCCEEDED(hrc))
    {
        /*
         * Iterate through the collection and construct a list of machines
         * we have to check.
         */
        for (size_t i = 0; i < machines.size(); ++i)
        {
            ComPtr<IMachine> pMachine = machines[i];

            if (pMachine.isNotNull())
            {
                Bstr strName;
                CHECK_ERROR_BREAK(pMachine, COMGETTER(Name)(strName.asOutParam()));

                BOOL fAccessible;
                CHECK_ERROR_BREAK(pMachine, COMGETTER(Accessible)(&fAccessible));
                if (!fAccessible)
                {
                    autostartSvcLogVerbose(1, "Machine '%ls' is not accessible, skipping\n", strName.raw());
                    continue;
                }

                AUTOSTOPVM autostopVM;

                AutostopType_T enmAutostopType;
                CHECK_ERROR_BREAK(pMachine, COMGETTER(AutostopType)(&enmAutostopType));
                if (enmAutostopType != AutostopType_Disabled)
                {
                    CHECK_ERROR_BREAK(pMachine, COMGETTER(Id)(autostopVM.strId.asOutParam()));
                    autostopVM.enmAutostopType = enmAutostopType;

                    listVM.push_back(autostopVM);
                }

                autostartSvcLogVerbose(1, "Machine '%ls': Autostop type is %#x\n",
                                       strName.raw(), autostopVM.enmAutostopType);
            }
        }

        if (   SUCCEEDED(hrc)
            && !listVM.empty())
        {
            std::list<AUTOSTOPVM>::iterator it;
            for (it = listVM.begin(); it != listVM.end(); ++it)
            {
                MachineState_T enmMachineState;
                ComPtr<IMachine> machine;

                CHECK_ERROR_BREAK(g_pVirtualBox, FindMachine((*it).strId.raw(),
                                                             machine.asOutParam()));

                Bstr strName;
                CHECK_ERROR_BREAK(machine, COMGETTER(Name)(strName.asOutParam()));

                CHECK_ERROR_BREAK(machine, COMGETTER(State)(&enmMachineState));

                /* Wait until the VM changes from a transient state back. */
                while (   enmMachineState >= MachineState_FirstTransient
                       && enmMachineState <= MachineState_LastTransient)
                {
                    RTThreadSleep(1000);
                    CHECK_ERROR_BREAK(machine, COMGETTER(State)(&enmMachineState));
                }

                /* Only power off running machines. */
                if (   enmMachineState == MachineState_Running
                    || enmMachineState == MachineState_Paused)
                {
                    ComPtr<IConsole> console;
                    ComPtr<IProgress> progress;

                    /* open a session for the VM */
                    CHECK_ERROR_BREAK(machine, LockMachine(g_pSession, LockType_Shared));

                    /* get the associated console */
                    CHECK_ERROR_BREAK(g_pSession, COMGETTER(Console)(console.asOutParam()));

                    switch ((*it).enmAutostopType)
                    {
                        case AutostopType_SaveState:
                        {
                            hrc = autostartSaveVMState(console);
                            break;
                        }
                        case AutostopType_PowerOff:
                        {
                            CHECK_ERROR_BREAK(console, PowerDown(progress.asOutParam()));

                            hrc = showProgress(progress);
                            CHECK_PROGRESS_ERROR(progress, ("Failed to powering off machine '%ls'", strName.raw()));
                            if (FAILED(hrc))
                                autostartSvcLogError("Powering off machine '%ls' failed with %Rhrc\n", strName.raw(), hrc);
                            break;
                        }
                        case AutostopType_AcpiShutdown:
                        {
                            BOOL fGuestEnteredACPI = false;
                            CHECK_ERROR_BREAK(console, GetGuestEnteredACPIMode(&fGuestEnteredACPI));
                            if (   fGuestEnteredACPI
                                && enmMachineState == MachineState_Running)
                            {
                                CHECK_ERROR_BREAK(console, PowerButton());

                                autostartSvcLogVerbose(1, "Waiting for machine '%ls' to power off...\n", strName.raw());

                                uint64_t     const tsStartMs = RTTimeMilliTS();
                                RTMSINTERVAL const msTimeout = RT_MS_5MIN; /* Should be enough time, shouldn't it? */

                                while (RTTimeMilliTS() - tsStartMs <= msTimeout)
                                {
                                    CHECK_ERROR_BREAK(machine, COMGETTER(State)(&enmMachineState));
                                    if (enmMachineState != MachineState_Running)
                                        break;
                                    RTThreadSleep(RT_MS_1SEC);
                                }

                                if (RTTimeMilliTS() - tsStartMs > msTimeout)
                                    autostartSvcLogWarning("Machine '%ls' did not power off via ACPI within time\n", strName.raw());
                            }
                            else
                            {
                                /* Use save state instead and log this to the console. */
                                autostartSvcLogWarning("The guest of machine '%ls' does not support ACPI shutdown or is currently paused, saving state...\n",
                                                       strName.raw());
                                hrc = autostartSaveVMState(console);
                            }
                            break;
                        }
                        default:
                            autostartSvcLogWarning("Unknown autostop type for machine '%ls', skipping\n", strName.raw());
                    }
                    g_pSession->UnlockMachine();
                }
            }
        }
    }

    return VINF_SUCCESS; /** @todo r=andy Report back the overall status here. */
}

