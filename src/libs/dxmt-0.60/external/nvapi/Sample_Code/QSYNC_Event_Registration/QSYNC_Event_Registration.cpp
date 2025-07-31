/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

////////////////////////////////////////////////////////////////////////////////////
// @brief: This is the entry point for the console application.
//         This program registers QSYNC event to the system.
//         The received QSYNC event will be logged on the console of this application.
//
// @assumptions: This code is designed for Win10+. It assumes that the system has
//               atleast one GPU and a QSYNC board.
//
// @driver support: R460+
////////////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include <stdio.h>
#include <tchar.h>
#include <Windows.h>
#include "nvapi.h"


#define STATUS_SUCCESS  0
#define STATUS_FAIL     1


// forward declarations
void __cdecl HandleQSYNCEvent(NV_QSYNC_EVENT_DATA qsyncEventData, void *callbackParam);

int _tmain(int argc, _TCHAR* argv[])
{
    int iRet = STATUS_FAIL;  // default to fail
    NvEventHandle hEventQSYNC = NULL;
    NvAPI_Status status = NVAPI_ERROR;

    do
    {
        status = NvAPI_Initialize();
        if (NVAPI_OK != status)
        {
            // Initialization failed
            printf("NvAPI_Initialize() failed = 0x%x", status);
            break;
        }

        NvGSyncDeviceHandle nvGSyncHandles[NVAPI_MAX_GSYNC_DEVICES];
        NvU32 gsyncCount = 0;
        status = NvAPI_GSync_EnumSyncDevices(nvGSyncHandles, &gsyncCount);
        if (status == NVAPI_NVIDIA_DEVICE_NOT_FOUND || gsyncCount == 0)
        {
            printf("No Quadro Sync Devices found on this system\n");
            break;
        }

        NV_EVENT_REGISTER_CALLBACK nvQSYNCEventRegisterCallback;
        ZeroMemory(&nvQSYNCEventRegisterCallback, sizeof(nvQSYNCEventRegisterCallback));
        nvQSYNCEventRegisterCallback.nvCallBackFunc.nvQSYNCEventCallback = HandleQSYNCEvent;
        nvQSYNCEventRegisterCallback.eventId = NV_EVENT_TYPE_QSYNC;
        nvQSYNCEventRegisterCallback.version = NV_EVENT_REGISTER_CALLBACK_VERSION;

        // Register for Quadro sync events.
        // This is central callback for the events of all Quadro Sync devices in the system.
        status = NvAPI_Event_RegisterCallback(&nvQSYNCEventRegisterCallback, &hEventQSYNC);
        if (NVAPI_OK != status)
        {
            printf("NvAPI_Event_RegisterCallback() failed = 0x%x", status);
            break;
        }

        // Wait for user's input for exit. Log all events until then.
        printf("The console would log Quadro Sync (QSYNC) related events\n");
        printf("(Please wait for events or Press any key to exit...)\n");
        getchar();

        // we've succeeded
        iRet = STATUS_SUCCESS;
    } while (false);

    // Cleanup
    if (hEventQSYNC != NULL)
    {
        status = NvAPI_Event_UnregisterCallback(hEventQSYNC);
        if (status != NVAPI_OK)
        {
            iRet = STATUS_FAIL;
            printf("NvAPI_Event_UnregisterCallback() failed = 0x%x", status);
        }
        hEventQSYNC = NULL;
    }

    return iRet;
}


// This function is hit when a QSYNC event is broadcasted by the display driver.
void __cdecl HandleQSYNCEvent(NV_QSYNC_EVENT_DATA qsyncEventData, void *callbackParam)
{
    switch (qsyncEventData.qsyncEvent)
    {
        case NV_QSYNC_EVENT_SYNC_LOSS:
            printf("\n Received event NV_QSYNC_EVENT_SYNC_LOSS");
            break;
        case NV_QSYNC_EVENT_SYNC_GAIN:
            printf("\n Received event NV_QSYNC_EVENT_SYNC_GAIN");
            break;
        case NV_QSYNC_EVENT_HOUSESYNC_GAIN:
            printf("\n Received event NV_QSYNC_EVENT_HOUSESYNC_GAIN");
            break;
        case NV_QSYNC_EVENT_HOUSESYNC_LOSS:
            printf("\n Received event NV_QSYNC_EVENT_HOUSESYNC_LOSS");
            break;
        case NV_QSYNC_EVENT_RJ45_GAIN:
            printf("\n Received event NV_QSYNC_EVENT_RJ45_GAIN");
            break;
        case NV_QSYNC_EVENT_RJ45_LOSS:
            printf("\n Received event NV_QSYNC_EVENT_RJ45_LOSS");
            break;
        default:
            printf("\n Received Unknown event");
            break;
    }
}