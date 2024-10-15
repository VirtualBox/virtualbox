/* $Id: VBoxAutostartStart.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxAutostart - VirtualBox Autostart service, start machines during system boot.
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

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/thread.h>

#include <algorithm>
#include <list>

#include "VBoxAutostart.h"

extern unsigned g_cVerbosity;

using namespace com;

/**
 * VM list entry.
 */
typedef struct AUTOSTARTVM
{
    /** ID of the VM to start. */
    Bstr  strId;
    /** Startup delay of the VM. */
    ULONG uStartupDelay;
} AUTOSTARTVM;

static DECLCALLBACK(bool) autostartVMCmp(const AUTOSTARTVM &vm1, const AUTOSTARTVM &vm2)
{
    return vm1.uStartupDelay <= vm2.uStartupDelay;
}

DECLHIDDEN(int) autostartStartMain(PCFGAST pCfgAst)
{
    int vrc = VINF_SUCCESS;
    std::list<AUTOSTARTVM> listVM;
    uint32_t uStartupDelay = 0;

    autostartSvcLogVerbose(1, "Starting machines ...\n");

    pCfgAst = autostartConfigAstGetByName(pCfgAst, "startup_delay");
    if (pCfgAst)
    {
        if (pCfgAst->enmType == CFGASTNODETYPE_KEYVALUE)
        {
            vrc = RTStrToUInt32Full(pCfgAst->u.KeyValue.aszValue, 10, &uStartupDelay);
            if (RT_FAILURE(vrc))
                return autostartSvcLogErrorRc(vrc, "'startup_delay' must be an unsigned number");
        }
    }

    if (uStartupDelay)
    {
        autostartSvcLogVerbose(1, "Delaying start for %RU32 seconds ...\n", uStartupDelay);
        vrc = RTThreadSleep(uStartupDelay * 1000);
    }

    if (vrc == VERR_INTERRUPTED)
        return VINF_SUCCESS;

    /*
     * Build a list of all VMs we need to autostart first, apply the overrides
     * from the configuration and start the VMs afterwards.
     */
    com::SafeIfaceArray<IMachine> machines;
    HRESULT hrc = g_pVirtualBox->COMGETTER(Machines)(ComSafeArrayAsOutParam(machines));
    if (SUCCEEDED(hrc))
    {
        /*
         * Iterate through the collection
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

                AUTOSTARTVM autostartVM;

                BOOL fAutostart;
                CHECK_ERROR_BREAK(pMachine, COMGETTER(AutostartEnabled)(&fAutostart));
                if (fAutostart)
                {
                    CHECK_ERROR_BREAK(pMachine, COMGETTER(Id)(autostartVM.strId.asOutParam()));
                    CHECK_ERROR_BREAK(pMachine, COMGETTER(AutostartDelay)(&autostartVM.uStartupDelay));

                    listVM.push_back(autostartVM);
                }

                autostartSvcLogVerbose(1, "Machine '%ls': Autostart is %s (startup delay is %RU32 seconds)\n",
                                       strName.raw(), fAutostart ? "enabled" : "disabled",
                                       fAutostart ? autostartVM.uStartupDelay : 0);
            }
        }

        /**
         * @todo r=uwe I'm not reindenting this whole burnt offering
         * to mistinterpreted Dijkstra's "single exit" commandment
         * just to add this log, hence a bit of duplicate logic here.
         */
        if (SUCCEEDED(hrc))
        {
            if (machines.size() == 0)
                autostartSvcLogWarning("No virtual machines found.\n"
                                       "This either could be a configuration problem (access rights), "
                                       "or there are no VMs configured yet.");
            else if (listVM.empty())
                autostartSvcLogWarning("No virtual machines configured for autostart.\n"
                                       "Please consult the manual about how to enable auto starting VMs.\n");
        }
        else
            autostartSvcLogError("Enumerating virtual machines failed with %Rhrc\n", hrc);

        if (   SUCCEEDED(hrc)
            && !listVM.empty())
        {
            ULONG uDelayCur = 0;

            /* Sort by startup delay and apply base override. */
            listVM.sort(autostartVMCmp);

            std::list<AUTOSTARTVM>::iterator it;
            for (it = listVM.begin(); it != listVM.end(); ++it)
            {
                ComPtr<IMachine> machine;
                ComPtr<IProgress> progress;

                CHECK_ERROR_BREAK(g_pVirtualBox, FindMachine((*it).strId.raw(), machine.asOutParam()));

                Bstr strName;
                CHECK_ERROR_BREAK(machine, COMGETTER(Name)(strName.asOutParam()));

                if ((*it).uStartupDelay > uDelayCur)
                {
                    autostartSvcLogVerbose(1, "Waiting for %ul seconds before starting machine '%s' ...\n",
                                           (*it).uStartupDelay - uDelayCur, strName.raw());
                    RTThreadSleep(((*it).uStartupDelay - uDelayCur) * 1000);
                    uDelayCur = (*it).uStartupDelay;
                }

                CHECK_ERROR_BREAK(machine, LaunchVMProcess(g_pSession, Bstr("headless").raw(),
                                                           ComSafeArrayNullInParam(), progress.asOutParam()));
                if (SUCCEEDED(hrc) && !progress.isNull())
                {
                    autostartSvcLogVerbose(1, "Waiting for machine '%ls' to power on ...\n", strName.raw());
                    CHECK_ERROR(progress, WaitForCompletion(-1));
                    if (SUCCEEDED(hrc))
                    {
                        BOOL completed = true;
                        CHECK_ERROR(progress, COMGETTER(Completed)(&completed));
                        if (SUCCEEDED(hrc))
                        {
                            ASSERT(completed);

                            LONG iRc;
                            CHECK_ERROR(progress, COMGETTER(ResultCode)(&iRc));
                            if (SUCCEEDED(hrc))
                            {
                                if (FAILED(iRc))
                                {
                                    ProgressErrorInfo info(progress);
                                    com::GluePrintErrorInfo(info);
                                }
                                else
                                    autostartSvcLogVerbose(1, "Machine '%ls' has been successfully started.\n", strName.raw());
                            }
                        }
                    }
                }
                SessionState_T enmSessionState;
                CHECK_ERROR(g_pSession, COMGETTER(State)(&enmSessionState));
                if (SUCCEEDED(hrc) && enmSessionState == SessionState_Locked)
                    g_pSession->UnlockMachine();
            }
        }
    }

    return vrc;
}

