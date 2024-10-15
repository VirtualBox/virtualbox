/* $Id: VBoxSmcUtil-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSmcUtil - Quick hack for viewing SMC data on a mac.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/ctype.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
enum
{
    kSMCSuccess = 0,
    kSMCError = 1
};

typedef enum
{
    kSMCUserClientOpen = 0,
    kSMCUserClientClose,
    kSMCHandleYPCEvent,

    kSMCPlaceholder1,
    kSMCNumberOfMethods,

    kSMCReadKey,
    kSMCWriteKey,
    kSMCGetKeyCount,
    kSMCGetKeyFromIndex,
    kSMCGetKeyInfo,

    kSMCFireInterrupt,
    kSMCGetPLimits,
    kSMCGetVers,

    kSMCPlaceholder2,

    kSMCReadStatus,
    kSMCReadResult,
    kSMCVariableCommand
} KSMCFUNCTION;

typedef struct
{
    RTUINT32U           uKey;
    struct
    {
        uint8_t         uMajor;
        uint8_t         uMinor;
        uint8_t         uBuild;
        uint8_t         uReserved;
        uint16_t        uRelease;
    }                   Version;
    struct
    {
        uint16_t        uVer;
        uint16_t        cb;
        uint32_t        uCpuPLimit;
        uint32_t        uGpuPLimit;
        uint32_t        uMemPLimit;
    } SMCPLimitData;

    struct
    {
        IOByteCount     cbData;
        RTUINT32U       uDataType;
        uint8_t         fAttr;
    }                   KeyInfo;

    uint8_t             uResult;
    uint8_t             fStatus;
    uint8_t             bData;
    uint32_t            u32Data;
    uint8_t             abValue[32];
} SMCPARAM;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
io_service_t    g_hSmcService = IO_OBJECT_NULL;
io_connect_t    g_hSmcConnect = IO_OBJECT_NULL;


static int ConnectToSmc(void)
{
    RT_GCC_NO_WARN_DEPRECATED_BEGIN
    g_hSmcService = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("AppleSMC")); /* kIOMasterPortDefault: Deprecated since 12.0. */
    RT_GCC_NO_WARN_DEPRECATED_END
    if (g_hSmcService == IO_OBJECT_NULL)
        return VERR_NOT_FOUND;

    IOReturn rcIo = IOServiceOpen(g_hSmcService, mach_task_self(), 1, &g_hSmcConnect);
    if (rcIo == kIOReturnSuccess && g_hSmcConnect != IO_OBJECT_NULL)
    {
        rcIo = IOConnectCallMethod(g_hSmcConnect, kSMCUserClientOpen, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
        if (rcIo == kIOReturnSuccess)
            return VINF_SUCCESS;
        RTMsgError("kSMCUserClientOpen failed: %#x (%#x)\n", rcIo, rcIo);
    }
    else
    {
        RTMsgError("IOServiceOpen failed: %#x (%#x)\n", rcIo, rcIo);
        g_hSmcConnect = IO_OBJECT_NULL;
    }
    return RTErrConvertFromDarwinIO(rcIo);
}


