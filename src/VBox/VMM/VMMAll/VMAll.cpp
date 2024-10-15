/* $Id: VMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VM - Virtual Machine All Contexts.
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
#include "VMInternal.h"
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/thread.h>


#ifdef IN_RING3

/**
 * Sets the error message.
 *
 * @returns rc. Meaning you can do:
 *    @code
 *    return VM_SET_ERROR(pVM, VERR_OF_YOUR_CHOICE, "descriptive message");
 *    @endcode
 * @param   pVM             The cross context VM structure.
 * @param   rc              VBox status code.
 * @param   SRC_POS         Use RT_SRC_POS.
 * @param   pszFormat       Error message format string.
 * @param   ...             Error message arguments.
 * @thread  Any
 */
VMMDECL(int) VMSetError(PVMCC pVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    int rc2 = VMSetErrorV(pVM, rc, RT_SRC_POS_ARGS, pszFormat, args); Assert(rc == rc2); NOREF(rc2);
    va_end(args);
    return rc;
}


/**
 * Sets the error message.
 *
 * @returns rc. Meaning you can do:
 *    @code
 *    return VM_SET_ERROR(pVM, VERR_OF_YOUR_CHOICE, "descriptive message");
 *    @endcode
 * @param   pVM             The cross context VM structure.
 * @param   rc              VBox status code.
 * @param   SRC_POS         Use RT_SRC_POS.
 * @param   pszFormat       Error message format string.
 * @param   args            Error message arguments.
 * @thread  Any
 */
VMMDECL(int) VMSetErrorV(PVMCC pVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list args)
{
# ifdef IN_RING3
    /*
     * Switch to EMT.
     */
    va_list va2;
    va_copy(va2, args); /* Have to make a copy here or GCC will break. */
    VMR3ReqPriorityCallWait(pVM, VMCPUID_ANY, (PFNRT)vmR3SetErrorUV, 7,   /* ASSUMES 3 source pos args! */
                            pVM->pUVM, rc, RT_SRC_POS_ARGS, pszFormat, &va2);
    va_end(va2);

# else
    /*
     * We're already on the EMT thread and can safely create a VMERROR chunk.
     */
    vmSetErrorCopy(pVM, rc, RT_SRC_POS_ARGS, pszFormat, args);
    VMMRZCallRing3NoCpu(pVM, VMMCALLRING3_VM_SET_ERROR, 0);
# endif
    return rc;
}


/**
 * Copies the error to a VMERROR structure.
 *
 * This is mainly intended for Ring-0 and GC where the error must be copied to
 * memory accessible from ring-3. But it's just possible that we might add
 * APIs for retrieving the VMERROR copy later.
 *
 * @param   pVM             The cross context VM structure.
 * @param   rc              VBox status code.
 * @param   SRC_POS         Use RT_SRC_POS.
 * @param   pszFormat       Error message format string.
 * @param   args            Error message arguments.
 * @thread  EMT
 */
void vmSetErrorCopy(PVM pVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list args)
{
    NOREF(pVM); NOREF(rc); RT_SRC_POS_NOREF(); NOREF(pszFormat); NOREF(args);
# if 0 /// @todo implement Ring-0 and GC VMSetError
    /*
     * Create the untranslated message copy.
     */
    /* free any old message. */
    MMHyperFree(pVM, MMHyperR32Ctx(pVM, pVM->vm.s.pError));
    pVM->vm.s.pError = NULL;

    /* calc reasonable start size. */
    size_t cchFile = pszFile ? strlen(pszFile) : 0;
    size_t cchFunction = pszFunction ? strlen(pszFunction) : 0;
    size_t cchFormat = strlen(pszFormat);
    size_t cb = sizeof(VMERROR)
              + cchFile + 1
              + cchFunction + 1
              + cchFormat + 32;

    /* allocate it */
    void *pv;
    int rc2 = MMHyperAlloc(pVM, cb, 0, MM_TAG_VM, &pv);
    if (RT_SUCCESS(rc2))
    {
        /* initialize it. */
        PVMERROR pErr = (PVMERROR)pv;
        pErr->cbAllocated = cb;
        pErr->iLine = iLine;
        pErr->off = sizeof(VMERROR);
        pErr->offFile = pErr->offFunction = 0;

        if (cchFile)
        {
            pErr->offFile = pErr->off;
            memcpy((uint8_t *)pErr + pErr->off, pszFile, cchFile + 1);
            pErr->off += cchFile + 1;
        }

        if (cchFunction)
        {
            pErr->offFunction = pErr->off;
            memcpy((uint8_t *)pErr + pErr->off, pszFunction, cchFunction + 1);
            pErr->off += cchFunction + 1;
        }

        pErr->offMessage = pErr->off;

        /* format the message (pErr might be reallocated) */
        VMSETERRORFMTARGS Args;
        Args.pVM = pVM;
        Args.pErr = pErr;

        va_list va2;
        va_copy(va2, args);
        RTStrFormatV(vmSetErrorFmtOut, &pErr, NULL, NULL, &pszFormatTmp, args);
        va_end(va2);

        /* done. */
        pVM->vm.s.pErrorR3 = MMHyper2HC(pVM, (uintptr_t)pArgs.pErr);
    }
# endif
}

