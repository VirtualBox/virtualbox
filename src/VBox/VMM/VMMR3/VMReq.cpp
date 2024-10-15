/* $Id: VMReq.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VM - Virtual Machine
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VM
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vmm.h>
#include "VMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>

#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  vmR3ReqProcessOne(PVMREQ pReq);


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns an VBox status code,
 * (2) that you want it's return code on success, and (3) that you wish to wait
 * for ever for it to return.
 *
 * @returns VBox status code.  In the unlikely event that VMR3ReqCallVU fails,
 *          its status code is return.  Otherwise, the status of pfnFunction is
 *          returned.
 *
 * @param   pVM             The cross context VM structure.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 * @internal
 */
VMMR3_INT_DECL(int) VMR3ReqCallWait(PVM pVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PVMREQ pReq;
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pVM->pUVM, idDstCpu, &pReq, RT_INDEFINITE_WAIT, VMREQFLAGS_VBOX_STATUS,
                           pfnFunction, cArgs, va);
    va_end(va);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);
    return rc;
}


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns an VBox status code,
 * (2) that you want it's return code on success, and (3) that you wish to wait
 * for ever for it to return.
 *
 * @returns VBox status code.  In the unlikely event that VMR3ReqCallVU fails,
 *          its status code is return.  Otherwise, the status of pfnFunction is
 *          returned.
 *
 * @param   pUVM            The user mode VM structure.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 * @internal
 */
VMMR3DECL(int) VMR3ReqCallWaitU(PUVM pUVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PVMREQ pReq;
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pUVM, idDstCpu, &pReq, RT_INDEFINITE_WAIT, VMREQFLAGS_VBOX_STATUS,
                           pfnFunction, cArgs, va);
    va_end(va);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);
    return rc;
}


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns an VBox status code
 * and that you do not wish to wait for it to complete.
 *
 * @returns VBox status code returned by VMR3ReqCallVU.
 *
 * @param   pVM             The cross context VM structure.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 * @internal
 */
VMMR3DECL(int) VMR3ReqCallNoWait(PVM pVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pVM->pUVM, idDstCpu, NULL, 0, VMREQFLAGS_VBOX_STATUS | VMREQFLAGS_NO_WAIT,
                           pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns an VBox status code
 * and that you do not wish to wait for it to complete.
 *
 * @returns VBox status code returned by VMR3ReqCallVU.
 *
 * @param   pUVM            Pointer to the VM.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 */
VMMR3DECL(int) VMR3ReqCallNoWaitU(PUVM pUVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pUVM, idDstCpu, NULL, 0, VMREQFLAGS_VBOX_STATUS | VMREQFLAGS_NO_WAIT,
                           pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns void, and (2) that
 * you wish to wait for ever for it to return.
 *
 * @returns VBox status code of VMR3ReqCallVU.
 *
 * @param   pVM             The cross context VM structure.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 * @internal
 */
VMMR3_INT_DECL(int) VMR3ReqCallVoidWait(PVM pVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PVMREQ pReq;
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pVM->pUVM, idDstCpu, &pReq, RT_INDEFINITE_WAIT, VMREQFLAGS_VOID,
                           pfnFunction, cArgs, va);
    va_end(va);
    VMR3ReqFree(pReq);
    return rc;
}


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns void, and (2) that
 * you wish to wait for ever for it to return.
 *
 * @returns VBox status code of VMR3ReqCallVU.
 *
 * @param   pUVM            Pointer to the VM.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 */
VMMR3DECL(int) VMR3ReqCallVoidWaitU(PUVM pUVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PVMREQ pReq;
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pUVM, idDstCpu, &pReq, RT_INDEFINITE_WAIT, VMREQFLAGS_VOID,
                           pfnFunction, cArgs, va);
    va_end(va);
    VMR3ReqFree(pReq);
    return rc;
}


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns void, and (2) that
 * you do not wish to wait for it to complete.
 *
 * @returns VBox status code of VMR3ReqCallVU.
 *
 * @param   pVM             The cross context VM structure.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 * @internal
 */
VMMR3DECL(int) VMR3ReqCallVoidNoWait(PVM pVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PVMREQ pReq;
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pVM->pUVM, idDstCpu, &pReq, RT_INDEFINITE_WAIT, VMREQFLAGS_VOID | VMREQFLAGS_NO_WAIT,
                           pfnFunction, cArgs, va);
    va_end(va);
    VMR3ReqFree(pReq);
    return rc;
}


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns an VBox status code,
 * (2) that you want it's return code on success, (3) that you wish to wait for
 * ever for it to return, and (4) that it's priority request that can be safely
 * be handled during async suspend and power off.
 *
 * @returns VBox status code.  In the unlikely event that VMR3ReqCallVU fails,
 *          its status code is return.  Otherwise, the status of pfnFunction is
 *          returned.
 *
 * @param   pVM             The cross context VM structure.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 * @internal
 */
VMMR3DECL(int) VMR3ReqPriorityCallWait(PVM pVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PVMREQ pReq;
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pVM->pUVM, idDstCpu, &pReq, RT_INDEFINITE_WAIT, VMREQFLAGS_VBOX_STATUS | VMREQFLAGS_PRIORITY,
                           pfnFunction, cArgs, va);
    va_end(va);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);
    return rc;
}


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns an VBox status code,
 * (2) that you want it's return code on success, (3) that you wish to wait for
 * ever for it to return, and (4) that it's priority request that can be safely
 * be handled during async suspend and power off.
 *
 * @returns VBox status code.  In the unlikely event that VMR3ReqCallVU fails,
 *          its status code is return.  Otherwise, the status of pfnFunction is
 *          returned.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 */
VMMR3DECL(int) VMR3ReqPriorityCallWaitU(PUVM pUVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PVMREQ pReq;
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pUVM, idDstCpu, &pReq, RT_INDEFINITE_WAIT, VMREQFLAGS_VBOX_STATUS | VMREQFLAGS_PRIORITY,
                           pfnFunction, cArgs, va);
    va_end(va);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);
    return rc;
}


