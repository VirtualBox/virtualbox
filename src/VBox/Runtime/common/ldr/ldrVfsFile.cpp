/* $Id: ldrVfsFile.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, The File Oriented Parts, VFS variant.
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
#include <iprt/vfs.h>
#include <iprt/formats/mz.h>
#include "internal/ldr.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * VFS file Reader instance.
 * This provides raw image bits from a file.
 */
typedef struct RTLDRREADERVFSFILE
{
    /** The core. */
    RTLDRREADER     Core;
    /** The VFS file. */
    RTVFSFILE       hVfsFile;
    /** Number of users or the mapping. */
    RTUINT          cMappings;
    /** Pointer to the in memory mapping. */
    void           *pvMapping;
    /** The filename (variable size). */
    char            szFilename[1];
} RTLDRREADERVFSFILE;
typedef RTLDRREADERVFSFILE *PRTLDRREADERVFSFILE;


/** @copydoc RTLDRREADER::pfnRead */
static DECLCALLBACK(int) rtldrVfsFileRead(PRTLDRREADER pReader, void *pvBuf, size_t cb, RTFOFF off)
{
    PRTLDRREADERVFSFILE pFileReader = (PRTLDRREADERVFSFILE)pReader;
    return RTVfsFileReadAt(pFileReader->hVfsFile, off, pvBuf, cb, NULL);
}


/** @copydoc RTLDRREADER::pfnTell */
static DECLCALLBACK(RTFOFF) rtldrVfsFileTell(PRTLDRREADER pReader)
{
    PRTLDRREADERVFSFILE pFileReader = (PRTLDRREADERVFSFILE)pReader;
    return RTVfsFileTell(pFileReader->hVfsFile);
}


/** @copydoc RTLDRREADER::pfnSize */
static DECLCALLBACK(uint64_t) rtldrVfsFileSize(PRTLDRREADER pReader)
{
    PRTLDRREADERVFSFILE pFileReader = (PRTLDRREADERVFSFILE)pReader;
    uint64_t cbFile;
    int rc = RTVfsFileQuerySize(pFileReader->hVfsFile, &cbFile);
    if (RT_SUCCESS(rc))
        return cbFile;
    return 0;
}


/** @copydoc RTLDRREADER::pfnLogName */
static DECLCALLBACK(const char *) rtldrVfsFileLogName(PRTLDRREADER pReader)
{
    PRTLDRREADERVFSFILE pFileReader = (PRTLDRREADERVFSFILE)pReader;
    return pFileReader->szFilename;
}


