/* $Id: vbsfhlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders - File System Driver system helpers
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "vbsf.h"
#include <iprt/err.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef DEBUG
static int volatile g_cAllocations = 0;
#endif


/**
 * Convert VBox error code to NT status code
 *
 * @returns NT status code
 * @param   vrc             VBox status code.
 *
 */
NTSTATUS vbsfNtVBoxStatusToNt(int vrc)
{
    NTSTATUS Status;

    switch (vrc)
    {
        case VINF_SUCCESS:
            Status = STATUS_SUCCESS;
            break;

        case VERR_ACCESS_DENIED:
            Status = STATUS_ACCESS_DENIED;
            break;

        case VERR_NO_MORE_FILES:
            Status = STATUS_NO_MORE_FILES;
            break;

        case VERR_PATH_NOT_FOUND:
            Status = STATUS_OBJECT_PATH_NOT_FOUND;
            break;

        case VERR_FILE_NOT_FOUND:
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            break;

        case VERR_DIR_NOT_EMPTY:
            Status = STATUS_DIRECTORY_NOT_EMPTY;
            break;

        case VERR_SHARING_VIOLATION:
            Status = STATUS_SHARING_VIOLATION;
            break;

        case VERR_FILE_LOCK_VIOLATION:
            Status = STATUS_FILE_LOCK_CONFLICT;
            break;

        case VERR_FILE_LOCK_FAILED:
            Status = STATUS_LOCK_NOT_GRANTED;
            break;

        case VINF_BUFFER_OVERFLOW:
            Status = STATUS_BUFFER_OVERFLOW;
            break;

        case VERR_EOF:
        case VINF_EOF:
            Status = STATUS_END_OF_FILE;
            break;

        case VERR_READ_ERROR:
        case VERR_WRITE_ERROR:
        case VERR_FILE_IO_ERROR:
            Status = STATUS_UNEXPECTED_IO_ERROR;
            break;

        case VERR_WRITE_PROTECT:
            Status = STATUS_MEDIA_WRITE_PROTECTED;
            break;

        case VERR_ALREADY_EXISTS:
            Status = STATUS_OBJECT_NAME_COLLISION;
            break;

        case VERR_NOT_A_DIRECTORY:
            Status = STATUS_NOT_A_DIRECTORY;
            break;

        case VERR_SEEK:
            Status = STATUS_INVALID_PARAMETER;
            break;

        case VERR_INVALID_PARAMETER:
            Status = STATUS_INVALID_PARAMETER;
            break;

        case VERR_NOT_SUPPORTED:
            Status = STATUS_NOT_SUPPORTED;
            break;

        case VERR_INVALID_NAME:
            Status = STATUS_OBJECT_NAME_INVALID;
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            Log(("Unexpected vbox error %Rrc\n",
                 vrc));
            break;
    }
    return Status;
}

/**
 * Wrapper around ExAllocatePoolWithTag.
 */
PVOID vbsfNtAllocNonPagedMem(ULONG cbMemory)
{
    /* Tag is reversed (a.k.a "SHFL") to display correctly in debuggers, so search for "SHFL" */
    PVOID pMemory = ExAllocatePoolWithTag(NonPagedPool, cbMemory, 'LFHS');
    if (NULL != pMemory)
    {
        RtlZeroMemory(pMemory, cbMemory);
#ifdef DEBUG
        int const cAllocations = g_cAllocations += 1;
        Log(("vbsfNtAllocNonPagedMem: Allocated %u bytes of memory at %p (g_iAllocRefCount=%d)\n", cbMemory, pMemory, cAllocations));
#endif
    }
#ifdef DEBUG
    else
        Log(("vbsfNtAllocNonPagedMem: ERROR: Could not allocate %u bytes of memory!\n", cbMemory));
#endif
    return pMemory;
}

/**
 * Wrapper around ExFreePoolWithTag.
 */
void vbsfNtFreeNonPagedMem(PVOID pvMemory)
{
#ifdef DEBUG
    int cAllocations = g_cAllocations -= 1;
    Log(("vbsfNtFreeNonPagedMem: %p (g_cAllocations=%d)\n", pvMemory, cAllocations));
#endif
    AssertPtr(pvMemory);

    /* Tagged allocations must be freed using the same tag as used when allocating the memory. */
    ExFreePoolWithTag(pvMemory, 'LFHS');
}

