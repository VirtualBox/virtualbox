/* $Id: FlashCore.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevFlash - A simple Flash device
 *
 * A simple non-volatile byte-wide (x8) memory device modeled after Intel 28F008
 * FlashFile. See 28F008SA datasheet, Intel order number 290429-007.
 *
 * Implemented as an MMIO device attached directly to the CPU, not behind any
 * bus. Typically mapped as part of the firmware image.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_FLASH
#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/file.h>

#include "VBoxDD.h"
#include "FlashCore.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name CUI (Command User Interface) Commands.
 *  @{ */
#define FLASH_CMD_ALT_WRITE             0x10
#define FLASH_CMD_ERASE_SETUP           0x20
#define FLASH_CMD_WRITE                 0x40
#define FLASH_CMD_STS_CLEAR             0x50
#define FLASH_CMD_STS_READ              0x70
#define FLASH_CMD_READ_ID               0x90
#define FLASH_CMD_ERASE_SUS_RES         0xB0
#define FLASH_CMD_ERASE_CONFIRM         0xD0
#define FLASH_CMD_ARRAY_READ            0xFF
/** @} */

/** @name Status register bits.
 *  @{ */
#define FLASH_STATUS_WSMS               0x80    /* Write State Machine Status, 1=Ready */
#define FLASH_STATUS_ESS                0x40    /* Erase Suspend Status, 1=Suspended */
#define FLASH_STATUS_ES                 0x20    /* Erase Status, 1=Error */
#define FLASH_STATUS_BWS                0x10    /* Byte Write Status, 1=Error */
#define FLASH_STATUS_VPPS               0x08    /* Vpp Status, 1=Low Vpp */
/* The remaining bits 0-2 are reserved/unused */
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#ifndef VBOX_DEVICE_STRUCT_TESTCASE



/**
 * Worker for flashWrite that deals with a single byte.
 *
 * @retval  VINF_SUCCESS on success, which is always the case in ring-3.
 * @retval  VINF_IOM_R3_MMIO_WRITE can be returned when not in ring-3.
 */
static int flashMemWriteByte(PFLASHCORE pThis, uint32_t off, uint8_t bCmd)
{
    /* NB: Older datasheets (e.g. 28F008SA) suggest that for two-cycle commands like byte write or
     * erase setup, the address is significant in both cycles, but do not explain what happens
     * should the addresses not match. Newer datasheets (e.g. 28F008B3) clearly say that the address
     * in the first byte cycle never matters. We prefer the latter interpretation.
     */

    if (pThis->cBusCycle == 0)
    {
        /* First bus write cycle, start processing a new command. Address is ignored. */
        switch (bCmd)
        {
            case FLASH_CMD_ARRAY_READ:
            case FLASH_CMD_STS_READ:
            case FLASH_CMD_ERASE_SUS_RES:
            case FLASH_CMD_READ_ID:
                /* Single-cycle write commands, only change the current command. */
                pThis->bCmd = bCmd;
                break;
            case FLASH_CMD_STS_CLEAR:
                /* Status clear continues in read mode. */
                pThis->bStatus = 0;
                pThis->bCmd = FLASH_CMD_ARRAY_READ;
                break;
            case FLASH_CMD_WRITE:
            case FLASH_CMD_ALT_WRITE:
            case FLASH_CMD_ERASE_SETUP:
                /* Two-cycle commands, advance the bus write cycle. */
                pThis->bCmd = bCmd;
                pThis->cBusCycle++;
                break;
            default:
                LogFunc(("1st cycle command %02X, current cmd %02X\n", bCmd, pThis->bCmd));
                break;
        }
    }
    else
    {
        /* Second write of a two-cycle command. */
        Assert(pThis->cBusCycle == 1);
        switch (pThis->bCmd)
        {
            case FLASH_CMD_WRITE:
            case FLASH_CMD_ALT_WRITE:
                if (off < pThis->cbFlashSize)
                {
#ifdef IN_RING3
                    pThis->pbFlash[off] = bCmd;
# ifdef FLASH_WITH_RZ_READ_CACHE_SIZE
                    uint32_t const offInCache = off - pThis->offCache;
                    if (offInCache < sizeof(pThis->CacheData) && pThis->offCache != UINT32_MAX)
                        pThis->CacheData.ab[offInCache] = bCmd;
# endif

                    /* NB: Writes are instant and never fail. */
                    LogFunc(("wrote byte to flash at %08RX32: %02X\n", off, bCmd));
#else
                    return VINF_IOM_R3_MMIO_WRITE;
#endif
                }
                else
                    LogFunc(("ignoring write at %08RX32: %02X\n", off, bCmd));
                break;
            case FLASH_CMD_ERASE_SETUP:
                if (bCmd == FLASH_CMD_ERASE_CONFIRM)
                {
#ifdef IN_RING3
                    /* The current address determines the block to erase. */
                    unsigned uOffset = off & ~(pThis->cbBlockSize - 1);
                    memset(pThis->pbFlash + uOffset, 0xff, pThis->cbBlockSize);
                    LogFunc(("Erasing block at offset %u\n", uOffset));
#else
                    return VINF_IOM_R3_MMIO_WRITE;
#endif
                }
                else
                {
                    /* Anything else is a command erorr. Transition to status read mode. */
                    LogFunc(("2st cycle erase command is %02X, should be confirm (%02X)\n", bCmd, FLASH_CMD_ERASE_CONFIRM));
                    pThis->bCmd = FLASH_CMD_STS_READ;
                    pThis->bStatus |= FLASH_STATUS_BWS | FLASH_STATUS_ES;
                }
                break;
            default:
                LogFunc(("2st cycle bad command %02X, current cmd %02X\n", bCmd, pThis->bCmd));
                break;
        }
        pThis->cBusCycle = 0;
    }
    LogFlow(("flashMemWriteByte: write access at %08RX32: %#x\n", off, bCmd));
    return VINF_SUCCESS;
}

