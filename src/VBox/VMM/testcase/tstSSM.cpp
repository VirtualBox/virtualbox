/* $Id: tstSSM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Saved State Manager Testcase.
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
#include <VBox/vmm/ssm.h>
#include "VMInternal.h" /* createFakeVM */
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/stam.h>

#include <VBox/log.h>
#include <VBox/sup.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/path.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define TSTSSM_BIG_CONFIG   1

#ifdef TSTSSM_BIG_CONFIG
# define TSTSSM_ITEM_SIZE    (512*_1M)
#else
# define TSTSSM_ITEM_SIZE    (5*_1M)
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
const uint8_t   gabPage[GUEST_PAGE_SIZE] = {0};
const char      gachMem1[] = "sdfg\1asdfa\177hjkl;sdfghjkl;dfghjkl;dfghjkl;\0\0asdf;kjasdf;lkjasd;flkjasd;lfkjasd\0;lfk";
#ifdef TSTSSM_BIG_CONFIG
uint8_t         gabBigMem[_1M];
#else
uint8_t         gabBigMem[8*_1M];
#endif


/** initializes gabBigMem with some non zero stuff. */
static void initBigMem(void)
{
#if 0
    uint32_t *puch = (uint32_t *)&gabBigMem[0];
    uint32_t *puchEnd = (uint32_t *)&gabBigMem[sizeof(gabBigMem)];
    uint32_t  u32 = 0xdeadbeef;
    for (; puch < puchEnd; puch++)
    {
        *puch = u32;
        u32 += 19;
        u32 = (u32 << 1) | (u32 >> 31);
    }
#else
    uint8_t *pb = &gabBigMem[0];
    uint8_t *pbEnd = &gabBigMem[sizeof(gabBigMem)];
    for (; pb < pbEnd; pb += 16)
    {
        char szTmp[17];
        RTStrPrintf(szTmp, sizeof(szTmp), "aaaa%08Xzzzz", (uint32_t)(uintptr_t)pb);
        memcpy(pb, szTmp, 16);
    }

    /* add some zero pages */
    memset(&gabBigMem[sizeof(gabBigMem) / 4],     0, GUEST_PAGE_SIZE * 4);
    memset(&gabBigMem[sizeof(gabBigMem) / 4 * 3], 0, GUEST_PAGE_SIZE * 4);
#endif
}

/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM handle.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) Item01Save(PVM pVM, PSSMHANDLE pSSM)
{
    uint64_t u64Start = RTTimeNanoTS();
    NOREF(pVM);

    /*
     * Test writing some memory block.
     */
    int rc = SSMR3PutMem(pSSM, gachMem1, sizeof(gachMem1));
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item01: #1 - SSMR3PutMem -> %Rrc\n", rc);
        return rc;
    }

    /*
     * Test writing a zeroterminated string.
     */
    rc = SSMR3PutStrZ(pSSM, "String");
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item01: #1 - SSMR3PutMem -> %Rrc\n", rc);
        return rc;
    }


    /*
     * Test the individual integer put functions to see that they all work.
     * (Testcases are also known as "The Land of The Ugly Code"...)
     */
