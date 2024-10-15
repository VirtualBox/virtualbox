/* $Id: tstVBoxAPIWin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * tstVBoxAPIWin - sample program to illustrate the VirtualBox
 *                 COM API for machine management on Windows.
                   It only uses standard C/C++ and COM semantics,
 *                 no additional VBox classes/macros/helpers. To
 *                 make things even easier to follow, only the
 *                 standard Win32 API has been used. Typically,
 *                 C++ developers would make use of Microsoft's
 *                 ATL to ease development.
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

/*
 * PURPOSE OF THIS SAMPLE PROGRAM
 * ------------------------------
 *
 * This sample program is intended to demonstrate the minimal code necessary
 * to use VirtualBox COM API for learning puroses only. The program uses pure
 * Win32 API and doesn't have any extra dependencies to let you better
 * understand what is going on when a client talks to the VirtualBox core
 * using the COM framework.
 *
 * However, if you want to write a real application, it is highly recommended
 * to use our MS COM XPCOM Glue library and helper C++ classes. This way, you
 * will get at least the following benefits:
 *
 * a) better portability: both the MS COM (used on Windows) and XPCOM (used
 *    everywhere else) VirtualBox client application from the same source code
 *    (including common smart C++ templates for automatic interface pointer
 *    reference counter and string data management);
 * b) simpler XPCOM initialization and shutdown (only a single method call
 *    that does everything right).
 *
 * Currently, there is no separate sample program that uses the VirtualBox MS
 * COM XPCOM Glue library. Please refer to the sources of stock VirtualBox
 * applications such as the VirtualBox GUI frontend or the VBoxManage command
 * line frontend.
 */


#include <stdio.h>
#include <iprt/win/windows.h>  /* Avoid -Wall warnings. */
#include "VirtualBox.h"

#define SAFE_RELEASE(x) \
    if (x) { \
        x->Release(); \
        x = NULL; \
    }

int listVMs(IVirtualBox *virtualBox)
{
    HRESULT rc;

    /*
     * First we have to get a list of all registered VMs
     */
    SAFEARRAY *machinesArray = NULL;

    rc = virtualBox->get_Machines(&machinesArray);
    if (SUCCEEDED(rc))
    {
        IMachine **machines;
        rc = SafeArrayAccessData(machinesArray, (void **) &machines);
        if (SUCCEEDED(rc))
        {
            for (ULONG i = 0; i < machinesArray->rgsabound[0].cElements; ++i)
            {
                BSTR str;

                rc = machines[i]->get_Name(&str);
                if (SUCCEEDED(rc))
                {
                    printf("Name: %S\n", str);
                    SysFreeString(str);
                }
            }

            SafeArrayUnaccessData(machinesArray);
        }

        SafeArrayDestroy(machinesArray);
    }

    return 0;
}


int testErrorInfo(IVirtualBox *virtualBox)
{
    HRESULT rc;

    /* Try to find a machine that doesn't exist */
    IMachine *machine = NULL;
    BSTR machineName = SysAllocString(L"Foobar");

    rc = virtualBox->FindMachine(machineName, &machine);

    if (FAILED(rc))
    {
        IErrorInfo *errorInfo;

        rc = GetErrorInfo(0, &errorInfo);

        if (FAILED(rc))
            printf("Error getting error info! rc=%#lx\n", rc);
        else
        {
            BSTR errorDescription = NULL;

            rc = errorInfo->GetDescription(&errorDescription);

            if (FAILED(rc) || !errorDescription)
                printf("Error getting error description! rc=%#lx\n", rc);
            else
            {
                printf("Successfully retrieved error description: %S\n", errorDescription);

                SysFreeString(errorDescription);
            }

            errorInfo->Release();
        }
    }

    SAFE_RELEASE(machine);
    SysFreeString(machineName);

    return 0;
}


