/* $Id: DBGFR3BugCheck.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, NT Bug Checks.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/tm.h>
#include "DBGFInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/message.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNDBGFHANDLERINT dbgfR3BugCheckInfo;


/**
 * Initializes the bug check state and registers the info callback.
 *
 * No termination function needed.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 */
int dbgfR3BugCheckInit(PVM pVM)
{
    PCFGMNODE const pCfgNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "DBGF/");

    /** @cfgm{/DBGF/SuspendOnBsod, boolean, false}
     * Enables suspending (pausing) of the VM on a BSOD.
     */
    int rc = CFGMR3QueryBoolDef(pCfgNode, "SuspendOnBsod", &pVM->dbgf.s.BugCheck.fCfgSuspendOnBsod, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/DBGF/PowerOffOnBsod, boolean, false}
     * Enables powering off the VM automatically on a BSOD.
     */
    rc = CFGMR3QueryBoolDef(pCfgNode, "PowerOffOnBsod", &pVM->dbgf.s.BugCheck.fCfgPowerOffOnBsod, false);
    AssertLogRelRCReturn(rc, rc);

    pVM->dbgf.s.BugCheck.idCpu    = NIL_VMCPUID;
    pVM->dbgf.s.BugCheck.enmEvent = DBGFEVENT_END;

    return DBGFR3InfoRegisterInternal(pVM, "bugcheck",
                                      "Show bugcheck info.  Can specify bug check code and parameters to lookup info.",
                                      dbgfR3BugCheckInfo);
}


/**
 * Names a few common NT status codes for DBGFR3FormatBugCheck.
 */
static const char *dbgfR3GetNtStatusName(uint32_t uNtStatus)
{
    switch (uNtStatus)
    {
        case 0x80000001: return " - STATUS_GUARD_PAGE_VIOLATION";
        case 0x80000002: return " - STATUS_DATATYPE_MISALIGNMENT";
        case 0x80000003: return " - STATUS_BREAKPOINT";
        case 0x80000004: return " - STATUS_SINGLE_STEP";
        case 0xc0000008: return " - STATUS_INVALID_HANDLE";
        case 0xc0000005: return " - STATUS_ACCESS_VIOLATION";
        case 0xc0000027: return " - STATUS_UNWIND";
        case 0xc0000028: return " - STATUS_BAD_STACK";
        case 0xc0000029: return " - STATUS_INVALID_UNWIND_TARGET";
        default:         return "";
    }
}


/**
 * Formats a symbol for DBGFR3FormatBugCheck.
 */
static const char *dbgfR3FormatSymbol(PUVM pUVM, char *pszSymbol, size_t cchSymbol, const char *pszPrefix, uint64_t uFlatAddr)
{
    DBGFADDRESS  Addr;
    RTGCINTPTR   offDisp = 0;
    PRTDBGSYMBOL pSym = DBGFR3AsSymbolByAddrA(pUVM, DBGF_AS_GLOBAL, DBGFR3AddrFromFlat(pUVM, &Addr, uFlatAddr),
                                              RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                              &offDisp, NULL /*phMod*/);
    if (pSym)
    {
        if (!offDisp)
            RTStrPrintf(pszSymbol, cchSymbol, "%s%s", pszPrefix, pSym->szName);
        else if (offDisp > 0)
            RTStrPrintf(pszSymbol, cchSymbol, "%s%s + %#RX64", pszPrefix, pSym->szName, (uint64_t)offDisp);
        else
            RTStrPrintf(pszSymbol, cchSymbol, "%s%s - %#RX64", pszPrefix, pSym->szName, (uint64_t)-offDisp);
        RTDbgSymbolFree(pSym);
    }
    else
        *pszSymbol = '\0';
    return pszSymbol;
}


/**
 * Formats a windows bug check (BSOD).
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_BUFFER_OVERFLOW if there is more data than the buffer can handle.
 *
 * @param   pUVM                The usermode VM handle.
 * @param   pszDetails          The output buffer.
 * @param   cbDetails           The size of the output buffer.
 * @param   uBugCheck           The bugheck code.
 * @param   uP1                 Bug check parameter 1.
 * @param   uP2                 Bug check parameter 2.
 * @param   uP3                 Bug check parameter 3.
 * @param   uP4                 Bug check parameter 4.
 */
