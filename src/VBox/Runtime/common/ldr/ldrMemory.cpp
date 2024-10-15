
/* $Id: ldrMemory.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, The Memory/Debugger Oriented Parts.
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
#define LOG_GROUP RTLOGGROUP_LDR
#include <iprt/ldr.h>
#include "internal/iprt.h"

#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include "internal/ldr.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Memory reader (for debuggers) instance.
 */
typedef struct RTLDRRDRMEM
{
    /** The core. */
    RTLDRREADER         Core;
    /** The size of the image. */
    size_t              cbImage;
    /** The current offset. */
    size_t              offCur;

    /** User parameter for the reader and destructor functions.*/
    void               *pvUser;
    /** Read function. */
    PFNRTLDRRDRMEMREAD  pfnRead;
    /** Destructor callback. */
    PFNRTLDRRDRMEMDTOR  pfnDtor;

    /** Mapping of the file. */
    void               *pvMapping;
    /** Mapping usage counter. */
    uint32_t            cMappings;

    /** The fake filename (variable size). */
    char                szName[1];
} RTLDRRDRMEM;
/** Memory based loader reader instance data. */
typedef RTLDRRDRMEM *PRTLDRRDRMEM;


/**
 * @callback_method_impl{FNRTLDRRDRMEMDTOR,
 *      Default destructor - pvUser points to the image memory block}
 */
static DECLCALLBACK(void) rtldrRdrMemDefaultDtor(void *pvUser, size_t cbImage)
{
    RT_NOREF(cbImage);
    RTMemFree(pvUser);
}


/**
 * @callback_method_impl{FNRTLDRRDRMEMREAD,
 *      Default memory reader - pvUser points to the image memory block}
 */
static DECLCALLBACK(int) rtldrRdrMemDefaultReader(void *pvBuf, size_t cb, size_t off, void *pvUser)
{
    memcpy(pvBuf, (uint8_t *)pvUser + off, cb);
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTLDRREADER,pfnRead} */
static DECLCALLBACK(int) rtldrRdrMem_Read(PRTLDRREADER pReader, void *pvBuf, size_t cb, RTFOFF off)
{
    PRTLDRRDRMEM pThis = (PRTLDRRDRMEM)pReader;

    AssertReturn(off >= 0, VERR_INVALID_PARAMETER);
    if (    cb > pThis->cbImage
        || off > (RTFOFF)pThis->cbImage
        || off + (RTFOFF)cb > (RTFOFF)pThis->cbImage)
    {
        pThis->offCur = pThis->cbImage;
        return VERR_EOF;
    }

    int rc = pThis->pfnRead(pvBuf, cb, (size_t)off, pThis->pvUser);
    if (RT_SUCCESS(rc))
        pThis->offCur = (size_t)off + cb;
    else
        pThis->offCur = ~(size_t)0;
    return rc;
}


/** @interface_method_impl{RTLDRREADER,pfnTell} */
static DECLCALLBACK(RTFOFF) rtldrRdrMem_Tell(PRTLDRREADER pReader)
{
    PRTLDRRDRMEM pThis = (PRTLDRRDRMEM)pReader;
    return pThis->offCur;
}


/** @interface_method_impl{RTLDRREADER,pfnSize} */
static DECLCALLBACK(uint64_t) rtldrRdrMem_Size(PRTLDRREADER pReader)
{
    PRTLDRRDRMEM pThis = (PRTLDRRDRMEM)pReader;
    return pThis->cbImage;
}


/** @interface_method_impl{RTLDRREADER,pfnLogName} */
static DECLCALLBACK(const char *) rtldrRdrMem_LogName(PRTLDRREADER pReader)
{
    PRTLDRRDRMEM pThis = (PRTLDRRDRMEM)pReader;
    return pThis->szName;
}


/** @interface_method_impl{RTLDRREADER,pfnMap} */
static DECLCALLBACK(int) rtldrRdrMem_Map(PRTLDRREADER pReader, const void **ppvBits)
{
    PRTLDRRDRMEM pThis = (PRTLDRRDRMEM)pReader;

    /*
     * Already mapped?
     */
    if (pThis->pvMapping)
    {
        pThis->cMappings++;
        *ppvBits = pThis->pvMapping;
        return VINF_SUCCESS;
    }

    /*
     * Allocate memory.
     */
    pThis->pvMapping = RTMemAlloc(pThis->cbImage);
    if (!pThis->pvMapping)
        return VERR_NO_MEMORY;
    int rc = rtldrRdrMem_Read(pReader, pThis->pvMapping, pThis->cbImage, 0);
    if (RT_SUCCESS(rc))
    {
        pThis->cMappings = 1;
        *ppvBits = pThis->pvMapping;
    }
    else
    {
        RTMemFree(pThis->pvMapping);
        pThis->pvMapping = NULL;
    }

    return rc;
}