#endif /* IN_RING3 */
#ifdef IN_RING3

/**
 * Sets the runtime error message.
 *
 * As opposed VMSetError(), this method is intended to inform the VM user about
 * errors and error-like conditions that happen at an arbitrary point during VM
 * execution (like "host memory low" or "out of host disk space").
 *
 * @returns VBox status code. For some flags the status code <b>must</b> be
 *          propagated up the stack.
 *
 * @param   pVM             The cross context VM structure.
 *
 * @param   fFlags          Flags indicating which actions to take.
 *                          See VMSETRTERR_FLAGS_* for details on each flag.
 *
 * @param   pszErrorId      Unique error identification string. This is used by
 *                          the frontends and maybe other devices or drivers, so
 *                          once an ID has been selected it's essentially
 *                          unchangable. Employ camelcase when constructing the
 *                          string, leave out spaces.
 *
 *                          The registered runtime error callbacks should string
 *                          switch on this and handle the ones it knows
 *                          specifically and the unknown ones generically.
 *
 * @param   pszFormat       Error message format string.
 * @param   ...             Error message arguments.
 *
 * @thread  Any
 */
VMMDECL(int) VMSetRuntimeError(PVMCC pVM, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = VMSetRuntimeErrorV(pVM, fFlags, pszErrorId, pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * va_list version of VMSetRuntimeError.
 *
 * @returns VBox status code. For some flags the status code <b>must</b> be
 *          propagated up the stack.
 *
 * @param   pVM             The cross context VM structure.
 * @param   fFlags          Flags indicating which actions to take. See
 *                          VMSETRTERR_FLAGS_*.
 * @param   pszErrorId      Error ID string.
 * @param   pszFormat       Error message format string.
 * @param   va              Error message arguments.
 *
 * @thread  Any
 */
VMMDECL(int) VMSetRuntimeErrorV(PVMCC pVM, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list va)
{
    Log(("VMSetRuntimeErrorV: fFlags=%#x pszErrorId=%s\n", fFlags, pszErrorId));

    /*
     * Relaxed parameter validation.
     */
    AssertPtr(pVM);
    AssertMsg(!(fFlags & ~(VMSETRTERR_FLAGS_NO_WAIT | VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_FATAL)), ("%#x\n", fFlags));
    Assert(!(fFlags & VMSETRTERR_FLAGS_NO_WAIT) || !VM_IS_EMT(pVM));
    Assert(!(fFlags & VMSETRTERR_FLAGS_SUSPEND) || !(fFlags & VMSETRTERR_FLAGS_FATAL));
    AssertPtr(pszErrorId);
    Assert(*pszErrorId);
    Assert(RTStrEnd(pszErrorId, 128) != NULL);
    AssertPtr(pszFormat);
    Assert(RTStrEnd(pszFormat, 512) != NULL);

# ifdef IN_RING3
    /*
     * Switch to EMT.
     *
     * If it's a no-wait request, we have to format the message into a buffer
     * here since the variable arguments list will become invalid once we call
     * va_end and return.
     */
    int rc;
    if (    !(fFlags & VMSETRTERR_FLAGS_NO_WAIT)
        ||  VM_IS_EMT(pVM))
    {
        fFlags &= ~VMSETRTERR_FLAGS_NO_WAIT;

        va_list va2;
        va_copy(va2, va); /* Have to make a copy here or GCC will break. */
        rc = VMR3ReqPriorityCallWait(pVM, VMCPUID_ANY, (PFNRT)vmR3SetRuntimeErrorV, 5,
                                     pVM, fFlags, pszErrorId, pszFormat, &va2);
        va_end(va2);
    }
    else
    {
        char *pszMessage = MMR3HeapAPrintfV(pVM, MM_TAG_VM, pszFormat, va);
        rc = VMR3ReqCallNoWait(pVM, VMCPUID_ANY, (PFNRT)vmR3SetRuntimeError, 4,
                               pVM, fFlags, pszErrorId, pszMessage);
        if (RT_FAILURE(rc))
            MMR3HeapFree(pszMessage);
    }

# else
    /*
     * We're already on the EMT and can safely create a VMRUNTIMEERROR chunk.
     */
    AssertReleaseMsgFailed(("Congratulations! You will have the pleasure of debugging the RC/R0 path.\n"));
    vmSetRuntimeErrorCopy(pVM, fFlags, pszErrorId, pszFormat, va);

    int rc = VMMRZCallRing3NoCpu(pVM, VMMCALLRING3_VM_SET_RUNTIME_ERROR, 0);
# endif

    Log(("VMSetRuntimeErrorV: returns %Rrc (pszErrorId=%s)\n", rc, pszErrorId));
    return rc;
}


/**
 * Copies the error to a VMRUNTIMEERROR structure.
 *
 * This is mainly intended for Ring-0 and RC where the error must be copied to
 * memory accessible from ring-3. But it's just possible that we might add
 * APIs for retrieving the VMRUNTIMEERROR copy later.
 *
 * @param   pVM             The cross context VM structure.
 * @param   fFlags          The error flags.
 * @param   pszErrorId      Error ID string.
 * @param   pszFormat       Error message format string.
 * @param   va              Error message arguments. This is of course spoiled
 *                          by this call.
 * @thread  EMT
 */
void vmSetRuntimeErrorCopy(PVM pVM, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list va)
{
    NOREF(pVM); NOREF(fFlags); NOREF(pszErrorId); NOREF(pszFormat); NOREF(va);
# if 0 /// @todo implement Ring-0 and GC VMSetError
    /*
     * Create the untranslated message copy.
     */
    /* free any old message. */
    MMHyperFree(pVM, MMHyperR32Ctx(pVM, pVM->vm.s.pRuntimeErrorR3));
    pVM->vm.s.pRuntimeErrorR3 = NULL;

    /* calc reasonable start size. */
    size_t cchErrorID = pszErrorId ? strlen(pszErrorId) : 0;
    size_t cchFormat = strlen(pszFormat);
    size_t cb = sizeof(VMRUNTIMEERROR)
              + cchErrorID + 1
              + cchFormat + 32;

    /* allocate it */
    void *pv;
    int rc2 = MMHyperAlloc(pVM, cb, 0, MM_TAG_VM, &pv);
    if (RT_SUCCESS(rc2))
    {
        /* initialize it. */
        PVMRUNTIMEERROR pErr = (PVMRUNTIMEERROR)pv;
        pErr->cbAllocated = cb;
        pErr->fFlags = fFlags;
        pErr->off = sizeof(PVMRUNTIMEERROR);
        pErr->offErrorID = 0;

        if (cchErrorID)
        {
            pErr->offErrorID = pErr->off;
            memcpy((uint8_t *)pErr + pErr->off, pszErrorId, cchErrorID + 1);
            pErr->off += cchErrorID + 1;
        }

        pErr->offMessage = pErr->off;

        /* format the message (pErr might be reallocated) */
        VMSETRUNTIMEERRORFMTARGS Args;
        Args.pVM = pVM;
        Args.pErr = pErr;

        va_list va2;
        va_copy(va2, args);
        RTStrFormatV(vmSetRuntimeErrorFmtOut, &pErr, NULL, NULL, &pszFormatTmp, args);
        va_end(va2);

        /* done. */
        pVM->vm.s.pErrorRuntimeR3 = MMHyper2HC(pVM, (uintptr_t)pArgs.pErr);
    }
# endif
}

#endif /* IN_RING3 */

/**
 * Gets the name of VM state.
 *
 * @returns Pointer to a read-only string with the state name.
 * @param   enmState    The state.
 */
VMMDECL(const char *) VMGetStateName(VMSTATE enmState)
{
    switch (enmState)
    {
#define MY_CASE(enm) case VMSTATE_##enm: return #enm;
        MY_CASE(CREATING);
        MY_CASE(CREATED);
        MY_CASE(RUNNING);
        MY_CASE(LOADING);
        MY_CASE(LOAD_FAILURE);
        MY_CASE(SAVING);
        MY_CASE(SUSPENDED);
        MY_CASE(RESETTING);
        MY_CASE(GURU_MEDITATION);
        MY_CASE(OFF);
        MY_CASE(DESTROYING);
        MY_CASE(TERMINATED);
#undef MY_CASE
        default:
            return "Unknown";
    }
}


/**
 * Gets the total reset count.
 *
 * @returns Reset count. UINT32_MAX if @a pVM is invalid.
 * @param   pVM         The VM handle.
 */
VMMDECL(uint32_t) VMGetResetCount(PVMCC pVM)
{
    VM_ASSERT_VALID_EXT_RETURN(pVM, UINT32_MAX);
    return pVM->vm.s.cResets;
}


/**
 * Gets the soft reset count.
 *
 * @returns Soft reset count. UINT32_MAX if @a pVM is invalid.
 * @param   pVM         The VM handle.
 */
VMMDECL(uint32_t) VMGetSoftResetCount(PVMCC pVM)
{
    VM_ASSERT_VALID_EXT_RETURN(pVM, UINT32_MAX);
    return pVM->vm.s.cSoftResets;
}


/**
 * Gets the hard reset count.
 *
 * @returns Hard reset count. UINT32_MAX if @a pVM is invalid.
 * @param   pVM         The VM handle.
 */
VMMDECL(uint32_t) VMGetHardResetCount(PVMCC pVM)
{
    VM_ASSERT_VALID_EXT_RETURN(pVM, UINT32_MAX);
    return pVM->vm.s.cHardResets;
}

