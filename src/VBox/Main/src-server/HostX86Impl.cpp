/* $Id: HostX86Impl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - x86 host specific IHost methods / attributes.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_HOSTX86
#include "LoggingNew.h"

#include "HostX86Impl.h"

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/mp.h>


/*
 * HostX86 implementation.
 */
DEFINE_EMPTY_CTOR_DTOR(HostX86)

HRESULT HostX86::FinalConstruct()
{
    return BaseFinalConstruct();
}

void HostX86::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

HRESULT HostX86::init(void)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

void HostX86::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
}

/**
 * Returns the specific CPUID leaf.
 *
 * @returns COM status code
 * @param   aCpuId              The CPU number. Mostly ignored.
 * @param   aLeaf               The leaf number.
 * @param   aSubLeaf            The sub-leaf number.
 * @param   aValEAX             Where to return EAX.
 * @param   aValEBX             Where to return EBX.
 * @param   aValECX             Where to return ECX.
 * @param   aValEDX             Where to return EDX.
 */
HRESULT HostX86::getProcessorCPUIDLeaf(ULONG aCpuId, ULONG aLeaf, ULONG aSubLeaf,
                                       ULONG *aValEAX, ULONG *aValEBX, ULONG *aValECX, ULONG *aValEDX)
{
    // no locking required

    /* Check that the CPU is online. */
    /** @todo later use RTMpOnSpecific. */
    if (!RTMpIsCpuOnline(aCpuId))
        return RTMpIsCpuPresent(aCpuId)
             ? setError(E_FAIL, tr("CPU no.%u is not present"), aCpuId)
             : setError(E_FAIL, tr("CPU no.%u is not online"), aCpuId);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    uint32_t uEAX, uEBX, uECX, uEDX;
    ASMCpuId_Idx_ECX(aLeaf, aSubLeaf, &uEAX, &uEBX, &uECX, &uEDX);
    *aValEAX = uEAX;
    *aValEBX = uEBX;
    *aValECX = uECX;
    *aValEDX = uEDX;
#else
    RT_NOREF(aLeaf, aSubLeaf);
    *aValEAX = 0;
    *aValEBX = 0;
    *aValECX = 0;
    *aValEDX = 0;
#endif

    return S_OK;
}