/** @interface_method_impl{RTLDRREADER,pfnUnmap} */
static DECLCALLBACK(int) rtldrRdrMem_Unmap(PRTLDRREADER pReader, const void *pvBits)
{
    PRTLDRRDRMEM pThis = (PRTLDRRDRMEM)pReader;
    AssertReturn(pThis->cMappings > 0, VERR_INVALID_PARAMETER);

    if (!--pThis->cMappings)
    {
        RTMemFree(pThis->pvMapping);
        pThis->pvMapping = NULL;
    }

    NOREF(pvBits);
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTLDRREADER,pfnDestroy} */
static DECLCALLBACK(int) rtldrRdrMem_Destroy(PRTLDRREADER pReader)
{
    PRTLDRRDRMEM pThis = (PRTLDRRDRMEM)pReader;
    pThis->pfnDtor(pThis->pvUser, pThis->cbImage);
    pThis->pfnDtor = NULL;
    pThis->pvUser = NULL;
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


/**
 * Opens a memory based loader reader.
 *
 * @returns iprt status code.
 * @param   ppReader        Where to store the reader instance on success.
 * @param   pszName         The name to give the image.
 * @param   cbImage         The image size.
 * @param   pfnRead         The reader function.  If NULL, a default reader is
 *                          used that assumes pvUser points to a memory buffer
 *                          of at least @a cbImage size.
 * @param   pfnDtor         The destructor.  If NULL, a default destructore is
 *                          used that will call RTMemFree on @a pvUser.
 * @param   pvUser          User argument.  If either @a pfnRead or @a pfnDtor
 *                          is NULL, this must be a pointer to readable memory
 *                          (see above).
 */
static int rtldrRdrMem_Create(PRTLDRREADER *ppReader, const char *pszName, size_t cbImage,
                              PFNRTLDRRDRMEMREAD pfnRead, PFNRTLDRRDRMEMDTOR pfnDtor, void *pvUser)
{
#if ARCH_BITS > 32 /* 'ing gcc. */
    AssertReturn(cbImage < RTFOFF_MAX, VERR_INVALID_PARAMETER);
#endif
    AssertReturn((RTFOFF)cbImage > 0, VERR_INVALID_PARAMETER);

    size_t cchName = strlen(pszName);
    int rc = VERR_NO_MEMORY;
    PRTLDRRDRMEM pThis = (PRTLDRRDRMEM)RTMemAlloc(sizeof(*pThis) + cchName);
    if (pThis)
    {
        memcpy(pThis->szName, pszName, cchName + 1);
        pThis->cbImage      = cbImage;
        pThis->pvUser       = pvUser;
        pThis->offCur       = 0;
        pThis->pvUser       = pvUser;
        pThis->pfnRead      = pfnRead ? pfnRead : rtldrRdrMemDefaultReader;
        pThis->pfnDtor      = pfnDtor ? pfnDtor : rtldrRdrMemDefaultDtor;
        pThis->pvMapping    = NULL;
        pThis->cMappings    = 0;
        pThis->Core.uMagic     = RTLDRREADER_MAGIC;
        pThis->Core.pfnRead    = rtldrRdrMem_Read;
        pThis->Core.pfnTell    = rtldrRdrMem_Tell;
        pThis->Core.pfnSize    = rtldrRdrMem_Size;
        pThis->Core.pfnLogName = rtldrRdrMem_LogName;
        pThis->Core.pfnMap     = rtldrRdrMem_Map;
        pThis->Core.pfnUnmap   = rtldrRdrMem_Unmap;
        pThis->Core.pfnDestroy = rtldrRdrMem_Destroy;
        *ppReader = &pThis->Core;
        return VINF_SUCCESS;
    }

    *ppReader = NULL;
    return rc;
}


RTDECL(int) RTLdrOpenInMemory(const char *pszName, uint32_t fFlags, RTLDRARCH enmArch, size_t cbImage,
                              PFNRTLDRRDRMEMREAD pfnRead, PFNRTLDRRDRMEMDTOR pfnDtor, void *pvUser,
                              PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo)
{
    LogFlow(("RTLdrOpenInMemory: pszName=%p:{%s} fFlags=%#x enmArch=%d cbImage=%#zx pfnRead=%p pfnDtor=%p pvUser=%p phLdrMod=%p pErrInfo=%p\n",
             pszName, pszName, fFlags, enmArch, cbImage, pfnRead, pfnDtor, pvUser, phLdrMod, pErrInfo));

    if (!pfnRead || !pfnDtor)
        AssertPtrReturn(pvUser, VERR_INVALID_POINTER);
    if (!pfnDtor)
        pfnDtor = rtldrRdrMemDefaultDtor;
    else
        AssertPtrReturn(pfnDtor, VERR_INVALID_POINTER);

    /* The rest of the validations will call the destructor. */
    AssertMsgReturnStmt(!(fFlags & ~RTLDR_O_VALID_MASK), ("%#x\n", fFlags),
                        pfnDtor(pvUser, cbImage), VERR_INVALID_PARAMETER);
    AssertMsgReturnStmt(enmArch > RTLDRARCH_INVALID && enmArch < RTLDRARCH_END, ("%d\n", enmArch),
                        pfnDtor(pvUser, cbImage), VERR_INVALID_PARAMETER);
    if (!pfnRead)
        pfnRead = rtldrRdrMemDefaultReader;
    else
        AssertReturnStmt(RT_VALID_PTR(pfnRead), pfnDtor(pvUser, cbImage), VERR_INVALID_POINTER);
    AssertReturnStmt(cbImage > 0, pfnDtor(pvUser, cbImage), VERR_INVALID_PARAMETER);

    /*
     * Resolve RTLDRARCH_HOST.
     */
    if (enmArch == RTLDRARCH_HOST)
        enmArch = RTLdrGetHostArch();

    /*
     * Create file reader & invoke worker which identifies and calls the image interpreter.
     */
    PRTLDRREADER pReader = NULL; /* gcc may be wrong */
    int rc = rtldrRdrMem_Create(&pReader, pszName, cbImage, pfnRead, pfnDtor, pvUser);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrOpenWithReader(pReader, fFlags, enmArch, phLdrMod, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            LogFlow(("RTLdrOpen: return %Rrc *phLdrMod=%p\n", rc, *phLdrMod));
            return rc;
        }

        pReader->pfnDestroy(pReader);
    }
    else
    {
        pfnDtor(pvUser, cbImage);
        rc = RTErrInfoSetF(pErrInfo, rc, "rtldrRdrMem_Create failed: %Rrc", rc);
    }
    *phLdrMod = NIL_RTLDRMOD;

    LogFlow(("RTLdrOpen: return %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrOpenInMemory);