/**
 * Performs a write to the given flash offset.
 *
 * Parent device calls this from its MMIO write callback.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS on success, which is always the case in ring-3.
 * @retval  VINF_IOM_R3_MMIO_WRITE can be returned when not in ring-3.
 *
 * @param   pThis               The UART core instance.
 * @param   off                 Offset to start writing to.
 * @param   pv                  The value to write.
 * @param   cb                  Number of bytes to write.
 */
DECLHIDDEN(VBOXSTRICTRC) flashWrite(PFLASHCORE pThis, uint32_t off, const void *pv, size_t cb)
{
    const uint8_t *pbSrc = (const uint8_t *)pv;

#ifndef IN_RING3
    /*
     * If multiple bytes are written, just go to ring-3 and do it there as it's
     * too much trouble to validate the sequence in adanvce and it is usually
     * not restartable as device state changes.
     */
    VBOXSTRICTRC rcStrict;
    if (cb == 1)
    {
        rcStrict = flashMemWriteByte(pThis, off, *pbSrc);
        if (rcStrict == VINF_SUCCESS)
            LogFlow(("flashWrite: completed write at %08RX32 (LB %u)\n", off, cb));
        else
            LogFlow(("flashWrite: incomplete write at %08RX32 (LB %u): rc=%Rrc bCmd=%#x cBusCycle=%u\n",
                     off, cb, VBOXSTRICTRC_VAL(rcStrict), *pbSrc, pThis->cBusCycle));
    }
    else
    {
        LogFlow(("flashWrite: deferring multi-byte write at %08RX32 (LB %u) to ring-3\n", off, cb));
        rcStrict = VINF_IOM_R3_IOPORT_WRITE;
    }
    return rcStrict;

#else  /* IN_RING3 */

    for (uint32_t offWrite = 0; offWrite < cb; ++offWrite)
        flashMemWriteByte(pThis, off + offWrite, pbSrc[offWrite]);

    LogFlow(("flashWrite: completed write at %08RX32 (LB %u)\n", off, cb));
    return VINF_SUCCESS;
#endif /* IN_RING3 */
}

#if defined(FLASH_WITH_RZ_READ_CACHE_SIZE) && defined(IN_RING3)
/**
 * Fills the RZ cache with data.
 */
