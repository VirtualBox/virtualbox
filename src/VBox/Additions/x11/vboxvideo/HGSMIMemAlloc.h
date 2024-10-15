/* $Id: HGSMIMemAlloc.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


/* In builds inside of the VirtualBox source tree we override the default
 * HGSMIMemAlloc.h using -include, therefore this define must match the one
 * there. */

#ifndef VBOX_INCLUDED_Graphics_HGSMIMemAlloc_h
#define VBOX_INCLUDED_Graphics_HGSMIMemAlloc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "HGSMIDefs.h"
#include "VBoxVideoIPRT.h"

#define HGSMI_MA_DESC_ORDER_BASE UINT32_C(5)

#define HGSMI_MA_BLOCK_SIZE_MIN (UINT32_C(1) << (HGSMI_MA_DESC_ORDER_BASE + 0))

typedef struct HGSMIMADATA
{
    HGSMIAREA area;
    bool fAllocated;
} HGSMIMADATA;

RT_C_DECLS_BEGIN

int HGSMIMAInit(HGSMIMADATA *pMA, const HGSMIAREA *pArea,
                HGSMIOFFSET *paDescriptors, uint32_t cDescriptors,
                HGSMISIZE cbMaxBlock, const HGSMIENV *pEnv);
void HGSMIMAUninit(HGSMIMADATA *pMA);

void RT_UNTRUSTED_VOLATILE_GUEST *HGSMIMAAlloc(HGSMIMADATA *pMA, HGSMISIZE cb);
void HGSMIMAFree(HGSMIMADATA *pMA, void RT_UNTRUSTED_VOLATILE_GUEST *pv);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_Graphics_HGSMIMemAlloc_h */
