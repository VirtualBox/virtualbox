/* $Id: xptcstubs_arm64_vbox.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * XPCOM - XPTC stubs for arm64.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include "xptcprivate.h"
#include <iprt/cdefs.h>
#include <iprt/alloca.h>
#include <iprt/assert.h>

#include "xptc_arm64_vbox.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECL_NO_INLINE(extern "C", nsresult)
CommonXPTCStubCWorker(nsXPTCStubBase *pThis, uint32_t idxMethod, uint64_t *pauGprArgs, uint64_t *pauFprArgs, uint64_t *puStackArgs);

/*
 * clang supports the naked attribute on arm64 whereas gcc does not, so we have to resort to this ugliness
 * in order to support both and not require a separate assembly file so we need only one set of defines...
 * Luckily this abormination is contained in this file.
 */
__asm__(
    "BEGINCODE \n"
    "BEGINPROC_HIDDEN CommonXPTCStub\n"
    ".cfi_startproc\n"
    /* Prologue - reserve space for frame+link reg spill and the GPR and FPR arrays. */ "\
    sub     sp, sp,        #" RT_XSTR(SZ_ARGS_IN_GPRS_AND_FPRS) " + 16 \n\
    stp     x29, x30, [sp, #" RT_XSTR(SZ_ARGS_IN_GPRS_AND_FPRS) "] \n\
    add     x29, sp,       #" RT_XSTR(SZ_ARGS_IN_GPRS_AND_FPRS) "\n\
    .cfi_def_cfa        x29, 16 \n\
    .cfi_rel_offset     x30, -8 \n\
    .cfi_rel_offset     x29, -16 \n\
"
    /* reserve stack space for the integer and floating point registers and save them: */ "\
    \n\
    stp     x0, x1, [sp, #0] \n\
    stp     x2, x3, [sp, #16] \n\
    stp     x4, x5, [sp, #32] \n\
    stp     x6, x7, [sp, #48] \n\
    \n\
    stp     d0, d1, [sp, #" RT_XSTR(SZ_ARGS_IN_GPRS) "] \n\
    stp     d2, d3, [sp, #" RT_XSTR(SZ_ARGS_IN_GPRS) " + 16] \n\
    stp     d4, d5, [sp, #" RT_XSTR(SZ_ARGS_IN_GPRS) " + 32] \n\
    stp     d6, d7, [sp, #" RT_XSTR(SZ_ARGS_IN_GPRS) " + 48] \n\
\n"
    /* Call the C worker. We keep x0 as is.
       Set w1 to the w17 method index from the stubs. */ "\
    mov     w1, w17 \n\
    mov     x2, sp \n\
    add     x3, sp, #" RT_XSTR(SZ_ARGS_IN_GPRS) "\n\
    add     x4, sp, #" RT_XSTR(SZ_ARGS_IN_GPRS_AND_FPRS) " + 16 \n\
    bl      " NAME_PREFIX_STR "CommonXPTCStubCWorker \n\
"
    /* Epilogue (clang does not emit the .cfi's here, so drop them too?): */ "\
    ldp     x29, x30, [sp, #" RT_XSTR(SZ_ARGS_IN_GPRS_AND_FPRS) "] \n\
    add     sp, sp,        #" RT_XSTR(SZ_ARGS_IN_GPRS_AND_FPRS) " + 16 \n\
    .cfi_def_cfa sp, 0 \n\
    .cfi_restore x29 \n\
    .cfi_restore x30 \n\
    ret \n"
    ".cfi_endproc\n"
);