/**
 * Convenience wrapper for VMR3ReqCallU.
 *
 * This assumes (1) you're calling a function that returns void, (2) that you
 * wish to wait for ever for it to return, and (3) that it's priority request
 * that can be safely be handled during async suspend and power off.
 *
 * @returns VBox status code of VMR3ReqCallVU.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 */
VMMR3DECL(int) VMR3ReqPriorityCallVoidWaitU(PUVM pUVM, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PVMREQ pReq;
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pUVM, idDstCpu, &pReq, RT_INDEFINITE_WAIT, VMREQFLAGS_VOID | VMREQFLAGS_PRIORITY,
                           pfnFunction, cArgs, va);
    va_end(va);
    VMR3ReqFree(pReq);
    return rc;
}


/**
 * Allocate and queue a call request to a void function.
 *
 * If it's desired to poll on the completion of the request set cMillies
 * to 0 and use VMR3ReqWait() to check for completion. In the other case
 * use RT_INDEFINITE_WAIT.
 * The returned request packet must be freed using VMR3ReqFree().
 *
 * @returns VBox status code.
 *          Will not return VERR_INTERRUPTED.
 * @returns VERR_TIMEOUT if cMillies was reached without the packet being completed.
 *
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   ppReq           Where to store the pointer to the request.
 *                          This will be NULL or a valid request pointer not matter what happens, unless fFlags
 *                          contains VMREQFLAGS_NO_WAIT when it will be optional and always NULL.
 * @param   cMillies        Number of milliseconds to wait for the request to
 *                          be completed. Use RT_INDEFINITE_WAIT to only
 *                          wait till it's completed.
 * @param   fFlags          A combination of the VMREQFLAGS values.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 * @param   ...             Function arguments.
 *
 * @remarks See remarks on VMR3ReqCallVU.
 */