DECL_FORCE_INLINE(void) flashFillRzCache(PFLASHCORE pThis, uint32_t off)
{
    AssertCompile(RT_IS_POWER_OF_TWO(sizeof(pThis->CacheData)));
    uint32_t const offCache = (off + 1) & ~(sizeof(pThis->CacheData) - 1);
    if (offCache < pThis->cbFlashSize)
    {
        Log2(("flashMemReadByte: Filling RZ cache: offset %#x\n", offCache));
# if FLASH_WITH_RZ_READ_CACHE_SIZE < 8
        uint64_t const * const pu64Src = ((uint64_t const *)&pThis->pbFlash[offCache]);
        pThis->CacheData.au64[0]  = pu64Src[0];
#  if FLASH_WITH_RZ_READ_CACHE_SIZE > 1
        pThis->CacheData.au64[1]  = pu64Src[1];
#  endif
#  if FLASH_WITH_RZ_READ_CACHE_SIZE > 2
        pThis->CacheData.au64[2]  = pu64Src[2];
#  endif
#  if FLASH_WITH_RZ_READ_CACHE_SIZE > 3
        pThis->CacheData.au64[3]  = pu64Src[3];
#  endif
#  if FLASH_WITH_RZ_READ_CACHE_SIZE > 4
        pThis->CacheData.au64[4]  = pu64Src[4];
#  endif
#  if FLASH_WITH_RZ_READ_CACHE_SIZE > 5
        pThis->CacheData.au64[5]  = pu64Src[5];
#  endif
#  if FLASH_WITH_RZ_READ_CACHE_SIZE > 6
        pThis->CacheData.au64[6]  = pu64Src[6];
#  endif
#  if FLASH_WITH_RZ_READ_CACHE_SIZE > 7
        pThis->CacheData.au64[7]  = pu64Src[7];
#  endif
#  if FLASH_WITH_RZ_READ_CACHE_SIZE > 8
        pThis->CacheData.au64[8]  = pu64Src[8];
#  endif
# else
        memcpy(pThis->CacheData.ab, &pThis->pbFlash[offCache], sizeof(pThis->CacheData.ab));
# endif
        pThis->offCache           = offCache;
    }
}
#endif /* FLASH_WITH_RZ_READ_CACHE_SIZE && IN_RING3 */

/**
 * Worker for flashRead that deals with a single byte.
 *
 * @retval  VINF_SUCCESS on success, which is always the case in ring-3.
 * @retval  VINF_IOM_R3_MMIO_READ can be returned when not in ring-3.
 */
static int flashMemReadByte(PFLASHCORE pThis, uint32_t off, uint8_t *pbData)
{
    uint8_t bValue;

    /*
     * Reads are only defined in three states: Array read, status register read,
     * and ID read.
     */
    switch (pThis->bCmd)
    {
        case FLASH_CMD_ARRAY_READ:
            if (off < pThis->cbFlashSize)
            {
#ifdef IN_RING3
# ifdef FLASH_WITH_RZ_READ_CACHE_SIZE
                AssertCompile(RT_IS_POWER_OF_TWO(sizeof(pThis->CacheData)));
                if (off + 1 - pThis->offCache < sizeof(pThis->CacheData) && pThis->offCache != UINT32_MAX)
                { }
                else
                    flashFillRzCache(pThis, off);
# endif
                bValue = pThis->pbFlash[off];
#else
# ifdef FLASH_WITH_RZ_READ_CACHE_SIZE
                uint32_t const offInCache = off - pThis->offCache;
                if (offInCache < sizeof(pThis->CacheData) && pThis->offCache != UINT32_MAX)
                {
                    Log2(("flashMemReadByte: cache hit (at %#RX32 in cache)\n", offInCache));
                    bValue = pThis->CacheData.ab[offInCache];
                }
                else
                {
                    Log2(("flashMemReadByte: cache miss: offInCache=%#RX32 offCache=%#RX32\n", offInCache, pThis->offCache));
                    return VINF_IOM_R3_MMIO_READ;
                }
# else
                return VINF_IOM_R3_MMIO_READ;
# endif
#endif
            }
            else
                bValue = 0xff; /* Play safe and return the default value of non initialized flash. */
            LogFunc(("read byte at %08RX32: %02X\n", off, bValue));
            break;
        case FLASH_CMD_STS_READ:
            bValue = pThis->bStatus;
            break;
        case FLASH_CMD_READ_ID:
            bValue = off & 1 ?  RT_HI_U8(pThis->u16FlashId) : RT_LO_U8(pThis->u16FlashId);
            break;
        default:
            bValue = 0xff;
            break;
    }
    *pbData = bValue;

    LogFlow(("flashMemReadByte: read access at %08RX32: %02X (cmd=%02X)\n", off, bValue, pThis->bCmd));
    return VINF_SUCCESS;
}