#define STUB_ENTRY(n) \
    asm("BEGINCODE\n" \
        ".if    " #n " < 10\n" \
        "BEGINPROC _ZN14nsXPTCStubBase5Stub" #n "Ev\n" \
        ".elseif    " #n " < 100\n" \
        "BEGINPROC _ZN14nsXPTCStubBase6Stub" #n "Ev\n" \
        ".elseif    " #n " < 1000\n" \
        "BEGINPROC _ZN14nsXPTCStubBase7Stub" #n "Ev\n" \
        ".else\n" \
        ".err   \"stub number " #n " >= 1000 not yet supported\"\n" \
        ".endif\n" \
        "mov  w17, #" #n "\n" \
        "b    " NAME_PREFIX_STR "CommonXPTCStub\n" \
        ".if    " #n " < 10\n" \
        "    .size " NAME_PREFIX_STR "_ZN14nsXPTCStubBase5Stub" #n "Ev,.-" NAME_PREFIX_STR "_ZN14nsXPTCStubBase5Stub" #n "Ev\n" \
        ".elseif    " #n " < 100\n" \
        "    .size " NAME_PREFIX_STR "_ZN14nsXPTCStubBase6Stub" #n "Ev,.-" NAME_PREFIX_STR "_ZN14nsXPTCStubBase6Stub" #n "Ev\n" \
        ".else\n" \
        "    .size " NAME_PREFIX_STR "_ZN14nsXPTCStubBase7Stub" #n "Ev,.-" NAME_PREFIX_STR "_ZN14nsXPTCStubBase7Stub" #n "Ev\n" \
        ".endif");

