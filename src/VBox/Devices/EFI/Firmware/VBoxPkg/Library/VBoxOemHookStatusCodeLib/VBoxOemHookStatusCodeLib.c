/* $Id: VBoxOemHookStatusCodeLib.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DxeVBoxOemHookStatusCodeLib.c - Logging.
 */


/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <Library/ReportStatusCodeLib.h>
#include <Library/OemHookStatusCodeLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseMemoryLib.h>
#include <Guid/StatusCodeDataTypeId.h>
#include <Guid/StatusCodeDataTypeDebug.h>
#if 0 /* See VBoxSecExtractDebugInfo */
# include <DebugInfo.h>
#endif

#include "VBoxDebugLib.h"
#include "DevEFI.h"



EFI_STATUS EFIAPI
OemHookStatusCodeInitialize(VOID)
{
    VBoxPrintString("OemHookStatusCodeInitialize\n");
    return EFI_SUCCESS;
}


#if 0 /* vvl: With thbe new version the API changed a bit and VA_LIST isn't used any more. Before applying
       *      any changes here I would like to understand in which cases we need this help function.
       * bird: Some components sent information in this format. Search for the UUID or EFI_DEBUG_INFO usage.
       */
/**
 * Helper VBoxSecPeiReportStatusCode uses for catching some odd reports.
 */
static BOOLEAN
VBoxSecExtractDebugInfo(IN CONST EFI_STATUS_CODE_DATA *pData,
                        OUT UINT32 *puErrorLevel,
                        OUT VA_LIST *pVa,
                        OUT CHAR8 **ppszFormat)
{
    EFI_DEBUG_INFO *pDebugInfo;

    if (    !CompareGuid(&pData->Type, &gEfiStatusCodeSpecificDataGuid)
        ||  pData->HeaderSize != sizeof(*pData)
        ||  pData->Size <= sizeof(UINT64) * 12 + sizeof(EFI_DEBUG_INFO) + 1)
        return FALSE;

    pDebugInfo  = (EFI_DEBUG_INFO *)(pData + 1);
    *pVa        = (VA_LIST)(pDebugInfo + 1);
    *ppszFormat = (CHAR8 *)((UINT64 *)pVa + 12);
    return TRUE;
}
#endif

/** Worker that dumps the raw data. */
static void
VBoxOemHookStatusCodeReportRawDump(EFI_STATUS_CODE_TYPE Type,
                                   EFI_STATUS_CODE_VALUE Value,
                                   UINT32 Instance,
                                   CONST EFI_GUID *CallerId)
{
    VBoxPrintString("Report: Type=");
    VBoxPrintHex(Type, sizeof(Type));
    VBoxPrintString(" Value=");
    VBoxPrintHex(Value, sizeof(Value));

    VBoxPrintString(" Instance=");
    VBoxPrintHex(Instance, sizeof(Instance));
    if (CallerId)
    {
        VBoxPrintString(" CallerId=");
        VBoxPrintGuid(CallerId);
    }

#define CASE_PRINT(Head,Print,Tail) \
        case Head ## Print ## Tail : VBoxPrintString(" " #Print); break
    switch (Type & EFI_STATUS_CODE_SEVERITY_MASK) /* quick guess work... */
    {
        CASE_PRINT(EFI_ERROR_,MINOR,);
        CASE_PRINT(EFI_ERROR_,MAJOR,);
        CASE_PRINT(EFI_ERROR_,UNRECOVERED,);
        CASE_PRINT(EFI_ERROR_,UNCONTAINED,);
    }
    switch (Type & EFI_STATUS_CODE_TYPE_MASK) /* quick guess work... */
    {
        CASE_PRINT(EFI_,PROGRESS,_CODE);
        CASE_PRINT(EFI_,ERROR,_CODE);
        CASE_PRINT(EFI_,DEBUG,_CODE);
    }
#undef CASE_PRINT
    VBoxPrintChar('\n');
}


EFI_STATUS EFIAPI
OemHookStatusCodeReport(IN EFI_STATUS_CODE_TYPE Type,
                        IN EFI_STATUS_CODE_VALUE Value,
                        IN UINT32 Instance,
                        IN EFI_GUID *CallerId OPTIONAL,
                        IN EFI_STATUS_CODE_DATA *Data OPTIONAL)
{
    /*
     * Try figure out the data payload
     */
    if (Data != NULL)
    {
        CHAR8      *pszFilename;
        CHAR8      *pszDescription;
        UINT32      uLine;
        UINT32      uErrorLevel;
        BASE_LIST   bs;
        CHAR8      *pszFormat;

        if (ReportStatusCodeExtractAssertInfo(Type, Value, Data, &pszFilename,
                                              &pszDescription, &uLine))
        {
            VBoxPrintString("Assertion Failed! Line=0x");
            VBoxPrintHex(uLine, sizeof(uLine));
            if (pszFilename)
            {
                VBoxPrintString(" File=");
                VBoxPrintString(pszFilename);
            }
            if (pszDescription)
            {
                VBoxPrintString(" Desc=");
                VBoxPrintString(pszDescription);
            }
            VBoxPrintChar('\n');
        }
        else if (   ReportStatusCodeExtractDebugInfo(Data, &uErrorLevel, &bs, &pszFormat)
#if 0 /* See question at VBoxSecExtractDebugInfo. */
                 || VBoxSecExtractDebugInfo(Data, &uErrorLevel, &va, &pszFormat)
#endif
        )
        {
            CHAR8   szBuf[128];
            UINTN   cch;

            cch = AsciiBSPrint(szBuf, sizeof(szBuf), pszFormat, bs);
            if (cch >= sizeof(szBuf))
                cch = sizeof(szBuf) - 1;
            while (     cch > 0
                   &&   (   szBuf[cch - 1] == '\n'
                         || szBuf[cch - 1] == '\r'))
                cch--;
            szBuf[cch] = '\0';

            VBoxPrintString("DBG/");
            VBoxPrintHex(uErrorLevel, sizeof(uErrorLevel));
            VBoxPrintString(": ");
            VBoxPrintString(szBuf);
            VBoxPrintChar('\n');
        }
        else
        {
            /*
             * Unknown data, resort to raw dump of everything.
             */
            VBoxOemHookStatusCodeReportRawDump(Type, Value, Instance, CallerId);

            VBoxPrintString("OemReport: Unknown data type ");
            VBoxPrintGuid(&Data->Type);
            VBoxPrintString(" (Size=");
            VBoxPrintHex(Data->Size, sizeof(Data->Size));
            VBoxPrintString(" HeaderSize=");
            VBoxPrintHex(Data->HeaderSize, sizeof(Data->HeaderSize));
            VBoxPrintString(")\n");
            if (Data->Size > 0 && Data->Size <= 128)
                VBoxPrintHexDump(Data + 1, Data->Size);
        }
    }
    /*
     * No data, do a raw dump.
     */
    else
        VBoxOemHookStatusCodeReportRawDump(Type, Value, Instance, CallerId);

    return EFI_SUCCESS;
}

