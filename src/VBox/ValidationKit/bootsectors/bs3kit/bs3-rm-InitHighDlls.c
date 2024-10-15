/* $Id: bs3-rm-InitHighDlls.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Initialize any high DLLs, real mode.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
//#define BS3_USE_RM_TEXT_SEG 1
#include "bs3kit-template-header.h"
#include "bs3kit-linker.h"
#include "bs3-cmn-memory.h"


extern BS3HIGHDLLENTRY BS3_FAR_DATA BS3_DATA_NM(g_aBs3HighDllTable)[];
extern BS3HIGHDLLENTRY BS3_FAR_DATA BS3_DATA_NM(g_Bs3HighDllTable_End);



/**
 * Load the DLL. This is where we ASSUME real-mode, pre-PIC setup,
 * and interrupts enabled as we need to use int13 for the actual
 * reading. We switch to 32-bit protected mode and copies the
 * chunk from pbBuf and to the actual load address.
 *
 * Note! When reading we must make sure to not switch head as the
 *       BIOS code messes up the result if it does the wraparound.
 */
static void bs3InitHighDllLoadImage(BS3HIGHDLLENTRY RT_FAR * const pHighDllEntry, const char RT_FAR * const pszFilename,
                                    uint8_t BS3_FAR * const pbBuf, uint8_t const cBufSectors, uint32_t const uFlatBuf,
                                    uint8_t const bDrive, uint8_t const uMaxHead, uint8_t const uMaxSector,
                                    uint16_t const cSectorsPerCylinder)
{
    int             rc;
#ifdef BS3_WITH_LOAD_CHECKSUMS
# if 0 /* for debugging */
    uint32_t        iCurSector          = 0;
# endif
    uint32_t        uChecksum           = BS3_CALC_CHECKSUM_INITIAL_VALUE;
#endif
    uint32_t        cSectorsLeftToLoad  = pHighDllEntry->cbInImage  / 512U;
    uint32_t        uCurFlatLoadAddr    = pHighDllEntry->uLoadAddr;
    /* Calculate the current CHS position: */
    uint32_t const  uCurSectorInImage   = pHighDllEntry->offInImage / 512U;
    uint16_t        uCylinder           = uCurSectorInImage / cSectorsPerCylinder;
    uint16_t const  uRemainder          = uCurSectorInImage % cSectorsPerCylinder;
    uint8_t         uHead               = uRemainder / uMaxSector;
    uint8_t         uSector             = (uRemainder % uMaxSector) + 1;
    while (cSectorsLeftToLoad > 0)
    {
        /* Figure out how much we dare read.  Only up to the end of the track. */
        uint8_t cSectors = uMaxSector + 1 - uSector;
        if (cSectors > cBufSectors)
            cSectors = cBufSectors;
        if (cSectorsLeftToLoad < cSectors)
            cSectors = (uint8_t)(cSectorsLeftToLoad);

        //Bs3TestPrintf("Calling Bs3DiskRead(%#x,%#x,%#x,%#x,%#x,%p) [uCurFlatLoadAddr=%RX32]\n",
        //              bDrive, uCylinder, uHead, uSector, cSectors, pbBuf, uCurFlatLoadAddr);
        rc = Bs3DiskRead_rm(bDrive, uCylinder, uHead, uSector, cSectors, pbBuf);
        if (rc != 0)
        {
            Bs3TestPrintf("Bs3DiskRead(%#x,%#x,%#x,%#x,%#x,) failed: %#x\n",
                          bDrive, uCylinder, uHead, uSector, cBufSectors, rc);
            Bs3Shutdown();
        }

#ifdef BS3_WITH_LOAD_CHECKSUMS
        /* Checksum what we just loaded. */
# if 0 /* For debugging. */
        {
            uint16_t j;
            uint32_t uChecksumTmp = uChecksum;
            for (j = 0; j < cSectors; j++, iCurSector++)
                Bs3TestPrintf("sector #%RU32:  %#RX32 %#010RX32\n", iCurSector,
                              uChecksumTmp = Bs3CalcChecksum(uChecksumTmp, &pbBuf[j * 512U], 512U),
                              Bs3CalcChecksum(BS3_CALC_CHECKSUM_INITIAL_VALUE, &pbBuf[j * 512U], 512U));
            uChecksum = Bs3CalcChecksum(uChecksum, pbBuf, 512U * cSectors);
            if (uChecksum != uChecksumTmp)
                Bs3TestPrintf("Checksum error: %#RX32, expected %#RX32!\n", uChecksum, uChecksumTmp);
        }
# else
        uChecksum = Bs3CalcChecksum(uChecksum, pbBuf, 512U * cSectors);
# endif
#endif

        /* Copy the page to where the DLL is being loaded.  */
        Bs3MemCopyFlat_rm_far(uCurFlatLoadAddr, uFlatBuf, 512U * cSectors);
        Bs3PrintChr('.');

        /* Advance */
        uCurFlatLoadAddr   += cSectors * 512U;
        cSectorsLeftToLoad -= cSectors;
        uSector            += cSectors;
        if (!uSector || uSector > uMaxSector)
        {
            uSector        = 1;
            uHead++;
            if (uHead > uMaxHead)
            {
                uHead      = 0;
                uCylinder++;
            }
        }
    }

#ifdef BS3_WITH_LOAD_CHECKSUMS
    /* Verify the checksum. */
    if (uChecksum != pHighDllEntry->uChecksum)
    {
        Bs3TestPrintf("Checksum mismatch for '%s': %#RX32 vs %#RX32\n", pszFilename, uChecksum, pHighDllEntry->uChecksum);
        Bs3Shutdown();
    }
#endif
}