/**
 * Performs a read from the given flash offset.
 *
 * Parent device calls this from its MMIO read callback.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS on success, which is always the case in ring-3.
 * @retval  VINF_IOM_R3_MMIO_READ can be returned when not in ring-3.
 *
 * @param   pThis               The UART core instance.
 * @param   off                 Offset to start reading from.
 * @param   pv                  Where to store the read data.
 * @param   cb                  Number of bytes to read.
 */
DECLHIDDEN(VBOXSTRICTRC) flashRead(PFLASHCORE pThis, uint32_t off, void *pv, size_t cb)
{
    uint8_t *pbDst = (uint8_t *)pv;

    /*
     * Reads do not change the device state, so we don't need to take any
     * precautions when we're not in ring-3 as the read can always be restarted.
     */
    for (uint32_t offRead = 0; offRead < cb; ++offRead)
    {
#ifdef IN_RING3
        flashMemReadByte(pThis, off + offRead, &pbDst[offRead]);
#else
        VBOXSTRICTRC rcStrict = flashMemReadByte(pThis, off + offRead, &pbDst[offRead]);
        if (rcStrict != VINF_SUCCESS)
        {
            LogFlow(("flashRead: incomplete read at %08RX32+%#x (LB %u): rc=%Rrc bCmd=%#x\n",
                     off, offRead, cb, VBOXSTRICTRC_VAL(rcStrict), pThis->bCmd));
            return rcStrict;
        }
#endif
    }

    LogFlow(("flashRead: completed read at %08RX32 (LB %u)\n", off, cb));
    return VINF_SUCCESS;
}

#ifdef IN_RING3

/**
 * Initialiizes the given flash device instance.
 *
 * @returns VBox status code.
 * @param   pThis               The flash device core instance.
 * @param   pDevIns             Pointer to the owning device instance.
 * @param   idFlashDev          The flash device ID.
 * @param   GCPhysFlashBase     Base MMIO address where the flash is located.
 * @param   cbFlash             Size of the flash device in bytes.
 * @param   cbBlock             Size of a flash block.
 */
DECLHIDDEN(int) flashR3Init(PFLASHCORE pThis, PPDMDEVINS pDevIns, uint16_t idFlashDev, uint32_t cbFlash, uint16_t cbBlock)
{
    pThis->u16FlashId  = idFlashDev;
    pThis->cbBlockSize = cbBlock;
    pThis->cbFlashSize = cbFlash;
#ifdef FLASH_WITH_RZ_READ_CACHE_SIZE
    pThis->offCache    = UINT32_MAX;
#endif

    /* Set up the flash data. */
    pThis->pbFlash = (uint8_t *)PDMDevHlpMMHeapAlloc(pDevIns, pThis->cbFlashSize);
    if (!pThis->pbFlash)
        return PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY, N_("Failed to allocate heap memory"));

    /* Default value for empty flash. */
    memset(pThis->pbFlash, 0xff, pThis->cbFlashSize);

    /* Reset the dynamic state.*/
    flashR3Reset(pThis);
    return VINF_SUCCESS;
}

/**
 * Destroys the given flash device instance.
 *
 * @param   pDevIns             The parent device instance.
 * @param   pThis               The flash device core instance.
 */
DECLHIDDEN(void) flashR3Destruct(PFLASHCORE pThis, PPDMDEVINS pDevIns)
{
    if (pThis->pbFlash)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pbFlash);
        pThis->pbFlash = NULL;
    }
}

/**
 * Loads the flash content from the given file.
 *
 * @returns VBox status code.
 * @param   pThis               The flash device core instance.
 * @param   pDevIns             The parent device instance.
 * @param   pszFilename         The file to load the flash content from.
 */