/** Allocate and initialize a SHFLSTRING from a UNICODE string.
 *
 *  @param ppShflString Where to store the pointer to the allocated SHFLSTRING structure.
 *                      The structure must be deallocated with vbsfNtFreeNonPagedMem.
 *  @param pwc          The UNICODE string. If NULL then SHFL is only allocated.
 *  @param cb           Size of the UNICODE string in bytes without the trailing nul.
 *
 *  @return Status code.
 */
NTSTATUS vbsfNtShflStringFromUnicodeAlloc(PSHFLSTRING *ppShflString, const WCHAR *pwc, uint16_t cb)
{
    NTSTATUS    Status;

    /* Calculate length required for the SHFL structure: header + chars + nul. */
    ULONG const cbShflString = SHFLSTRING_HEADER_SIZE + cb + sizeof(WCHAR);
    PSHFLSTRING pShflString  = (PSHFLSTRING)vbsfNtAllocNonPagedMem(cbShflString);
    if (pShflString)
    {
        if (ShflStringInitBuffer(pShflString, cbShflString))
        {
            if (pwc)
            {
                RtlCopyMemory(pShflString->String.ucs2, pwc, cb);
                pShflString->String.ucs2[cb / sizeof(WCHAR)] = 0;
                pShflString->u16Length = cb; /* without terminating null */
                AssertMsg(pShflString->u16Length + sizeof(WCHAR) == pShflString->u16Size,
                          ("u16Length %d, u16Size %d\n", pShflString->u16Length, pShflString->u16Size));
            }
            else
            {
                /** @todo r=bird: vbsfNtAllocNonPagedMem already zero'ed it...   */
                RtlZeroMemory(pShflString->String.ucs2, cb + sizeof(WCHAR));
                pShflString->u16Length = 0; /* without terminating null */
                AssertMsg(pShflString->u16Size >= sizeof(WCHAR),
                          ("u16Size %d\n", pShflString->u16Size));
            }

            *ppShflString = pShflString;
            Status = STATUS_SUCCESS;
        }
        else
        {
            vbsfNtFreeNonPagedMem(pShflString);
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
        Status = STATUS_INSUFFICIENT_RESOURCES;

    return Status;
}

#if defined(DEBUG) || defined(LOG_ENABLED)

/** Debug routine for translating a minor PNP function to a string.  */
static const char *vbsfNtMinorPnpFunctionName(LONG MinorFunction)
{
    switch (MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            return "IRP_MJ_PNP - IRP_MN_START_DEVICE";
        case IRP_MN_QUERY_REMOVE_DEVICE:
            return "IRP_MJ_PNP - IRP_MN_QUERY_REMOVE_DEVICE";
        case IRP_MN_REMOVE_DEVICE:
            return "IRP_MJ_PNP - IRP_MN_REMOVE_DEVICE";
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            return "IRP_MJ_PNP - IRP_MN_CANCEL_REMOVE_DEVICE";
        case IRP_MN_STOP_DEVICE:
            return "IRP_MJ_PNP - IRP_MN_STOP_DEVICE";
        case IRP_MN_QUERY_STOP_DEVICE:
            return "IRP_MJ_PNP - IRP_MN_QUERY_STOP_DEVICE";
        case IRP_MN_CANCEL_STOP_DEVICE:
            return "IRP_MJ_PNP - IRP_MN_CANCEL_STOP_DEVICE";
        case IRP_MN_QUERY_DEVICE_RELATIONS:
            return "IRP_MJ_PNP - IRP_MN_QUERY_DEVICE_RELATIONS";
        case IRP_MN_QUERY_INTERFACE:
            return "IRP_MJ_PNP - IRP_MN_QUERY_INTERFACE";
        case IRP_MN_QUERY_CAPABILITIES:
            return "IRP_MJ_PNP - IRP_MN_QUERY_CAPABILITIES";
        case IRP_MN_QUERY_RESOURCES:
            return "IRP_MJ_PNP - IRP_MN_QUERY_RESOURCES";
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            return "IRP_MJ_PNP - IRP_MN_QUERY_RESOURCE_REQUIREMENTS";
        case IRP_MN_QUERY_DEVICE_TEXT:
            return "IRP_MJ_PNP - IRP_MN_QUERY_DEVICE_TEXT";
        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            return "IRP_MJ_PNP - IRP_MN_FILTER_RESOURCE_REQUIREMENTS";
        case IRP_MN_READ_CONFIG:
            return "IRP_MJ_PNP - IRP_MN_READ_CONFIG";
        case IRP_MN_WRITE_CONFIG:
            return "IRP_MJ_PNP - IRP_MN_WRITE_CONFIG";
        case IRP_MN_EJECT:
            return "IRP_MJ_PNP - IRP_MN_EJECT";
        case IRP_MN_SET_LOCK:
            return "IRP_MJ_PNP - IRP_MN_SET_LOCK";
        case IRP_MN_QUERY_ID:
            return "IRP_MJ_PNP - IRP_MN_QUERY_ID";
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            return "IRP_MJ_PNP - IRP_MN_QUERY_PNP_DEVICE_STATE";
        case IRP_MN_QUERY_BUS_INFORMATION:
            return "IRP_MJ_PNP - IRP_MN_QUERY_BUS_INFORMATION";
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            return "IRP_MJ_PNP - IRP_MN_DEVICE_USAGE_NOTIFICATION";
        case IRP_MN_SURPRISE_REMOVAL:
            return "IRP_MJ_PNP - IRP_MN_SURPRISE_REMOVAL";
        default:
            return "IRP_MJ_PNP - unknown_pnp_irp";
    }
}

/** Debug routine for translating a major+minor IPR function to a string.  */
const char *vbsfNtMajorFunctionName(UCHAR MajorFunction, LONG MinorFunction)
{
    switch (MajorFunction)
    {
        RT_CASE_RET_STR(IRP_MJ_CREATE);
        RT_CASE_RET_STR(IRP_MJ_CREATE_NAMED_PIPE);
        RT_CASE_RET_STR(IRP_MJ_CLOSE);
        RT_CASE_RET_STR(IRP_MJ_READ);
        RT_CASE_RET_STR(IRP_MJ_WRITE);
        RT_CASE_RET_STR(IRP_MJ_QUERY_INFORMATION);
        RT_CASE_RET_STR(IRP_MJ_SET_INFORMATION);
        RT_CASE_RET_STR(IRP_MJ_QUERY_EA);
        RT_CASE_RET_STR(IRP_MJ_SET_EA);
        RT_CASE_RET_STR(IRP_MJ_FLUSH_BUFFERS);
        RT_CASE_RET_STR(IRP_MJ_QUERY_VOLUME_INFORMATION);
        RT_CASE_RET_STR(IRP_MJ_SET_VOLUME_INFORMATION);
        RT_CASE_RET_STR(IRP_MJ_DIRECTORY_CONTROL);
        RT_CASE_RET_STR(IRP_MJ_FILE_SYSTEM_CONTROL);
        RT_CASE_RET_STR(IRP_MJ_DEVICE_CONTROL);
        RT_CASE_RET_STR(IRP_MJ_INTERNAL_DEVICE_CONTROL);
        RT_CASE_RET_STR(IRP_MJ_SHUTDOWN);
        RT_CASE_RET_STR(IRP_MJ_LOCK_CONTROL);
        RT_CASE_RET_STR(IRP_MJ_CLEANUP);
        RT_CASE_RET_STR(IRP_MJ_CREATE_MAILSLOT);
        RT_CASE_RET_STR(IRP_MJ_QUERY_SECURITY);
        RT_CASE_RET_STR(IRP_MJ_SET_SECURITY);
        RT_CASE_RET_STR(IRP_MJ_POWER);
        RT_CASE_RET_STR(IRP_MJ_SYSTEM_CONTROL);
        RT_CASE_RET_STR(IRP_MJ_DEVICE_CHANGE);
        RT_CASE_RET_STR(IRP_MJ_QUERY_QUOTA);
        RT_CASE_RET_STR(IRP_MJ_SET_QUOTA);
        case IRP_MJ_PNP:
            return vbsfNtMinorPnpFunctionName(MinorFunction);
        default:
            return "IRP_MJ_UNKNOWN";
    }
}

#endif /* DEBUG || LOG_ENABLED */