/**
 * Initializes any special (16-bit) segments for the given high DLL.
 */
static void bs3InitHighDllSetUpSegments(BS3HIGHDLLENTRY RT_FAR *pHighDllEntry, const char RT_FAR *pszFilename)
{
    PBS3HIGHDLLSEGMENT const    paSegments = (PBS3HIGHDLLSEGMENT)((char RT_FAR *)pHighDllEntry + pHighDllEntry->offSegments);
    unsigned const              cSegments  = pHighDllEntry->cSegments;
    unsigned                    iSeg;

    for (iSeg = 0; iSeg < cSegments; iSeg++)
    {
        Bs3Printf("Segment #%u: %#010RX32 LB %#010RX32 idxSel=%#06x fFlags=%#x\n",
                  iSeg, paSegments[iSeg].uAddr, paSegments[iSeg].cb, paSegments[iSeg].idxSel, paSegments[iSeg].fFlags);
        if (paSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_16BIT)
        {
            X86DESC BS3_FAR *pDesc = &Bs3Gdt[paSegments[iSeg].idxSel >> X86_SEL_SHIFT];

            if (paSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_EXEC)
            {
                BS3_ASSERT((unsigned)(paSegments[iSeg].idxSel - BS3_SEL_HIGH16_CS_FIRST) < (unsigned)BS3_SEL_HIGH16_CS_COUNT);
                Bs3SelSetup16BitCode(pDesc, paSegments[iSeg].uAddr, 0);
                if (paSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_CONFORMING)
                    pDesc->Gen.u4Type = X86_SEL_TYPE_ER_CONF_ACC;
                //Bs3TestPrintf("Segment #%u: 16-bit code %p %#x\n", iSeg, pDesc, paSegments[iSeg].idxSel);
            }
            else
            {
                BS3_ASSERT((unsigned)(paSegments[iSeg].idxSel - BS3_SEL_HIGH16_DS_FIRST) < (unsigned)BS3_SEL_HIGH16_DS_COUNT);
                Bs3SelSetup16BitData(pDesc, paSegments[iSeg].uAddr);
                //Bs3TestPrintf("Segment #%u: 16-bit data %p %#x\n", iSeg, pDesc, paSegments[iSeg].idxSel);
            }
            if (paSegments[iSeg].cb < _64K)
                pDesc->Gen.u16LimitLow = paSegments[iSeg].cb - 1;
        }
        //else
        //    Bs3TestPrintf("Segment #%u: Not 16-bit\n", iSeg);
    }

}


/**
 * Allocates/reserves the memory backing for the given DLL.
 */
static void bs3InitHighDllAllocateMemory(BS3HIGHDLLENTRY RT_FAR *pHighDllEntry, const char RT_FAR *pszFilename)
{
    uint16_t const cPagesToLoad = (uint16_t)(pHighDllEntry->cbLoaded / _4K);
    uint16_t       iPage        = Bs3SlabAllocFixed(&g_Bs3Mem4KUpperTiled.Core, pHighDllEntry->uLoadAddr, cPagesToLoad);
    if (iPage == 0 || iPage == UINT16_MAX)
    {
        Bs3TestPrintf("Bs3SlabAllocFixed(,%#RX32, %#RX16) failed: %#RX16 (%s)\n",
                      pHighDllEntry->uLoadAddr, cPagesToLoad, iPage, pszFilename);
        Bs3Shutdown();
    }
    /** @todo We don't have any memory management above 16MB... at the moment. */
}