#define ITEM(suff,bits, val) \
    rc = SSMR3Put##suff(pSSM, val); \
    if (RT_FAILURE(rc)) \
    { \
        RTPrintf("Item01: #" #suff " - SSMR3Put" #suff "(," #val ") -> %Rrc\n", rc); \
        return rc; \
    }
    /* copy & past with the load one! */
    ITEM(U8,  uint8_t,  0xff);
    ITEM(U8,  uint8_t,  0x0);
    ITEM(U8,  uint8_t,  1);
    ITEM(U8,  uint8_t,  42);
    ITEM(U8,  uint8_t,  230);
    ITEM(S8,   int8_t,  -128);
    ITEM(S8,   int8_t,  127);
    ITEM(S8,   int8_t,  12);
    ITEM(S8,   int8_t,  -76);
    ITEM(U16, uint16_t, 0xffff);
    ITEM(U16, uint16_t, 0x0);
    ITEM(S16,  int16_t, 32767);
    ITEM(S16,  int16_t, -32768);
    ITEM(U32, uint32_t, 4294967295U);
    ITEM(U32, uint32_t, 0);
    ITEM(U32, uint32_t, 42);
    ITEM(U32, uint32_t, 2342342344U);
    ITEM(S32,  int32_t, -2147483647-1);
    ITEM(S32,  int32_t, 2147483647);
    ITEM(S32,  int32_t, 42);
    ITEM(S32,  int32_t, 568459834);
    ITEM(S32,  int32_t, -58758999);
    ITEM(U64, uint64_t, 18446744073709551615ULL);
    ITEM(U64, uint64_t, 0);
    ITEM(U64, uint64_t, 42);
    ITEM(U64, uint64_t, 593023944758394234ULL);
    ITEM(S64,  int64_t, 9223372036854775807LL);
    ITEM(S64,  int64_t, -9223372036854775807LL - 1);
    ITEM(S64,  int64_t, 42);
    ITEM(S64,  int64_t, 21398723459873LL);
    ITEM(S64,  int64_t, -5848594593453453245LL);
#undef ITEM

    uint64_t u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Saved 1st item in %'RI64 ns\n", u64Elapsed);
    return 0;
}

/**
 * Prepare state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM handle.
 * @param   pSSM            SSM operation handle.
 * @param   uVersion        The data layout version.
 * @param   uPass           The data pass.
 */
static DECLCALLBACK(int) Item01Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    NOREF(pVM); NOREF(uPass);
    if (uVersion != 0)
    {
        RTPrintf("Item01: uVersion=%#x, expected 0\n", uVersion);
        return VERR_GENERAL_FAILURE;
    }

    /*
     * Load the memory block.
     */
    char achTmp[sizeof(gachMem1)];
    int rc = SSMR3GetMem(pSSM, achTmp, sizeof(gachMem1));
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item01: #1 - SSMR3GetMem -> %Rrc\n", rc);
        return rc;
    }

    /*
     * Load the string.
     */
    rc = SSMR3GetStrZ(pSSM, achTmp, sizeof(achTmp));
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item01: #2 - SSMR3GetStrZ -> %Rrc\n", rc);
        return rc;
    }

    /*
     * Test the individual integer put functions to see that they all work.
     * (Testcases are also known as "The Land of The Ugly Code"...)
     */