static void DisconnectFromSmc(void)
{
    if (g_hSmcConnect)
    {
        IOConnectCallMethod(g_hSmcConnect, kSMCUserClientClose, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
        IOServiceClose(g_hSmcConnect);
        g_hSmcConnect = IO_OBJECT_NULL;
    }

    if (g_hSmcService)
    {
        IOServiceClose(g_hSmcService);
        g_hSmcService = IO_OBJECT_NULL;
    }
}

static int CallSmc(KSMCFUNCTION enmFunction, SMCPARAM *pIn, SMCPARAM *pOut)
{
    RT_ZERO(*pOut);
    pIn->bData    = enmFunction;
    size_t cbOut  = sizeof(*pOut);
    IOReturn rcIo = IOConnectCallStructMethod(g_hSmcConnect, kSMCHandleYPCEvent, pIn, sizeof(*pIn), pOut, &cbOut);
    if (rcIo == kIOReturnSuccess)
        return VINF_SUCCESS;
    RTMsgError("SMC call %d failed: rcIo=%d (%#x)\n", enmFunction, rcIo, rcIo);
    return RTErrConvertFromDarwinIO(rcIo);
}

static int GetKeyCount(uint32_t *pcKeys)
{
    SMCPARAM In;
    SMCPARAM Out;
    RT_ZERO(In); RT_ZERO(Out);
    In.KeyInfo.cbData = sizeof(uint32_t);
    int rc = CallSmc(kSMCGetKeyCount, &In, &Out);
    if (RT_SUCCESS(rc))
        *pcKeys = RT_BE2H_U32(Out.u32Data);
    else
        *pcKeys = 1;
    return rc;
}

static int GetKeyByIndex(uint32_t iKey, SMCPARAM *pKeyData)
{
    SMCPARAM In;
    RT_ZERO(In);
    In.u32Data = iKey;
    int rc = CallSmc(kSMCGetKeyFromIndex, &In, pKeyData);
    if (RT_SUCCESS(rc))
    {
        if (pKeyData->uResult == kSMCSuccess)
        {
            SMCPARAM Tmp = *pKeyData;

            /* Get the key info. */
            RT_ZERO(In);
            In.uKey.u = Tmp.uKey.u;
            rc = CallSmc(kSMCGetKeyInfo, &In, pKeyData);
            if (RT_SUCCESS(rc) && pKeyData->uResult == kSMCSuccess)
            {
                Tmp.KeyInfo = pKeyData->KeyInfo;

                /* Get the key value. */
                RT_ZERO(In);
                In.uKey = Tmp.uKey;
                In.KeyInfo = Tmp.KeyInfo;
                rc = CallSmc(kSMCReadKey, &In, pKeyData);
                if (RT_SUCCESS(rc) && (pKeyData->uResult == kSMCSuccess || pKeyData->uResult == 0x85 /* not readable */))
                {
                    pKeyData->uKey    = Tmp.uKey;
                    pKeyData->KeyInfo = Tmp.KeyInfo;
                    rc = VINF_SUCCESS;
                }
                else if (RT_SUCCESS(rc))
                {
                    RTMsgError("kSMCReadKey failed on #%x/%.4s: %#x\n", iKey, Tmp.uKey.au8, pKeyData->uResult);
                    rc = VERR_IO_GEN_FAILURE;
                }
            }
            else if (RT_SUCCESS(rc))
            {
                RTMsgError("kSMCGetKeyInfo failed on #%x/%.4s: %#x\n", iKey, Tmp.uKey.au8, pKeyData->uResult);
                rc = VERR_IO_GEN_FAILURE;
            }
        }
        else
        {
            RTMsgError("kSMCGetKeyFromIndex failed on #%x: %#x\n", iKey, pKeyData->uResult);
            rc = VERR_IO_GEN_FAILURE;
        }
    }
    return rc;
}


static int GetKeyByName(uint32_t uKey, SMCPARAM *pKeyData)
{
    SMCPARAM In;
    RT_ZERO(In);
    In.uKey.u = uKey;
    int rc = CallSmc(kSMCGetKeyInfo, &In, pKeyData);
    if (RT_SUCCESS(rc) && pKeyData->uResult == kSMCSuccess)
    {
        SMCPARAM Tmp = *pKeyData;

        /* Get the key value. */
        RT_ZERO(In);
        In.uKey.u = uKey;
        In.KeyInfo = Tmp.KeyInfo;
        rc = CallSmc(kSMCReadKey, &In, pKeyData);
        if (RT_SUCCESS(rc) && (pKeyData->uResult == kSMCSuccess || pKeyData->uResult == 0x85 /* not readable */))
        {
            pKeyData->uKey.u  = uKey;
            pKeyData->KeyInfo = Tmp.KeyInfo;
            rc = VINF_SUCCESS;
        }
        else if (RT_SUCCESS(rc))
        {
            RTMsgError("kSMCReadKey failed on %.4s: %#x\n", &uKey, pKeyData->uResult);
            rc = VERR_IO_GEN_FAILURE;
        }
    }
    else if (RT_SUCCESS(rc))
    {
        RTMsgError("kSMCGetKeyInfo failed on %.4s: %#x\n", &uKey, pKeyData->uResult);
        rc = VERR_IO_GEN_FAILURE;
    }
    return rc;
}

static void DisplayKey(SMCPARAM *pKey)
{
    pKey->uKey.u = RT_BE2H_U32(pKey->uKey.u);
    pKey->KeyInfo.uDataType.u = RT_BE2H_U32(pKey->KeyInfo.uDataType.u);
    RTPrintf("key=%4.4s  type=%4.4s  cb=%#04x  fAttr=%#04x",
             pKey->uKey.au8, pKey->KeyInfo.uDataType.au8, pKey->KeyInfo.cbData, pKey->KeyInfo.fAttr);
    if (pKey->uResult == kSMCSuccess)
    {
        bool fPrintable = true;
        for (uint32_t off = 0; off < pKey->KeyInfo.cbData; off++)
            if (!RT_C_IS_PRINT(pKey->abValue[off]))
            {
                fPrintable = false;
                break;
            }
       if (fPrintable)
           RTPrintf("  %.*s\n", pKey->KeyInfo.cbData, pKey->abValue);
       else
           RTPrintf("  %.*Rhxs\n", pKey->KeyInfo.cbData, pKey->abValue);
    }
    else if (pKey->uResult == 0x85)
        RTPrintf("  <not readable>\n");
}

static void DisplayKeyByName(uint32_t uKey)
{
    SMCPARAM Key;
    int rc = GetKeyByName(uKey, &Key);
    if (RT_SUCCESS(rc))
        DisplayKey(&Key);
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    rc = ConnectToSmc();
    if (RT_SUCCESS(rc))
    {
        /*
         * Dump the keys.
         */
        uint32_t cKeys;
        rc = GetKeyCount(&cKeys);
        if (RT_SUCCESS(rc))
            RTPrintf("#Keys=%u\n", cKeys);
        for (uint32_t iKey = 0; iKey < cKeys; iKey++)
        {
            SMCPARAM Key;
            rc = GetKeyByIndex(iKey, &Key);
            if (RT_SUCCESS(rc))
            {
                RTPrintf("%#06x: ", iKey);
                DisplayKey(&Key);
            }
        }

        /*
         * Known keys that doesn't make it into the enumeration.
         */
        DisplayKeyByName(RT_MAKE_U32_FROM_MSB_U8('O','S','K','0'));
        DisplayKeyByName(RT_MAKE_U32_FROM_MSB_U8('O','S','K','1'));
        DisplayKeyByName(RT_MAKE_U32_FROM_MSB_U8('O','S','K','2'));

        /* Negative checks, sometimes maybe. */
        DisplayKeyByName(RT_MAKE_U32_FROM_MSB_U8('$','N','u','m'));
        DisplayKeyByName(RT_MAKE_U32_FROM_MSB_U8('M','S','T','f'));
        DisplayKeyByName(RT_MAKE_U32_FROM_MSB_U8('M','S','D','S'));
        DisplayKeyByName(RT_MAKE_U32_FROM_MSB_U8('L','S','O','F'));
    }
    DisconnectFromSmc();

    if (RT_SUCCESS(rc))
        return RTEXITCODE_SUCCESS;
    return RTEXITCODE_FAILURE;
}