int testStartVM(IVirtualBox *virtualBox)
{
    HRESULT rc;

    /* Try to start a VM called "WinXP SP2". */
    IMachine *machine = NULL;
    BSTR machineName = SysAllocString(L"WinXP SP2");

    rc = virtualBox->FindMachine(machineName, &machine);

    if (FAILED(rc))
    {
        IErrorInfo *errorInfo;

        rc = GetErrorInfo(0, &errorInfo);

        if (FAILED(rc))
            printf("Error getting error info! rc=%#lx\n", rc);
        else
        {
            BSTR errorDescription = NULL;

            rc = errorInfo->GetDescription(&errorDescription);

            if (FAILED(rc) || !errorDescription)
                printf("Error getting error description! rc=%#lx\n", rc);
            else
            {
                printf("Successfully retrieved error description: %S\n", errorDescription);

                SysFreeString(errorDescription);
            }

            SAFE_RELEASE(errorInfo);
        }
    }
    else
    {
        ISession *session = NULL;
        IConsole *console = NULL;
        IProgress *progress = NULL;
        BSTR sessiontype = SysAllocString(L"gui");
        BSTR guid;

        do
        {
            rc = machine->get_Id(&guid); /* Get the GUID of the machine. */
            if (!SUCCEEDED(rc))
            {
                printf("Error retrieving machine ID! rc=%#lx\n", rc);
                break;
            }

            /* Create the session object. */
            rc = CoCreateInstance(CLSID_Session,        /* the VirtualBox base object */
                                  NULL,                 /* no aggregation */
                                  CLSCTX_INPROC_SERVER, /* the object lives in the current process */
                                  IID_ISession,         /* IID of the interface */
                                  (void**)&session);
            if (!SUCCEEDED(rc))
            {
                printf("Error creating Session instance! rc=%#lx\n", rc);
                break;
            }

            /* Start a VM session using the delivered VBox GUI. */
            rc = machine->LaunchVMProcess(session, sessiontype,
                                          NULL, &progress);
            if (!SUCCEEDED(rc))
            {
                printf("Could not open remote session! rc=%#lx\n", rc);
                break;
            }

            /* Wait until VM is running. */
            printf("Starting VM, please wait ...\n");
            rc = progress->WaitForCompletion(-1);

            /* Get console object. */
            session->get_Console(&console);

            /* Bring console window to front. */
            machine->ShowConsoleWindow(0);

            printf("Press enter to power off VM and close the session...\n");
            getchar();

            /* Power down the machine. */
            rc = console->PowerDown(&progress);

            /* Wait until VM is powered down. */
            printf("Powering off VM, please wait ...\n");
            rc = progress->WaitForCompletion(-1);

            /* Close the session. */
            rc = session->UnlockMachine();

        } while (0);

        SAFE_RELEASE(console);
        SAFE_RELEASE(progress);
        SAFE_RELEASE(session);
        SysFreeString(guid);
        SysFreeString(sessiontype);
        SAFE_RELEASE(machine);
    }

    SysFreeString(machineName);

    return 0;
}


int main()
{
    /* Initialize the COM subsystem. */
    CoInitialize(NULL);

    /* Instantiate the VirtualBox root object. */
    IVirtualBoxClient *virtualBoxClient;
    HRESULT rc = CoCreateInstance(CLSID_VirtualBoxClient, /* the VirtualBoxClient object */
                                  NULL,                   /* no aggregation */
                                  CLSCTX_INPROC_SERVER,   /* the object lives in the current process */
                                  IID_IVirtualBoxClient,  /* IID of the interface */
                                  (void**)&virtualBoxClient);
    if (SUCCEEDED(rc))
    {
        IVirtualBox *virtualBox;
        rc = virtualBoxClient->get_VirtualBox(&virtualBox);
        if (SUCCEEDED(rc))
        {
            listVMs(virtualBox);

            testErrorInfo(virtualBox);

            /* Enable the following line to get a VM started. */
            //testStartVM(virtualBox);

            /* Release the VirtualBox object. */
            virtualBox->Release();
            virtualBoxClient->Release();
        }
        else
            printf("Error creating VirtualBox instance! rc=%#lx\n", rc);
    }

    CoUninitialize();
    return 0;
}