#define ITEM(suff, type, val) \
    do { \
        type var = {0}; \
        rc = SSMR3Get##suff(pSSM, &var); \
        if (RT_FAILURE(rc)) \
        { \
            RTPrintf("Item01: #" #suff " - SSMR3Get" #suff "(," #val ") -> %Rrc\n", rc); \
            return rc; \
        } \
        if (var != val) \
        { \
            RTPrintf("Item01: #" #suff " - SSMR3Get" #suff "(," #val ") -> %d returned wrong value!\n", rc); \
            return VERR_GENERAL_FAILURE; \
        } \
    } while (0)
    /* copy & past with the load one! */
    ITEM(U8,  uint8_t,  0xff);
    ITEM(U8,  uint8_t,  0x0);
    ITEM(U8,  uint8_t,  1);
    ITEM(U8,  uint8_t,  42);
    ITEM(U8,  uint8_t,  230);
    ITEM(S8,   int8_t,  -128);
    ITEM(S8,   int8_t,  127);
    ITEM(S8,   int8_t,  12);
    ITEM(S8,   int8_t,  -76);
    ITEM(U16, uint16_t, 0xffff);
    ITEM(U16, uint16_t, 0x0);
    ITEM(S16,  int16_t, 32767);
    ITEM(S16,  int16_t, -32768);
    ITEM(U32, uint32_t, 4294967295U);
    ITEM(U32, uint32_t, 0);
    ITEM(U32, uint32_t, 42);
    ITEM(U32, uint32_t, 2342342344U);
    ITEM(S32,  int32_t, -2147483647-1);
    ITEM(S32,  int32_t, 2147483647);
    ITEM(S32,  int32_t, 42);
    ITEM(S32,  int32_t, 568459834);
    ITEM(S32,  int32_t, -58758999);
    ITEM(U64, uint64_t, 18446744073709551615ULL);
    ITEM(U64, uint64_t, 0);
    ITEM(U64, uint64_t, 42);
    ITEM(U64, uint64_t, 593023944758394234ULL);
    ITEM(S64,  int64_t, 9223372036854775807LL);
    ITEM(S64,  int64_t, -9223372036854775807LL - 1);
    ITEM(S64,  int64_t, 42);
    ITEM(S64,  int64_t, 21398723459873LL);
    ITEM(S64,  int64_t, -5848594593453453245LL);
#undef ITEM

    return 0;
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM handle.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) Item02Save(PVM pVM, PSSMHANDLE pSSM)
{
    NOREF(pVM);
    uint64_t u64Start = RTTimeNanoTS();

    /*
     * Put the size.
     */
    uint32_t cb = sizeof(gabBigMem);
    int rc = SSMR3PutU32(pSSM, cb);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item02: PutU32 -> %Rrc\n", rc);
        return rc;
    }

    /*
     * Put 8MB of memory to the file in 3 chunks.
     */
    uint8_t *pbMem = &gabBigMem[0];
    uint32_t cbChunk = cb / 47;
    rc = SSMR3PutMem(pSSM, pbMem, cbChunk);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item02: PutMem(,%p,%#x) -> %Rrc\n", pbMem, cbChunk, rc);
        return rc;
    }
    cb -= cbChunk;
    pbMem += cbChunk;

    /* next piece. */
    cbChunk *= 19;
    rc = SSMR3PutMem(pSSM, pbMem, cbChunk);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item02: PutMem(,%p,%#x) -> %Rrc\n", pbMem, cbChunk, rc);
        return rc;
    }
    cb -= cbChunk;
    pbMem += cbChunk;

    /* last piece. */
    cbChunk = cb;
    rc = SSMR3PutMem(pSSM, pbMem, cbChunk);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item02: PutMem(,%p,%#x) -> %Rrc\n", pbMem, cbChunk, rc);
        return rc;
    }

    uint64_t u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Saved 2nd item in %'RI64 ns\n", u64Elapsed);
    return 0;
}

/**
 * Prepare state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM handle.
 * @param   pSSM            SSM operation handle.
 * @param   uVersion        The data layout version.
 * @param   uPass           The data pass.
 */
static DECLCALLBACK(int) Item02Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    NOREF(pVM); NOREF(uPass);
    if (uVersion != 0)
    {
        RTPrintf("Item02: uVersion=%#x, expected 0\n", uVersion);
        return VERR_GENERAL_FAILURE;
    }

    /*
     * Load the size.
     */
    uint32_t cb;
    int rc = SSMR3GetU32(pSSM, &cb);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item02: SSMR3GetU32 -> %Rrc\n", rc);
        return rc;
    }
    if (cb != sizeof(gabBigMem))
    {
        RTPrintf("Item02: loaded size doesn't match the real thing. %#x != %#x\n", cb, sizeof(gabBigMem));
        return VERR_GENERAL_FAILURE;
    }

    /*
     * Load the memory chunk by chunk.
     */
    uint8_t    *pbMem = &gabBigMem[0];
    char        achTmp[16383];
    uint32_t    cbChunk = sizeof(achTmp);
    while (cb > 0)
    {
        cbChunk -= 7;
        if (cbChunk < 64)
            cbChunk = sizeof(achTmp) - (cbChunk % 47);
        if (cbChunk > cb)
            cbChunk = cb;
        rc = SSMR3GetMem(pSSM, &achTmp[0], cbChunk);
        if (RT_FAILURE(rc))
        {
            RTPrintf("Item02: SSMR3GetMem(,,%#x) -> %d offset %#x\n", cbChunk, rc, pbMem - &gabBigMem[0]);
            return rc;
        }
        if (memcmp(achTmp, pbMem, cbChunk))
        {
            RTPrintf("Item02: compare failed. mem offset=%#x cbChunk=%#x\n", pbMem - &gabBigMem[0], cbChunk);
            return VERR_GENERAL_FAILURE;
        }

        /* next */
        pbMem += cbChunk;
        cb -= cbChunk;
    }

    return 0;
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM handle.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) Item03Save(PVM pVM, PSSMHANDLE pSSM)
{
    NOREF(pVM);
    uint64_t u64Start = RTTimeNanoTS();

    /*
     * Put the size.
     */
    uint32_t cb = TSTSSM_ITEM_SIZE;
    int rc = SSMR3PutU32(pSSM, cb);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item03: PutU32 -> %Rrc\n", rc);
        return rc;
    }

    /*
     * Put 512 MB page by page.
     */
    const uint8_t *pu8Org = &gabBigMem[0];
    while (cb > 0)
    {
        rc = SSMR3PutMem(pSSM, pu8Org, GUEST_PAGE_SIZE);
        if (RT_FAILURE(rc))
        {
            RTPrintf("Item03: PutMem(,%p,%#x) -> %Rrc\n", pu8Org, GUEST_PAGE_SIZE, rc);
            return rc;
        }

        /* next */
        cb -= GUEST_PAGE_SIZE;
        pu8Org += GUEST_PAGE_SIZE;
        if (pu8Org >= &gabBigMem[sizeof(gabBigMem)])
            pu8Org = &gabBigMem[0];
    }

    uint64_t u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Saved 3rd item in %'RI64 ns\n", u64Elapsed);
    return 0;
}