VMMR3DECL(int) VMR3ReqCallU(PUVM pUVM, VMCPUID idDstCpu, PVMREQ *ppReq, RTMSINTERVAL cMillies, uint32_t fFlags,
                            PFNRT pfnFunction, unsigned cArgs, ...)
{
    va_list va;
    va_start(va, cArgs);
    int rc = VMR3ReqCallVU(pUVM, idDstCpu, ppReq, cMillies, fFlags, pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}


/**
 * Allocate and queue a call request.
 *
 * If it's desired to poll on the completion of the request set cMillies
 * to 0 and use VMR3ReqWait() to check for completion. In the other case
 * use RT_INDEFINITE_WAIT.
 * The returned request packet must be freed using VMR3ReqFree().
 *
 * @returns VBox status code.
 *          Will not return VERR_INTERRUPTED.
 * @returns VERR_TIMEOUT if cMillies was reached without the packet being completed.
 *
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 * @param   ppReq           Where to store the pointer to the request.
 *                          This will be NULL or a valid request pointer not matter what happens, unless fFlags
 *                          contains VMREQFLAGS_NO_WAIT when it will be optional and always NULL.
 * @param   cMillies        Number of milliseconds to wait for the request to
 *                          be completed. Use RT_INDEFINITE_WAIT to only
 *                          wait till it's completed.
 * @param   pfnFunction     Pointer to the function to call.
 * @param   fFlags          A combination of the VMREQFLAGS values.
 * @param   cArgs           Number of arguments following in the ellipsis.
 *                          The max is VMREQ_MAX_ARGS (9) (ARM64/darwin
 *                          peculiarities), unless VMREQ_F_EXTRA_ARGS_ALL_PTRS
 *                          is ORed in.
 *                          Stuff which differs in size from uintptr_t is gonna make trouble, so don't try!
 * @param   Args            Argument vector.
 *
 * @remarks Caveats:
 *              - Do not pass anything which is larger than an uintptr_t.
 *              - 64-bit integers are larger than uintptr_t on 32-bit hosts.
 *                Pass integers > 32-bit by reference (pointers).
 *              - Don't use NULL since it should be the integer 0 in C++ and may
 *                therefore end up with garbage in the bits 63:32 on 64-bit
 *                hosts because 'int' is 32-bit.
 *                Use (void *)NULL or (uintptr_t)0 instead of NULL.
 *              - The max number of arguments is currently limited to 9, because
 *                on macOS/arm64 arguments passed on the stack that are 32-bit
 *                or smaller will not get a full 64-bit stack slot.  So,
 *                we cannot pretend @a pfnFunction takes a list of @a cArgs
 *                uintptr_t parameters, unless all parameters above 9 actually
 *                are more than 32 bits wide.   (This would've kind of worked
 *                iff the variadict functions didn't use different size round up
 *                and alignment rules.)  See @bugref{10725}.
 */
VMMR3DECL(int) VMR3ReqCallVU(PUVM pUVM, VMCPUID idDstCpu, PVMREQ *ppReq, RTMSINTERVAL cMillies, uint32_t fFlags,
                             PFNRT pfnFunction, unsigned cArgs, va_list Args)
{
    LogFlow(("VMR3ReqCallV: idDstCpu=%u cMillies=%d fFlags=%#x pfnFunction=%p cArgs=%#x\n", idDstCpu, cMillies, fFlags, pfnFunction, cArgs));

    /*
     * Validate input.
     */
    AssertPtrReturn(pfnFunction, VERR_INVALID_POINTER);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(!(fFlags & ~(VMREQFLAGS_RETURN_MASK | VMREQFLAGS_NO_WAIT | VMREQFLAGS_POKE | VMREQFLAGS_PRIORITY)), VERR_INVALID_PARAMETER);
    if (!(fFlags & VMREQFLAGS_NO_WAIT) || ppReq)
    {
        AssertPtrReturn(ppReq, VERR_INVALID_POINTER);
        *ppReq = NULL;
    }
    AssertMsgReturn(   cArgs <= VMREQ_MAX_ARGS
                    || (   (cArgs & VMREQ_F_EXTRA_ARGS_ALL_PTRS)
                        && (cArgs & ~(unsigned)VMREQ_F_EXTRA_ARGS_ALL_PTRS) <= VMREQ_MAX_ARGS_EXTENDED),
                    ("cArgs=%#x\n", cArgs),
                    VERR_TOO_MUCH_DATA);
    cArgs &= ~(unsigned)VMREQ_F_EXTRA_ARGS_ALL_PTRS;

    /*
     * Allocate request
     */
    PVMREQ pReq = NULL;
    AssertCompile(VMREQ_MAX_ARGS_EXTENDED * sizeof(uintptr_t) <= sizeof(pReq->u.Internal.aArgs));
    int rc = VMR3ReqAlloc(pUVM, &pReq, VMREQTYPE_INTERNAL, idDstCpu);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Initialize the request data.
     */
    pReq->fFlags            = fFlags;
    pReq->u.Internal.pfn    = pfnFunction;
    pReq->u.Internal.cArgs  = cArgs;
    for (unsigned iArg = 0; iArg < cArgs; iArg++)
        pReq->u.Internal.aArgs[iArg] = va_arg(Args, uintptr_t);

    /*
     * Queue the request and return.
     */
    rc = VMR3ReqQueue(pReq, cMillies);
    if (    RT_FAILURE(rc)
        && rc != VERR_TIMEOUT)
    {
        VMR3ReqFree(pReq);
        pReq = NULL;
    }
    if (!(fFlags & VMREQFLAGS_NO_WAIT))
    {
        *ppReq = pReq;
        LogFlow(("VMR3ReqCallV: returns %Rrc *ppReq=%p\n", rc, pReq));
    }
    else
        LogFlow(("VMR3ReqCallV: returns %Rrc\n", rc));
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}


/**
 * Joins the list pList with whatever is linked up at *pHead.
 */
static void vmr3ReqJoinFreeSub(volatile PVMREQ *ppHead, PVMREQ pList)
{
    for (unsigned cIterations = 0;; cIterations++)
    {
        PVMREQ pHead = ASMAtomicXchgPtrT(ppHead, pList, PVMREQ);
        if (!pHead)
            return;
        PVMREQ pTail = pHead;
        while (pTail->pNext)
            pTail = pTail->pNext;
        ASMAtomicWritePtr(&pTail->pNext, pList);
        ASMCompilerBarrier();
        if (ASMAtomicCmpXchgPtr(ppHead, pHead, pList))
            return;
        ASMAtomicWriteNullPtr(&pTail->pNext);
        ASMCompilerBarrier();
        if (ASMAtomicCmpXchgPtr(ppHead, pHead, NULL))
            return;
        pList = pHead;
        Assert(cIterations != 32);
        Assert(cIterations != 64);
    }
}


/**
 * Joins the list pList with whatever is linked up at *pHead.
 */
static void vmr3ReqJoinFree(PVMINTUSERPERVM pVMInt, PVMREQ pList)
{
    /*
     * Split the list if it's too long.
     */
    unsigned cReqs = 1;
    PVMREQ pTail = pList;
    while (pTail->pNext)
    {
        if (cReqs++ > 25)
        {
            const uint32_t i = pVMInt->iReqFree;
            vmr3ReqJoinFreeSub(&pVMInt->apReqFree[(i + 2) % RT_ELEMENTS(pVMInt->apReqFree)], pTail->pNext);

            pTail->pNext = NULL;
            vmr3ReqJoinFreeSub(&pVMInt->apReqFree[(i + 2 + (i == pVMInt->iReqFree)) % RT_ELEMENTS(pVMInt->apReqFree)], pTail->pNext);
            return;
        }
        pTail = pTail->pNext;
    }
    vmr3ReqJoinFreeSub(&pVMInt->apReqFree[(pVMInt->iReqFree + 2) % RT_ELEMENTS(pVMInt->apReqFree)], pList);
}


/**
 * Allocates a request packet.
 *
 * The caller allocates a request packet, fills in the request data
 * union and queues the request.
 *
 * @returns VBox status code.
 *
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   ppReq           Where to store the pointer to the allocated packet.
 * @param   enmType         Package type.
 * @param   idDstCpu        The destination CPU(s). Either a specific CPU ID or
 *                          one of the following special values:
 *                              VMCPUID_ANY, VMCPUID_ANY_QUEUE, VMCPUID_ALL or VMCPUID_ALL_REVERSE.
 */
VMMR3DECL(int) VMR3ReqAlloc(PUVM pUVM, PVMREQ *ppReq, VMREQTYPE enmType, VMCPUID idDstCpu)
{
    /*
     * Validate input.
     */
    AssertMsgReturn(enmType > VMREQTYPE_INVALID && enmType < VMREQTYPE_MAX,
                    ("Invalid package type %d valid range %d-%d inclusively.\n",
                     enmType, VMREQTYPE_INVALID + 1, VMREQTYPE_MAX - 1),
                    VERR_VM_REQUEST_INVALID_TYPE);
    AssertPtrReturn(ppReq, VERR_INVALID_POINTER);
    AssertMsgReturn(    idDstCpu == VMCPUID_ANY
                    ||  idDstCpu == VMCPUID_ANY_QUEUE
                    ||  idDstCpu < pUVM->cCpus
                    ||  idDstCpu == VMCPUID_ALL
                    ||  idDstCpu == VMCPUID_ALL_REVERSE,
                    ("Invalid destination %u (max=%u)\n", idDstCpu, pUVM->cCpus), VERR_INVALID_PARAMETER);

    /*
     * Try get a recycled packet.
     * While this could all be solved with a single list with a lock, it's a sport
     * of mine to avoid locks.
     */
    int cTries = RT_ELEMENTS(pUVM->vm.s.apReqFree) * 2;
    while (--cTries >= 0)
    {
        PVMREQ volatile *ppHead = &pUVM->vm.s.apReqFree[ASMAtomicIncU32(&pUVM->vm.s.iReqFree) % RT_ELEMENTS(pUVM->vm.s.apReqFree)];
#if 0 /* sad, but this won't work safely because the reading of pReq->pNext. */
        PVMREQ pNext = NULL;
        PVMREQ pReq = *ppHead;
        if (    pReq
            &&  !ASMAtomicCmpXchgPtr(ppHead, (pNext = pReq->pNext), pReq)
            &&  (pReq = *ppHead)
            &&  !ASMAtomicCmpXchgPtr(ppHead, (pNext = pReq->pNext), pReq))
            pReq = NULL;
        if (pReq)
        {
            Assert(pReq->pNext == pNext); NOREF(pReq);
#else
        PVMREQ pReq = ASMAtomicXchgPtrT(ppHead, NULL, PVMREQ);
        if (pReq)
        {
            PVMREQ pNext = pReq->pNext;
            if (    pNext
                &&  !ASMAtomicCmpXchgPtr(ppHead, pNext, NULL))
            {
                STAM_COUNTER_INC(&pUVM->vm.s.StatReqAllocRaces);
                vmr3ReqJoinFree(&pUVM->vm.s, pReq->pNext);
            }
#endif
            ASMAtomicDecU32(&pUVM->vm.s.cReqFree);

            /*
             * Make sure the event sem is not signaled.
             */
            if (!pReq->fEventSemClear)
            {
                int rc = RTSemEventWait(pReq->EventSem, 0);
                if (rc != VINF_SUCCESS && rc != VERR_TIMEOUT)
                {
                    /*
                     * This shall not happen, but if it does we'll just destroy
                     * the semaphore and create a new one.
                     */
                    AssertMsgFailed(("rc=%Rrc from RTSemEventWait(%#x).\n", rc, pReq->EventSem));
                    RTSemEventDestroy(pReq->EventSem);
                    rc = RTSemEventCreate(&pReq->EventSem);
                    AssertRC(rc);
                    if (RT_FAILURE(rc))
                        return rc;
#if 0 /// @todo @bugref{4725} - def RT_LOCK_STRICT
                    for (VMCPUID idCpu = 0; idCpu < pUVM->cCpus; idCpu++)
                        RTSemEventAddSignaller(pReq->EventSem, pUVM->aCpus[idCpu].vm.s.ThreadEMT);
#endif
                }
                pReq->fEventSemClear = true;
            }
            else
                Assert(RTSemEventWait(pReq->EventSem, 0) == VERR_TIMEOUT);

            /*
             * Initialize the packet and return it.
             */
            Assert(pReq->enmType == VMREQTYPE_INVALID);
            Assert(pReq->enmState == VMREQSTATE_FREE);
            Assert(pReq->pUVM == pUVM);
            ASMAtomicWriteNullPtr(&pReq->pNext);
            pReq->enmState = VMREQSTATE_ALLOCATED;
            pReq->iStatus  = VERR_VM_REQUEST_STATUS_STILL_PENDING;
            pReq->fFlags   = VMREQFLAGS_VBOX_STATUS;
            pReq->enmType  = enmType;
            pReq->idDstCpu = idDstCpu;

            *ppReq = pReq;
            STAM_COUNTER_INC(&pUVM->vm.s.StatReqAllocRecycled);
            LogFlow(("VMR3ReqAlloc: returns VINF_SUCCESS *ppReq=%p recycled\n", pReq));
            return VINF_SUCCESS;
        }
    }

    /*
     * Ok allocate one.
     */
    PVMREQ pReq = (PVMREQ)MMR3HeapAllocU(pUVM, MM_TAG_VM_REQ, sizeof(*pReq));
    if (!pReq)
        return VERR_NO_MEMORY;

    /*
     * Create the semaphore.
     */
    int rc = RTSemEventCreate(&pReq->EventSem);
    AssertRC(rc);
    if (RT_FAILURE(rc))
    {
        MMR3HeapFree(pReq);
        return rc;
    }
#if 0 /// @todo @bugref{4725} - def RT_LOCK_STRICT
    for (VMCPUID idCpu = 0; idCpu < pUVM->cCpus; idCpu++)
        RTSemEventAddSignaller(pReq->EventSem, pUVM->aCpus[idCpu].vm.s.ThreadEMT);
#endif

    /*
     * Initialize the packet and return it.
     */
    pReq->pNext    = NULL;
    pReq->pUVM     = pUVM;
    pReq->enmState = VMREQSTATE_ALLOCATED;
    pReq->iStatus  = VERR_VM_REQUEST_STATUS_STILL_PENDING;
    pReq->fEventSemClear = true;
    pReq->fFlags   = VMREQFLAGS_VBOX_STATUS;
    pReq->enmType  = enmType;
    pReq->idDstCpu = idDstCpu;

    *ppReq = pReq;
    STAM_COUNTER_INC(&pUVM->vm.s.StatReqAllocNew);
    LogFlow(("VMR3ReqAlloc: returns VINF_SUCCESS *ppReq=%p new\n", pReq));
    return VINF_SUCCESS;
}


/**
 * Free a request packet.
 *
 * @returns VBox status code.
 *
 * @param   pReq            Package to free.
 * @remark  The request packet must be in allocated or completed state!
 */
VMMR3DECL(int) VMR3ReqFree(PVMREQ pReq)
{
    /*
     * Ignore NULL (all free functions should do this imho).
     */
    if (!pReq)
        return VINF_SUCCESS;

    /*
     * Check packet state.
     */
    switch (pReq->enmState)
    {
        case VMREQSTATE_ALLOCATED:
        case VMREQSTATE_COMPLETED:
            break;
        default:
            AssertMsgFailed(("Invalid state %d!\n", pReq->enmState));
            return VERR_VM_REQUEST_STATE;
    }

    /*
     * Make it a free packet and put it into one of the free packet lists.
     */
    pReq->enmState = VMREQSTATE_FREE;
    pReq->iStatus  = VERR_VM_REQUEST_STATUS_FREED;
    pReq->enmType  = VMREQTYPE_INVALID;

    PUVM pUVM = pReq->pUVM;
    STAM_COUNTER_INC(&pUVM->vm.s.StatReqFree);

    if (pUVM->vm.s.cReqFree < 128)
    {
        ASMAtomicIncU32(&pUVM->vm.s.cReqFree);
        PVMREQ volatile *ppHead = &pUVM->vm.s.apReqFree[ASMAtomicIncU32(&pUVM->vm.s.iReqFree) % RT_ELEMENTS(pUVM->vm.s.apReqFree)];
        PVMREQ pNext;
        do
        {
            pNext = ASMAtomicUoReadPtrT(ppHead, PVMREQ);
            ASMAtomicWritePtr(&pReq->pNext, pNext);
            ASMCompilerBarrier();
        } while (!ASMAtomicCmpXchgPtr(ppHead, pReq, pNext));
    }
    else
    {
        STAM_COUNTER_INC(&pReq->pUVM->vm.s.StatReqFreeOverflow);
        RTSemEventDestroy(pReq->EventSem);
        MMR3HeapFree(pReq);
    }
    return VINF_SUCCESS;
}


/**
 * Queue a request.
 *
 * The quest must be allocated using VMR3ReqAlloc() and contain
 * all the required data.
 * If it's desired to poll on the completion of the request set cMillies
 * to 0 and use VMR3ReqWait() to check for completion. In the other case
 * use RT_INDEFINITE_WAIT.
 *
 * @returns VBox status code.
 *          Will not return VERR_INTERRUPTED.
 * @returns VERR_TIMEOUT if cMillies was reached without the packet being completed.
 *
 * @param   pReq            The request to queue.
 * @param   cMillies        Number of milliseconds to wait for the request to
 *                          be completed. Use RT_INDEFINITE_WAIT to only
 *                          wait till it's completed.
 */
VMMR3DECL(int) VMR3ReqQueue(PVMREQ pReq, RTMSINTERVAL cMillies)
{
    LogFlow(("VMR3ReqQueue: pReq=%p cMillies=%d\n", pReq, cMillies));
    /*
     * Verify the supplied package.
     */
    AssertMsgReturn(pReq->enmState == VMREQSTATE_ALLOCATED, ("%d\n", pReq->enmState), VERR_VM_REQUEST_STATE);
    AssertMsgReturn(    RT_VALID_PTR(pReq->pUVM)
                    &&  !pReq->pNext
                    &&  pReq->EventSem != NIL_RTSEMEVENT,
                    ("Invalid request package! Anyone cooking their own packages???\n"),
                    VERR_VM_REQUEST_INVALID_PACKAGE);
    AssertMsgReturn(    pReq->enmType > VMREQTYPE_INVALID
                    &&  pReq->enmType < VMREQTYPE_MAX,
                    ("Invalid package type %d valid range %d-%d inclusively. This was verified on alloc too...\n",
                     pReq->enmType, VMREQTYPE_INVALID + 1, VMREQTYPE_MAX - 1),
                    VERR_VM_REQUEST_INVALID_TYPE);
    Assert(!(pReq->fFlags & ~(VMREQFLAGS_RETURN_MASK | VMREQFLAGS_NO_WAIT | VMREQFLAGS_POKE | VMREQFLAGS_PRIORITY)));

    /*
     * Are we the EMT or not?
     * Also, store pVM (and fFlags) locally since pReq may be invalid after queuing it.
     */
    int     rc      = VINF_SUCCESS;
    PUVM    pUVM    = ((VMREQ volatile *)pReq)->pUVM;                 /* volatile paranoia */
    PUVMCPU pUVCpu  = (PUVMCPU)RTTlsGet(pUVM->vm.s.idxTLS);

    if (pReq->idDstCpu == VMCPUID_ALL)
    {
        /* One-by-one. */
        Assert(!(pReq->fFlags & VMREQFLAGS_NO_WAIT));
        for (unsigned i = 0; i < pUVM->cCpus; i++)
        {
            /* Reinit some members. */
            pReq->enmState = VMREQSTATE_ALLOCATED;
            pReq->idDstCpu = i;
            rc = VMR3ReqQueue(pReq, cMillies);
            if (RT_FAILURE(rc))
                break;
        }
    }
    else if (pReq->idDstCpu == VMCPUID_ALL_REVERSE)
    {
        /* One-by-one. */
        Assert(!(pReq->fFlags & VMREQFLAGS_NO_WAIT));
        for (int i = pUVM->cCpus-1; i >= 0; i--)
        {
            /* Reinit some members. */
            pReq->enmState = VMREQSTATE_ALLOCATED;
            pReq->idDstCpu = i;
            rc = VMR3ReqQueue(pReq, cMillies);
            if (RT_FAILURE(rc))
                break;
        }
    }
    else if (   pReq->idDstCpu != VMCPUID_ANY   /* for a specific VMCPU? */
             && pReq->idDstCpu != VMCPUID_ANY_QUEUE
             && (   !pUVCpu                     /* and it's not the current thread. */
                 || pUVCpu->idCpu != pReq->idDstCpu))
    {
        VMCPUID  idTarget = pReq->idDstCpu;     Assert(idTarget < pUVM->cCpus);
        PVMCPU   pVCpu = pUVM->pVM->apCpusR3[idTarget];
        unsigned fFlags = ((VMREQ volatile *)pReq)->fFlags;     /* volatile paranoia */

        /* Fetch the right UVMCPU */
        pUVCpu = &pUVM->aCpus[idTarget];

        /*
         * Insert it.
         */
        volatile PVMREQ *ppQueueHead = pReq->fFlags & VMREQFLAGS_PRIORITY ? &pUVCpu->vm.s.pPriorityReqs : &pUVCpu->vm.s.pNormalReqs;
        pReq->enmState = VMREQSTATE_QUEUED;
        PVMREQ pNext;
        do
        {
            pNext = ASMAtomicUoReadPtrT(ppQueueHead, PVMREQ);
            ASMAtomicWritePtr(&pReq->pNext, pNext);
            ASMCompilerBarrier();
        } while (!ASMAtomicCmpXchgPtr(ppQueueHead, pReq, pNext));

        /*
         * Notify EMT.
         */
        if (pUVM->pVM)
            VMCPU_FF_SET(pVCpu, VMCPU_FF_REQUEST);
        VMR3NotifyCpuFFU(pUVCpu, fFlags & VMREQFLAGS_POKE ? VMNOTIFYFF_FLAGS_POKE : 0);

        /*
         * Wait and return.
         */
        if (!(fFlags & VMREQFLAGS_NO_WAIT))
            rc = VMR3ReqWait(pReq, cMillies);
        LogFlow(("VMR3ReqQueue: returns %Rrc\n", rc));
    }
    else if (   (    pReq->idDstCpu == VMCPUID_ANY
                 && !pUVCpu /* only EMT threads have a valid pointer stored in the TLS slot. */)
             || pReq->idDstCpu == VMCPUID_ANY_QUEUE)
    {
        unsigned fFlags = ((VMREQ volatile *)pReq)->fFlags;     /* volatile paranoia */

        /* Note: pUVCpu may or may not be NULL in the VMCPUID_ANY_QUEUE case; we don't care. */

        /*
         * Insert it.
         */
        volatile PVMREQ *ppQueueHead = pReq->fFlags & VMREQFLAGS_PRIORITY ? &pUVM->vm.s.pPriorityReqs : &pUVM->vm.s.pNormalReqs;
        pReq->enmState = VMREQSTATE_QUEUED;
        PVMREQ pNext;
        do
        {
            pNext = ASMAtomicUoReadPtrT(ppQueueHead, PVMREQ);
            ASMAtomicWritePtr(&pReq->pNext, pNext);
            ASMCompilerBarrier();
        } while (!ASMAtomicCmpXchgPtr(ppQueueHead, pReq, pNext));

        /*
         * Notify EMT.
         */
        if (pUVM->pVM)
            VM_FF_SET(pUVM->pVM, VM_FF_REQUEST);
        VMR3NotifyGlobalFFU(pUVM, fFlags & VMREQFLAGS_POKE ? VMNOTIFYFF_FLAGS_POKE : 0);

        /*
         * Wait and return.
         */
        if (!(fFlags & VMREQFLAGS_NO_WAIT))
            rc = VMR3ReqWait(pReq, cMillies);
        LogFlow(("VMR3ReqQueue: returns %Rrc\n", rc));
    }
    else
    {
        Assert(pUVCpu);

        /*
         * The requester was an EMT, just execute it.
         */
        pReq->enmState = VMREQSTATE_QUEUED;
        rc = vmR3ReqProcessOne(pReq);
        LogFlow(("VMR3ReqQueue: returns %Rrc (processed)\n", rc));
    }
    return rc;
}


/**
 * Wait for a request to be completed.
 *
 * @returns VBox status code.
 * @returns VERR_TIMEOUT if cMillies was reached without the packet being completed.
 *
 * @param   pReq            The request to wait for.
 * @param   cMillies        Number of milliseconds to wait.
 *                          Use RT_INDEFINITE_WAIT to only wait till it's completed.
 */
VMMR3DECL(int) VMR3ReqWait(PVMREQ pReq, RTMSINTERVAL cMillies)
{
    LogFlow(("VMR3ReqWait: pReq=%p cMillies=%d\n", pReq, cMillies));

    /*
     * Verify the supplied package.
     */
    AssertMsgReturn(    pReq->enmState == VMREQSTATE_QUEUED
                    ||  pReq->enmState == VMREQSTATE_PROCESSING
                    ||  pReq->enmState == VMREQSTATE_COMPLETED,
                    ("Invalid state %d\n", pReq->enmState),
                    VERR_VM_REQUEST_STATE);
    AssertMsgReturn(    RT_VALID_PTR(pReq->pUVM)
                    &&  pReq->EventSem != NIL_RTSEMEVENT,
                    ("Invalid request package! Anyone cooking their own packages???\n"),
                    VERR_VM_REQUEST_INVALID_PACKAGE);
    AssertMsgReturn(    pReq->enmType > VMREQTYPE_INVALID
                    &&  pReq->enmType < VMREQTYPE_MAX,
                    ("Invalid package type %d valid range %d-%d inclusively. This was verified on alloc too...\n",
                     pReq->enmType, VMREQTYPE_INVALID + 1, VMREQTYPE_MAX - 1),
                    VERR_VM_REQUEST_INVALID_TYPE);

    /*
     * Check for deadlock condition
     */
    PUVM pUVM = pReq->pUVM;
    NOREF(pUVM);

    /*
     * Wait on the package.
     */
    int rc;
    if (cMillies != RT_INDEFINITE_WAIT)
        rc = RTSemEventWait(pReq->EventSem, cMillies);
    else
    {
        do
        {
            rc = RTSemEventWait(pReq->EventSem, RT_INDEFINITE_WAIT);
            Assert(rc != VERR_TIMEOUT);
        } while (   pReq->enmState != VMREQSTATE_COMPLETED
                 && pReq->enmState != VMREQSTATE_INVALID);
    }
    if (RT_SUCCESS(rc))
        ASMAtomicXchgSize(&pReq->fEventSemClear, true);
    if (pReq->enmState == VMREQSTATE_COMPLETED)
        rc = VINF_SUCCESS;
    LogFlow(("VMR3ReqWait: returns %Rrc\n", rc));
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}


/**
 * Sets the relevant FF.
 *
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   idDstCpu        VMCPUID_ANY or the ID of the current CPU.
 */
DECLINLINE(void) vmR3ReqSetFF(PUVM pUVM, VMCPUID idDstCpu)
{
    if (RT_LIKELY(pUVM->pVM))
    {
        if (idDstCpu == VMCPUID_ANY)
            VM_FF_SET(pUVM->pVM, VM_FF_REQUEST);
        else
            VMCPU_FF_SET(pUVM->pVM->apCpusR3[idDstCpu], VMCPU_FF_REQUEST);
    }
}


/**
 * VMR3ReqProcessU helper that handles cases where there are more than one
 * pending request.
 *
 * @returns The oldest request.
 * @param   pUVM                Pointer to the user mode VM structure
 * @param   idDstCpu            VMCPUID_ANY or virtual CPU ID.
 * @param   pReqList            The list of requests.
 * @param   ppReqs              Pointer to the list head.
 */
static PVMREQ vmR3ReqProcessUTooManyHelper(PUVM pUVM, VMCPUID idDstCpu, PVMREQ pReqList, PVMREQ volatile *ppReqs)
{
    STAM_COUNTER_INC(&pUVM->vm.s.StatReqMoreThan1);

    /*
     * Chop off the last one (pReq).
     */
    PVMREQ pPrev;
    PVMREQ pReqRet = pReqList;
    do
    {
        pPrev = pReqRet;
        pReqRet = pReqRet->pNext;
    } while (pReqRet->pNext);
    ASMAtomicWriteNullPtr(&pPrev->pNext);

    /*
     * Push the others back onto the list (end of it).
     */
    Log2(("VMR3ReqProcess: Pushing back %p %p...\n", pReqList, pReqList->pNext));
    if (RT_UNLIKELY(!ASMAtomicCmpXchgPtr(ppReqs, pReqList, NULL)))
    {
        STAM_COUNTER_INC(&pUVM->vm.s.StatReqPushBackRaces);
        do
        {
            ASMNopPause();
            PVMREQ pReqList2 = ASMAtomicXchgPtrT(ppReqs, NULL, PVMREQ);
            if (pReqList2)
            {
                PVMREQ pLast = pReqList2;
                while (pLast->pNext)
                    pLast = pLast->pNext;
                ASMAtomicWritePtr(&pLast->pNext, pReqList);
                pReqList = pReqList2;
            }
        } while (!ASMAtomicCmpXchgPtr(ppReqs, pReqList, NULL));
    }

    vmR3ReqSetFF(pUVM, idDstCpu);
    return pReqRet;
}


/**
 * Process pending request(s).
 *
 * This function is called from a forced action handler in the EMT
 * or from one of the EMT loops.
 *
 * @returns VBox status code.
 *
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   idDstCpu        Pass VMCPUID_ANY to process the common request queue
 *                          and the CPU ID for a CPU specific one. In the latter
 *                          case the calling thread must be the EMT of that CPU.
 * @param   fPriorityOnly   When set, only process the priority request queue.
 *
 * @note    SMP safe (multiple EMTs trying to satisfy VM_FF_REQUESTs).
 *
 * @remarks This was made reentrant for async PDM handling, the debugger and
 *          others.
 * @internal
 */
VMMR3_INT_DECL(int) VMR3ReqProcessU(PUVM pUVM, VMCPUID idDstCpu, bool fPriorityOnly)
{
    LogFlow(("VMR3ReqProcessU: (enmVMState=%d) idDstCpu=%d\n", pUVM->pVM ? pUVM->pVM->enmVMState : VMSTATE_CREATING, idDstCpu));

    /*
     * Determine which queues to process.
     */
    PVMREQ volatile *ppNormalReqs;
    PVMREQ volatile *ppPriorityReqs;
    if (idDstCpu == VMCPUID_ANY)
    {
        ppPriorityReqs = &pUVM->vm.s.pPriorityReqs;
        ppNormalReqs   = !fPriorityOnly ? &pUVM->vm.s.pNormalReqs                 : ppPriorityReqs;
    }
    else
    {
        Assert(idDstCpu < pUVM->cCpus);
        Assert(pUVM->aCpus[idDstCpu].vm.s.NativeThreadEMT == RTThreadNativeSelf());
        ppPriorityReqs = &pUVM->aCpus[idDstCpu].vm.s.pPriorityReqs;
        ppNormalReqs   = !fPriorityOnly ? &pUVM->aCpus[idDstCpu].vm.s.pNormalReqs : ppPriorityReqs;
    }

    /*
     * Process loop.
     *
     * We do not repeat the outer loop if we've got an informational status code
     * since that code needs processing by our caller (usually EM).
     */
    int rc = VINF_SUCCESS;
    for (;;)
    {
        /*
         * Get the pending requests.
         *
         * If there are more than one request, unlink the oldest and put the
         * rest back so that we're reentrant.
         */
        if (RT_LIKELY(pUVM->pVM))
        {
            if (idDstCpu == VMCPUID_ANY)
                VM_FF_CLEAR(pUVM->pVM, VM_FF_REQUEST);
            else
                VMCPU_FF_CLEAR(pUVM->pVM->apCpusR3[idDstCpu], VMCPU_FF_REQUEST);
        }

        PVMREQ pReq = ASMAtomicXchgPtrT(ppPriorityReqs, NULL, PVMREQ);
        if (pReq)
        {
            if (RT_UNLIKELY(pReq->pNext))
                pReq = vmR3ReqProcessUTooManyHelper(pUVM, idDstCpu, pReq, ppPriorityReqs);
            else if (ASMAtomicReadPtrT(ppNormalReqs, PVMREQ))
                vmR3ReqSetFF(pUVM, idDstCpu);
        }
        else
        {
            pReq = ASMAtomicXchgPtrT(ppNormalReqs, NULL, PVMREQ);
            if (!pReq)
                break;
            if (RT_UNLIKELY(pReq->pNext))
                pReq = vmR3ReqProcessUTooManyHelper(pUVM, idDstCpu, pReq, ppNormalReqs);
        }

        /*
         * Process the request
         */
        STAM_COUNTER_INC(&pUVM->vm.s.StatReqProcessed);
        int rc2 = vmR3ReqProcessOne(pReq);
        if (    rc2 >= VINF_EM_FIRST
            &&  rc2 <= VINF_EM_LAST)
        {
            rc = rc2;
            break;
        }
    }

    LogFlow(("VMR3ReqProcess: returns %Rrc (enmVMState=%d)\n", rc, pUVM->pVM ? pUVM->pVM->enmVMState : VMSTATE_CREATING));
    return rc;
}


/**
 * Process one request.
 *
 * @returns VBox status code.
 *
 * @param   pReq        Request packet to process.
 */
static int  vmR3ReqProcessOne(PVMREQ pReq)
{
    LogFlow(("vmR3ReqProcessOne: pReq=%p type=%d fFlags=%#x\n", pReq, pReq->enmType, pReq->fFlags));

    /*
     * Process the request.
     */
    Assert(pReq->enmState == VMREQSTATE_QUEUED);
    pReq->enmState = VMREQSTATE_PROCESSING;
    int     rcRet = VINF_SUCCESS;           /* the return code of this function. */
    int     rcReq = VERR_NOT_IMPLEMENTED;   /* the request status. */
    switch (pReq->enmType)
    {
        /*
         * A packed down call frame.
         */
        case VMREQTYPE_INTERNAL:
        {
            uintptr_t *pauArgs = &pReq->u.Internal.aArgs[0];
            union
            {
                PFNRT pfn;
                DECLCALLBACKMEMBER(int, pfn00,(void));
                DECLCALLBACKMEMBER(int, pfn01,(uintptr_t));
                DECLCALLBACKMEMBER(int, pfn02,(uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn03,(uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn04,(uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn05,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn06,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn07,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn08,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn09,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn10,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn11,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn12,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn13,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn14,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                DECLCALLBACKMEMBER(int, pfn15,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
            } u;
            u.pfn = pReq->u.Internal.pfn;
#ifndef RT_ARCH_X86
            switch (pReq->u.Internal.cArgs)
            {
                case 0:  rcRet = u.pfn00(); break;
                case 1:  rcRet = u.pfn01(pauArgs[0]); break;
                case 2:  rcRet = u.pfn02(pauArgs[0], pauArgs[1]); break;
                case 3:  rcRet = u.pfn03(pauArgs[0], pauArgs[1], pauArgs[2]); break;
                case 4:  rcRet = u.pfn04(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3]); break;
                case 5:  rcRet = u.pfn05(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4]); break;
                case 6:  rcRet = u.pfn06(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5]); break;
                case 7:  rcRet = u.pfn07(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6]); break;
                case 8:  rcRet = u.pfn08(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7]); break;
                case 9:  rcRet = u.pfn09(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8]); break;
                case 10: rcRet = u.pfn10(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8], pauArgs[9]); break;
                case 11: rcRet = u.pfn11(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8], pauArgs[9], pauArgs[10]); break;
                case 12: rcRet = u.pfn12(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8], pauArgs[9], pauArgs[10], pauArgs[11]); break;
                case 13: rcRet = u.pfn13(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8], pauArgs[9], pauArgs[10], pauArgs[11], pauArgs[12]); break;
                case 14: rcRet = u.pfn14(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8], pauArgs[9], pauArgs[10], pauArgs[11], pauArgs[12], pauArgs[13]); break;
                case 15: rcRet = u.pfn15(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8], pauArgs[9], pauArgs[10], pauArgs[11], pauArgs[12], pauArgs[13], pauArgs[14]); break;
                default:
                    AssertReleaseMsgFailed(("cArgs=%d\n", pReq->u.Internal.cArgs));
                    rcRet = rcReq = VERR_VM_REQUEST_TOO_MANY_ARGS_IPE;
                    break;
            }