VMMR3DECL(int) DBGFR3FormatBugCheck(PUVM pUVM, char *pszDetails, size_t cbDetails,
                                    uint64_t uBugCheck, uint64_t uP1, uint64_t uP2, uint64_t uP3, uint64_t uP4)
{
    /*
     * Start with bug check line typically seen in windbg.
     */
    size_t cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                 "BugCheck %RX64 {%RX64, %RX64, %RX64, %RX64}\n", uBugCheck, uP1, uP2, uP3, uP4);
    if (cchUsed >= cbDetails)
        return VINF_BUFFER_OVERFLOW;
    pszDetails += cchUsed;
    cbDetails  -= cchUsed;

    /*
     * Try name the bugcheck and format parameters if we can/care.
     */
    char szSym[512];
    switch (uBugCheck)
    {
        case 0x00000001: cchUsed = RTStrPrintf(pszDetails, cbDetails, "APC_INDEX_MISMATCH\n"); break;
        case 0x00000002: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DEVICE_QUEUE_NOT_BUSY\n"); break;
        case 0x00000003: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_AFFINITY_SET\n"); break;
        case 0x00000004: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_DATA_ACCESS_TRAP\n"); break;
        case 0x00000005: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_PROCESS_ATTACH_ATTEMPT\n"); break;
        case 0x00000006: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_PROCESS_DETACH_ATTEMPT\n"); break;
        case 0x00000007: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_SOFTWARE_INTERRUPT\n"); break;
        case 0x00000008: cchUsed = RTStrPrintf(pszDetails, cbDetails, "IRQL_NOT_DISPATCH_LEVEL\n"); break;
        case 0x00000009: cchUsed = RTStrPrintf(pszDetails, cbDetails, "IRQL_NOT_GREATER_OR_EQUAL\n"); break;
        case 0x0000000a:
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "IRQL_NOT_LESS_OR_EQUAL\n"
                                  "P1: %016RX64 - memory referenced\n"
                                  "P2: %016RX64 - IRQL\n"
                                  "P3: %016RX64 - bitfield\n"
                                  "    b0: %u - %s operation\n"
                                  "    b3: %u - %sexecute operation\n"
                                  "P4: %016RX64 - EIP/RIP%s\n",
                                  uP1, uP2, uP3,
                                  RT_BOOL(uP3 & RT_BIT_64(0)), uP3 & RT_BIT_64(0) ? "write" : "read",
                                  RT_BOOL(uP3 & RT_BIT_64(3)), uP3 & RT_BIT_64(3) ? "not-" : "",
                                  uP4, dbgfR3FormatSymbol(pUVM, szSym, sizeof(szSym), ": ", uP4));
            break;
        case 0x0000000b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NO_EXCEPTION_HANDLING_SUPPORT\n"); break;
        case 0x0000000c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MAXIMUM_WAIT_OBJECTS_EXCEEDED\n"); break;
        case 0x0000000d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MUTEX_LEVEL_NUMBER_VIOLATION\n"); break;
        case 0x0000000e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NO_USER_MODE_CONTEXT\n"); break;
        case 0x0000000f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SPIN_LOCK_ALREADY_OWNED\n"); break;
        case 0x00000010: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SPIN_LOCK_NOT_OWNED\n"); break;
        case 0x00000011: cchUsed = RTStrPrintf(pszDetails, cbDetails, "THREAD_NOT_MUTEX_OWNER\n"); break;
        case 0x00000012: cchUsed = RTStrPrintf(pszDetails, cbDetails, "TRAP_CAUSE_UNKNOWN\n"); break;
        case 0x00000013: cchUsed = RTStrPrintf(pszDetails, cbDetails, "EMPTY_THREAD_REAPER_LIST\n"); break;
        case 0x00000014: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CREATE_DELETE_LOCK_NOT_LOCKED\n"); break;
        case 0x00000015: cchUsed = RTStrPrintf(pszDetails, cbDetails, "LAST_CHANCE_CALLED_FROM_KMODE\n"); break;
        case 0x00000016: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CID_HANDLE_CREATION\n"); break;
        case 0x00000017: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CID_HANDLE_DELETION\n"); break;
        case 0x00000018: cchUsed = RTStrPrintf(pszDetails, cbDetails, "REFERENCE_BY_POINTER\n"); break;
        case 0x00000019: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BAD_POOL_HEADER\n"); break;
        case 0x0000001a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MEMORY_MANAGEMENT\n"); break;
        case 0x0000001b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PFN_SHARE_COUNT\n"); break;
        case 0x0000001c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PFN_REFERENCE_COUNT\n"); break;
        case 0x0000001d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NO_SPIN_LOCK_AVAILABLE\n"); break;
        case 0x0000001e:
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "KMODE_EXCEPTION_NOT_HANDLED\n"
                                  "P1: %016RX64 - exception code%s\n"
                                  "P2: %016RX64 - EIP/RIP%s\n"
                                  "P3: %016RX64 - Xcpt param #0\n"
                                  "P4: %016RX64 - Xcpt param #1\n",
                                  uP1, dbgfR3GetNtStatusName((uint32_t)uP1),
                                  uP2, dbgfR3FormatSymbol(pUVM, szSym, sizeof(szSym), ": ", uP2),
                                  uP3, uP4);
            break;
        case 0x0000001f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SHARED_RESOURCE_CONV_ERROR\n"); break;
        case 0x00000020: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_APC_PENDING_DURING_EXIT\n"); break;
        case 0x00000021: cchUsed = RTStrPrintf(pszDetails, cbDetails, "QUOTA_UNDERFLOW\n"); break;
        case 0x00000022: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FILE_SYSTEM\n"); break;
        case 0x00000023: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FAT_FILE_SYSTEM\n"); break;
        case 0x00000024: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NTFS_FILE_SYSTEM\n"); break;
        case 0x00000025: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NPFS_FILE_SYSTEM\n"); break;
        case 0x00000026: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CDFS_FILE_SYSTEM\n"); break;
        case 0x00000027: cchUsed = RTStrPrintf(pszDetails, cbDetails, "RDR_FILE_SYSTEM\n"); break;
        case 0x00000028: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CORRUPT_ACCESS_TOKEN\n"); break;
        case 0x00000029: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SECURITY_SYSTEM\n"); break;
        case 0x0000002a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INCONSISTENT_IRP\n"); break;
        case 0x0000002b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PANIC_STACK_SWITCH\n"); break;
        case 0x0000002c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PORT_DRIVER_INTERNAL\n"); break;
        case 0x0000002d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SCSI_DISK_DRIVER_INTERNAL\n"); break;
        case 0x0000002e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DATA_BUS_ERROR\n"); break;
        case 0x0000002f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INSTRUCTION_BUS_ERROR\n"); break;
        case 0x00000030: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SET_OF_INVALID_CONTEXT\n"); break;
        case 0x00000031: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PHASE0_INITIALIZATION_FAILED\n"); break;
        case 0x00000032: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PHASE1_INITIALIZATION_FAILED\n"); break;
        case 0x00000033: cchUsed = RTStrPrintf(pszDetails, cbDetails, "UNEXPECTED_INITIALIZATION_CALL\n"); break;
        case 0x00000034: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CACHE_MANAGER\n"); break;
        case 0x00000035: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NO_MORE_IRP_STACK_LOCATIONS\n"); break;
        case 0x00000036: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DEVICE_REFERENCE_COUNT_NOT_ZERO\n"); break;
        case 0x00000037: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FLOPPY_INTERNAL_ERROR\n"); break;
        case 0x00000038: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SERIAL_DRIVER_INTERNAL\n"); break;
        case 0x00000039: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SYSTEM_EXIT_OWNED_MUTEX\n"); break;
        case 0x0000003a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SYSTEM_UNWIND_PREVIOUS_USER\n"); break;
        case 0x0000003b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SYSTEM_SERVICE_EXCEPTION\n"); break;
        case 0x0000003c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INTERRUPT_UNWIND_ATTEMPTED\n"); break;
        case 0x0000003d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INTERRUPT_EXCEPTION_NOT_HANDLED\n"); break;
        case 0x0000003e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED\n"); break;
        case 0x0000003f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NO_MORE_SYSTEM_PTES\n"); break;
        case 0x00000040: cchUsed = RTStrPrintf(pszDetails, cbDetails, "TARGET_MDL_TOO_SMALL\n"); break;
        case 0x00000041: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MUST_SUCCEED_POOL_EMPTY\n"); break;
        case 0x00000042: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ATDISK_DRIVER_INTERNAL\n"); break;
        case 0x00000043: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NO_SUCH_PARTITION\n"); break;
        case 0x00000044: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MULTIPLE_IRP_COMPLETE_REQUESTS\n"); break;
        case 0x00000045: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INSUFFICIENT_SYSTEM_MAP_REGS\n"); break;
        case 0x00000046: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DEREF_UNKNOWN_LOGON_SESSION\n"); break;
        case 0x00000047: cchUsed = RTStrPrintf(pszDetails, cbDetails, "REF_UNKNOWN_LOGON_SESSION\n"); break;
        case 0x00000048: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CANCEL_STATE_IN_COMPLETED_IRP\n"); break;
        case 0x00000049: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PAGE_FAULT_WITH_INTERRUPTS_OFF\n"); break;
        case 0x0000004a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "IRQL_GT_ZERO_AT_SYSTEM_SERVICE\n"); break;
        case 0x0000004b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "STREAMS_INTERNAL_ERROR\n"); break;
        case 0x0000004c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FATAL_UNHANDLED_HARD_ERROR\n"); break;
        case 0x0000004d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NO_PAGES_AVAILABLE\n"); break;
        case 0x0000004e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PFN_LIST_CORRUPT\n"); break;
        case 0x0000004f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NDIS_INTERNAL_ERROR\n"); break;
        case 0x00000050: /* PAGE_FAULT_IN_NONPAGED_AREA */
        case 0x10000050: /* PAGE_FAULT_IN_NONPAGED_AREA_M */
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "PAGE_FAULT_IN_NONPAGED_AREA%s\n"
                                  "P1: %016RX64 - memory referenced\n"
                                  "P2: %016RX64 - IRQL\n"
                                  "P3: %016RX64 - %s\n"
                                  "P4: %016RX64 - reserved\n",
                                  uBugCheck & 0x10000000 ? "_M" : "", uP1, uP2, uP3, uP3 & RT_BIT_64(0) ? "write" : "read", uP4);
            break;
        case 0x00000051: cchUsed = RTStrPrintf(pszDetails, cbDetails, "REGISTRY_ERROR\n"); break;
        case 0x00000052: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MAILSLOT_FILE_SYSTEM\n"); break;
        case 0x00000053: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NO_BOOT_DEVICE\n"); break;
        case 0x00000054: cchUsed = RTStrPrintf(pszDetails, cbDetails, "LM_SERVER_INTERNAL_ERROR\n"); break;
        case 0x00000055: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DATA_COHERENCY_EXCEPTION\n"); break;
        case 0x00000056: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INSTRUCTION_COHERENCY_EXCEPTION\n"); break;
        case 0x00000057: cchUsed = RTStrPrintf(pszDetails, cbDetails, "XNS_INTERNAL_ERROR\n"); break;
        case 0x00000058: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VOLMGRX_INTERNAL_ERROR\n"); break;
        case 0x00000059: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PINBALL_FILE_SYSTEM\n"); break;
        case 0x0000005a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CRITICAL_SERVICE_FAILED\n"); break;
        case 0x0000005b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SET_ENV_VAR_FAILED\n"); break;
        case 0x0000005c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HAL_INITIALIZATION_FAILED\n"); break;
        case 0x0000005d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "UNSUPPORTED_PROCESSOR\n"); break;
        case 0x0000005e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "OBJECT_INITIALIZATION_FAILED\n"); break;
        case 0x0000005f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SECURITY_INITIALIZATION_FAILED\n"); break;
        case 0x00000060: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PROCESS_INITIALIZATION_FAILED\n"); break;
        case 0x00000061: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HAL1_INITIALIZATION_FAILED\n"); break;
        case 0x00000062: cchUsed = RTStrPrintf(pszDetails, cbDetails, "OBJECT1_INITIALIZATION_FAILED\n"); break;
        case 0x00000063: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SECURITY1_INITIALIZATION_FAILED\n"); break;
        case 0x00000064: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SYMBOLIC_INITIALIZATION_FAILED\n"); break;
        case 0x00000065: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MEMORY1_INITIALIZATION_FAILED\n"); break;
        case 0x00000066: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CACHE_INITIALIZATION_FAILED\n"); break;
        case 0x00000067: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CONFIG_INITIALIZATION_FAILED\n"); break;
        case 0x00000068: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FILE_INITIALIZATION_FAILED\n"); break;
        case 0x00000069: cchUsed = RTStrPrintf(pszDetails, cbDetails, "IO1_INITIALIZATION_FAILED\n"); break;
        case 0x0000006a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "LPC_INITIALIZATION_FAILED\n"); break;
        case 0x0000006b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PROCESS1_INITIALIZATION_FAILED\n"); break;
        case 0x0000006c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "REFMON_INITIALIZATION_FAILED\n"); break;
        case 0x0000006d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SESSION1_INITIALIZATION_FAILED\n"); break;
        case 0x0000006e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BOOTPROC_INITIALIZATION_FAILED\n"); break;
        case 0x0000006f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VSL_INITIALIZATION_FAILED\n"); break;
        case 0x00000070: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SOFT_RESTART_FATAL_ERROR\n"); break;
        case 0x00000072: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ASSIGN_DRIVE_LETTERS_FAILED\n"); break;
        case 0x00000073: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CONFIG_LIST_FAILED\n"); break;
        case 0x00000074: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BAD_SYSTEM_CONFIG_INFO\n"); break;
        case 0x00000075: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CANNOT_WRITE_CONFIGURATION\n"); break;
        case 0x00000076: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PROCESS_HAS_LOCKED_PAGES\n"); break;
        case 0x00000077: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_STACK_INPAGE_ERROR\n"); break;
        case 0x00000078: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PHASE0_EXCEPTION\n"); break;
        case 0x00000079: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MISMATCHED_HAL\n"); break;
        case 0x0000007a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_DATA_INPAGE_ERROR\n"); break;
        case 0x0000007b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INACCESSIBLE_BOOT_DEVICE\n"); break;
        case 0x0000007c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BUGCODE_NDIS_DRIVER\n"); break;
        case 0x0000007d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INSTALL_MORE_MEMORY\n"); break;
        case 0x0000007e: /* SYSTEM_THREAD_EXCEPTION_NOT_HANDLED */
        case 0x1000007e: /* SYSTEM_THREAD_EXCEPTION_NOT_HANDLED_M */
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED%s\n"
                                  "P1: %016RX64 - exception code%s\n"
                                  "P2: %016RX64 - EIP/RIP%s\n"
                                  "P3: %016RX64 - Xcpt address\n"
                                  "P4: %016RX64 - Context address\n",
                                  uBugCheck & 0x10000000 ? "_M" : "", uP1, dbgfR3GetNtStatusName((uint32_t)uP1),
                                  uP2, dbgfR3FormatSymbol(pUVM, szSym, sizeof(szSym), ": ", uP2),
                                  uP3, uP4);
            break;
        case 0x0000007f: /* UNEXPECTED_KERNEL_MODE_TRAP */
        case 0x1000007f: /* UNEXPECTED_KERNEL_MODE_TRAP_M */
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "UNEXPECTED_KERNEL_MODE_TRAP%s\n"
                                  "P1: %016RX64 - x86 trap number\n"
                                  "P2: %016RX64 - reserved/errorcode?\n"
                                  "P3: %016RX64 - reserved\n"
                                  "P4: %016RX64 - reserved\n",
                                  uBugCheck & 0x10000000 ? "_M" : "", uP1, uP2, uP3, uP4);
            break;
        case 0x00000080: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NMI_HARDWARE_FAILURE\n"); break;
        case 0x00000081: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SPIN_LOCK_INIT_FAILURE\n"); break;
        case 0x00000082: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DFS_FILE_SYSTEM\n"); break;
        case 0x00000083: cchUsed = RTStrPrintf(pszDetails, cbDetails, "OFS_FILE_SYSTEM\n"); break;
        case 0x00000084: cchUsed = RTStrPrintf(pszDetails, cbDetails, "RECOM_DRIVER\n"); break;
        case 0x00000085: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SETUP_FAILURE\n"); break;
        case 0x00000086: cchUsed = RTStrPrintf(pszDetails, cbDetails, "AUDIT_FAILURE\n"); break;
        case 0x0000008b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MBR_CHECKSUM_MISMATCH\n"); break;
        case 0x0000008e: /* KERNEL_MODE_EXCEPTION_NOT_HANDLED */
        case 0x1000008e: /* KERNEL_MODE_EXCEPTION_NOT_HANDLED_M */
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "KERNEL_MODE_EXCEPTION_NOT_HANDLED%s\n"
                                  "P1: %016RX64 - exception code%s\n"
                                  "P2: %016RX64 - EIP/RIP%s\n"
                                  "P3: %016RX64 - Trap frame address\n"
                                  "P4: %016RX64 - reserved\n",
                                  uBugCheck & 0x10000000 ? "_M" : "", uP1, dbgfR3GetNtStatusName((uint32_t)uP1),
                                  uP2, dbgfR3FormatSymbol(pUVM, szSym, sizeof(szSym), ": ", uP2),
                                  uP3, uP4);
            break;
        case 0x0000008f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PP0_INITIALIZATION_FAILED\n"); break;
        case 0x00000090: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PP1_INITIALIZATION_FAILED\n"); break;
        case 0x00000091: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WIN32K_INIT_OR_RIT_FAILURE\n"); break;
        case 0x00000092: cchUsed = RTStrPrintf(pszDetails, cbDetails, "UP_DRIVER_ON_MP_SYSTEM\n"); break;
        case 0x00000093: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_KERNEL_HANDLE\n"); break;
        case 0x00000094: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_STACK_LOCKED_AT_EXIT\n"); break;
        case 0x00000095: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PNP_INTERNAL_ERROR\n"); break;
        case 0x00000096: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_WORK_QUEUE_ITEM\n"); break;
        case 0x00000097: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BOUND_IMAGE_UNSUPPORTED\n"); break;
        case 0x00000098: cchUsed = RTStrPrintf(pszDetails, cbDetails, "END_OF_NT_EVALUATION_PERIOD\n"); break;
        case 0x00000099: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_REGION_OR_SEGMENT\n"); break;
        case 0x0000009a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SYSTEM_LICENSE_VIOLATION\n"); break;
        case 0x0000009b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "UDFS_FILE_SYSTEM\n"); break;
        case 0x0000009c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MACHINE_CHECK_EXCEPTION\n"); break;
        case 0x0000009e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "USER_MODE_HEALTH_MONITOR\n"); break;
        case 0x0000009f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_POWER_STATE_FAILURE\n"); break;
        case 0x000000a0: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INTERNAL_POWER_ERROR\n"); break;
        case 0x000000a1: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PCI_BUS_DRIVER_INTERNAL\n"); break;
        case 0x000000a2: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MEMORY_IMAGE_CORRUPT\n"); break;
        case 0x000000a3: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ACPI_DRIVER_INTERNAL\n"); break;
        case 0x000000a4: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CNSS_FILE_SYSTEM_FILTER\n"); break;
        case 0x000000a5: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ACPI_BIOS_ERROR\n"); break;
        case 0x000000a6: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FP_EMULATION_ERROR\n"); break;
        case 0x000000a7: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BAD_EXHANDLE\n"); break;
        case 0x000000a8: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BOOTING_IN_SAFEMODE_MINIMAL\n"); break;
        case 0x000000a9: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BOOTING_IN_SAFEMODE_NETWORK\n"); break;
        case 0x000000aa: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BOOTING_IN_SAFEMODE_DSREPAIR\n"); break;
        case 0x000000ab: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SESSION_HAS_VALID_POOL_ON_EXIT\n"); break;
        case 0x000000ac: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HAL_MEMORY_ALLOCATION\n"); break;
        case 0x000000b1: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BGI_DETECTED_VIOLATION\n"); break;
        case 0x000000b4: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_DRIVER_INIT_FAILURE\n"); break;
        case 0x000000b5: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BOOTLOG_LOADED\n"); break;
        case 0x000000b6: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BOOTLOG_NOT_LOADED\n"); break;
        case 0x000000b7: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BOOTLOG_ENABLED\n"); break;
        case 0x000000b8: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ATTEMPTED_SWITCH_FROM_DPC\n"); break;
        case 0x000000b9: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CHIPSET_DETECTED_ERROR\n"); break;
        case 0x000000ba: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SESSION_HAS_VALID_VIEWS_ON_EXIT\n"); break;
        case 0x000000bb: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NETWORK_BOOT_INITIALIZATION_FAILED\n"); break;
        case 0x000000bc: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NETWORK_BOOT_DUPLICATE_ADDRESS\n"); break;
        case 0x000000bd: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_HIBERNATED_STATE\n"); break;
        case 0x000000be: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ATTEMPTED_WRITE_TO_READONLY_MEMORY\n"); break;
        case 0x000000bf: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MUTEX_ALREADY_OWNED\n"); break;
        case 0x000000c0: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PCI_CONFIG_SPACE_ACCESS_FAILURE\n"); break;
        case 0x000000c1: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SPECIAL_POOL_DETECTED_MEMORY_CORRUPTION\n"); break;

        case 0x000000c2:
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "BAD_POOL_CALLER\n"
                                  "P1: %016RX64 - ", uP1);
            if (cchUsed >= cbDetails)
                return VINF_BUFFER_OVERFLOW;
            cbDetails  -= cchUsed;
            pszDetails += cchUsed;
            switch (uP1)
            {
                case 1:
                case 2:
                case 4:
                    cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                          "Pool header corrupted!\n"
                                          "P2: %016RX64 - Pool header address\n"
                                          "P3: %016RX64 - Pool header contents\n"
                                          "P4: %016RX64 - reserved\n", uP2, uP3, uP4);
                    break;
                case 6:
                    cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                          "Double free w/o tag!\n"
                                          "P2: %016RX64 - reserved\n"
                                          "P3: %016RX64 - Pool header address\n"
                                          "P4: %016RX64 - Pool header contents\n", uP2, uP3, uP4);
                    break;
                case 7:
                    cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                          "Double free w/ tag!\n"
                                          "P2: %016RX64 - tag %c%c%c%c\n"
                                          "P3: %016RX64 - Pool header contents\n"
                                          "P4: %016RX64 - Free address\n",
                                          uP2,
                                          RT_C_IS_PRINT(RT_BYTE1(uP2)) ? RT_BYTE1(uP2) : '.',
                                          RT_C_IS_PRINT(RT_BYTE2(uP2)) ? RT_BYTE2(uP2) : '.',
                                          RT_C_IS_PRINT(RT_BYTE3(uP2)) ? RT_BYTE3(uP2) : '.',
                                          RT_C_IS_PRINT(RT_BYTE4(uP2)) ? RT_BYTE4(uP2) : '.',
                                          uP3, uP4);
                    break;
                case 8:
                    cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                          "Wrong IRQL for allocation!\n"
                                          "P2: %016RX64 - IRQL\n"
                                          "P3: %016RX64 - Pool type\n"
                                          "P4: %016RX64 - Allocation size\n",
                                          uP2, uP3, uP4);
                    break;
                case 9:
                    cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                          "Wrong IRQL for free!\n"
                                          "P2: %016RX64 - IRQL\n"
                                          "P3: %016RX64 - Pool type\n"
                                          "P4: %016RX64 - Pool address\n",
                                          uP2, uP3, uP4);
                    break;
                /** @todo fill in more BAD_POOL_CALLER types here as needed.*/
                default:
                    cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                          "Unknown pool violation type\n"
                                          "P2: %016RX64 - type specific\n"
                                          "P3: %016RX64 - type specific\n"
                                          "P4: %016RX64 - type specific\n",
                                          uP2, uP3, uP4);
                    break;
            }
            break;

        case 0x000000c3: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SYSTEM_IMAGE_BAD_SIGNATURE\n"); break;
        case 0x000000c4: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_VERIFIER_DETECTED_VIOLATION\n"); break;
        case 0x000000c5: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_CORRUPTED_EXPOOL\n"); break;
        case 0x000000c6: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_CAUGHT_MODIFYING_FREED_POOL\n"); break;
        case 0x000000c7: cchUsed = RTStrPrintf(pszDetails, cbDetails, "TIMER_OR_DPC_INVALID\n"); break;
        case 0x000000c8: cchUsed = RTStrPrintf(pszDetails, cbDetails, "IRQL_UNEXPECTED_VALUE\n"); break;
        case 0x000000c9: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_VERIFIER_IOMANAGER_VIOLATION\n"); break;
        case 0x000000ca: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PNP_DETECTED_FATAL_ERROR\n"); break;
        case 0x000000cb: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_LEFT_LOCKED_PAGES_IN_PROCESS\n"); break;
        case 0x000000cc: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PAGE_FAULT_IN_FREED_SPECIAL_POOL\n"); break;
        case 0x000000cd: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PAGE_FAULT_BEYOND_END_OF_ALLOCATION\n"); break;
        case 0x000000ce: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS\n"); break;
        case 0x000000cf: cchUsed = RTStrPrintf(pszDetails, cbDetails, "TERMINAL_SERVER_DRIVER_MADE_INCORRECT_MEMORY_REFERENCE\n"); break;
        case 0x000000d0: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_CORRUPTED_MMPOOL\n"); break;
        case 0x000000d1:
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "DRIVER_IRQL_NOT_LESS_OR_EQUAL\n"
                                  "P1: %016RX64 - memory referenced\n"
                                  "P2: %016RX64 - IRQL\n"
                                  "P3: %016RX64 - %s\n"
                                  "P4: %016RX64 - EIP/RIP%s\n",
                                  uP1, uP2, uP3, uP3 & RT_BIT_64(0) ? "write" : "read",
                                  uP4, dbgfR3FormatSymbol(pUVM, szSym, sizeof(szSym), ": ", uP4));
            break;
        case 0x000000d2: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BUGCODE_ID_DRIVER\n"); break;
        case 0x000000d3: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_PORTION_MUST_BE_NONPAGED\n"); break;
        case 0x000000d4: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SYSTEM_SCAN_AT_RAISED_IRQL_CAUGHT_IMPROPER_DRIVER_UNLOAD\n"); break;
        case 0x000000d5: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_PAGE_FAULT_IN_FREED_SPECIAL_POOL\n"); break;
        case 0x000000d6: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_PAGE_FAULT_BEYOND_END_OF_ALLOCATION\n"); break;
        case 0x100000d6: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_PAGE_FAULT_BEYOND_END_OF_ALLOCATION_M\n"); break;
        case 0x000000d7: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_UNMAPPING_INVALID_VIEW\n"); break;
        case 0x000000d8:
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "DRIVER_USED_EXCESSIVE_PTES\n"
                                  "P1: %016RX64 - Driver name pointer\n"
                                  "P2: %016RX64 - Number of PTEs\n"
                                  "P3: %016RX64 - Free system PTEs\n"
                                  "P4: %016RX64 - System PTEs\n",
                                  uP1, uP2, uP3, uP4);
            break;
        case 0x000000d9: cchUsed = RTStrPrintf(pszDetails, cbDetails, "LOCKED_PAGES_TRACKER_CORRUPTION\n"); break;
        case 0x000000da: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SYSTEM_PTE_MISUSE\n"); break;
        case 0x000000db: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_CORRUPTED_SYSPTES\n"); break;
        case 0x000000dc: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_INVALID_STACK_ACCESS\n"); break;
        case 0x000000de: cchUsed = RTStrPrintf(pszDetails, cbDetails, "POOL_CORRUPTION_IN_FILE_AREA\n"); break;
        case 0x000000df: cchUsed = RTStrPrintf(pszDetails, cbDetails, "IMPERSONATING_WORKER_THREAD\n"); break;
        case 0x000000e0: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ACPI_BIOS_FATAL_ERROR\n"); break;
        case 0x000000e1: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WORKER_THREAD_RETURNED_AT_BAD_IRQL\n"); break;
        case 0x000000e2: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MANUALLY_INITIATED_CRASH\n"); break;
        case 0x000000e3: cchUsed = RTStrPrintf(pszDetails, cbDetails, "RESOURCE_NOT_OWNED\n"); break;
        case 0x000000e4: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WORKER_INVALID\n"); break;
        case 0x000000e5: cchUsed = RTStrPrintf(pszDetails, cbDetails, "POWER_FAILURE_SIMULATE\n"); break;
        case 0x000000e6: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_VERIFIER_DMA_VIOLATION\n"); break;
        case 0x000000e7: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_FLOATING_POINT_STATE\n"); break;
        case 0x000000e8: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_CANCEL_OF_FILE_OPEN\n"); break;
        case 0x000000e9: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ACTIVE_EX_WORKER_THREAD_TERMINATION\n"); break;
        case 0x000000ea: cchUsed = RTStrPrintf(pszDetails, cbDetails, "THREAD_STUCK_IN_DEVICE_DRIVER\n"); break;
        case 0x100000ea: cchUsed = RTStrPrintf(pszDetails, cbDetails, "THREAD_STUCK_IN_DEVICE_DRIVER_M\n"); break;
        case 0x000000eb: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DIRTY_MAPPED_PAGES_CONGESTION\n"); break;
        case 0x000000ec: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SESSION_HAS_VALID_SPECIAL_POOL_ON_EXIT\n"); break;
        case 0x000000ed: cchUsed = RTStrPrintf(pszDetails, cbDetails, "UNMOUNTABLE_BOOT_VOLUME\n"); break;
        case 0x000000ef: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CRITICAL_PROCESS_DIED\n"); break;
        case 0x000000f0: cchUsed = RTStrPrintf(pszDetails, cbDetails, "STORAGE_MINIPORT_ERROR\n"); break;
        case 0x000000f1: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SCSI_VERIFIER_DETECTED_VIOLATION\n"); break;
        case 0x000000f2: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HARDWARE_INTERRUPT_STORM\n"); break;
        case 0x000000f3: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DISORDERLY_SHUTDOWN\n"); break;
        case 0x000000f4: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CRITICAL_OBJECT_TERMINATION\n"); break;
        case 0x000000f5: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FLTMGR_FILE_SYSTEM\n"); break;
        case 0x000000f6: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PCI_VERIFIER_DETECTED_VIOLATION\n"); break;
        case 0x000000f7: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_OVERRAN_STACK_BUFFER\n"); break;
        case 0x000000f8: cchUsed = RTStrPrintf(pszDetails, cbDetails, "RAMDISK_BOOT_INITIALIZATION_FAILED\n"); break;
        case 0x000000f9: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_RETURNED_STATUS_REPARSE_FOR_VOLUME_OPEN\n"); break;
        case 0x000000fa: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HTTP_DRIVER_CORRUPTED\n"); break;
        case 0x000000fb: cchUsed = RTStrPrintf(pszDetails, cbDetails, "RECURSIVE_MACHINE_CHECK\n"); break;
        case 0x000000fc: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY\n"); break;
        case 0x000000fd: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DIRTY_NOWRITE_PAGES_CONGESTION\n"); break;
        case 0x000000fe: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BUGCODE_USB_DRIVER\n"); break;
        case 0x000000ff: cchUsed = RTStrPrintf(pszDetails, cbDetails, "RESERVE_QUEUE_OVERFLOW\n"); break;
        case 0x00000100: cchUsed = RTStrPrintf(pszDetails, cbDetails, "LOADER_BLOCK_MISMATCH\n"); break;
        case 0x00000101: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLOCK_WATCHDOG_TIMEOUT\n"); break;
        case 0x00000102: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DPC_WATCHDOG_TIMEOUT\n"); break;
        case 0x00000103: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MUP_FILE_SYSTEM\n"); break;
        case 0x00000104: cchUsed = RTStrPrintf(pszDetails, cbDetails, "AGP_INVALID_ACCESS\n"); break;
        case 0x00000105: cchUsed = RTStrPrintf(pszDetails, cbDetails, "AGP_GART_CORRUPTION\n"); break;
        case 0x00000106: cchUsed = RTStrPrintf(pszDetails, cbDetails, "AGP_ILLEGALLY_REPROGRAMMED\n"); break;
        case 0x00000107: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_EXPAND_STACK_ACTIVE\n"); break;
        case 0x00000108: cchUsed = RTStrPrintf(pszDetails, cbDetails, "THIRD_PARTY_FILE_SYSTEM_FAILURE\n"); break;
        case 0x00000109: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CRITICAL_STRUCTURE_CORRUPTION\n"); break;
        case 0x0000010a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "APP_TAGGING_INITIALIZATION_FAILED\n"); break;
        case 0x0000010b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DFSC_FILE_SYSTEM\n"); break;
        case 0x0000010c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FSRTL_EXTRA_CREATE_PARAMETER_VIOLATION\n"); break;
        case 0x0000010d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WDF_VIOLATION\n"); break;
        case 0x0000010e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_MEMORY_MANAGEMENT_INTERNAL\n"); break;
        case 0x00000110: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_INVALID_CRUNTIME_PARAMETER\n"); break;
        case 0x00000111: cchUsed = RTStrPrintf(pszDetails, cbDetails, "RECURSIVE_NMI\n"); break;
        case 0x00000112: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MSRPC_STATE_VIOLATION\n"); break;
        case 0x00000113: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_DXGKRNL_FATAL_ERROR\n"); break;
        case 0x00000114: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_SHADOW_DRIVER_FATAL_ERROR\n"); break;
        case 0x00000115: cchUsed = RTStrPrintf(pszDetails, cbDetails, "AGP_INTERNAL\n"); break;
        case 0x00000116: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_TDR_FAILURE\n"); break;
        case 0x00000117: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_TDR_TIMEOUT_DETECTED\n"); break;
        case 0x00000118: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NTHV_GUEST_ERROR\n"); break;
        case 0x00000119: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_SCHEDULER_INTERNAL_ERROR\n"); break;
        case 0x0000011a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "EM_INITIALIZATION_ERROR\n"); break;
        case 0x0000011b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_RETURNED_HOLDING_CANCEL_LOCK\n"); break;
        case 0x0000011c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ATTEMPTED_WRITE_TO_CM_PROTECTED_STORAGE\n"); break;
        case 0x0000011d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "EVENT_TRACING_FATAL_ERROR\n"); break;
        case 0x0000011e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "TOO_MANY_RECURSIVE_FAULTS\n"); break;
        case 0x0000011f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_DRIVER_HANDLE\n"); break;
        case 0x00000120: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BITLOCKER_FATAL_ERROR\n"); break;
        case 0x00000121: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_VIOLATION\n"); break;
        case 0x00000122: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WHEA_INTERNAL_ERROR\n"); break;
        case 0x00000123: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CRYPTO_SELF_TEST_FAILURE\n"); break;
        case 0x00000124: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WHEA_UNCORRECTABLE_ERROR\n"); break;
        case 0x00000125: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NMR_INVALID_STATE\n"); break;
        case 0x00000126: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NETIO_INVALID_POOL_CALLER\n"); break;
        case 0x00000127: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PAGE_NOT_ZERO\n"); break;
        case 0x00000128: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WORKER_THREAD_RETURNED_WITH_BAD_IO_PRIORITY\n"); break;
        case 0x00000129: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WORKER_THREAD_RETURNED_WITH_BAD_PAGING_IO_PRIORITY\n"); break;
        case 0x0000012a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MUI_NO_VALID_SYSTEM_LANGUAGE\n"); break;
        case 0x0000012b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FAULTY_HARDWARE_CORRUPTED_PAGE\n"); break;
        case 0x0000012c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "EXFAT_FILE_SYSTEM\n"); break;
        case 0x0000012d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VOLSNAP_OVERLAPPED_TABLE_ACCESS\n"); break;
        case 0x0000012e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_MDL_RANGE\n"); break;
        case 0x0000012f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VHD_BOOT_INITIALIZATION_FAILED\n"); break;
        case 0x00000130: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DYNAMIC_ADD_PROCESSOR_MISMATCH\n"); break;
        case 0x00000131: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_EXTENDED_PROCESSOR_STATE\n"); break;
        case 0x00000132: cchUsed = RTStrPrintf(pszDetails, cbDetails, "RESOURCE_OWNER_POINTER_INVALID\n"); break;
        case 0x00000133: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DPC_WATCHDOG_VIOLATION\n"); break;
        case 0x00000134: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVE_EXTENDER\n"); break;
        case 0x00000135: cchUsed = RTStrPrintf(pszDetails, cbDetails, "REGISTRY_FILTER_DRIVER_EXCEPTION\n"); break;
        case 0x00000136: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VHD_BOOT_HOST_VOLUME_NOT_ENOUGH_SPACE\n"); break;
        case 0x00000137: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WIN32K_HANDLE_MANAGER\n"); break;
        case 0x00000138: cchUsed = RTStrPrintf(pszDetails, cbDetails, "GPIO_CONTROLLER_DRIVER_ERROR\n"); break;

        case 0x00000139: /* __fastfail(P1) triggers this via INT 29h(?) and P1 in rcx. */
        {
            const char *pszCheck;
            switch (uP1)
            {
                case 0x00:  pszCheck = "Stack buffer overrun (/GS)"; break;
                case 0x01:  pszCheck = "Illegal virtual function table use (VTGuard)"; break;
                case 0x02:  pszCheck = "Stack buffer overrun (via cookie)"; break;
                case 0x03:  pszCheck = "Correupt LIST_ENTRY"; break;
                case 0x04:  pszCheck = "Out of bounds stack pointer"; break; /* "Reserved" on learn.microsoft.com */
                case 0x05:  pszCheck = "Invalid parameter (fatal)"; break;
                case 0x06:  pszCheck = "Uninitialized stack cookie (by loader prior to Win8)"; break;
                case 0x07:  pszCheck = "Fatal program exit request"; break;
                case 0x08:  pszCheck = "Compiler bounds check violation"; break;
                case 0x09:  pszCheck = "Direct RtlQueryRegistryValues w/o typechecking on untrusted hive"; break;
                /* https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check---bug-check-0x139-kernel-security-check-failure
                   and !analyze -show differs on the following.  Looks like a decimal/hex mixup.
                   Using the web info, hoping it is more accurate (matches 0x1d better)... */
                case 0x0a:  pszCheck = "Invalid indirect control transfer (call guard)"; break;
                case 0x0b:  pszCheck = "Invalid memory write (write guard)"; break;
                case 0x0c:  pszCheck = "Invalid context for fiber switch"; break;
                case 0x0d:  pszCheck = "Invalid register context (in assignment)"; break;
                case 0x0e:  pszCheck = "Invalid object reference count"; break;

                case 0x0f:  pszCheck = "Memory safety violation [?]"; break;       /* windbg; undocument on learn.microsoft.com */
                case 0x10:  pszCheck = "Invalid indirect call (call guard) [?]"; break;      /* ditto */
                case 0x11:  pszCheck = "Invalid memory write (write guard) [?]"; break;      /* ditto */

                case 0x12:  pszCheck = "Invalid jmp_buf;"; break;
                case 0x13:  pszCheck = "Modifying read-only data"; break;
                case 0x14:  pszCheck = "Crypto self-test fail"; break;
                case 0x15:  pszCheck = "Invalid exception chain"; break;
                case 0x16:  pszCheck = "Crypto library error"; break;
                case 0x17:  pszCheck = "Invalid call from within DllMain"; break; /* sounds very userlandish... */
                case 0x18:  pszCheck = "Invalid image base address"; break;
                case 0x19:  pszCheck = "Problem protecting delay load import."; break;
                case 0x1a:  pszCheck = "Call to unsafe extension"; break;
                case 0x1b:  pszCheck = "Deprecated service call"; break;
                case 0x1c:  pszCheck = "Out of bounce buffer access"; break;
                case 0x1d:  pszCheck = "Corrupt RTL_BALANCED_NODE (often heap related)"; break;

                case 0x23:  pszCheck = "RtlpHpAllocWithExceptionProtection/RtlpHpFreeWithExceptionProtection problem"; break; /* disas */

                case 0x25:  pszCheck = "Out of bound jump table (switch) attempt"; break;
                case 0x26:  pszCheck = "Bogus longjmp target"; break;
                case 0x27:  pszCheck = "Invalid call target (export suppressed; RtlUnwindEx,RtlDispatchException)"; break;

                case 0x32:  pszCheck = "RtlpHpFixedVsFree problem"; break; /* disas */

                default:    pszCheck = "Todo/Unknown"; break;
            }
            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "KERNEL_SECURITY_CHECK_FAILURE\n"
                                  "P1: %016RX64 - %s!\n"
                                  "P2: %016RX64 - Trap frame address\n"
                                  "P3: %016RX64 - Exception record\n"
                                  "P4: %016RX64 - reserved\n", uP1, pszCheck, uP2, uP3, uP4);
            break;
        }

        case 0x0000013a:
        {
            const char *pszCheck;
            switch (uP1)
            {
                case 0x03: pszCheck = "a corrupt entry header"; break;
                case 0x04: pszCheck = "multiple corrupt entry headers"; break;
                case 0x05: pszCheck = "a corrupt entry header for a large allocation"; break;
                case 0x06: pszCheck = "buffer overrun (possibly)"; break;
                case 0x07: pszCheck = "buffer underrun (possibly)"; break;
                case 0x08: pszCheck = "block is not busy - it is free"; break; /* same as 0xf? */
                case 0x09: pszCheck = "invalid argument"; break;
                case 0x0a: pszCheck = "invalid allocation type"; break;
                case 0x0b: pszCheck = "use after free (possibly)"; break;
                case 0x0c: pszCheck = "wrong heap given"; break;
                case 0x0d: pszCheck = "free list corruption"; break;
                case 0x0e: pszCheck = "non-free list corruption"; break;
                case 0x0f: pszCheck = "block is not busy - it is free"; break; /* same as 0x8? */
                case 0x10: pszCheck = "bogus state due to buffer overrun (possibly)"; break;
                case 0x11: pszCheck = "bogus state due to buffer overrun (possibly)"; break;
                case 0x12: pszCheck = "bogus state due to buffer overrun (possibly)"; break;
                case 0x13: pszCheck = "NULL heap handle"; break;
                case 0x14: pszCheck = "request too big"; break;
                case 0x15: pszCheck = "commit limit exceeded"; break;
                case 0x16: pszCheck = "invalid VA manage query size/whatever"; break;
                default:   pszCheck = "Todo/Unknown"; break;
            }

            cchUsed = RTStrPrintf(pszDetails, cbDetails,
                                  "KERNEL_MODE_HEAP_CORRUPTION\n"
                                  "P1: %016RX64 - %s!\n"
                                  "P2: %016RX64 - Heap address\n"
                                  "P3: %016RX64 - Corruption address\n"
                                  "P4: %016RX64 - reserved\n", uP1, pszCheck, uP2, uP3, uP4);
            break;
        }
        case 0x0000013b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PASSIVE_INTERRUPT_ERROR\n"); break;
        case 0x0000013c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_IO_BOOST_STATE\n"); break;
        case 0x0000013d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CRITICAL_INITIALIZATION_FAILURE\n"); break;
        case 0x0000013e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ERRATA_WORKAROUND_UNSUCCESSFUL\n"); break;
        case 0x00000140: cchUsed = RTStrPrintf(pszDetails, cbDetails, "STORAGE_DEVICE_ABNORMALITY_DETECTED\n"); break;
        case 0x00000141: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_ENGINE_TIMEOUT_DETECTED\n"); break;
        case 0x00000142: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_TDR_APPLICATION_BLOCKED\n"); break;
        case 0x00000143: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PROCESSOR_DRIVER_INTERNAL\n"); break;
        case 0x00000144: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BUGCODE_USB3_DRIVER\n"); break;
        case 0x00000145: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SECURE_BOOT_VIOLATION\n"); break;
        case 0x00000146: cchUsed = RTStrPrintf(pszDetails, cbDetails, "NDIS_NET_BUFFER_LIST_INFO_ILLEGALLY_TRANSFERRED\n"); break;
        case 0x00000147: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ABNORMAL_RESET_DETECTED\n"); break;
        case 0x00000148: cchUsed = RTStrPrintf(pszDetails, cbDetails, "IO_OBJECT_INVALID\n"); break;
        case 0x00000149: cchUsed = RTStrPrintf(pszDetails, cbDetails, "REFS_FILE_SYSTEM\n"); break;
        case 0x0000014a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_WMI_INTERNAL\n"); break;
        case 0x0000014b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SOC_SUBSYSTEM_FAILURE\n"); break;
        case 0x0000014c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FATAL_ABNORMAL_RESET_ERROR\n"); break;
        case 0x0000014d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "EXCEPTION_SCOPE_INVALID\n"); break;
        case 0x0000014e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SOC_CRITICAL_DEVICE_REMOVED\n"); break;
        case 0x0000014f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PDC_WATCHDOG_TIMEOUT\n"); break;
        case 0x00000150: cchUsed = RTStrPrintf(pszDetails, cbDetails, "TCPIP_AOAC_NIC_ACTIVE_REFERENCE_LEAK\n"); break;
        case 0x00000151: cchUsed = RTStrPrintf(pszDetails, cbDetails, "UNSUPPORTED_INSTRUCTION_MODE\n"); break;
        case 0x00000152: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_PUSH_LOCK_FLAGS\n"); break;
        case 0x00000153: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_LOCK_ENTRY_LEAKED_ON_THREAD_TERMINATION\n"); break;
        case 0x00000154: cchUsed = RTStrPrintf(pszDetails, cbDetails, "UNEXPECTED_STORE_EXCEPTION\n"); break;
        case 0x00000155: cchUsed = RTStrPrintf(pszDetails, cbDetails, "OS_DATA_TAMPERING\n"); break;
        case 0x00000156: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WINSOCK_DETECTED_HUNG_CLOSESOCKET_LIVEDUMP\n"); break;
        case 0x00000157: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_THREAD_PRIORITY_FLOOR_VIOLATION\n"); break;
        case 0x00000158: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ILLEGAL_IOMMU_PAGE_FAULT\n"); break;
        case 0x00000159: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HAL_ILLEGAL_IOMMU_PAGE_FAULT\n"); break;
        case 0x0000015a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SDBUS_INTERNAL_ERROR\n"); break;
        case 0x0000015b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WORKER_THREAD_RETURNED_WITH_SYSTEM_PAGE_PRIORITY_ACTIVE\n"); break;
        case 0x0000015c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PDC_WATCHDOG_TIMEOUT_LIVEDUMP\n"); break;
        case 0x0000015d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SOC_SUBSYSTEM_FAILURE_LIVEDUMP\n"); break;
        case 0x0000015e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BUGCODE_NDIS_DRIVER_LIVE_DUMP\n"); break;
        case 0x0000015f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CONNECTED_STANDBY_WATCHDOG_TIMEOUT_LIVEDUMP\n"); break;
        case 0x00000160: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WIN32K_ATOMIC_CHECK_FAILURE\n"); break;
        case 0x00000161: cchUsed = RTStrPrintf(pszDetails, cbDetails, "LIVE_SYSTEM_DUMP\n"); break;
        case 0x00000162: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_AUTO_BOOST_INVALID_LOCK_RELEASE\n"); break;
        case 0x00000163: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WORKER_THREAD_TEST_CONDITION\n"); break;
        case 0x00000164: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WIN32K_CRITICAL_FAILURE\n"); break;
        case 0x00000165: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CSV_STATUS_IO_TIMEOUT_LIVEDUMP\n"); break;
        case 0x00000166: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_RESOURCE_CALL_TIMEOUT_LIVEDUMP\n"); break;
        case 0x00000167: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CSV_SNAPSHOT_DEVICE_INFO_TIMEOUT_LIVEDUMP\n"); break;
        case 0x00000168: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CSV_STATE_TRANSITION_TIMEOUT_LIVEDUMP\n"); break;
        case 0x00000169: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CSV_VOLUME_ARRIVAL_LIVEDUMP\n"); break;
        case 0x0000016a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CSV_VOLUME_REMOVAL_LIVEDUMP\n"); break;
        case 0x0000016b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CSV_CLUSTER_WATCHDOG_LIVEDUMP\n"); break;
        case 0x0000016c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_RUNDOWN_PROTECTION_FLAGS\n"); break;
        case 0x0000016d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_SLOT_ALLOCATOR_FLAGS\n"); break;
        case 0x0000016e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ERESOURCE_INVALID_RELEASE\n"); break;
        case 0x0000016f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CSV_STATE_TRANSITION_INTERVAL_TIMEOUT_LIVEDUMP\n"); break;
        case 0x00000170: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CSV_CLUSSVC_DISCONNECT_WATCHDOG\n"); break;
        case 0x00000171: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CRYPTO_LIBRARY_INTERNAL_ERROR\n"); break;
        case 0x00000173: cchUsed = RTStrPrintf(pszDetails, cbDetails, "COREMSGCALL_INTERNAL_ERROR\n"); break;
        case 0x00000174: cchUsed = RTStrPrintf(pszDetails, cbDetails, "COREMSG_INTERNAL_ERROR\n"); break;
        case 0x00000175: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PREVIOUS_FATAL_ABNORMAL_RESET_ERROR\n"); break;
        case 0x00000178: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ELAM_DRIVER_DETECTED_FATAL_ERROR\n"); break;
        case 0x00000179: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CLUSPORT_STATUS_IO_TIMEOUT_LIVEDUMP\n"); break;
        case 0x0000017b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PROFILER_CONFIGURATION_ILLEGAL\n"); break;
        case 0x0000017c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PDC_LOCK_WATCHDOG_LIVEDUMP\n"); break;
        case 0x0000017d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PDC_UNEXPECTED_REVOCATION_LIVEDUMP\n"); break;
        case 0x00000180: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WVR_LIVEDUMP_REPLICATION_IOCONTEXT_TIMEOUT\n"); break;
        case 0x00000181: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WVR_LIVEDUMP_STATE_TRANSITION_TIMEOUT\n"); break;
        case 0x00000182: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WVR_LIVEDUMP_RECOVERY_IOCONTEXT_TIMEOUT\n"); break;
        case 0x00000183: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WVR_LIVEDUMP_APP_IO_TIMEOUT\n"); break;
        case 0x00000184: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WVR_LIVEDUMP_MANUALLY_INITIATED\n"); break;
        case 0x00000185: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WVR_LIVEDUMP_STATE_FAILURE\n"); break;
        case 0x00000186: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WVR_LIVEDUMP_CRITICAL_ERROR\n"); break;
        case 0x00000187: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_DWMINIT_TIMEOUT_FALLBACK_BDD\n"); break;
        case 0x00000188: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_CSVFS_LIVEDUMP\n"); break;
        case 0x00000189: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BAD_OBJECT_HEADER\n"); break;
        case 0x0000018a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SILO_CORRUPT\n"); break;
        case 0x0000018b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SECURE_KERNEL_ERROR\n"); break;
        case 0x0000018c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HYPERGUARD_VIOLATION\n"); break;
        case 0x0000018d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SECURE_FAULT_UNHANDLED\n"); break;
        case 0x0000018e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_PARTITION_REFERENCE_VIOLATION\n"); break;
        case 0x00000190: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WIN32K_CRITICAL_FAILURE_LIVEDUMP\n"); break;
        case 0x00000191: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PF_DETECTED_CORRUPTION\n"); break;
        case 0x00000192: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_AUTO_BOOST_LOCK_ACQUISITION_WITH_RAISED_IRQL\n"); break;
        case 0x00000193: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_DXGKRNL_LIVEDUMP\n"); break;
        case 0x00000194: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_NONRESPONSIVEPROCESS\n"); break;
        case 0x00000195: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SMB_SERVER_LIVEDUMP\n"); break;
        case 0x00000196: cchUsed = RTStrPrintf(pszDetails, cbDetails, "LOADER_ROLLBACK_DETECTED\n"); break;
        case 0x00000197: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WIN32K_SECURITY_FAILURE\n"); break;
        case 0x00000198: cchUsed = RTStrPrintf(pszDetails, cbDetails, "UFX_LIVEDUMP\n"); break;
        case 0x00000199: cchUsed = RTStrPrintf(pszDetails, cbDetails, "KERNEL_STORAGE_SLOT_IN_USE\n"); break;
        case 0x0000019a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WORKER_THREAD_RETURNED_WHILE_ATTACHED_TO_SILO\n"); break;
        case 0x0000019b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "TTM_FATAL_ERROR\n"); break;
        case 0x0000019c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WIN32K_POWER_WATCHDOG_TIMEOUT\n"); break;
        case 0x0000019d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CLUSTER_SVHDX_LIVEDUMP\n"); break;
        case 0x0000019e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BUGCODE_NETADAPTER_DRIVER\n"); break;
        case 0x0000019f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "PDC_PRIVILEGE_CHECK_LIVEDUMP\n"); break;
        case 0x000001a0: cchUsed = RTStrPrintf(pszDetails, cbDetails, "TTM_WATCHDOG_TIMEOUT\n"); break;
        case 0x000001a1: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WIN32K_CALLOUT_WATCHDOG_LIVEDUMP\n"); break;
        case 0x000001a2: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WIN32K_CALLOUT_WATCHDOG_BUGCHECK\n"); break;
        case 0x000001a3: cchUsed = RTStrPrintf(pszDetails, cbDetails, "CALL_HAS_NOT_RETURNED_WATCHDOG_TIMEOUT_LIVEDUMP\n"); break;
        case 0x000001a4: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIPS_SW_HW_DIVERGENCE_LIVEDUMP\n"); break;
        case 0x000001a5: cchUsed = RTStrPrintf(pszDetails, cbDetails, "USB_DRIPS_BLOCKER_SURPRISE_REMOVAL_LIVEDUMP\n"); break;
        case 0x000001c4: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_VERIFIER_DETECTED_VIOLATION_LIVEDUMP\n"); break;
        case 0x000001c5: cchUsed = RTStrPrintf(pszDetails, cbDetails, "IO_THREADPOOL_DEADLOCK_LIVEDUMP\n"); break;
        case 0x000001c6: cchUsed = RTStrPrintf(pszDetails, cbDetails, "FAST_ERESOURCE_PRECONDITION_VIOLATION\n"); break;
        case 0x000001c7: cchUsed = RTStrPrintf(pszDetails, cbDetails, "STORE_DATA_STRUCTURE_CORRUPTION\n"); break;
        case 0x000001c8: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MANUALLY_INITIATED_POWER_BUTTON_HOLD\n"); break;
        case 0x000001c9: cchUsed = RTStrPrintf(pszDetails, cbDetails, "USER_MODE_HEALTH_MONITOR_LIVEDUMP\n"); break;
        case 0x000001ca: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HYPERVISOR_WATCHDOG_TIMEOUT\n"); break;
        case 0x000001cb: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_SILO_DETACH\n"); break;
        case 0x000001cc: cchUsed = RTStrPrintf(pszDetails, cbDetails, "EXRESOURCE_TIMEOUT_LIVEDUMP\n"); break;
        case 0x000001cd: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_CALLBACK_STACK_ADDRESS\n"); break;
        case 0x000001ce: cchUsed = RTStrPrintf(pszDetails, cbDetails, "INVALID_KERNEL_STACK_ADDRESS\n"); break;
        case 0x000001cf: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HARDWARE_WATCHDOG_TIMEOUT\n"); break;
        case 0x000001d0: cchUsed = RTStrPrintf(pszDetails, cbDetails, "ACPI_FIRMWARE_WATCHDOG_TIMEOUT\n"); break;
        case 0x000001d1: cchUsed = RTStrPrintf(pszDetails, cbDetails, "TELEMETRY_ASSERTS_LIVEDUMP\n"); break;
        case 0x000001d2: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WORKER_THREAD_INVALID_STATE\n"); break;
        case 0x000001d3: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WFP_INVALID_OPERATION\n"); break;
        case 0x000001d4: cchUsed = RTStrPrintf(pszDetails, cbDetails, "UCMUCSI_LIVEDUMP\n"); break;
        case 0x000001d5: cchUsed = RTStrPrintf(pszDetails, cbDetails, "DRIVER_PNP_WATCHDOG\n"); break;
        case 0x00000315: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_MTBFCOMMANDTIMEOUT\n"); break;
        case 0x00000356: cchUsed = RTStrPrintf(pszDetails, cbDetails, "XBOX_ERACTRL_CS_TIMEOUT\n"); break;
        case 0x00000357: cchUsed = RTStrPrintf(pszDetails, cbDetails, "XBOX_CORRUPTED_IMAGE\n"); break;
        case 0x00000358: cchUsed = RTStrPrintf(pszDetails, cbDetails, "XBOX_INVERTED_FUNCTION_TABLE_OVERFLOW\n"); break;
        case 0x00000359: cchUsed = RTStrPrintf(pszDetails, cbDetails, "XBOX_CORRUPTED_IMAGE_BASE\n"); break;
        case 0x00000360: cchUsed = RTStrPrintf(pszDetails, cbDetails, "XBOX_360_SYSTEM_CRASH\n"); break;
        case 0x00000420: cchUsed = RTStrPrintf(pszDetails, cbDetails, "XBOX_360_SYSTEM_CRASH_RESERVED\n"); break;
        case 0x00000bfe: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BC_BLUETOOTH_VERIFIER_FAULT\n"); break;
        case 0x00000bff: cchUsed = RTStrPrintf(pszDetails, cbDetails, "BC_BTHMINI_VERIFIER_FAULT\n"); break;
        case 0x00008866: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_SICKAPPLICATION\n"); break;
        case 0x0000f000: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_UNSPECIFIED\n"); break;
        case 0x0000f002: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_BLANKSCREEN\n"); break;
        case 0x0000f003: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_INPUT\n"); break;
        case 0x0000f004: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_WATCHDOG\n"); break;
        case 0x0000f005: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_STARTNOTVISIBLE\n"); break;
        case 0x0000f006: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_NAVIGATIONMODEL\n"); break;
        case 0x0000f007: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_OUTOFMEMORY\n"); break;
        case 0x0000f008: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_GRAPHICS\n"); break;
        case 0x0000f009: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_NAVSERVERTIMEOUT\n"); break;
        case 0x0000f00a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_CHROMEPROCESSCRASH\n"); break;
        case 0x0000f00b: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_NOTIFICATIONDISMISSAL\n"); break;
        case 0x0000f00c: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_SPEECHDISMISSAL\n"); break;
        case 0x0000f00d: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_CALLDISMISSAL\n"); break;
        case 0x0000f00e: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_APPBARDISMISSAL\n"); break;
        case 0x0000f00f: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_RILADAPTATIONCRASH\n"); break;
        case 0x0000f010: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_APPLISTUNREACHABLE\n"); break;
        case 0x0000f011: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_REPORTNOTIFICATIONFAILURE\n"); break;
        case 0x0000f012: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_UNEXPECTEDSHUTDOWN\n"); break;
        case 0x0000f013: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_RPCFAILURE\n"); break;
        case 0x0000f014: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_AUXILIARYFULLDUMP\n"); break;
        case 0x0000f015: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_ACCOUNTPROVSVCINITFAILURE\n"); break;
        case 0x0000f101: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_MTBFCOMMANDHANG\n"); break;
        case 0x0000f102: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_MTBFPASSBUGCHECK\n"); break;
        case 0x0000f103: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_MTBFIOERROR\n"); break;
        case 0x0000f200: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_RENDERTHREADHANG\n"); break;
        case 0x0000f201: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_RENDERMOBILEUIOOM\n"); break;
        case 0x0000f300: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_DEVICEUPDATEUNSPECIFIED\n"); break;
        case 0x0000f400: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_AUDIODRIVERHANG\n"); break;
        case 0x0000f500: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_BATTERYPULLOUT\n"); break;
        case 0x0000f600: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_MEDIACORETESTHANG\n"); break;
        case 0x0000f700: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_RESOURCEMANAGEMENT\n"); break;
        case 0x0000f800: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_CAPTURESERVICE\n"); break;
        case 0x0000f900: cchUsed = RTStrPrintf(pszDetails, cbDetails, "SAVER_WAITFORSHELLREADY\n"); break;
        case 0x00020001: cchUsed = RTStrPrintf(pszDetails, cbDetails, "HYPERVISOR_ERROR\n"); break;
        case 0x4000008a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "THREAD_TERMINATE_HELD_MUTEX\n"); break;
        case 0x400000ad: cchUsed = RTStrPrintf(pszDetails, cbDetails, "VIDEO_DRIVER_DEBUG_REPORT_REQUEST\n"); break;
        case 0xc000021a: cchUsed = RTStrPrintf(pszDetails, cbDetails, "WINLOGON_FATAL_ERROR\n"); break;
        case 0xdeaddead: cchUsed = RTStrPrintf(pszDetails, cbDetails, "MANUALLY_INITIATED_CRASH1\n"); break;
        default: cchUsed = 0; break;
    }
    if (cchUsed < cbDetails)
        return VINF_SUCCESS;
    return VINF_BUFFER_OVERFLOW;
}


