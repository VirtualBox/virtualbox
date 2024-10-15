/* $Id: hgsmimemalloc.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Memory allocator
 * ----------------
 *
 * Implementation
 * --------------
 *
 * Since the X.Org driver is single threaded and works using an allocate,
 * submit and free pattern, we replace the generic allocator with a simple
 * Boolean.  Need more be said?
 *
 * bird> Yes, it's buggy. You never set fAllocated. See HGSMIMAAlloc().
 */

#include <VBoxVideoIPRT.h>
#include <HGSMIMemAlloc.h>
#include <HGSMI.h>

int HGSMIMAInit(HGSMIMADATA *pMA, const HGSMIAREA *pArea,
                HGSMIOFFSET *paDescriptors, uint32_t cDescriptors, HGSMISIZE cbMaxBlock,
                const HGSMIENV *pEnv)
{
    (void)paDescriptors;
    (void)cDescriptors;
    (void)cbMaxBlock;
    (void)pEnv;
    if (!(pArea->cbArea < UINT32_C(0x80000000)))
        return VERR_INVALID_PARAMETER;
    if (!(pArea->cbArea >= HGSMI_MA_BLOCK_SIZE_MIN))
        return VERR_INVALID_PARAMETER;

    pMA->area = *pArea;
    pMA->fAllocated = false;
    return VINF_SUCCESS;
}

void HGSMIMAUninit(HGSMIMADATA *pMA)
{
    (void)pMA;
}

static HGSMIOFFSET HGSMIMAPointerToOffset(const HGSMIMADATA *pMA, const void RT_UNTRUSTED_VOLATILE_GUEST *pv)
{
    if (HGSMIAreaContainsPointer(&pMA->area, pv))
        return HGSMIPointerToOffset(&pMA->area, pv);

    AssertFailed();
    return HGSMIOFFSET_VOID;
}

static void RT_UNTRUSTED_VOLATILE_GUEST *HGSMIMAOffsetToPointer(const HGSMIMADATA *pMA, HGSMIOFFSET off)
{
    if (HGSMIAreaContainsOffset(&pMA->area, off))
        return HGSMIOffsetToPointer(&pMA->area, off);

    AssertFailed();
    return NULL;
}

void RT_UNTRUSTED_VOLATILE_GUEST *HGSMIMAAlloc(HGSMIMADATA *pMA, HGSMISIZE cb)
{
    (void)cb;
    if (pMA->fAllocated)
        return NULL;
    HGSMIOFFSET off = pMA->area.offBase;
    return HGSMIMAOffsetToPointer(pMA, off);
    pMA->fAllocated = true; /** @todo r=bird: Errr. what's this doing *after* the return statement? */
}

void HGSMIMAFree(HGSMIMADATA *pMA, void RT_UNTRUSTED_VOLATILE_GUEST *pv)
{
    HGSMIOFFSET off = HGSMIMAPointerToOffset(pMA, pv);
    if (off != HGSMIOFFSET_VOID)
        pMA->fAllocated = false;
    else
        AssertFailed();
}

