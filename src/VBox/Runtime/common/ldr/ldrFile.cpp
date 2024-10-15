/* $Id: ldrFile.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, The File Oriented Parts.
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
#include <iprt/file.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/formats/mz.h>
#include "internal/ldr.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * File Reader instance.
 * This provides raw image bits from a file.
 */
typedef struct RTLDRREADERFILE
{
    /** The core. */
    RTLDRREADER     Core;
    /** The file. */
    RTFILE          hFile;
    /** The file size. */
    uint64_t        cbFile;
    /** The current offset. */
    RTFOFF          off;
    /** Number of users or the mapping. */
    RTUINT          cMappings;
    /** Pointer to the in memory mapping. */
    void           *pvMapping;
    /** The filename (variable size). */
    char            szFilename[1];
} RTLDRREADERFILE, *PRTLDRREADERFILE;


/** @copydoc RTLDRREADER::pfnRead */
static DECLCALLBACK(int) rtldrFileRead(PRTLDRREADER pReader, void *pvBuf, size_t cb, RTFOFF off)
{
    PRTLDRREADERFILE pFileReader = (PRTLDRREADERFILE)pReader;

    /*
     * Seek.
     */
    if (pFileReader->off != off)
    {
        int rc = RTFileSeek(pFileReader->hFile, off, RTFILE_SEEK_BEGIN, NULL);
        if (RT_FAILURE(rc))
        {
            pFileReader->off = -1;
            return rc;
        }
        pFileReader->off = off;
    }

    /*
     * Read.
     */
    int rc = RTFileRead(pFileReader->hFile, pvBuf, cb, NULL);
    if (RT_SUCCESS(rc))
        pFileReader->off += cb;
    else
        pFileReader->off = -1;
    return rc;
}


/** @copydoc RTLDRREADER::pfnTell */
static DECLCALLBACK(RTFOFF) rtldrFileTell(PRTLDRREADER pReader)
{
    PRTLDRREADERFILE pFileReader = (PRTLDRREADERFILE)pReader;
    return pFileReader->off;
}


/** @copydoc RTLDRREADER::pfnSize */
static DECLCALLBACK(uint64_t) rtldrFileSize(PRTLDRREADER pReader)
{
    PRTLDRREADERFILE pFileReader = (PRTLDRREADERFILE)pReader;
    return pFileReader->cbFile;
}


/** @copydoc RTLDRREADER::pfnLogName */
static DECLCALLBACK(const char *) rtldrFileLogName(PRTLDRREADER pReader)
{
    PRTLDRREADERFILE pFileReader = (PRTLDRREADERFILE)pReader;
    return pFileReader->szFilename;
}


/** @copydoc RTLDRREADER::pfnMap */
static DECLCALLBACK(int) rtldrFileMap(PRTLDRREADER pReader, const void **ppvBits)
{
    PRTLDRREADERFILE pFileReader = (PRTLDRREADERFILE)pReader;

    /*
     * Already mapped?
     */
    if (pFileReader->pvMapping)
    {
        pFileReader->cMappings++;
        *ppvBits = pFileReader->pvMapping;
        return VINF_SUCCESS;
    }

    /*
     * Allocate memory.
     */
    size_t cb = (size_t)pFileReader->cbFile;
    if ((uint64_t)cb != pFileReader->cbFile)
        return VERR_IMAGE_TOO_BIG;
    pFileReader->pvMapping = RTMemAlloc(cb);
    if (!pFileReader->pvMapping)
        return VERR_NO_MEMORY;
    int rc = rtldrFileRead(pReader, pFileReader->pvMapping, cb, 0);
    if (RT_SUCCESS(rc))
    {
        pFileReader->cMappings = 1;
        *ppvBits = pFileReader->pvMapping;
    }
    else
    {
        RTMemFree(pFileReader->pvMapping);
        pFileReader->pvMapping = NULL;
    }

    return rc;
}


/** @copydoc RTLDRREADER::pfnUnmap */
static DECLCALLBACK(int) rtldrFileUnmap(PRTLDRREADER pReader, const void *pvBits)
{
    PRTLDRREADERFILE pFileReader = (PRTLDRREADERFILE)pReader;
    AssertReturn(pFileReader->cMappings > 0, VERR_INVALID_PARAMETER);

    if (!--pFileReader->cMappings)
    {
        RTMemFree(pFileReader->pvMapping);
        pFileReader->pvMapping = NULL;
    }

    NOREF(pvBits);
    return VINF_SUCCESS;
}