DECLHIDDEN(int) flashR3LoadFromFile(PFLASHCORE pThis, PPDMDEVINS pDevIns, const char *pszFilename)
{
    RTFILE hFlashFile = NIL_RTFILE;

    int rc = RTFileOpen(&hFlashFile, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to open flash file"));

    size_t cbRead = 0;
    rc = RTFileRead(hFlashFile, pThis->pbFlash, pThis->cbFlashSize, &cbRead);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to read flash file"));
    Log(("Read %zu bytes from file (asked for %u)\n.", cbRead, pThis->cbFlashSize));

    RTFileClose(hFlashFile);
    return VINF_SUCCESS;
}

/**
 * Loads the flash content from the given buffer.
 *
 * @returns VBox status code.
 * @param   pThis               The flash device core instance.
 * @param   pvBuf               The buffer to load the content from.
 * @param   cbBuf               Size of the buffer in bytes.
 */
DECLHIDDEN(int) flashR3LoadFromBuf(PFLASHCORE pThis, void const *pvBuf, size_t cbBuf)
{
    AssertReturn(pThis->cbFlashSize >= cbBuf, VERR_BUFFER_OVERFLOW);

    memcpy(pThis->pbFlash, pvBuf, RT_MIN(cbBuf, pThis->cbFlashSize));
    return VINF_SUCCESS;
}

/**
 * Loads the flash content using the PDM VFS interface.
 *
 * @returns VBox status code.
 * @param   pThis               The flash device core instance.
 * @param   pDevIns             The owning device instance.
 * @param   pDrvVfs             Pointer to the VFS interface.
 * @param   pszNamespace        The namespace to load from.
 * @param   pszPath             The path to the flash content to load.
 */
DECLHIDDEN(int) flashR3LoadFromVfs(PFLASHCORE pThis, PPDMDEVINS pDevIns, PPDMIVFSCONNECTOR pDrvVfs,
                                   const char *pszNamespace, const char *pszPath)
{
    uint64_t cbFlash = 0;
    int rc = pDrvVfs->pfnQuerySize(pDrvVfs, pszNamespace, pszPath, &cbFlash);
    if (RT_SUCCESS(rc))
    {
        if (cbFlash <= pThis->cbFlashSize)
            rc = pDrvVfs->pfnReadAll(pDrvVfs, pszNamespace, pszPath, pThis->pbFlash, pThis->cbFlashSize);
        else
            return PDMDEV_SET_ERROR(pDevIns, VERR_BUFFER_OVERFLOW, N_("Configured flash size is too small to fit the saved NVRAM content"));
    }

    return rc;
}

/**
 * Saves the flash content to the given file.
 *
 * @returns VBox status code.
 * @param   pThis               The flash device core instance.
 * @param   pDevIns             The parent device instance.
 * @param   pszFilename         The file to save the flash content to.
 */
DECLHIDDEN(int) flashR3SaveToFile(PFLASHCORE pThis, PPDMDEVINS pDevIns, const char *pszFilename)
{
    RTFILE hFlashFile = NIL_RTFILE;

    int rc = RTFileOpen(&hFlashFile, pszFilename, RTFILE_O_READWRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to open flash file"));

    rc = RTFileWrite(hFlashFile, pThis->pbFlash, pThis->cbFlashSize, NULL);
    RTFileClose(hFlashFile);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to write flash file"));

    return VINF_SUCCESS;
}

/**
 * Saves the flash content to the given buffer.
 *
 * @returns VBox status code.
 * @param   pThis               The flash device core instance.
 * @param   pvBuf               The buffer to save the content to.
 * @param   cbBuf               Size of the buffer in bytes.
 */
DECLHIDDEN(int) flashR3SaveToBuf(PFLASHCORE pThis, void *pvBuf, size_t cbBuf)
{
    AssertReturn(pThis->cbFlashSize <= cbBuf, VERR_BUFFER_OVERFLOW);

    memcpy(pvBuf, pThis->pbFlash, RT_MIN(cbBuf, pThis->cbFlashSize));
    return VINF_SUCCESS;
}

/**
 * Saves the flash content using the given PDM VFS interface.
 *
 * @returns VBox status code.
 * @param   pThis               The flash device core instance.
 * @param   pDevIns             The owning device instance.
 * @param   pDrvVfs             Pointer to the VFS interface.
 * @param   pszNamespace        The namespace to store to.
 * @param   pszPath             The path to store the flash content under.
 */
DECLHIDDEN(int) flashR3SaveToVfs(PFLASHCORE pThis, PPDMDEVINS pDevIns, PPDMIVFSCONNECTOR pDrvVfs,
                                 const char *pszNamespace, const char *pszPath)
{
    RT_NOREF(pDevIns);
    return pDrvVfs->pfnWriteAll(pDrvVfs, pszNamespace, pszPath, pThis->pbFlash, pThis->cbFlashSize);
}

/**
 * Resets the dynamic part of the flash device state.
 *
 * @param   pThis               The flash device core instance.
 */
DECLHIDDEN(void) flashR3Reset(PFLASHCORE pThis)
{
    /*
     * Initialize the device state.
     */
    pThis->bCmd      = FLASH_CMD_ARRAY_READ;
    pThis->bStatus   = 0;
    pThis->cBusCycle = 0;
}

/**
 * Saves the flash device state to the given SSM handle.
 *
 * @returns VBox status code.
 * @param   pThis               The flash device core instance.
 * @param   pDevIns             The parent device instance.
 * @param   pSSM                The SSM handle to save to.
 */
DECLHIDDEN(int) flashR3SaveExec(PFLASHCORE pThis, PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    pHlp->pfnSSMPutU32(pSSM, FLASH_SAVED_STATE_VERSION);

    /* Save the device state. */
    pHlp->pfnSSMPutU8(pSSM, pThis->bCmd);
    pHlp->pfnSSMPutU8(pSSM, pThis->bStatus);
    pHlp->pfnSSMPutU8(pSSM, pThis->cBusCycle);

    /* Save the current configuration for validation purposes. */
    pHlp->pfnSSMPutU16(pSSM, pThis->cbBlockSize);
    pHlp->pfnSSMPutU16(pSSM, pThis->u16FlashId);

    /* Save the current flash contents. */
    pHlp->pfnSSMPutU32(pSSM, pThis->cbFlashSize);
    return pHlp->pfnSSMPutMem(pSSM, pThis->pbFlash, pThis->cbFlashSize);
}

/**
 * Loads the flash device state from the given SSM handle.
 *
 * @returns VBox status code.
 * @param   pThis               The flash device core instance.
 * @param   pDevIns             The parent device instance.
 * @param   pSSM                The SSM handle to load from.
 */
DECLHIDDEN(int) flashR3LoadExec(PFLASHCORE pThis, PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    uint32_t uVersion = FLASH_SAVED_STATE_VERSION;
    int rc = pHlp->pfnSSMGetU32(pSSM, &uVersion);
    AssertRCReturn(rc, rc);

    /*
     * Do the actual restoring.
     */
    if (uVersion == FLASH_SAVED_STATE_VERSION)
    {
        uint16_t    u16Val;
        uint32_t    u32Val;

        pHlp->pfnSSMGetU8(pSSM, &pThis->bCmd);
        pHlp->pfnSSMGetU8(pSSM, &pThis->bStatus);
        pHlp->pfnSSMGetU8(pSSM, &pThis->cBusCycle);

        /* Make sure configuration didn't change behind our back. */
        rc = pHlp->pfnSSMGetU16(pSSM, &u16Val);
        AssertRCReturn(rc, rc);
        if (u16Val != pThis->cbBlockSize)
            return VERR_SSM_LOAD_CONFIG_MISMATCH;
        rc = pHlp->pfnSSMGetU16(pSSM, &u16Val);
        AssertRCReturn(rc, rc);
        if (u16Val != pThis->u16FlashId)
            return VERR_SSM_LOAD_CONFIG_MISMATCH;
        rc = pHlp->pfnSSMGetU32(pSSM, &u32Val);
        AssertRCReturn(rc, rc);
        if (u32Val != pThis->cbFlashSize)
            return VERR_SSM_LOAD_CONFIG_MISMATCH;

        /* Suck in the flash contents. */
        rc = pHlp->pfnSSMGetMem(pSSM, pThis->pbFlash, pThis->cbFlashSize);
    }
    else
        rc = VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    return rc;
}

#endif /* IN_RING3 */

#endif /* VBOX_DEVICE_STRUCT_TESTCASE */