/**
 * Report a bug check.
 *
 * @returns
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per virtual CPU structure.
 * @param   enmEvent        The kind of BSOD event this is.
 * @param   uBugCheck       The bug check number.
 * @param   uP1             The bug check parameter \#1.
 * @param   uP2             The bug check parameter \#2.
 * @param   uP3             The bug check parameter \#3.
 * @param   uP4             The bug check parameter \#4.
 */
VMMR3DECL(VBOXSTRICTRC) DBGFR3ReportBugCheck(PVM pVM, PVMCPU pVCpu, DBGFEVENTTYPE enmEvent, uint64_t uBugCheck,
                                             uint64_t uP1, uint64_t uP2, uint64_t uP3, uint64_t uP4)
{
    /*
     * Be careful.
     */
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VMCPU_ASSERT_EMT_RETURN(pVCpu, VERR_INVALID_VMCPU_HANDLE);
    const char *pszSource;
    switch (enmEvent)
    {
        case DBGFEVENT_BSOD_MSR:        pszSource = "GIMHv"; break;
        case DBGFEVENT_BSOD_EFI:        pszSource = "EFI"; break;
        case DBGFEVENT_BSOD_VMMDEV:     pszSource = "VMMDev"; break;
        default:
            AssertMsgFailedReturn(("enmEvent=%d\n", enmEvent), VERR_INVALID_PARAMETER);
    }

    /*
     * Note it down.
     */
    pVM->dbgf.s.BugCheck.enmEvent        = enmEvent;
    pVM->dbgf.s.BugCheck.uBugCheck       = uBugCheck;
    pVM->dbgf.s.BugCheck.auParameters[0] = uP1;
    pVM->dbgf.s.BugCheck.auParameters[1] = uP2;
    pVM->dbgf.s.BugCheck.auParameters[2] = uP3;
    pVM->dbgf.s.BugCheck.auParameters[3] = uP4;
    pVM->dbgf.s.BugCheck.idCpu           = pVCpu->idCpu;
    pVM->dbgf.s.BugCheck.uTimestamp      = TMVirtualGet(pVM);
    pVM->dbgf.s.BugCheck.uResetNo        = VMGetResetCount(pVM);

    /*
     * Log the details.
     */
    char szDetails[2048];
    DBGFR3FormatBugCheck(pVM->pUVM, szDetails, sizeof(szDetails), uBugCheck, uP1, uP2, uP3, uP4);
    LogRel(("%s: %s", pszSource, szDetails));

    /*
     * Raise debugger event.
     */
    VBOXSTRICTRC rc = VINF_SUCCESS;
    if (DBGF_IS_EVENT_ENABLED(pVM, enmEvent))
        rc = DBGFEventGenericWithArgs(pVM, pVCpu, enmEvent, DBGFEVENTCTX_OTHER, 5 /*cArgs*/, uBugCheck, uP1, uP2, uP3, uP4);

    /*
     * Take actions.
     */
    if (pVM->dbgf.s.BugCheck.fCfgPowerOffOnBsod)
    {
        RTMsgError("Powering off - guest BSOD: %s\n", szDetails);
        PUVM const pUVM = pVM->pUVM;
        VMR3ReqCallNoWaitU(pUVM, VMCPUID_ANY_QUEUE, (PFNRT)VMR3PowerOff, 1, pUVM);
    }
    else if (pVM->dbgf.s.BugCheck.fCfgSuspendOnBsod)
    {
        RTMsgError("Suspending - guest BSOD: %s\n", szDetails);
        PUVM const pUVM = pVM->pUVM;
        VMR3ReqCallNoWaitU(pUVM, VMCPUID_ANY_QUEUE, (PFNRT)VMR3Suspend, 2, pUVM, VMSUSPENDREASON_RUNTIME_ERROR);
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGFHANDLERINT, bugcheck}
 */
static DECLCALLBACK(void) dbgfR3BugCheckInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    char szDetails[2048];

    /*
     * Any arguments for bug check formatting?
     */
    if (pszArgs && *pszArgs)
        pszArgs = RTStrStripL(pszArgs);
    if (pszArgs && *pszArgs)
    {
        uint64_t auData[5] = { 0, 0, 0, 0, 0 };
        unsigned iData = 0;
        do
        {
            /* Find the next hex digit  */
            char ch;
            while ((ch = *pszArgs) != '\0' && !RT_C_IS_XDIGIT(ch))
                pszArgs++;
            if (ch == '\0')
                break;

            /* Extract the number. */
            char *pszNext = (char *)pszArgs + 1;
            RTStrToUInt64Ex(pszArgs, &pszNext, 16, &auData[iData]);

            /* Advance. */
            pszArgs = pszNext;
            iData++;
        } while (iData < RT_ELEMENTS(auData) && *pszArgs);

        /* Format it. */
        DBGFR3FormatBugCheck(pVM->pUVM, szDetails, sizeof(szDetails), auData[0], auData[1], auData[2], auData[3], auData[4]);
        pHlp->pfnPrintf(pHlp, "%s", szDetails);
    }
    /*
     * Format what's been reported (if any).
     */
    else if (pVM->dbgf.s.BugCheck.enmEvent != DBGFEVENT_END)
    {
        DBGFR3FormatBugCheck(pVM->pUVM, szDetails, sizeof(szDetails), pVM->dbgf.s.BugCheck.uBugCheck,
                             pVM->dbgf.s.BugCheck.auParameters[0], pVM->dbgf.s.BugCheck.auParameters[1],
                             pVM->dbgf.s.BugCheck.auParameters[2], pVM->dbgf.s.BugCheck.auParameters[3]);
        const char *pszSource  = pVM->dbgf.s.BugCheck.enmEvent == DBGFEVENT_BSOD_MSR    ? "GIMHv"
                               : pVM->dbgf.s.BugCheck.enmEvent == DBGFEVENT_BSOD_EFI    ? "EFI"
                               : pVM->dbgf.s.BugCheck.enmEvent == DBGFEVENT_BSOD_VMMDEV ? "VMMDev" : "<unknown>";
        uint32_t const uFreq   = TMVirtualGetFreq(pVM);
        uint64_t const cSecs   = pVM->dbgf.s.BugCheck.uTimestamp / uFreq;
        uint32_t const cMillis = (pVM->dbgf.s.BugCheck.uTimestamp - cSecs * uFreq) * 1000 / uFreq;
        pHlp->pfnPrintf(pHlp, "BugCheck on CPU #%u after %RU64.%03u s VM uptime, %u resets ago (src: %s)\n%s",
                        pVM->dbgf.s.BugCheck.idCpu, cSecs, cMillis,  VMGetResetCount(pVM) - pVM->dbgf.s.BugCheck.uResetNo,
                        pszSource, szDetails);
    }
    else
        pHlp->pfnPrintf(pHlp, "No bug check reported.\n");
}