/** @copydoc RTLDRREADER::pfnDestroy */
static DECLCALLBACK(int) rtldrFileDestroy(PRTLDRREADER pReader)
{
    int rc = VINF_SUCCESS;
    PRTLDRREADERFILE pFileReader = (PRTLDRREADERFILE)pReader;

    Assert(!pFileReader->cMappings);

    if (pFileReader->hFile != NIL_RTFILE)
    {
        rc = RTFileClose(pFileReader->hFile);
        AssertRC(rc);
        pFileReader->hFile = NIL_RTFILE;
    }

    if (pFileReader->pvMapping)
    {
        RTMemFree(pFileReader->pvMapping);
        pFileReader->pvMapping = NULL;
    }

    RTMemFree(pFileReader);
    return rc;
}


/**
 * Opens a loader file reader.
 *
 * @returns iprt status code.
 * @param   ppReader        Where to store the reader instance on success.
 * @param   pszFilename     The file to open.
 */
static int rtldrFileCreate(PRTLDRREADER *ppReader, const char *pszFilename)
{
    size_t cchFilename = strlen(pszFilename);
    int rc = VERR_NO_MEMORY;
    PRTLDRREADERFILE pFileReader = (PRTLDRREADERFILE)RTMemAlloc(sizeof(*pFileReader) + cchFilename);
    if (pFileReader)
    {
        memcpy(pFileReader->szFilename, pszFilename, cchFilename + 1);
        rc = RTFileOpen(&pFileReader->hFile, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileQuerySize(pFileReader->hFile, &pFileReader->cbFile);
            if (RT_SUCCESS(rc))
            {
                pFileReader->Core.uMagic     = RTLDRREADER_MAGIC;
                pFileReader->Core.pfnRead    = rtldrFileRead;
                pFileReader->Core.pfnTell    = rtldrFileTell;
                pFileReader->Core.pfnSize    = rtldrFileSize;
                pFileReader->Core.pfnLogName = rtldrFileLogName;
                pFileReader->Core.pfnMap     = rtldrFileMap;
                pFileReader->Core.pfnUnmap   = rtldrFileUnmap;
                pFileReader->Core.pfnDestroy = rtldrFileDestroy;
                pFileReader->off       = 0;
                pFileReader->cMappings = 0;
                pFileReader->pvMapping = NULL;
                *ppReader = &pFileReader->Core;
                return VINF_SUCCESS;
            }

            RTFileClose(pFileReader->hFile);
        }
        RTMemFree(pFileReader);
    }
    *ppReader = NULL;
    return rc;
}


/**
 * Open a binary image file.
 *
 * @returns iprt status code.
 * @param   pszFilename Image filename.
 * @param   fFlags      Valid RTLDR_O_XXX combination.
 * @param   enmArch     CPU architecture specifier for the image to be loaded.
 * @param   phLdrMod    Where to store the handle to the loader module.
 */
RTDECL(int) RTLdrOpen(const char *pszFilename, uint32_t fFlags, RTLDRARCH enmArch, PRTLDRMOD phLdrMod)
{
    return RTLdrOpenEx(pszFilename, fFlags, enmArch, phLdrMod, NULL /*pErrInfo*/);
}
RT_EXPORT_SYMBOL(RTLdrOpen);


/**
 * Open a binary image file, extended version.
 *
 * @returns iprt status code.
 * @param   pszFilename Image filename.
 * @param   fFlags      Valid RTLDR_O_XXX combination.
 * @param   enmArch     CPU architecture specifier for the image to be loaded.
 * @param   phLdrMod    Where to store the handle to the loader module.
 * @param   pErrInfo    Where to return extended error information. Optional.
 */
RTDECL(int) RTLdrOpenEx(const char *pszFilename, uint32_t fFlags, RTLDRARCH enmArch, PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo)
{
    LogFlow(("RTLdrOpenEx: pszFilename=%p:{%s} fFlags=%#x enmArch=%d phLdrMod=%p\n",
             pszFilename, pszFilename, fFlags, enmArch, phLdrMod));
    AssertMsgReturn(!(fFlags & ~RTLDR_O_VALID_MASK), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    AssertMsgReturn(enmArch > RTLDRARCH_INVALID && enmArch < RTLDRARCH_END, ("%d\n", enmArch), VERR_INVALID_PARAMETER);

    /*
     * Create file reader & invoke worker which identifies and calls the image interpreter.
     */
    PRTLDRREADER pReader;
    int rc = rtldrFileCreate(&pReader, pszFilename);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrOpenWithReader(pReader, fFlags, enmArch, phLdrMod, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            LogFlow(("RTLdrOpenEx: return %Rrc *phLdrMod=%p\n", rc, *phLdrMod));
            return rc;
        }
        pReader->pfnDestroy(pReader);
    }
    *phLdrMod = NIL_RTLDRMOD;
    LogFlow(("RTLdrOpenEx: return %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrOpenEx);

