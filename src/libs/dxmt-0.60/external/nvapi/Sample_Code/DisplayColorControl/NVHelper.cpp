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

//////////////////////////////////////////////////////////////////////////////////////////
// @brief:    This sample code shows how to use the NvAPI NvAPI_Disp_ColorControl to control the color values.
// @driver support: R304+
//////////////////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "nvapi.h"
#include <stdarg.h>

NvAPI_ShortString errorDescStr;

char* GetNvAPIStatusString(NvAPI_Status nvapiErrorStatus)
{
    NvAPI_GetErrorMessage(nvapiErrorStatus, errorDescStr);
    return errorDescStr;
}

NvAPI_Status Initialize_NVAPI()
{
    NvAPI_Status nvapiReturnStatus = NVAPI_ERROR;

    nvapiReturnStatus = NvAPI_Initialize();
    if (nvapiReturnStatus != NVAPI_OK)
    {
        printf("\nNvAPI_Initialize() failed.\nReturn Error : %s", GetNvAPIStatusString( nvapiReturnStatus));
    }
    else
    {
        printf("\nNVAPI Initialized successfully");
    }

    return nvapiReturnStatus;
}

NvAPI_Status GetGPUs(NvPhysicalGpuHandle gpuHandleArray[NVAPI_MAX_PHYSICAL_GPUS], NvU32 &gpuCount)
{
    NvAPI_Status nvapiReturnStatus = NVAPI_ERROR;

    // Get all gpu handles.
    nvapiReturnStatus = NvAPI_EnumPhysicalGPUs(gpuHandleArray, &gpuCount);
    if (nvapiReturnStatus != NVAPI_OK)
    {
        printf("\nNvAPI_EnumPhysicalGPUs() failed.\nReturn Error : %s", GetNvAPIStatusString(nvapiReturnStatus));
        return nvapiReturnStatus;
    }

    return nvapiReturnStatus;
}

NvAPI_Status GetConnectedDisplays(NvPhysicalGpuHandle gpuHandle, NV_GPU_DISPLAYIDS *pDisplayID, NvU32 &displayIdCount)
{
    // First call to get the no. of displays connected by passing NULL
    NvAPI_Status nvapiReturnStatus = NVAPI_ERROR;
    NvU32 displayCount = 0;
    nvapiReturnStatus = NvAPI_GPU_GetConnectedDisplayIds(gpuHandle, NULL, &displayCount, 0);
    if (nvapiReturnStatus != NVAPI_OK)
    {
        printf("\nNvAPI_GPU_GetConnectedDisplayIds() failed.\nReturn Error : %s", GetNvAPIStatusString(nvapiReturnStatus));
        return nvapiReturnStatus;
    }

    if (displayCount == 0)
        return nvapiReturnStatus;

    // alocation for the display ids
    NV_GPU_DISPLAYIDS *dispIds = new NV_GPU_DISPLAYIDS[displayCount];
    if (!dispIds)
    {
        return NVAPI_OUT_OF_MEMORY;
    }

    dispIds[0].version = NV_GPU_DISPLAYIDS_VER;

    nvapiReturnStatus = NvAPI_GPU_GetConnectedDisplayIds(gpuHandle, dispIds, &displayCount, 0);
    if (nvapiReturnStatus == NVAPI_OK)
    {
        memcpy_s(pDisplayID, sizeof(NV_GPU_DISPLAYIDS) * NVAPI_MAX_DISPLAYS, dispIds, sizeof(NV_GPU_DISPLAYIDS) * displayCount);
        displayIdCount = displayCount;
    }

    delete[] dispIds;
    return nvapiReturnStatus;
}

void ColorControl(NV_COLOR_CMD command)
{
    NvAPI_Status nvapiReturnStatus = NVAPI_OK;

    switch (command)
    {
        case NV_COLOR_CMD_GET:
        case NV_COLOR_CMD_SET:
            break;
        default:
            nvapiReturnStatus = NVAPI_INVALID_ARGUMENT;
            break;
    }

    if (nvapiReturnStatus != NVAPI_OK)
    {
        printf("\nColorControl failed with error code : %s", GetNvAPIStatusString(nvapiReturnStatus));
        return;
    }

    NV_GPU_DISPLAYIDS pDisplayID[NVAPI_MAX_DISPLAYS];
    NvU32 displayIdCount = 0;

    NvPhysicalGpuHandle gpuHandleArray[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
    NvU32 gpuCount = 0;

    nvapiReturnStatus = GetGPUs(gpuHandleArray, gpuCount);
    if (nvapiReturnStatus != NVAPI_OK)
    {
        printf("\nGetGPUs failed with error code : %s ", GetNvAPIStatusString(nvapiReturnStatus));
        return;
    }

    // Get all active outputs info for all gpu's
    for (NvU32 i = 0; i < gpuCount; ++i)
    {
        printf("\n\nGPU %d (Gpu handle : 0x%x )", i + 1, gpuHandleArray[i]);
        nvapiReturnStatus = GetConnectedDisplays(gpuHandleArray[i], pDisplayID, displayIdCount);
        if (nvapiReturnStatus != NVAPI_OK)
        {
            printf("\nGetConnectedDisplays failed for this GPU with error code : %s ", GetNvAPIStatusString(nvapiReturnStatus));
            continue;
        }

        if (!displayIdCount)
        {
            printf("\n\tNo displays connected on this GPU");
            continue;
        }

        NV_COLOR_DATA colorData = { 0 };
        colorData.version = NV_COLOR_DATA_VER;
        colorData.size = sizeof(NV_COLOR_DATA);

        switch(command)
        {
            case NV_COLOR_CMD_GET:
                 colorData.cmd = NV_COLOR_CMD_GET;
                 break;
            case NV_COLOR_CMD_SET:
                 colorData.cmd = NV_COLOR_CMD_SET;
                 colorData.data.bpc = NV_BPC_10;
                 colorData.data.colorFormat = NV_COLOR_FORMAT_DEFAULT;
                 colorData.data.colorimetry = NV_COLOR_COLORIMETRY_DEFAULT;
                 colorData.data.colorSelectionPolicy = NV_COLOR_SELECTION_POLICY_USER;
                 colorData.data.dynamicRange = NV_DYNAMIC_RANGE_AUTO;
                 break;
        }

        for (NvU32 j = 0; j < displayIdCount; j++)
        {
            printf("\n\tDisplay %d (DisplayId 0x%x):", j + 1, pDisplayID[j].displayId);
            nvapiReturnStatus = NvAPI_Disp_ColorControl(pDisplayID[j].displayId, &colorData);
            if (nvapiReturnStatus != NVAPI_OK)
            {
                printf("\n\t\tNvAPI_Disp_ColorControl failed for this display with error code : %s ", GetNvAPIStatusString(nvapiReturnStatus));
                continue;
            }
            else
            {
                printf("\n\t\tNvAPI_Disp_ColorControl returned successfully for this display");
            }
        }
    }
}

void GetColorControl()
{
    printf("\n\nGetColorControl started\n");
    NvAPI_Status nvapiReturnStatus = NVAPI_ERROR;
    ColorControl(NV_COLOR_CMD_GET);
}

void SetColorControl()
{
    printf("\n\n\nSetColorControl started\n");
    NvAPI_Status nvapiReturnStatus = NVAPI_ERROR;
    ColorControl(NV_COLOR_CMD_SET);
}