BS3_DECL_FAR(void) Bs3InitHighDlls_rm_far(void)
{
    unsigned const cHighDlls = (unsigned)(&g_Bs3HighDllTable_End - &g_aBs3HighDllTable[0]);
    //Bs3TestPrintf("cHighDlls=%d g_uBs3CpuDetected=%#x Bs3InitHighDlls_rm_far=%p\n", cHighDlls, g_uBs3CpuDetected, &Bs3InitHighDlls_rm_far);

    if (   cHighDlls > 0
        && (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386)
    {
        /*
         * Get the drive geometry. ASSUMES the bootsector hasn't been trashed yet!
         */
        uint8_t const bDrive       = ((BS3BOOTSECTOR RT_FAR *)BS3_FP_MAKE(0,0x7c00))->bBootDrv;
        uint16_t      uMaxCylinder = 0;
        uint8_t       uMaxHead     = 0;
        uint8_t       uMaxSector   = 0;
        int rc = Bs3DiskQueryGeometry_rm(bDrive, &uMaxCylinder, &uMaxHead, &uMaxSector);
        //Bs3TestPrintf("Bs3DiskQueryGeometry(%#x)-> C=%#x H=%#x S=%#x\n", bDrive, uMaxCylinder, uMaxHead, uMaxSector);
        if (rc == 0)
        {
            uint16_t const cSectorsPerCylinder = uMaxSector * ((uint16_t)uMaxHead + 1);
            unsigned i;

            /*
             * We need a buffer first of all.  Try for one able to contain a
             * full track, as that'll give us the best reading speed.
             */
            uint32_t         uFlatBuf;
            uint8_t          cBufSectors;
            uint8_t BS3_FAR *pbBuf;
            uint16_t         cbBuf          = uMaxSector >= 72U ? 72U * 512U : uMaxSector * 512U;
            void BS3_FAR    *pvBufAllocated = Bs3MemAlloc(BS3MEMKIND_REAL, cbBuf);
            if (!pvBufAllocated)
            {
                cbBuf = cbBuf >= _32K ? _32K : 1 << ASMBitLastSetU16(cbBuf) /* no - 1!*/; /* converts to a power of two. */
                for (;;)
                {
                    pvBufAllocated = Bs3MemAlloc(BS3MEMKIND_REAL, cbBuf);
                    if (pvBufAllocated)
                        break;
                    if (cbBuf <= _4K)
                    {
                        Bs3TestPrintf("Failed to allocate 4 KiB memory buffer for loading high DLL(s)!\n");
                        Bs3Shutdown();
                    }
                    cbBuf >>= 1;
                }
            }
            cBufSectors = (uint8_t)(cbBuf / 512U);
            uFlatBuf    = BS3_FP_REAL_TO_FLAT(pvBufAllocated);
            pbBuf = (uint8_t BS3_FAR *)BS3_FP_MAKE(uFlatBuf >> 4, 0); /* make sure the whole buffer is within a segment.  */
            /** @todo Is the above pbBuf hack necessary? */
            //Bs3TestPrintf("pvBufAllocated=%p pbBuf=%p uFlatBuf=%RX32 cbBuf=%#x\n", pvBufAllocated, pbBuf, uFlatBuf, cbBuf);

            /*
             * Iterate the high DLL table and load it all into memory.
             */
            for (i = 0; i < cHighDlls; i++)
            {
                const char RT_FAR * const pszzStrings = (char RT_FAR *)&g_aBs3HighDllTable[i] + g_aBs3HighDllTable[i].offStrings;
                const char RT_FAR * const pszFilename = &pszzStrings[g_aBs3HighDllTable[i].offFilename];
                Bs3Printf("Loading dll '%s' at %#RX32..%#RX32\n", pszFilename, g_aBs3HighDllTable[i].uLoadAddr,
                          g_aBs3HighDllTable[i].uLoadAddr + g_aBs3HighDllTable[i].cbLoaded - 1);

                /*
                 * Allocate the memory taken by the DLL.
                 */
                bs3InitHighDllAllocateMemory(&g_aBs3HighDllTable[i], pszFilename);

                /*
                 * Process the segment table.
                 */
                bs3InitHighDllSetUpSegments(&g_aBs3HighDllTable[i], pszFilename);

                /*
                 * Load it.
                 */
                bs3InitHighDllLoadImage(&g_aBs3HighDllTable[i], pszFilename, pbBuf, cBufSectors, uFlatBuf,
                                        bDrive, uMaxHead, uMaxSector, cSectorsPerCylinder);
            }

            Bs3Printf("\n");
            Bs3MemFree(pvBufAllocated, cbBuf);
        }
        else
        {
            Bs3TestPrintf("Bs3DiskQueryGeometry(%#x) failed: %#x\n", bDrive, rc);
            Bs3Shutdown();
        }
    }
}