#else /* x86: */
            size_t cbArgs = pReq->u.Internal.cArgs * sizeof(uintptr_t);
# ifdef __GNUC__
            __asm__ __volatile__("movl  %%esp, %%edx\n\t"
                                 "subl  %2, %%esp\n\t"
                                 "andl  $0xfffffff0, %%esp\n\t"
                                 "shrl  $2, %2\n\t"
                                 "movl  %%esp, %%edi\n\t"
                                 "rep movsl\n\t"
                                 "movl  %%edx, %%edi\n\t"
                                 "call  *%%eax\n\t"
                                 "mov   %%edi, %%esp\n\t"
                                 : "=a" (rcRet),
                                   "=S" (pauArgs),
                                   "=c" (cbArgs)
                                 : "0" (u.pfn),
                                   "1" (pauArgs),
                                   "2" (cbArgs)
                                 : "edi", "edx");
# else
            __asm
            {
                xor     edx, edx        /* just mess it up. */
                mov     eax, u.pfn
                mov     ecx, cbArgs
                shr     ecx, 2
                mov     esi, pauArgs
                mov     ebx, esp
                sub     esp, cbArgs
                and     esp, 0xfffffff0
                mov     edi, esp
                rep movsd
                call    eax
                mov     esp, ebx
                mov     rcRet, eax
            }