/**
 * Prepare state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM handle.
 * @param   pSSM            SSM operation handle.
 * @param   uVersion        The data layout version.
 * @param   uPass           The data pass.
 */
static DECLCALLBACK(int) Item03Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    NOREF(pVM); NOREF(uPass);
    if (uVersion != 123)
    {
        RTPrintf("Item03: uVersion=%#x, expected 123\n", uVersion);
        return VERR_GENERAL_FAILURE;
    }

    /*
     * Load the size.
     */
    uint32_t cb;
    int rc = SSMR3GetU32(pSSM, &cb);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item03: SSMR3GetU32 -> %Rrc\n", rc);
        return rc;
    }
    if (cb != TSTSSM_ITEM_SIZE)
    {
        RTPrintf("Item03: loaded size doesn't match the real thing. %#x != %#x\n", cb, TSTSSM_ITEM_SIZE);
        return VERR_GENERAL_FAILURE;
    }

    /*
     * Load the memory page by page.
     */
    const uint8_t *pu8Org = &gabBigMem[0];
    while (cb > 0)
    {
        char achPage[GUEST_PAGE_SIZE];
        rc = SSMR3GetMem(pSSM, &achPage[0], GUEST_PAGE_SIZE);
        if (RT_FAILURE(rc))
        {
            RTPrintf("Item03: SSMR3GetMem(,,%#x) -> %Rrc offset %#x\n", GUEST_PAGE_SIZE, rc, TSTSSM_ITEM_SIZE - cb);
            return rc;
        }
        if (memcmp(achPage, pu8Org, GUEST_PAGE_SIZE))
        {
            RTPrintf("Item03: compare failed. mem offset=%#x\n", TSTSSM_ITEM_SIZE - cb);
            return VERR_GENERAL_FAILURE;
        }

        /* next */
        cb -= GUEST_PAGE_SIZE;
        pu8Org += GUEST_PAGE_SIZE;
        if (pu8Org >= &gabBigMem[sizeof(gabBigMem)])
            pu8Org = &gabBigMem[0];
    }

    return 0;
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM handle.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) Item04Save(PVM pVM, PSSMHANDLE pSSM)
{
    NOREF(pVM);
    uint64_t u64Start = RTTimeNanoTS();

    /*
     * Put the size.
     */
    uint32_t cb = 512*_1M;
    int rc = SSMR3PutU32(pSSM, cb);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item04: PutU32 -> %Rrc\n", rc);
        return rc;
    }

    /*
     * Put 512 MB page by page.
     */
    while (cb > 0)
    {
        rc = SSMR3PutMem(pSSM, gabPage, GUEST_PAGE_SIZE);
        if (RT_FAILURE(rc))
        {
            RTPrintf("Item04: PutMem(,%p,%#x) -> %Rrc\n", gabPage, GUEST_PAGE_SIZE, rc);
            return rc;
        }

        /* next */
        cb -= GUEST_PAGE_SIZE;
    }

    uint64_t u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Saved 4th item in %'RI64 ns\n", u64Elapsed);
    return 0;
}