/** @copydoc RTLDRREADER::pfnMap */
static DECLCALLBACK(int) rtldrVfsFileMap(PRTLDRREADER pReader, const void **ppvBits)
{
    PRTLDRREADERVFSFILE pFileReader = (PRTLDRREADERVFSFILE)pReader;

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
    uint64_t cbFile = rtldrVfsFileSize(pReader);
    size_t cb = (size_t)cbFile;
    if ((uint64_t)cb != cbFile)
        return VERR_IMAGE_TOO_BIG;
    pFileReader->pvMapping = RTMemAlloc(cb);
    if (!pFileReader->pvMapping)
        return VERR_NO_MEMORY;
    int rc = rtldrVfsFileRead(pReader, pFileReader->pvMapping, cb, 0);
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
static DECLCALLBACK(int) rtldrVfsFileUnmap(PRTLDRREADER pReader, const void *pvBits)
{
    PRTLDRREADERVFSFILE pFileReader = (PRTLDRREADERVFSFILE)pReader;
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
static DECLCALLBACK(int) rtldrVfsFileDestroy(PRTLDRREADER pReader)
{
    PRTLDRREADERVFSFILE pFileReader = (PRTLDRREADERVFSFILE)pReader;
    if (pFileReader->hVfsFile != NIL_RTVFSFILE)
    {
        RTVfsFileRelease(pFileReader->hVfsFile);
        pFileReader->hVfsFile = NIL_RTVFSFILE;
    }
    RTMemFree(pFileReader);
    return VINF_SUCCESS;
}


/**
 * Opens a loader file reader.
 *
 * @returns iprt status code.
 * @param   pszFilename     The file to open, can be VFS chain.
 * @param   ppReader        Where to store the reader instance on success.
 * @param   poffError       Where to return the offset into @a pszFilename of an VFS
 *                          chain element causing trouble.  Optional.
 * @param   pErrInfo        Where to return extended error information.  Optional.
 */
static int rtldrVfsFileCreate(const char *pszFilename, PRTLDRREADER *ppReader, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    size_t cbFilename = strlen(pszFilename) + 1;
    int rc = VERR_NO_MEMORY;
    PRTLDRREADERVFSFILE pFileReader = (PRTLDRREADERVFSFILE)RTMemAlloc(RT_UOFFSETOF_DYN(RTLDRREADERVFSFILE, szFilename[cbFilename]));
    if (pFileReader)
    {
        memcpy(pFileReader->szFilename, pszFilename, cbFilename);
        pFileReader->szFilename[0] = '\0';
        rc = RTVfsChainOpenFile(pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &pFileReader->hVfsFile,
                                poffError, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            pFileReader->Core.uMagic     = RTLDRREADER_MAGIC;
            pFileReader->Core.pfnRead    = rtldrVfsFileRead;
            pFileReader->Core.pfnTell    = rtldrVfsFileTell;
            pFileReader->Core.pfnSize    = rtldrVfsFileSize;
            pFileReader->Core.pfnLogName = rtldrVfsFileLogName;
            pFileReader->Core.pfnMap     = rtldrVfsFileMap;
            pFileReader->Core.pfnUnmap   = rtldrVfsFileUnmap;
            pFileReader->Core.pfnDestroy = rtldrVfsFileDestroy;
            pFileReader->cMappings = 0;
            pFileReader->pvMapping = NULL;
            *ppReader = &pFileReader->Core;
            return VINF_SUCCESS;
        }
        RTMemFree(pFileReader);
    }
    *ppReader = NULL;
    return rc;
}


/**
 * Open a binary image file allowing VFS chains in the filename.
 *
 * @returns iprt status code.
 * @param   pszFilename Image filename, VFS chain specifiers allowed.
 * @param   fFlags      Valid RTLDR_O_XXX combination.
 * @param   enmArch     CPU architecture specifier for the image to be loaded.
 * @param   phLdrMod    Where to store the handle to the loader module.
 * @param   poffError   Where to return the offset into @a pszFilename of an VFS
 *                      chain element causing trouble.  Optional.
 * @param   pErrInfo    Where to return extended error information.  Optional.
 */
RTDECL(int) RTLdrOpenVfsChain(const char *pszFilename, uint32_t fFlags, RTLDRARCH enmArch,
                              PRTLDRMOD phLdrMod, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    LogFlow(("RTLdrOpenVfsChain: pszFilename=%p:{%s} fFlags=%#x enmArch=%d phLdrMod=%p\n",
             pszFilename, pszFilename, fFlags, enmArch, phLdrMod));
    AssertMsgReturn(!(fFlags & ~RTLDR_O_VALID_MASK), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    AssertMsgReturn(enmArch > RTLDRARCH_INVALID && enmArch < RTLDRARCH_END, ("%d\n", enmArch), VERR_INVALID_PARAMETER);

    /*
     * Create file reader & invoke worker which identifies and calls the image interpreter.
     */
    PRTLDRREADER pReader;
    int rc = rtldrVfsFileCreate(pszFilename, &pReader, poffError, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        if (poffError)
            *poffError = 0;
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
RT_EXPORT_SYMBOL(RTLdrOpenVfsChain);


/**
 * Open a binary image file using kLdr allowing VFS chains in the filename.
 *
 * @returns iprt status code.
 * @param   pszFilename Image filename.
 * @param   fFlags      Reserved, MBZ.
 * @param   enmArch     CPU architecture specifier for the image to be loaded.
 * @param   phLdrMod    Where to store the handle to the loaded module.
 * @param   poffError   Where to return the offset into @a pszFilename of an VFS
 *                      chain element causing trouble.  Optional.
 * @param   pErrInfo    Where to return extended error information.  Optional.
 * @remark  Primarily for testing the loader.
 */
RTDECL(int) RTLdrOpenVfsChainkLdr(const char *pszFilename, uint32_t fFlags, RTLDRARCH enmArch,
                                  PRTLDRMOD phLdrMod, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    return RTLdrOpenVfsChain(pszFilename, fFlags, enmArch, phLdrMod, poffError, pErrInfo);
}
RT_EXPORT_SYMBOL(RTLdrOpenVfsChainkLdr);