# endif
#endif /* x86 */
            if ((pReq->fFlags & (VMREQFLAGS_RETURN_MASK)) == VMREQFLAGS_VOID)
                rcRet = VINF_SUCCESS;
            rcReq = rcRet;
            break;
        }

        default:
            AssertMsgFailed(("pReq->enmType=%d\n", pReq->enmType));
            rcReq = VERR_NOT_IMPLEMENTED;
            break;
    }

    /*
     * Complete the request.
     */
    pReq->iStatus  = rcReq;
    pReq->enmState = VMREQSTATE_COMPLETED;
    if (pReq->fFlags & VMREQFLAGS_NO_WAIT)
    {
        /* Free the packet, nobody is waiting. */
        LogFlow(("vmR3ReqProcessOne: Completed request %p: rcReq=%Rrc rcRet=%Rrc - freeing it\n",
                 pReq, rcReq, rcRet));
        VMR3ReqFree(pReq);
    }
    else
    {
        /* Notify the waiter and him free up the packet. */
        LogFlow(("vmR3ReqProcessOne: Completed request %p: rcReq=%Rrc rcRet=%Rrc - notifying waiting thread\n",
                 pReq, rcReq, rcRet));
        ASMAtomicXchgSize(&pReq->fEventSemClear, false);
        int rc2 = RTSemEventSignal(pReq->EventSem);
        if (RT_FAILURE(rc2))
        {
            AssertRC(rc2);
            rcRet = rc2;
        }
    }

    return rcRet;
}

