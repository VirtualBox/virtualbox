/* $Id: DisplayUtils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Implementation of IDisplay helpers, currently only used in VBoxSVC.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#include <DisplayUtils.h>

#include <iprt/log.h>
#include <VBox/err.h>
#include <VBox/vmm/ssm.h>
#include <VBoxVideo.h>

int readSavedDisplayScreenshot(SsmStream &ssmStream, const Utf8Str &strStateFilePath,
                               uint32_t u32Type, uint8_t **ppu8Data, uint32_t *pcbData,
                               uint32_t *pu32Width, uint32_t *pu32Height)
{
    LogFlowFunc(("u32Type = %d [%s]\n", u32Type, strStateFilePath.c_str()));

    /** @todo cache read data */
    if (strStateFilePath.isEmpty())
    {
        /* No saved state data. */
        return VERR_NOT_SUPPORTED;
    }

    uint8_t *pu8Data = NULL;
    uint32_t cbData = 0;
    uint32_t u32Width = 0;
    uint32_t u32Height = 0;

    PSSMHANDLE pSSM;
    int vrc = ssmStream.open(strStateFilePath.c_str(), false /*fWrite*/, &pSSM);
    if (RT_SUCCESS(vrc))
    {
        uint32_t uVersion;
        vrc = SSMR3Seek(pSSM, "DisplayScreenshot", 1100 /*iInstance*/, &uVersion);
        if (RT_SUCCESS(vrc))
        {
            if (uVersion == sSSMDisplayScreenshotVer)
            {
                uint32_t cBlocks;
                vrc = SSMR3GetU32(pSSM, &cBlocks);
                AssertRCReturn(vrc, vrc);

                for (uint32_t i = 0; i < cBlocks; i++)
                {
                    uint32_t cbBlock;
                    vrc = SSMR3GetU32(pSSM, &cbBlock);
                    AssertRCBreak(vrc);

                    uint32_t typeOfBlock;
                    vrc = SSMR3GetU32(pSSM, &typeOfBlock);
                    AssertRCBreak(vrc);

                    LogFlowFunc(("[%d] type %d, size %d bytes\n", i, typeOfBlock, cbBlock));

                    if (typeOfBlock == u32Type)
                    {
                        if (cbBlock > 2 * sizeof(uint32_t))
                        {
                            cbData = (uint32_t)(cbBlock - 2 * sizeof(uint32_t));
                            pu8Data = (uint8_t *)RTMemAlloc(cbData);
                            if (pu8Data == NULL)
                            {
                                vrc = VERR_NO_MEMORY;
                                break;
                            }

                            vrc = SSMR3GetU32(pSSM, &u32Width);
                            AssertRCBreak(vrc);
                            vrc = SSMR3GetU32(pSSM, &u32Height);
                            AssertRCBreak(vrc);
                            vrc = SSMR3GetMem(pSSM, pu8Data, cbData);
                            AssertRCBreak(vrc);
                        }
                        else
                        {
                            /* No saved state data. */
                            vrc = VERR_NOT_SUPPORTED;
                        }

                        break;
                    }
                    else
                    {
                        /* displaySSMSaveScreenshot did not write any data, if
                         * cbBlock was == 2 * sizeof (uint32_t).
                         */
                        if (cbBlock > 2 * sizeof (uint32_t))
                        {
                            vrc = SSMR3Skip(pSSM, cbBlock);
                            AssertRCBreak(vrc);
                        }
                    }
                }
            }
            else
            {
                vrc = VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
            }
        }

        ssmStream.close();
    }

    if (RT_SUCCESS(vrc))
    {
        if (u32Type == 0 && cbData % 4 != 0)
        {
            /* Bitmap is 32bpp, so data is invalid. */
            vrc = VERR_SSM_UNEXPECTED_DATA;
        }
    }

    if (RT_SUCCESS(vrc))
    {
        *ppu8Data = pu8Data;
        *pcbData = cbData;
        *pu32Width = u32Width;
        *pu32Height = u32Height;
        LogFlowFunc(("cbData %d, u32Width %d, u32Height %d\n", cbData, u32Width, u32Height));
    }

    LogFlowFunc(("vrc %Rrc\n", vrc));
    return vrc;
}

void freeSavedDisplayScreenshot(uint8_t *pu8Data)
{
    /** @todo not necessary when caching is implemented. */
    RTMemFree(pu8Data);
}

int readSavedGuestScreenInfo(SsmStream &ssmStream, const Utf8Str &strStateFilePath,
                             uint32_t u32ScreenId, uint32_t *pu32OriginX, uint32_t *pu32OriginY,
                             uint32_t *pu32Width, uint32_t *pu32Height, uint16_t *pu16Flags)
{
    LogFlowFunc(("u32ScreenId = %d [%s]\n", u32ScreenId, strStateFilePath.c_str()));

    /** @todo cache read data */
    if (strStateFilePath.isEmpty())
    {
        /* No saved state data. */
        return VERR_NOT_SUPPORTED;
    }

    PSSMHANDLE pSSM;
    int vrc = ssmStream.open(strStateFilePath.c_str(), false /*fWrite*/, &pSSM);
    if (RT_SUCCESS(vrc))
    {
        uint32_t uVersion;
        vrc = SSMR3Seek(pSSM, "DisplayData", 0 /*iInstance*/, &uVersion);
        if (RT_SUCCESS(vrc))
        {
            /* Starting from sSSMDisplayVer2 we have pu32Width and pu32Height.
             * Starting from sSSMDisplayVer3 we have all the rest of parameters we need. */
            if (uVersion >= sSSMDisplayVer2)
            {
                uint32_t cMonitors;
                SSMR3GetU32(pSSM, &cMonitors);
                if (u32ScreenId > cMonitors)
                {
                    vrc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    if (uVersion == sSSMDisplayVer2)
                    {
                        /* Skip all previous monitors, each 5 uint32_t, and the first 3 uint32_t entries. */
                        SSMR3Skip(pSSM, u32ScreenId * 5 * sizeof(uint32_t) + 3 * sizeof(uint32_t));
                        SSMR3GetU32(pSSM, pu32Width);
                        SSMR3GetU32(pSSM, pu32Height);
                        *pu32OriginX = 0;
                        *pu32OriginY = 0;
                        *pu16Flags = VBVA_SCREEN_F_ACTIVE;
                    }
                    else
                    {
                        /* Skip all previous monitors, each 8 uint32_t, and the first 3 uint32_t entries. */
                        SSMR3Skip(pSSM, u32ScreenId * 8 * sizeof(uint32_t) + 3 * sizeof(uint32_t));
                        SSMR3GetU32(pSSM, pu32Width);
                        SSMR3GetU32(pSSM, pu32Height);
                        SSMR3GetU32(pSSM, pu32OriginX);
                        SSMR3GetU32(pSSM, pu32OriginY);
                        uint32_t u32Flags = 0;
                        SSMR3GetU32(pSSM, &u32Flags);
                        *pu16Flags = (uint16_t)u32Flags;
                    }
                }
            }
            else
            {
                vrc = VERR_NOT_SUPPORTED;
            }
        }

        ssmStream.close();
    }

    LogFlowFunc(("vrc %Rrc\n", vrc));
    return vrc;
}