#define SENTINEL_ENTRY(n) \
    nsresult nsXPTCStubBase::Sentinel##n() \
    { \
        AssertMsgFailed(("nsXPTCStubBase::Sentinel" #n " called!\n")); \
        return NS_ERROR_NOT_IMPLEMENTED; \
    }

/* Instantiate the stubs and sentinels  */
#include "xptcstubsdef.inc"



/*
 * Function templates for fetching arguments
 */

template<typename Type> static inline void fetchStack(Type *pRet, uint64_t *&rpuStackArgs)
{
#ifdef RT_OS_DARWIN /* macOS compacts stack usage. */
    Type *pStackTmp = RT_ALIGN_PT(rpuStackArgs, sizeof(Type), Type *);
    *pRet = *pStackTmp;
    rpuStackArgs = (uint64_t *)(pStackTmp + 1);
#else
    *pRet = (Type)*rpuStackArgs++;
#endif
}

template<typename Type> static inline void fetchFpr(Type *pRet, uint64_t *pauFprArgs, unsigned &ridxFpr, uint64_t *&rpuStackArgs)
{
    if (ridxFpr < NUM_ARGS_IN_FPRS)
        *pRet = (Type)pauFprArgs[ridxFpr++];
    else
        fetchStack(pRet, rpuStackArgs);
}


template<typename Type> static inline void fetchGpr(Type *pRet, uint64_t *pauGprArgs, unsigned &ridxGpr, uint64_t *&rpuStackArgs)
{
    if (ridxGpr < NUM_ARGS_IN_GPRS)
        *pRet = (Type)pauGprArgs[ridxGpr++];
    else
        fetchStack(pRet, rpuStackArgs);
}


/**
 * Called by CommonXPTCStub below after it dumps registers and locates
 * arguments on the stack (if any).
 */
DECL_NO_INLINE(extern "C", nsresult)
CommonXPTCStubCWorker(nsXPTCStubBase *pThis, uint32_t idxMethod, uint64_t *pauGprArgs, uint64_t *pauFprArgs, uint64_t *puStackArgs)
{
    AssertReturn(pThis, NS_ERROR_UNEXPECTED);

    /* Get method information: */
    nsIInterfaceInfo *pInterfaceInfo = NULL;
    nsresult hrc = pThis->GetInterfaceInfo(&pInterfaceInfo);
    AssertReturn(NS_SUCCEEDED(hrc), hrc);
    AssertReturn(pInterfaceInfo, NS_ERROR_UNEXPECTED);

    const nsXPTMethodInfo *pMethodInfo;
    hrc = pInterfaceInfo->GetMethodInfo((PRUint16)idxMethod, &pMethodInfo);
    AssertReturn(NS_SUCCEEDED(hrc), hrc);
    AssertReturn(pMethodInfo, NS_ERROR_UNEXPECTED);

    /* Allocate dispatcher parameter array. */
    PRUint8 const cParams = pMethodInfo->GetParamCount();
    nsXPTCMiniVariant aParamsStatic[8];
    nsXPTCMiniVariant *paParams;
    if (cParams <= RT_ELEMENTS(aParamsStatic))
        paParams = aParamsStatic;
    else
    {
        paParams = (nsXPTCMiniVariant *)alloca(sizeof(paParams[0]) * cParams);
        AssertReturn(paParams, NS_ERROR_UNEXPECTED);
    }

    /*
     * Populate the dispatcher parameter array.
     */
    unsigned idxGprArgs = 1;    /* The 'pThis' pointer (x0) is not included in cParams. */
    unsigned idxFprArgs = 0;
    for (PRUint8 i = 0; i < cParams; i++)
    {
        const nsXPTParamInfo &rParam = pMethodInfo->GetParam(i);
        if (rParam.IsOut())
            fetchGpr(&paParams[i].val.p, pauGprArgs, idxGprArgs, puStackArgs);
        else
        {
            const nsXPTType Type  = rParam.GetType();
            switch (Type)
            {
                case nsXPTType::T_I8:       fetchGpr(&paParams[i].val.i8,  pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_I16:      fetchGpr(&paParams[i].val.i16, pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_I32:      fetchGpr(&paParams[i].val.i32, pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_I64:      fetchGpr(&paParams[i].val.i64, pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_U8:       fetchGpr(&paParams[i].val.u8,  pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_U16:      fetchGpr(&paParams[i].val.u16, pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_U32:      fetchGpr(&paParams[i].val.u32, pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_U64:      fetchGpr(&paParams[i].val.u64, pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_BOOL:     fetchGpr(&paParams[i].val.b,   pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_CHAR:     fetchGpr(&paParams[i].val.c,   pauGprArgs, idxGprArgs, puStackArgs); break;
                case nsXPTType::T_WCHAR:    fetchGpr(&paParams[i].val.wc,  pauGprArgs, idxGprArgs, puStackArgs); break;

                case nsXPTType::T_FLOAT:    fetchFpr(&paParams[i].val.f,   pauFprArgs, idxFprArgs, puStackArgs); break;
                case nsXPTType::T_DOUBLE:   fetchFpr(&paParams[i].val.d,   pauFprArgs, idxFprArgs, puStackArgs); break;

                default:
                    if (!Type.IsArithmetic())
                        fetchGpr(&paParams[i].val.p, pauGprArgs, idxGprArgs, puStackArgs);
                    else
                        AssertMsgFailedReturn(("%#x idxMethod=%#x\n",(unsigned)Type, idxMethod), NS_ERROR_UNEXPECTED);
                    break;
            }
        }
    }

    /*
     * Dispatch the method call.
     */
    hrc = pThis->CallMethod((PRUint16)idxMethod, pMethodInfo, paParams);

    NS_RELEASE(pInterfaceInfo);
    return hrc;

}

#if 0
extern "C" nsresult CommonXPTCStubTest(uint64_t x0,
                                       uint64_t x1,
                                       uint64_t x2,
                                       uint64_t x3,
                                       uint64_t x4,
                                       uint64_t x5,
                                       uint64_t x6,
                                       uint64_t x7)
{
    uint64_t aGprs[8];
    aGprs[0] = x0;
    aGprs[1] = x1;
    aGprs[2] = x2;
    aGprs[3] = x3;
    aGprs[4] = x4;
    aGprs[5] = x5;
    aGprs[6] = x6;
    aGprs[7] = x7;

    return CommonXPTCStubCWorker((nsXPTCStubBase *)x0, x1, aGprs, aGprs, aGprs);
}
#endif