/**
 * Prepare state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM handle.
 * @param   pSSM            SSM operation handle.
 * @param   uVersion        The data layout version.
 * @param   uPass           The data pass.
 */
static DECLCALLBACK(int) Item04Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    NOREF(pVM); NOREF(uPass);
    if (uVersion != 42)
    {
        RTPrintf("Item04: uVersion=%#x, expected 42\n", uVersion);
        return VERR_GENERAL_FAILURE;
    }

    /*
     * Load the size.
     */
    uint32_t cb;
    int rc = SSMR3GetU32(pSSM, &cb);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item04: SSMR3GetU32 -> %Rrc\n", rc);
        return rc;
    }
    if (cb != 512*_1M)
    {
        RTPrintf("Item04: loaded size doesn't match the real thing. %#x != %#x\n", cb, 512*_1M);
        return VERR_GENERAL_FAILURE;
    }

    /*
     * Load the memory page by page.
     */
    while (cb > 0)
    {
        char achPage[GUEST_PAGE_SIZE];
        rc = SSMR3GetMem(pSSM, &achPage[0], GUEST_PAGE_SIZE);
        if (RT_FAILURE(rc))
        {
            RTPrintf("Item04: SSMR3GetMem(,,%#x) -> %Rrc offset %#x\n", GUEST_PAGE_SIZE, rc, 512*_1M - cb);
            return rc;
        }
        if (memcmp(achPage, gabPage, GUEST_PAGE_SIZE))
        {
            RTPrintf("Item04: compare failed. mem offset=%#x\n", 512*_1M - cb);
            return VERR_GENERAL_FAILURE;
        }

        /* next */
        cb -= GUEST_PAGE_SIZE;
    }

    return 0;
}


/**
 * Creates a mockup VM structure for testing SSM.
 *
 * @returns 0 on success, 1 on failure.
 * @param   ppVM    Where to store Pointer to the VM.
 *
 * @todo    Move this to VMM/VM since it's stuff done by several testcases.
 */
static int createFakeVM(PVM *ppVM)
{
    /*
     * Allocate and init the UVM structure.
     */
    PUVM pUVM = (PUVM)RTMemPageAllocZ(sizeof(*pUVM));
    AssertReturn(pUVM, 1);
    pUVM->u32Magic = UVM_MAGIC;
    pUVM->vm.s.idxTLS = RTTlsAlloc();
    int rc = RTTlsSet(pUVM->vm.s.idxTLS, &pUVM->aCpus[0]);
    if (RT_SUCCESS(rc))
    {
        pUVM->aCpus[0].pUVM = pUVM;
        pUVM->aCpus[0].vm.s.NativeThreadEMT = RTThreadNativeSelf();

        rc = STAMR3InitUVM(pUVM);
        if (RT_SUCCESS(rc))
        {
            rc = MMR3InitUVM(pUVM);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Allocate and init the VM structure.
                 */
                PVM pVM = (PVM)RTMemPageAllocZ(sizeof(VM) + sizeof(VMCPU));
                rc = pVM ? VINF_SUCCESS : VERR_NO_PAGE_MEMORY;
                if (RT_SUCCESS(rc))
                {
                    pVM->enmVMState = VMSTATE_CREATED;
                    pVM->pVMR3 = pVM;
                    pVM->pUVM = pUVM;
                    pVM->cCpus = 1;

                    PVMCPU pVCpu = (PVMCPU)(pVM + 1);
                    pVCpu->pVMR3 = pVM;
                    pVCpu->hNativeThread = RTThreadNativeSelf();
                    pVM->apCpusR3[0] = pVCpu;

                    pUVM->pVM = pVM;
                    *ppVM = pVM;
                    return 0;
                }

                RTPrintf("Fatal error: failed to allocated pages for the VM structure, rc=%Rrc\n", rc);
            }
            else
                RTPrintf("Fatal error: MMR3InitUVM failed, rc=%Rrc\n", rc);
        }
        else
            RTPrintf("Fatal error: SSMR3InitUVM failed, rc=%Rrc\n", rc);
    }
    else
        RTPrintf("Fatal error: RTTlsSet failed, rc=%Rrc\n", rc);

    *ppVM = NULL;
    return 1;
}


/**
 * Destroy the VM structure.
 *
 * @param   pVM     Pointer to the VM.
 *
 * @todo    Move this to VMM/VM since it's stuff done by several testcases.
 */
static void destroyFakeVM(PVM pVM)
{
    SSMR3Term(pVM);
    STAMR3TermUVM(pVM->pUVM);
    MMR3TermUVM(pVM->pUVM);
}


/**
 *  Entry point.
 */
int main(int argc, char **argv)
{
    /*
     * Init runtime and static data.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    AssertRCReturn(rc, RTEXITCODE_INIT);
    RTPrintf("tstSSM: TESTING...\n");
    initBigMem();
    const char *pszFilename = "SSMTestSave#1";

    /*
     * Create an fake VM structure and init SSM.
     */
    PVM pVM;
    if (createFakeVM(&pVM))
        return 1;

    /*
     * Register a few callbacks.
     */
    rc = SSMR3RegisterInternal(pVM, "SSM Testcase Data Item no.1 (all types)", 1, 0, 256,
                               NULL, NULL, NULL,
                               NULL, Item01Save, NULL,
                               NULL, Item01Load, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Register #1 -> %Rrc\n", rc);
        return 1;
    }

    rc = SSMR3RegisterInternal(pVM, "SSM Testcase Data Item no.2 (rand mem)", 2, 0, _1M * 8,
                               NULL, NULL, NULL,
                               NULL, Item02Save, NULL,
                               NULL, Item02Load, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Register #2 -> %Rrc\n", rc);
        return 1;
    }

    rc = SSMR3RegisterInternal(pVM, "SSM Testcase Data Item no.3 (big mem)", 0, 123, 512*_1M,
                               NULL, NULL, NULL,
                               NULL, Item03Save, NULL,
                               NULL, Item03Load, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Register #3 -> %Rrc\n", rc);
        return 1;
    }

    rc = SSMR3RegisterInternal(pVM, "SSM Testcase Data Item no.4 (big zero mem)", 0, 42, 512*_1M,
                               NULL, NULL, NULL,
                               NULL, Item04Save, NULL,
                               NULL, Item04Load, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Register #4 -> %Rrc\n", rc);
        return 1;
    }

    /*
     * Attempt a save.
     */
    uint64_t u64Start = RTTimeNanoTS();
    rc = SSMR3Save(pVM, pszFilename, NULL, NULL, SSMAFTER_DESTROY, NULL, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Save #1 -> %Rrc\n", rc);
        return 1;
    }
    uint64_t u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Saved in %'RI64 ns\n", u64Elapsed);

    RTFSOBJINFO Info;
    rc = RTPathQueryInfo(pszFilename, &Info, RTFSOBJATTRADD_NOTHING);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstSSM: failed to query file size: %Rrc\n", rc);
        return 1;
    }
    RTPrintf("tstSSM: file size %'RI64 bytes\n", Info.cbObject);

    /*
     * Attempt a load.
     */
    u64Start = RTTimeNanoTS();
    rc = SSMR3Load(pVM, pszFilename, NULL /*pStreamOps*/, NULL /*pStreamOpsUser*/,
                   SSMAFTER_RESUME, NULL /*pfnProgress*/, NULL /*pvProgressUser*/);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Load #1 -> %Rrc\n", rc);
        return 1;
    }
    u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Loaded in %'RI64 ns\n", u64Elapsed);

    /*
     * Validate it.
     */
    u64Start = RTTimeNanoTS();
    rc = SSMR3ValidateFile(pszFilename, NULL /*pStreamOps*/, NULL /*pvStreamOps*/, false /* fChecksumIt*/ );
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3ValidateFile #1 -> %Rrc\n", rc);
        return 1;
    }
    u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Validated without checksumming in %'RI64 ns\n", u64Elapsed);

    u64Start = RTTimeNanoTS();
    rc = SSMR3ValidateFile(pszFilename, NULL /*pStreamOps*/, NULL /*pvStreamOps*/, true /* fChecksumIt */);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3ValidateFile #1 -> %Rrc\n", rc);
        return 1;
    }
    u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Validated and checksummed in %'RI64 ns\n", u64Elapsed);

    /*
     * Open it and read.
     */
    u64Start = RTTimeNanoTS();
    PSSMHANDLE pSSM;
    rc = SSMR3Open(pszFilename, NULL /*pStreamOps*/, NULL /*pvStreamOps*/, 0, &pSSM);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Open #1 -> %Rrc\n", rc);
        return 1;
    }
    u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Opened in %'RI64 ns\n", u64Elapsed);

    /* negative */
    u64Start = RTTimeNanoTS();
    rc = SSMR3Seek(pSSM, "some unit that doesn't exist", 0, NULL);
    if (rc != VERR_SSM_UNIT_NOT_FOUND)
    {
        RTPrintf("SSMR3Seek #1 negative -> %Rrc\n", rc);
        return 1;
    }
    u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Failed seek in %'RI64 ns\n", u64Elapsed);

    /* another negative, now only the instance number isn't matching. */
    rc = SSMR3Seek(pSSM, "SSM Testcase Data Item no.2 (rand mem)", 0, NULL);
    if (rc != VERR_SSM_UNIT_NOT_FOUND)
    {
        RTPrintf("SSMR3Seek #1 unit 2 -> %Rrc\n", rc);
        return 1;
    }

    /* 2nd unit */
    rc = SSMR3Seek(pSSM, "SSM Testcase Data Item no.2 (rand mem)", 2, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Seek #1 unit 2 -> %Rrc [2]\n", rc);
        return 1;
    }
    uint32_t uVersion = 0xbadc0ded;
    rc = SSMR3Seek(pSSM, "SSM Testcase Data Item no.2 (rand mem)", 2, &uVersion);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Seek #1 unit 2 -> %Rrc [3]\n", rc);
        return 1;
    }
    u64Start = RTTimeNanoTS();
    rc = Item02Load(NULL, pSSM, uVersion, SSM_PASS_FINAL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item02Load #1 -> %Rrc\n", rc);
        return 1;
    }
    u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Loaded 2nd item in %'RI64 ns\n", u64Elapsed);

    /* 1st unit */
    uVersion = 0xbadc0ded;
    rc = SSMR3Seek(pSSM, "SSM Testcase Data Item no.1 (all types)", 1, &uVersion);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Seek #1 unit 1 -> %Rrc\n", rc);
        return 1;
    }
    u64Start = RTTimeNanoTS();
    rc = Item01Load(NULL, pSSM, uVersion, SSM_PASS_FINAL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item01Load #1 -> %Rrc\n", rc);
        return 1;
    }
    u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Loaded 1st item in %'RI64 ns\n", u64Elapsed);

    /* 3st unit */
    uVersion = 0xbadc0ded;
    rc = SSMR3Seek(pSSM, "SSM Testcase Data Item no.3 (big mem)", 0, &uVersion);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Seek #3 unit 1 -> %Rrc\n", rc);
        return 1;
    }
    u64Start = RTTimeNanoTS();
    rc = Item03Load(NULL, pSSM, uVersion, SSM_PASS_FINAL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Item01Load #3 -> %Rrc\n", rc);
        return 1;
    }
    u64Elapsed = RTTimeNanoTS() - u64Start;
    RTPrintf("tstSSM: Loaded 3rd item in %'RI64 ns\n", u64Elapsed);

    /* close */
    rc = SSMR3Close(pSSM);
    if (RT_FAILURE(rc))
    {
        RTPrintf("SSMR3Close #1 -> %Rrc\n", rc);
        return 1;
    }

    destroyFakeVM(pVM);

    /* delete */
    RTFileDelete(pszFilename);

    RTPrintf("tstSSM: SUCCESS\n");
    return 0;
}

