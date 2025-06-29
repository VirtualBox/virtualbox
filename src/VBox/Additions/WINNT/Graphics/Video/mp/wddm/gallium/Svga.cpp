/* $Id: Svga.cpp 110039 2025-06-29 17:07:07Z vitali.pelenjow@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - VMSVGA.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#define GALOG_GROUP GALOG_GROUP_SVGA

#include "Svga.h"
#include "SvgaFifo.h"
#include "SvgaHw.h"
#include "SvgaCmd.h"

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/string.h>


static NTSTATUS SvgaObjectTablesDestroy(VBOXWDDM_EXT_VMSVGA *pSvga)
{
    NTSTATUS Status = STATUS_SUCCESS;

    for (uint32_t i = 0; i < RT_ELEMENTS(pSvga->aOT); ++i)
    {
        void *pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_SET_OTABLE_BASE64, sizeof(SVGA3dCmdSetOTableBase64), SVGA3D_INVALID_ID);
        AssertBreakStmt(pvCmd, Status = STATUS_INSUFFICIENT_RESOURCES);

        SVGA3dCmdSetOTableBase64 *pCmd = (SVGA3dCmdSetOTableBase64 *)pvCmd;
        pCmd->type             = (SVGAOTableType)i;
        pCmd->baseAddress      = 0;
        pCmd->sizeInBytes      = 0;
        pCmd->validSizeInBytes = 0;
        pCmd->ptDepth          = SVGA3D_MOBFMT_INVALID;

        SvgaCmdBufCommit(pSvga, sizeof(SVGA3dCmdSetOTableBase64));
    }

    SvgaCmdBufFlush(pSvga);

    for (uint32_t i = 0; i < RT_ELEMENTS(pSvga->aOT); ++i)
    {
        SvgaGboFree(&pSvga->aOT[i].gbo);

        RTR0MemObjFree(pSvga->aOT[i].hMemObj, true);
        pSvga->aOT[i].hMemObj = NIL_RTR0MEMOBJ;
    }

    return Status;
}


struct VMSVGAOTFREE
{
    VMSVGAGBO  gbo;
    RTR0MEMOBJ hMemObj;
};


static DECLCALLBACK(void) svgaOTFreeCb(VBOXWDDM_EXT_VMSVGA *pSvga, void *pvData, uint32_t cbData)
{
    RT_NOREF(pSvga);
    AssertReturnVoid(cbData == sizeof(struct VMSVGAOTFREE));
    struct VMSVGAOTFREE *p = (struct VMSVGAOTFREE *)pvData;
    SvgaGboFree(&p->gbo);
    RTR0MemObjFree(p->hMemObj, true);
}


typedef struct VMSVGAOTINFO
{
    uint32_t cbEntry;
    uint32_t cMaxEntries;
} VMSVGAOTINFO, *PVMSVGAOTINFO;


static VMSVGAOTINFO const s_aOTInfo[SVGA_OTABLE_DX_MAX] =
{
    { sizeof(SVGAOTableMobEntry),          SVGA3D_MAX_MOBS },               /* SVGA_OTABLE_MOB */
    { sizeof(SVGAOTableSurfaceEntry),      SVGA3D_MAX_SURFACE_IDS },        /* SVGA_OTABLE_SURFACE */
    { sizeof(SVGAOTableContextEntry),      SVGA3D_MAX_CONTEXT_IDS },        /* SVGA_OTABLE_CONTEXT */
    { sizeof(SVGAOTableShaderEntry),       0 /* not used */ },              /* SVGA_OTABLE_SHADER */
    { sizeof(SVGAOTableScreenTargetEntry), 64 /*VBOX_VIDEO_MAX_SCREENS*/ }, /* SVGA_OTABLE_SCREENTARGET */
    { sizeof(SVGAOTableDXContextEntry),    SVGA3D_MAX_CONTEXT_IDS },        /* SVGA_OTABLE_DXCONTEXT */
};


static NTSTATUS svgaObjectTablesNotify(VBOXWDDM_EXT_VMSVGA *pSvga, SVGAOTableType enmType, uint32_t id)
{
    AssertCompile(RT_ELEMENTS(pSvga->aOT) == RT_ELEMENTS(s_aOTInfo));
    AssertReturn(enmType < RT_ELEMENTS(pSvga->aOT), STATUS_INVALID_PARAMETER);

    if (!RT_BOOL(pSvga->u32Caps & SVGA_CAP_GBOBJECTS))
        return STATUS_SUCCESS; /* No otables for such host device. */

    PVMSVGAOT pOT = &pSvga->aOT[enmType];
    if (id < pOT->cEntries)
        return STATUS_SUCCESS; /* Still large enough. */

    VMSVGAOTINFO const *pOTInfo = &s_aOTInfo[enmType];
    AssertReturn(id < pOTInfo->cMaxEntries, STATUS_INVALID_PARAMETER);

    /*
     * Allocate a new larger mob and inform the host.
     */
    uint32_t cbRequired = (id + 1) * pOTInfo->cbEntry;
    cbRequired = RT_ALIGN_32(cbRequired, PAGE_SIZE);

    /* Try to double the current size. */
    uint32_t cbOT = pOT->cEntries ? pOT->cEntries * pOTInfo->cbEntry : PAGE_SIZE;
    while (cbRequired > cbOT)
        cbOT *= 2;

    /* Allocate pages for the new COTable. */
    RTR0MEMOBJ hMemObjOT;
    int rc = RTR0MemObjAllocPageTag(&hMemObjOT, cbOT, false /* executable R0 mapping */, "VMSVGAOT");
    AssertRCReturn(rc, STATUS_INSUFFICIENT_RESOURCES);

    memset(RTR0MemObjAddress(hMemObjOT), 0, cbOT);

    /* Allocate a new gbo. */
    VMSVGAGBO gbo;
    NTSTATUS Status = SvgaGboInit(&gbo, cbOT >> PAGE_SHIFT);
    AssertReturnStmt(NT_SUCCESS(Status),
                     RTR0MemObjFree(hMemObjOT, true),
                     Status);

    Status = SvgaGboFillPageTableForMemObj(&gbo, hMemObjOT);
    AssertReturnStmt(NT_SUCCESS(Status),
                     SvgaGboFree(&gbo); RTR0MemObjFree(hMemObjOT, true),
                     Status);

    if (pOT->cEntries == 0)
    {
        /* Set the pages for OTable. */
        void *pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_SET_OTABLE_BASE64, sizeof(SVGA3dCmdSetOTableBase64), SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SVGA3dCmdSetOTableBase64 *pCmd = (SVGA3dCmdSetOTableBase64 *)pvCmd;
            pCmd->type             = enmType;
            pCmd->baseAddress      = gbo.base;
            pCmd->sizeInBytes      = gbo.cbGbo;
            pCmd->validSizeInBytes = 0;
            pCmd->ptDepth          = gbo.enmMobFormat;

            SvgaCmdBufCommit(pSvga, sizeof(*pCmd));
        }
        else
            AssertFailedReturnStmt(SvgaGboFree(&gbo); RTR0MemObjFree(hMemObjOT, true),
                                   STATUS_INSUFFICIENT_RESOURCES);
    }
    else
    {
        /* Grow OTable and delete the old mob. */
        void *pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_GROW_OTABLE, sizeof(SVGA3dCmdGrowOTable), SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SVGA3dCmdGrowOTable *pCmd = (SVGA3dCmdGrowOTable *)pvCmd;
            pCmd->type             = enmType;
            pCmd->baseAddress      = gbo.base;
            pCmd->sizeInBytes      = gbo.cbGbo;
            pCmd->validSizeInBytes = pOT->cEntries * pOTInfo->cbEntry;
            pCmd->ptDepth          = gbo.enmMobFormat;

            /* Command buffer completion callback to free the old OT. */
            struct VMSVGAOTFREE callbackData;
            callbackData.gbo = pOT->gbo;
            callbackData.hMemObj = pOT->hMemObj;
            SvgaCmdBufCommitWithCompletionCallback(pSvga, sizeof(*pCmd),
                svgaOTFreeCb, &callbackData, sizeof(callbackData));
        }
        else
            AssertFailedReturnStmt(SvgaGboFree(&gbo); RTR0MemObjFree(hMemObjOT, true),
                                   STATUS_INSUFFICIENT_RESOURCES);

        memset(&pOT->gbo, 0, sizeof(pOT->gbo));
        pOT->hMemObj = NIL_RTR0MEMOBJ;
    }

    SvgaCmdBufFlush(pSvga);

    pOT->gbo = gbo;
    pOT->hMemObj = hMemObjOT;
    pOT->cEntries = cbOT / pOTInfo->cbEntry;

    return STATUS_SUCCESS;
}

static NTSTATUS svgaCBContextEnable(VBOXWDDM_EXT_VMSVGA *pSvga, SVGACBContext CBContext, bool fEnable)
{
    struct {
        uint32 id;
        SVGADCCmdStartStop body;
    } cmd;
    cmd.id = SVGA_DC_CMD_START_STOP_CONTEXT;
    cmd.body.enable = fEnable;
    cmd.body.context = CBContext;
    NTSTATUS Status = SvgaCmdBufDeviceCommand(pSvga, &cmd, sizeof(cmd));
    AssertReturn(NT_SUCCESS(Status), Status);
    return STATUS_SUCCESS;
}

static NTSTATUS svgaCreateMiniportMob(VBOXWDDM_EXT_VMSVGA *pSvga)
{
    NTSTATUS Status;

    uint32_t const cbMiniportMob = RT_ALIGN_32(sizeof(VMSVGAMINIPORTMOB), PAGE_SIZE);
    int rc = RTR0MemObjAllocPageTag(&pSvga->hMemObjMiniportMob, cbMiniportMob,
                                    false /* executable R0 mapping */, "VMSVGAMOB0");
    if (RT_SUCCESS(rc))
    {
        pSvga->pMiniportMobData = (VMSVGAMINIPORTMOB volatile *)RTR0MemObjAddress(pSvga->hMemObjMiniportMob);
        memset((void *)pSvga->pMiniportMobData, 0, cbMiniportMob);
        Status = STATUS_SUCCESS;
    }
    else
        AssertFailedStmt(Status = STATUS_INSUFFICIENT_RESOURCES);

    return Status;
}

static void svgaHwStop(VBOXWDDM_EXT_VMSVGA *pSvga)
{
    /* Undo svgaHwStart. */

    /* Send commands to host. */
    if (pSvga->u32Caps & SVGA_CAP_GBOBJECTS)
        SvgaObjectTablesDestroy(pSvga);

    /* Wait for buffers to complete. Up to 5 seconds, arbitrary. */
    int cIntervals = 0;
    while (!SvgaCmdBufIsIdle(pSvga) && cIntervals++ < 50)
    {
        /* Give the host some time to process them. */
        LARGE_INTEGER Interval;
        Interval.QuadPart = -(int64_t)100 /* ms */ * 10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    if (pSvga->u32Caps & SVGA_CAP_COMMAND_BUFFERS)
        SvgaCmdBufDestroy(pSvga);

    /* Disable IRQs. */
    SVGARegWrite(pSvga, SVGA_REG_IRQMASK, 0);

    if (pSvga->pCBState)
        svgaCBContextEnable(pSvga, SVGA_CB_CONTEXT_0, false);

    /* Disable SVGA. */
    SVGARegWrite(pSvga, SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
}

static NTSTATUS svgaHwStart(VBOXWDDM_EXT_VMSVGA *pSvga)
{
    pSvga->u32Caps      = SVGARegRead(pSvga, SVGA_REG_CAPABILITIES);
    pSvga->u32VramSize  = SVGARegRead(pSvga, SVGA_REG_VRAM_SIZE);
    pSvga->u32FifoSize  = SVGARegRead(pSvga, SVGA_REG_MEM_SIZE);
    pSvga->u32MaxWidth  = SVGARegRead(pSvga, SVGA_REG_MAX_WIDTH);
    pSvga->u32MaxHeight = SVGARegRead(pSvga, SVGA_REG_MAX_HEIGHT);

    if (pSvga->u32Caps & SVGA_CAP_GMR2)
    {
        pSvga->u32GmrMaxIds   = SVGARegRead(pSvga, SVGA_REG_GMR_MAX_IDS);
        pSvga->u32GmrMaxPages = SVGARegRead(pSvga, SVGA_REG_GMRS_MAX_PAGES);
        pSvga->u32MemorySize  = SVGARegRead(pSvga, SVGA_REG_MEMORY_SIZE);
        pSvga->u32MemorySize -= pSvga->u32VramSize;
    }
    else
    {
        /*
         * An arbitrary limit of 512MiB on surface
         * memory. But all HWV8 hardware supports GMR2.
         */
        /** @todo not supported */
        pSvga->u32MemorySize = 512*1024*1024;
    }

    pSvga->u32MaxTextureWidth = 8192;
    pSvga->u32MaxTextureHeight = 8192;

    /* 1 + floor(log2(max(u32MaxTextureWidth, u32MaxTextureHeight))):
     * In Direct3D the next mipmap level size is floor(prev_size / 2), for example 5 -> 2 -> 1
     * Therefore we only need to know the position of the highest non-zero bit. And since
     * ASMBitLastSetU32 returns a 1 based index, there is no need to add 1.
     */
    pSvga->u32MaxTextureLevels = ASMBitLastSetU32(RT_MAX(pSvga->u32MaxTextureWidth, pSvga->u32MaxTextureHeight));

    NTSTATUS Status = SvgaFifoInit(pSvga);
    AssertReturn(NT_SUCCESS(Status), Status);

    if (pSvga->u32Caps & SVGA_CAP_COMMAND_BUFFERS)
    {
        Status = SvgaCmdBufInit(pSvga);
        AssertReturn(NT_SUCCESS(Status), Status);
    }

    /* Enable SVGA device. */
    SVGARegWrite(pSvga, SVGA_REG_ENABLE, SVGA_REG_ENABLE_ENABLE);

    if (pSvga->pCBState)
    {
        svgaCBContextEnable(pSvga, SVGA_CB_CONTEXT_0, true);
        AssertReturn(NT_SUCCESS(Status), Status);
    }

    uint32_t u32IRQMask = SVGA_IRQFLAG_ANY_FENCE;
    if (pSvga->pCBState)
        u32IRQMask |= SVGA_IRQFLAG_COMMAND_BUFFER;
    SVGARegWrite(pSvga, SVGA_REG_IRQMASK, u32IRQMask);

    return STATUS_SUCCESS;
}

void SvgaAdapterStop(PVBOXWDDM_EXT_VMSVGA pSvga,
                     DXGKRNL_INTERFACE *pDxgkInterface)
{
    if (pSvga)
    {
        NTSTATUS Status = SvgaHostObjectsCleanup(pSvga);
        Assert(Status == STATUS_SUCCESS); RT_NOREF(Status);

        if (pSvga->pu32GMRBits)
        {
            if (pSvga->GMRTree != NULL)
            {
                /* Normally it is expected that all GMRs are freed already. */
                AssertFailed();

                /* Free GMRs. */
                SvgaRegionsDestroy(pSvga, NULL);

            }
            GaMemFree(pSvga->pu32GMRBits);
            pSvga->pu32GMRBits = NULL;
            pSvga->cbGMRBits = 0;
        }

        if (RT_BOOL(pSvga->u32Caps & SVGA_CAP_DX))
        {
            /* Free the miniport mob at last. Can't use SvgaMobDestroy here because it tells the host to write a fence
             * value to this mob. */
            void *pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_DESTROY_GB_MOB, sizeof(SVGA3dCmdDestroyGBMob), SVGA3D_INVALID_ID);
            if (pvCmd)
            {
                SVGA3dCmdDestroyGBMob *pCmd = (SVGA3dCmdDestroyGBMob *)pvCmd;
                pCmd->mobid = VMSVGAMOB_ID(pSvga->pMiniportMob);
                SvgaCmdBufCommit(pSvga, sizeof(*pCmd));
            }
            else
                AssertFailed();
        }

        svgaHwStop(pSvga);

        if (RT_BOOL(pSvga->u32Caps & SVGA_CAP_DX))
            SvgaMobFree(pSvga, pSvga->pMiniportMob); /* After svgaHwStop because it waits for command buffer completion. */

        Status = pDxgkInterface->DxgkCbUnmapMemory(pDxgkInterface->DeviceHandle,
                                                   (PVOID)pSvga->pu32FIFO);
        Assert(Status == STATUS_SUCCESS); RT_NOREF(Status);

        GaMemFree(pSvga);
    }
}

NTSTATUS SvgaAdapterStart(PVBOXWDDM_EXT_VMSVGA *ppSvga,
                          DXGKRNL_INTERFACE *pDxgkInterface,
                          PHYSICAL_ADDRESS physFIFO,
                          ULONG cbFIFO,
                          PHYSICAL_ADDRESS physIO,
                          ULONG cbIO)
{
    RT_NOREF(cbIO);

    NTSTATUS Status;
// ASMBreakpoint();

    VBOXWDDM_EXT_VMSVGA *pSvga = (VBOXWDDM_EXT_VMSVGA *)GaMemAllocZero(sizeof(VBOXWDDM_EXT_VMSVGA));
    if (!pSvga)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* The spinlock is required for hardware access. Init it as the very first. */
    KeInitializeSpinLock(&pSvga->HwSpinLock);
    KeInitializeSpinLock(&pSvga->HostObjectsSpinLock);
    KeInitializeSpinLock(&pSvga->IdSpinLock);
    ExInitializeFastMutex(&pSvga->SvgaMutex);
    KeInitializeSpinLock(&pSvga->MobSpinLock);
    // pSvga->GMRTree = NULL;
    // pSvga->SurfaceTree = NULL;
    // pSvga->MobTree = NULL;
    RTListInit(&pSvga->DeletedHostObjectsList);

    /* The port IO address is also needed for hardware access. */
    pSvga->ioportBase = (RTIOPORT)physIO.QuadPart;

    /* FIFO pointer is also needed for hardware access. */
    Status = pDxgkInterface->DxgkCbMapMemory(pDxgkInterface->DeviceHandle,
                 physFIFO,
                 cbFIFO,
                 FALSE, /* IN BOOLEAN InIoSpace */
                 FALSE, /* IN BOOLEAN MapToUserMode */
                 MmNonCached, /* IN MEMORY_CACHING_TYPE CacheType */
                 (PVOID *)&pSvga->pu32FIFO /*OUT PVOID *VirtualAddress*/);

    if (NT_SUCCESS(Status))
    {
        SVGARegWrite(pSvga, SVGA_REG_ID, SVGA_ID_2);
        const uint32_t u32SvgaId = SVGARegRead(pSvga, SVGA_REG_ID);
        if (u32SvgaId == SVGA_ID_2)
        {
            Status = svgaHwStart(pSvga);
            if (NT_SUCCESS(Status))
            {
                /*
                 * Check hardware capabilities.
                 */
                if (pSvga->u32GmrMaxIds > 0)
                {
                    pSvga->cbGMRBits = ((pSvga->u32GmrMaxIds + 31) / 32) * 4; /* 32bit align and 4 bytes per 32 bit. */
                    pSvga->pu32GMRBits = (uint32_t *)GaMemAllocZero(pSvga->cbGMRBits);
                    if (pSvga->pu32GMRBits)
                    {
                        /* Do not use id == 0. */
                        ASMBitSet(pSvga->pu32GMRBits, 0);
                        ASMBitSet(pSvga->au32ContextBits, 0);
                        ASMBitSet(pSvga->au32SurfaceBits, 0);
                    }
                    else
                    {
                        Status = STATUS_INSUFFICIENT_RESOURCES;
                    }
                }

                if (NT_SUCCESS(Status))
                {
                    if (RT_BOOL(pSvga->u32Caps & SVGA_CAP_DX))
                    {
                        RTListInit(&pSvga->listMobDeferredDestruction);
                        //pSvga->u64MobFence = 0;

                        /* Allocate memory for the MiniportMob.
                         *
                         * The MiniportMob id will be allocated on-demand in SvgaMobAlloc, because
                         * a mob allocation requires sending commands to the host device, but DxgkDdiStartDevice,
                         * which calls the SvgaAdapterStart function, is not allowed to send commands.
                         *
                         * If the device processes commands and generates an IRQ before DxgkDdiStartDevice returns,
                         * then OS stops using the device. Apparently Windows treats the early IRQ as an error.
                         */
                        Status = svgaCreateMiniportMob(pSvga);
                    }
                }
            }
        }
        else
        {
            GALOGREL(32, ("SVGA_ID_2 not supported. Device returned %d\n", u32SvgaId));
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    /* Caller's 'cleanup on error' code needs this pointer */
    *ppSvga = pSvga;

    return Status;
}

NTSTATUS SvgaQueryInfo(PVBOXWDDM_EXT_VMSVGA pSvga,
                       VBOXGAHWINFOSVGA *pSvgaInfo)
{
    pSvgaInfo->cbInfoSVGA = sizeof(VBOXGAHWINFOSVGA);

    int i;
    for (i = 0; i < RT_ELEMENTS(pSvgaInfo->au32Regs); ++i)
    {
        pSvgaInfo->au32Regs[i] = SVGARegRead(pSvga, i);
    }

    if (RT_LIKELY(pSvga->u32Caps & SVGA_CAP_GBOBJECTS))
    {
        for (i = 0; i < RT_ELEMENTS(pSvgaInfo->au32Caps); ++i)
            pSvgaInfo->au32Caps[i] = SVGADevCapRead(pSvga, i);
    }

    /* Beginning of FIFO. */
    memcpy(pSvgaInfo->au32Fifo, (void *)&pSvga->pu32FIFO[0], sizeof(pSvgaInfo->au32Fifo));

    return STATUS_SUCCESS;
}

NTSTATUS SvgaScreenDefine(PVBOXWDDM_EXT_VMSVGA pSvga,
                          uint32_t u32Offset,
                          uint32_t u32ScreenId,
                          int32_t xOrigin,
                          int32_t yOrigin,
                          uint32_t u32Width,
                          uint32_t u32Height,
                          bool fBlank)
{
    NTSTATUS Status = STATUS_SUCCESS;

    const uint32_t cbSubmit =   sizeof(uint32_t)
                              + sizeof(SVGAScreenObject);
    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        SvgaCmdDefineScreen(pvCmd, u32ScreenId, true,
                            xOrigin, yOrigin, u32Width, u32Height,
                            /* fPrimary = */ false, u32Offset, fBlank);
        SvgaCommit(pSvga, cbSubmit);
        SvgaFlush(pSvga);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaScreenDestroy(PVBOXWDDM_EXT_VMSVGA pSvga,
                           uint32_t u32ScreenId)
{
    NTSTATUS Status = STATUS_SUCCESS;

    const uint32_t cbSubmit =   sizeof(uint32_t)
                              + sizeof(SVGAFifoCmdDestroyScreen);
    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        SvgaCmdDestroyScreen(pvCmd, u32ScreenId);
        SvgaCommit(pSvga, cbSubmit);
        SvgaFlush(pSvga);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}


DECLINLINE(NTSTATUS) svgaIdAlloc(PVBOXWDDM_EXT_VMSVGA pSvga,
                                 uint32_t *pu32Bits,
                                 uint32_t cbBits,
                                 uint32_t u32Limit,
                                 uint32_t *pu32Id)
{
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSvga->IdSpinLock, &OldIrql);
    NTSTATUS Status = GaIdAlloc(pu32Bits, cbBits, u32Limit, pu32Id);
    KeReleaseSpinLock(&pSvga->IdSpinLock, OldIrql);
    return Status;
}


DECLINLINE(NTSTATUS) svgaIdFree(PVBOXWDDM_EXT_VMSVGA pSvga,
                                uint32_t *pu32Bits,
                                uint32_t cbBits,
                                uint32_t u32Limit,
                                uint32_t u32Id)
{
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSvga->IdSpinLock, &OldIrql);
    NTSTATUS Status = GaIdFree(pu32Bits, cbBits, u32Limit, u32Id);
    KeReleaseSpinLock(&pSvga->IdSpinLock, OldIrql);
    return Status;
}


static NTSTATUS svgaOTableIdAlloc(PVBOXWDDM_EXT_VMSVGA pSvga,
                                  uint32_t *pu32Bits,
                                  uint32_t cbBits,
                                  SVGAOTableType enmType,
                                  uint32_t *pu32Id)
{
    AssertReturn(enmType < RT_ELEMENTS(s_aOTInfo), STATUS_INVALID_PARAMETER);
    VMSVGAOTINFO const *pOTInfo = &s_aOTInfo[enmType];
    Assert(pOTInfo->cMaxEntries <= cbBits * 8);

    NTSTATUS Status = svgaIdAlloc(pSvga, pu32Bits, cbBits, pOTInfo->cMaxEntries, pu32Id);
    if (NT_SUCCESS(Status))
    {
        ExAcquireFastMutex(&pSvga->SvgaMutex);
        Status = svgaObjectTablesNotify(pSvga, enmType, *pu32Id);
        ExReleaseFastMutex(&pSvga->SvgaMutex);

        if (!NT_SUCCESS(Status))
            svgaIdFree(pSvga, pu32Bits, cbBits, pOTInfo->cMaxEntries, *pu32Id);
    }

    return Status;
}

static NTSTATUS svgaOTableIdFree(PVBOXWDDM_EXT_VMSVGA pSvga,
                                 uint32_t *pu32Bits,
                                 uint32_t cbBits,
                                 SVGAOTableType enmType,
                                 uint32_t u32Id)
{
    AssertReturn(enmType < RT_ELEMENTS(s_aOTInfo), STATUS_INVALID_PARAMETER);
    VMSVGAOTINFO const *pOTInfo = &s_aOTInfo[enmType];
    return svgaIdFree(pSvga, pu32Bits, cbBits, pOTInfo->cMaxEntries, u32Id);
}

NTSTATUS SvgaDXContextIdAlloc(PVBOXWDDM_EXT_VMSVGA pSvga,
                              uint32_t *pu32Cid)
{
    return svgaOTableIdAlloc(pSvga, pSvga->au32DXContextBits, sizeof(pSvga->au32DXContextBits),
                             SVGA_OTABLE_DXCONTEXT, pu32Cid);
}

NTSTATUS SvgaDXContextIdFree(PVBOXWDDM_EXT_VMSVGA pSvga,
                             uint32_t u32Cid)
{
    return svgaOTableIdFree(pSvga, pSvga->au32DXContextBits, sizeof(pSvga->au32DXContextBits),
                            SVGA_OTABLE_DXCONTEXT, u32Cid);
}

NTSTATUS SvgaMobIdAlloc(PVBOXWDDM_EXT_VMSVGA pSvga,
                        uint32_t *pu32MobId)
{
    return svgaOTableIdAlloc(pSvga, pSvga->au32MobBits, sizeof(pSvga->au32MobBits),
                             SVGA_OTABLE_MOB, pu32MobId);
}

NTSTATUS SvgaMobIdFree(PVBOXWDDM_EXT_VMSVGA pSvga,
                       uint32_t u32MobId)
{
    return svgaOTableIdFree(pSvga, pSvga->au32MobBits, sizeof(pSvga->au32MobBits),
                            SVGA_OTABLE_MOB, u32MobId);
}

NTSTATUS SvgaContextIdAlloc(PVBOXWDDM_EXT_VMSVGA pSvga,
                            uint32_t *pu32Cid)
{
    return svgaOTableIdAlloc(pSvga, pSvga->au32ContextBits, sizeof(pSvga->au32ContextBits),
                             SVGA_OTABLE_CONTEXT, pu32Cid);
}

NTSTATUS SvgaContextIdFree(PVBOXWDDM_EXT_VMSVGA pSvga,
                           uint32_t u32Cid)
{
    return svgaOTableIdFree(pSvga, pSvga->au32ContextBits, sizeof(pSvga->au32ContextBits),
                            SVGA_OTABLE_CONTEXT, u32Cid);
}

NTSTATUS SvgaSurfaceIdAlloc(PVBOXWDDM_EXT_VMSVGA pSvga,
                            uint32_t *pu32Sid)
{
    return svgaOTableIdAlloc(pSvga, pSvga->au32SurfaceBits, sizeof(pSvga->au32SurfaceBits),
                             SVGA_OTABLE_SURFACE, pu32Sid);
}

NTSTATUS SvgaSurfaceIdFree(PVBOXWDDM_EXT_VMSVGA pSvga,
                           uint32_t u32Sid)
{
    return svgaOTableIdFree(pSvga, pSvga->au32SurfaceBits, sizeof(pSvga->au32SurfaceBits),
                            SVGA_OTABLE_SURFACE, u32Sid);
}

NTSTATUS SvgaContextCreate(PVBOXWDDM_EXT_VMSVGA pSvga,
                           uint32_t u32Cid)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /*
     * Issue SVGA_3D_CMD_CONTEXT_DEFINE.
     */
    uint32_t cbSubmit =   sizeof(SVGA3dCmdHeader)
                        + sizeof(SVGA3dCmdDefineContext);
    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        Svga3dCmdDefineContext(pvCmd, u32Cid);
        SvgaCommit(pSvga, cbSubmit);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaContextDestroy(PVBOXWDDM_EXT_VMSVGA pSvga,
                            uint32_t u32Cid)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /*
     * Issue SVGA_3D_CMD_CONTEXT_DESTROY.
     */
    const uint32_t cbSubmit =   sizeof(SVGA3dCmdHeader)
                              + sizeof(SVGA3dCmdDestroyContext);
    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        Svga3dCmdDestroyContext(pvCmd, u32Cid);
        SvgaCommit(pSvga, cbSubmit);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaFence(PVBOXWDDM_EXT_VMSVGA pSvga,
                   uint32_t u32Fence)
{
    NTSTATUS Status = STATUS_SUCCESS;

    const uint32_t cbSubmit =  sizeof(uint32_t)
                             + sizeof(SVGAFifoCmdFence);
    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        SvgaCmdFence(pvCmd, u32Fence);

        SvgaCommit(pSvga, cbSubmit);
        SvgaFlush(pSvga);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaSurfaceDefine(PVBOXWDDM_EXT_VMSVGA pSvga,
                           GASURFCREATE const *pCreateParms,
                           GASURFSIZE const *paSizes,
                           uint32_t cSizes,
                           uint32_t u32Sid)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /* Size of SVGA_3D_CMD_SURFACE_DEFINE command for this surface. */
    const uint32_t cbSubmit =   sizeof(SVGA3dCmdHeader)
                              + sizeof(SVGA3dCmdDefineSurface)
                              + cSizes * sizeof(SVGA3dSize);

    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        Svga3dCmdDefineSurface(pvCmd, u32Sid, pCreateParms, paSizes, cSizes);
        SvgaCommit(pSvga, cbSubmit);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaSurfaceDestroy(PVBOXWDDM_EXT_VMSVGA pSvga,
                            uint32_t u32Sid)
{
    NTSTATUS Status = STATUS_SUCCESS;

    const uint32_t cbSubmit =   sizeof(SVGA3dCmdHeader)
                              + sizeof(SVGA3dCmdDestroySurface);
    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        Svga3dCmdDestroySurface(pvCmd, u32Sid);
        SvgaCommit(pSvga, cbSubmit);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaSharedSidInsert(VBOXWDDM_EXT_VMSVGA *pSvga,
                             uint32_t u32Sid,
                             uint32_t u32SharedSid)
{
    /* 'u32Sid' actually maps to 'u32SharedSid'. */
    AssertReturn(u32Sid != u32SharedSid, STATUS_INVALID_PARAMETER);

    /* Get the surface object, which must be redirected to the 'u32SharedSid'. */
    SURFACEOBJECT *pSO = SvgaSurfaceObjectQuery(pSvga, u32Sid);
    AssertPtrReturn(pSO, STATUS_INVALID_PARAMETER);

    /* The surface must not be redirected yet. */
    AssertReturnStmt(SVGAHOSTOBJECTID(&pSO->ho) == pSO->u32SharedSid, SvgaSurfaceObjectRelease(pSO), STATUS_INVALID_PARAMETER);

    /* The surface object to be mapped to. Query it to reference it.
     * If the surface id (u32SharedSid) is not in the surface objects, then it is OK.
     * It means that it is most likely from _D3D context.
     */
    SvgaSurfaceObjectQuery(pSvga, u32SharedSid);

    pSO->u32SharedSid = u32SharedSid;

    /* Release the redirected surface object only.
     * The shared surface object must keep the reference.
     */
    SvgaSurfaceObjectRelease(pSO);
    return STATUS_SUCCESS;
}

NTSTATUS SvgaSharedSidRemove(VBOXWDDM_EXT_VMSVGA *pSvga,
                             uint32_t u32Sid)
{
    /* Get the surface object, which was redirected. */
    SURFACEOBJECT *pSO = SvgaSurfaceObjectQuery(pSvga, u32Sid);
    AssertPtrReturn(pSO, STATUS_INVALID_PARAMETER);

    /* The surface must be redirected. */
    AssertReturnStmt(SVGAHOSTOBJECTID(&pSO->ho) != pSO->u32SharedSid, SvgaSurfaceObjectRelease(pSO), STATUS_INVALID_PARAMETER);

    /* The shared surface object, which the u32Sid was mapped to.
     * If the surface id (u32SharedSid) is not in the surface objects, then it is OK.
     * It means that it is most likely from _D3D context.
     */
    SURFACEOBJECT *pSharedSO = SvgaSurfaceObjectQuery(pSvga, pSO->u32SharedSid);

    pSO->u32SharedSid = SVGAHOSTOBJECTID(&pSO->ho);

    /* Remove the reference which was added by SvgaSharedSidInsert. */
    if (pSharedSO)
        SvgaSurfaceObjectRelease(pSharedSO);

    /* Release both surface objects. */
    if (pSharedSO)
        SvgaSurfaceObjectRelease(pSharedSO);
    SvgaSurfaceObjectRelease(pSO);
    return STATUS_SUCCESS;
}

typedef struct SVGAHOSTOBJECTARRAY
{
    GAHWRENDERDATA hdr;
    uint32_t cObjects;
    uint32_t u32Reserved;
    SVGAHOSTOBJECT *aObjects[(4096 - 2 * sizeof(uint32_t) - sizeof(GAHWRENDERDATA)) / sizeof(void *)];
} SVGAHOSTOBJECTARRAY;
AssertCompileSize(SVGAHOSTOBJECTARRAY, 4096);

NTSTATUS SvgaProcessSurface(VBOXWDDM_EXT_VMSVGA *pSvga,
                            uint32_t *pu32Sid,
                            SVGAHOSTOBJECTARRAY *pHOA)
{
    uint32_t const u32Sid = *pu32Sid;
    if (u32Sid != SVGA3D_INVALID_ID)
    {
        SURFACEOBJECT *pSO = NULL;
        for (uint32_t i = 0; i < pHOA->cObjects; ++i)
        {
            if (   pHOA->aObjects[i]->uType == SVGA_HOST_OBJECT_SURFACE
                && SVGAHOSTOBJECTID(pHOA->aObjects[i]) == u32Sid)
            {
                pSO = (SURFACEOBJECT *)pHOA->aObjects[i];
                break;
            }
        }

        if (!pSO)
        {
            pSO = SvgaSurfaceObjectQuery(pSvga, u32Sid);
            if (pSO)
            {
                AssertReturnStmt(pHOA->cObjects < RT_ELEMENTS(pHOA->aObjects),
                                 SvgaSurfaceObjectRelease(pSO),
                                 STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
                pHOA->aObjects[pHOA->cObjects++] = &pSO->ho;
            }
            else
            {
                /* Ignore the error. This is most likely a sid from _D3D context.*/
                return STATUS_SUCCESS;
            }
        }

        *pu32Sid = pSO->u32SharedSid;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS svgaReleaseHostObjects(uint32_t iStart,
                                       SVGAHOSTOBJECTARRAY *pHOA)
{
    AssertReturn(iStart <= pHOA->cObjects, STATUS_INVALID_PARAMETER);

    for (uint32_t i = iStart; i < pHOA->cObjects; ++i)
    {
        SVGAHOSTOBJECT *pHO = pHOA->aObjects[i];
        if (pHO->uType == SVGA_HOST_OBJECT_SURFACE)
        {
            SURFACEOBJECT *pSO = (SURFACEOBJECT *)pHO;
            SvgaSurfaceObjectRelease(pSO);
        }
        else
        {
            /* Should never happen. No other types of objects can be in the array. */
            AssertFailedReturn(STATUS_ILLEGAL_INSTRUCTION);
        }
    }

    pHOA->cObjects = iStart;

    return STATUS_SUCCESS;
}

NTSTATUS SvgaRenderComplete(PVBOXWDDM_EXT_VMSVGA pSvga,
                            GAHWRENDERDATA *pHwRenderData)
{
    RT_NOREF(pSvga);

    SVGAHOSTOBJECTARRAY *pHOA = (SVGAHOSTOBJECTARRAY *)pHwRenderData;

    NTSTATUS Status = svgaReleaseHostObjects(0, pHOA);

    GaMemFree(pHOA);

    return Status;
}

static NTSTATUS svgaUpdateCommand(VBOXWDDM_EXT_VMSVGA *pSvga,
                                  uint32_t u32CmdId,
                                  uint8_t *pu8Cmd,
                                  uint32_t cbCmd,
                                  SVGAHOSTOBJECTARRAY *pHOA)
{
    NTSTATUS Status = STATUS_SUCCESS;

    const SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pu8Cmd;
    uint8_t *pCommand = (uint8_t *)&pHeader[1];

    uint32_t iStart = pHOA->cObjects;

    switch (u32CmdId)
    {
        case SVGA_3D_CMD_PRESENT:
        case SVGA_3D_CMD_PRESENT_READBACK:
        {
            SVGA3dCmdPresent *p = (SVGA3dCmdPresent *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->sid, pHOA);
        } break;
        case SVGA_3D_CMD_SETRENDERTARGET:
        {
            SVGA3dCmdSetRenderTarget *p = (SVGA3dCmdSetRenderTarget *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->target.sid, pHOA);
        } break;
        case SVGA_3D_CMD_SURFACE_COPY:
        {
            SVGA3dCmdSurfaceCopy *p = (SVGA3dCmdSurfaceCopy *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->src.sid, pHOA);
            if (Status == STATUS_SUCCESS)
                Status = SvgaProcessSurface(pSvga, &p->dest.sid, pHOA);
        } break;
        case SVGA_3D_CMD_SURFACE_STRETCHBLT:
        {
            SVGA3dCmdSurfaceStretchBlt *p = (SVGA3dCmdSurfaceStretchBlt *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->src.sid, pHOA);
            if (Status == STATUS_SUCCESS)
                Status = SvgaProcessSurface(pSvga, &p->dest.sid, pHOA);
        } break;
        case SVGA_3D_CMD_SURFACE_DMA:
        {
            /// @todo gmrid?
            SVGA3dCmdSurfaceDMA *p = (SVGA3dCmdSurfaceDMA *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->host.sid, pHOA);
        } break;
        case SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN:
        {
            SVGA3dCmdBlitSurfaceToScreen *p = (SVGA3dCmdBlitSurfaceToScreen *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->srcImage.sid, pHOA);
        } break;
        case SVGA_3D_CMD_GENERATE_MIPMAPS:
        {
            SVGA3dCmdGenerateMipmaps *p = (SVGA3dCmdGenerateMipmaps *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->sid, pHOA);
        } break;
        case SVGA_3D_CMD_ACTIVATE_SURFACE:
        {
            SVGA3dCmdActivateSurface *p = (SVGA3dCmdActivateSurface *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->sid, pHOA);
        } break;
        case SVGA_3D_CMD_DEACTIVATE_SURFACE:
        {
            SVGA3dCmdDeactivateSurface *p = (SVGA3dCmdDeactivateSurface *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->sid, pHOA);
        } break;
        case SVGA_3D_CMD_SETTEXTURESTATE:
        {
            SVGA3dCmdSetTextureState *p = (SVGA3dCmdSetTextureState *)pCommand;
            uint32_t cStates = (pHeader->size - sizeof(SVGA3dCmdSetTextureState)) / sizeof(SVGA3dTextureState);
            SVGA3dTextureState *pState = (SVGA3dTextureState *)&p[1];
            while (cStates > 0)
            {
                if (pState->name == SVGA3D_TS_BIND_TEXTURE)
                {
                    Status = SvgaProcessSurface(pSvga, &pState->value, pHOA);
                    if (Status != STATUS_SUCCESS)
                        break;
                }

                ++pState;
                --cStates;
            }
        } break;
        case SVGA_3D_CMD_DRAW_PRIMITIVES:
        {
            SVGA3dCmdDrawPrimitives *p = (SVGA3dCmdDrawPrimitives *)pCommand;
            AssertBreakStmt(cbCmd >= sizeof(SVGA3dCmdDrawPrimitives), Status = STATUS_ILLEGAL_INSTRUCTION);
            AssertBreakStmt(   p->numVertexDecls <= SVGA3D_MAX_VERTEX_ARRAYS
                            && p->numRanges <= SVGA3D_MAX_DRAW_PRIMITIVE_RANGES, Status = STATUS_ILLEGAL_INSTRUCTION);
            AssertBreakStmt(cbCmd >= p->numVertexDecls * sizeof(SVGA3dVertexDecl)
                                   + p->numRanges * sizeof(SVGA3dPrimitiveRange), Status = STATUS_ILLEGAL_INSTRUCTION);

            /// @todo cid?

            SVGA3dVertexDecl *paDecls = (SVGA3dVertexDecl *)&p[1];
            SVGA3dPrimitiveRange *paRanges = (SVGA3dPrimitiveRange *)&paDecls[p->numVertexDecls];

            uint32_t i;
            for (i = 0; i < p->numVertexDecls; ++i)
            {
                Status = SvgaProcessSurface(pSvga, &paDecls[i].array.surfaceId, pHOA);
                if (Status != STATUS_SUCCESS)
                    break;
            }
            if (Status == STATUS_SUCCESS)
            {
                for (i = 0; i < p->numRanges; ++i)
                {
                    Status = SvgaProcessSurface(pSvga, &paRanges[i].indexArray.surfaceId, pHOA);
                    if (Status != STATUS_SUCCESS)
                        break;
                }
            }
        } break;

        case SVGA_3D_CMD_READBACK_GB_SURFACE:
        {
            SVGA3dCmdReadbackGBSurface *p = (SVGA3dCmdReadbackGBSurface *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->sid, pHOA);
        } break;

        /*
         * Unsupported commands, which might include a sid.
         * The VBox VMSVGA device does not implement them and most of them are not used by SVGA driver.
         */
        case SVGA_3D_CMD_SET_VERTEX_STREAMS:
        case SVGA_3D_CMD_LOGICOPS_BITBLT:
        case SVGA_3D_CMD_LOGICOPS_TRANSBLT:
        case SVGA_3D_CMD_LOGICOPS_STRETCHBLT:
        case SVGA_3D_CMD_LOGICOPS_COLORFILL:
        case SVGA_3D_CMD_LOGICOPS_ALPHABLEND:
        case SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND:
        case SVGA_3D_CMD_DEFINE_GB_SURFACE:
        case SVGA_3D_CMD_DESTROY_GB_SURFACE:
        case SVGA_3D_CMD_READBACK_GB_IMAGE:
        case SVGA_3D_CMD_READBACK_GB_IMAGE_PARTIAL:
        case SVGA_3D_CMD_INVALIDATE_GB_IMAGE_PARTIAL:
        case SVGA_3D_CMD_BIND_GB_SCREENTARGET:
        case SVGA_3D_CMD_SET_OTABLE_BASE:
        case SVGA_3D_CMD_SET_OTABLE_BASE64:
        case SVGA_3D_CMD_READBACK_OTABLE:
        case SVGA_3D_CMD_DRAW_INDEXED:
            AssertFailed();
            break;
        case SVGA_3D_CMD_BIND_GB_SURFACE:
        case SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH:
        case SVGA_3D_CMD_COND_BIND_GB_SURFACE:
        case SVGA_3D_CMD_UPDATE_GB_IMAGE:
        case SVGA_3D_CMD_UPDATE_GB_SURFACE:
        case SVGA_3D_CMD_INVALIDATE_GB_IMAGE:
        case SVGA_3D_CMD_INVALIDATE_GB_SURFACE:
            break;
        case SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER:
        {
            SVGA3dCmdDXSetSingleConstantBuffer *p = (SVGA3dCmdDXSetSingleConstantBuffer *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->sid, pHOA);
        } break;
        case SVGA_3D_CMD_DX_PRED_COPY_REGION:
        {
            SVGA3dCmdDXPredCopyRegion *p = (SVGA3dCmdDXPredCopyRegion *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->srcSid, pHOA);
            if (Status == STATUS_SUCCESS)
                Status = SvgaProcessSurface(pSvga, &p->dstSid, pHOA);
        } break;
        case SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW:
        {
            SVGA3dCmdDXDefineRenderTargetView *p = (SVGA3dCmdDXDefineRenderTargetView *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->sid, pHOA);
        } break;
        case SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW:
        {
            SVGA3dCmdDXDefineShaderResourceView *p = (SVGA3dCmdDXDefineShaderResourceView *)pCommand;
            Status = SvgaProcessSurface(pSvga, &p->sid, pHOA);
        } break;
        default:
            if (SVGA_3D_CMD_DX_MIN <= u32CmdId && u32CmdId <= SVGA_3D_CMD_DX_MAX)
            {
                /** @todo Implement. */
            }
            break;
    }

    if (Status != STATUS_SUCCESS)
    {
        svgaReleaseHostObjects(iStart, pHOA);
    }

    return Status;
}

/** Copy SVGA commands from pvSource to pvTarget and does the following:
 *    - verifies that all commands are valid;
 *    - tweaks and substitutes command parameters if necessary.
 *
 * Command parameters are changed when:
 *    - a command contains a shared surface id, which will be replaced by the original surface id.
 *
 * @param pSvga .
 * @param pSvgaContext .
 * @param pvTarget .
 * @param cbTarget .
 * @param pvSource .
 * @param cbSource .
 * @param pu32TargetLength How many bytes were copied to pvTarget buffer.
 * @param pu32ProcessedLength How many bytes were processed in the pvSource buffer.
 * @param ppvHwPrivate The hardware private data. SVGA stores information about host objects there.
 */
NTSTATUS SvgaRenderCommands(PVBOXWDDM_EXT_VMSVGA pSvga,
                            struct VMSVGACONTEXT *pSvgaContext,
                            void *pvTarget,
                            uint32_t cbTarget,
                            const void *pvSource,
                            uint32_t cbSource,
                            uint32_t *pu32TargetLength,
                            uint32_t *pu32ProcessedLength,
                            GAHWRENDERDATA **ppHwRenderData)
{
    /* All commands consist of 32 bit dwords. */
    AssertReturn(cbSource % sizeof(uint32_t) == 0, STATUS_ILLEGAL_INSTRUCTION);

    NTSTATUS Status = SvgaRenderCommandsD3D(pSvga,
                                            pSvgaContext,
                                            pvTarget,
                                            cbTarget,
                                            pvSource,
                                            cbSource,
                                            pu32TargetLength,
                                            pu32ProcessedLength);
    AssertReturn(NT_SUCCESS(Status), Status);

    SVGAHOSTOBJECTARRAY *pHO = (SVGAHOSTOBJECTARRAY *)GaMemAlloc(sizeof(SVGAHOSTOBJECTARRAY));
    if (!pHO)
        return STATUS_INSUFFICIENT_RESOURCES;
    pHO->cObjects = 0;
    pHO->u32Reserved = 0;

    uint8_t *pu8Src = (uint8_t *)pvTarget;
    uint8_t *pu8SrcEnd = (uint8_t *)pvTarget + (*pu32TargetLength);
    while (pu8SrcEnd > pu8Src)
    {
        const uint32_t cbSrcLeft = pu8SrcEnd - pu8Src;
        AssertBreakStmt(cbSrcLeft >= sizeof(uint32_t), Status = STATUS_ILLEGAL_INSTRUCTION);

        /* Get the command id and command length. */
        const uint32_t u32CmdId = *(uint32_t *)pu8Src;
        uint32_t cbCmd = 0;

        if (SVGA_3D_CMD_BASE <= u32CmdId && u32CmdId < SVGA_3D_CMD_MAX)
        {
            AssertBreakStmt(cbSrcLeft >= sizeof(SVGA3dCmdHeader), Status = STATUS_ILLEGAL_INSTRUCTION);

            const SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pu8Src;
            cbCmd = sizeof(SVGA3dCmdHeader) + pHeader->size;
            AssertBreakStmt(cbCmd % sizeof(uint32_t) == 0, Status = STATUS_ILLEGAL_INSTRUCTION);
            AssertBreakStmt(cbSrcLeft >= cbCmd, Status = STATUS_ILLEGAL_INSTRUCTION);
        }
        else
        {
            /* It is not expected that any of common SVGA commands will be in the command buffer
             * because the SVGA gallium driver does not use them.
             */
            AssertBreakStmt(0, Status = STATUS_ILLEGAL_INSTRUCTION);
        }

        /* Update the command in source place if necessary. */
        Status = svgaUpdateCommand(pSvga, u32CmdId, pu8Src, cbCmd, pHO);
        if (Status != STATUS_SUCCESS)
        {
            Assert(Status == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
            break;
        }

        pu8Src += cbCmd;
    }

    if (   Status == STATUS_SUCCESS
        || Status == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER)
    {
        if (pHO->cObjects)
            *ppHwRenderData = &pHO->hdr;
        else
        {
            SvgaRenderComplete(pSvga, &pHO->hdr);
            *ppHwRenderData = NULL;
        }
    }
    else
    {
        SvgaRenderComplete(pSvga, &pHO->hdr);
    }

    return Status;
}

NTSTATUS SvgaGenPresent(uint32_t u32Sid,
                        uint32_t u32Width,
                        uint32_t u32Height,
                        void *pvDst,
                        uint32_t cbDst,
                        uint32_t *pcbOut)
{
    const uint32_t cbRequired =   sizeof(SVGA3dCmdHeader)
                                + sizeof(SVGA3dCmdPresent)
                                + sizeof(SVGA3dCopyRect);
    if (pcbOut)
    {
        *pcbOut = cbRequired;
    }

    if (cbDst < cbRequired)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    Svga3dCmdPresent(pvDst, u32Sid, u32Width, u32Height);

    return STATUS_SUCCESS;
}

NTSTATUS SvgaPresent(PVBOXWDDM_EXT_VMSVGA pSvga,
                     uint32_t u32Sid,
                     uint32_t u32Width,
                     uint32_t u32Height)
{
    NTSTATUS Status = STATUS_SUCCESS;

    uint32_t cbSubmit = 0;
    SvgaGenPresent(0, 0, 0, NULL, 0, &cbSubmit);

    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        SvgaGenPresent(u32Sid, u32Width, u32Height, pvCmd, cbSubmit, NULL);
        SvgaCommit(pSvga, cbSubmit);
        SvgaFlush(pSvga);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaGenPresentVRAM(PVBOXWDDM_EXT_VMSVGA pSvga,
                            uint32_t u32Sid,
                            uint32_t u32Width,
                            uint32_t u32Height,
                            uint32_t u32VRAMOffset,
                            void *pvDst,
                            uint32_t cbDst,
                            uint32_t *pcbOut)
{
    RT_NOREF(pSvga);

    const uint32_t cbCmdSurfaceDMAToFB =   sizeof(SVGA3dCmdHeader)
                                         + sizeof(SVGA3dCmdSurfaceDMA)
                                         + sizeof(SVGA3dCopyBox)
                                         + sizeof(SVGA3dCmdSurfaceDMASuffix);
    const uint32_t cbCmdUpdate =  sizeof(uint32_t)
                                + sizeof(SVGAFifoCmdUpdate);

    const uint32_t cbRequired = cbCmdSurfaceDMAToFB + cbCmdUpdate;
    if (pcbOut)
    {
        *pcbOut = cbRequired;
    }

    if (cbDst < cbRequired)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    Svga3dCmdSurfaceDMAToFB(pvDst, u32Sid, u32Width, u32Height, u32VRAMOffset);
    SvgaCmdUpdate((uint8_t *)pvDst + cbCmdSurfaceDMAToFB, 0, 0, u32Width, u32Height);

    return STATUS_SUCCESS;
}

NTSTATUS SvgaPresentVRAM(PVBOXWDDM_EXT_VMSVGA pSvga,
                         uint32_t u32Sid,
                         uint32_t u32Width,
                         uint32_t u32Height,
                         uint32_t u32VRAMOffset)
{
    NTSTATUS Status = STATUS_SUCCESS;

    uint32_t cbSubmit = 0;
    SvgaGenPresentVRAM(pSvga, 0, 0, 0, 0, NULL, 0, &cbSubmit);

    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        Status = SvgaGenPresentVRAM(pSvga, u32Sid, u32Width, u32Height, u32VRAMOffset, pvCmd, cbSubmit, NULL);
        Assert(Status == STATUS_SUCCESS);
        SvgaCommit(pSvga, cbSubmit);
        SvgaFlush(pSvga);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaGenSurfaceDMA(PVBOXWDDM_EXT_VMSVGA pSvga,
                           SVGAGuestImage const *pGuestImage,
                           SVGA3dSurfaceImageId const *pSurfId,
                           SVGA3dTransferType enmTransferType, uint32_t xSrc, uint32_t ySrc,
                           uint32_t xDst, uint32_t yDst, uint32_t cWidth, uint32_t cHeight,
                           void *pvDst,
                           uint32_t cbDst,
                           uint32_t *pcbOut)
{
    RT_NOREF(pSvga);

    const uint32_t cbCmdSurfaceDMA =   sizeof(SVGA3dCmdHeader)
                                     + sizeof(SVGA3dCmdSurfaceDMA)
                                     + sizeof(SVGA3dCopyBox)
                                     + sizeof(SVGA3dCmdSurfaceDMASuffix);

    const uint32_t cbRequired = cbCmdSurfaceDMA;
    if (pcbOut)
    {
        *pcbOut = cbRequired;
    }

    if (cbDst < cbRequired)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    Svga3dCmdSurfaceDMA(pvDst, pGuestImage, pSurfId, enmTransferType,
                        xSrc, ySrc, xDst, yDst, cWidth, cHeight);

    return STATUS_SUCCESS;
}

NTSTATUS SvgaGenBlitGMRFBToScreen(PVBOXWDDM_EXT_VMSVGA pSvga,
                                  uint32_t idDstScreen,
                                  int32_t xSrc,
                                  int32_t ySrc,
                                  RECT const *pDstRect,
                                  void *pvDst,
                                  uint32_t cbDst,
                                  uint32_t *pcbOut)
{
    RT_NOREF(pSvga);

    const uint32_t cbRequired =   sizeof(uint32_t)
                                + sizeof(SVGAFifoCmdBlitGMRFBToScreen);
    if (pcbOut)
    {
        *pcbOut = cbRequired;
    }

    if (cbDst < cbRequired)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    SvgaCmdBlitGMRFBToScreen(pvDst, idDstScreen, xSrc, ySrc,
                             pDstRect->left, pDstRect->top,
                             pDstRect->right, pDstRect->bottom);

    return STATUS_SUCCESS;
}

NTSTATUS SvgaBlitGMRFBToScreen(PVBOXWDDM_EXT_VMSVGA pSvga,
                               uint32_t idDstScreen,
                               int32_t xSrc,
                               int32_t ySrc,
                               RECT const *pDstRect)
{
    NTSTATUS Status = STATUS_SUCCESS;
    uint32_t cbSubmit = 0;
    void *pvCmd = NULL;

    SvgaGenBlitGMRFBToScreen(pSvga, idDstScreen, xSrc, ySrc, pDstRect,
                             NULL, 0, &cbSubmit);

    pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        Status = SvgaGenBlitGMRFBToScreen(pSvga, idDstScreen, xSrc, ySrc, pDstRect,
                                          pvCmd, cbSubmit, NULL);
        Assert(Status == STATUS_SUCCESS);

        SvgaCommit(pSvga, cbSubmit);
        SvgaFlush(pSvga);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaGenBlitScreenToGMRFB(PVBOXWDDM_EXT_VMSVGA pSvga,
                                  uint32_t idSrcScreen,
                                  int32_t xSrc,
                                  int32_t ySrc,
                                  RECT const *pDstRect,
                                  void *pvDst,
                                  uint32_t cbDst,
                                  uint32_t *pcbOut)
{
    RT_NOREF(pSvga);

    const uint32_t cbRequired =   sizeof(uint32_t)
                                + sizeof(SVGAFifoCmdBlitScreenToGMRFB);
    if (pcbOut)
    {
        *pcbOut = cbRequired;
    }

    if (cbDst < cbRequired)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    SvgaCmdBlitScreenToGMRFB(pvDst, idSrcScreen, xSrc, ySrc,
                             pDstRect->left, pDstRect->top,
                             pDstRect->right, pDstRect->bottom);

    return STATUS_SUCCESS;
}

NTSTATUS SvgaGenBlitSurfaceToScreen(PVBOXWDDM_EXT_VMSVGA pSvga,
                                    uint32_t sid,
                                    RECT const *pSrcRect,
                                    uint32 idDstScreen,
                                    RECT const *pDstRect,
                                    uint32_t cDstClipRects,
                                    RECT const *paDstClipRects,
                                    void *pvDst,
                                    uint32_t cbDst,
                                    uint32_t *pcbOut,
                                    uint32_t *pcOutDstClipRects)
{
    RT_NOREF(pSvga);

    uint32_t const cbCmd =  sizeof(SVGA3dCmdHeader)
                          + sizeof(SVGA3dCmdBlitSurfaceToScreen);

    /* How many rectangles can fit into the buffer. */
    uint32_t const cMaxDstClipRects = cbDst >= cbCmd ? (cbDst - cbCmd) / sizeof(SVGASignedRect): 0;

    /* How many should be put into the buffer. */
    uint32_t const cOutDstClipRects = RT_MIN(cDstClipRects, cMaxDstClipRects);

    if (pcOutDstClipRects)
    {
        *pcOutDstClipRects = cOutDstClipRects;
    }

    /* Check if the command does not fit in any case. */
    if (   cbDst < cbCmd
        || (cDstClipRects > 0 && cOutDstClipRects == 0))
    {
        /* Command would not fit or no rectangles would fit. */
        if (pcbOut)
        {
            /* Return full size required for the command and ALL rectangles. */
            *pcbOut = cbCmd + cDstClipRects * sizeof(SVGASignedRect);
        }

        return STATUS_BUFFER_OVERFLOW;
    }

    /* Put as many rectangles as possible. */
    if (pcbOut)
    {
        /* Return full size required for the command and ALL rectangles. */
        *pcbOut = cbCmd + cOutDstClipRects * sizeof(SVGASignedRect);
    }

    Svga3dCmdBlitSurfaceToScreen(pvDst, sid, pSrcRect, idDstScreen, pDstRect, cOutDstClipRects, paDstClipRects);

    return STATUS_SUCCESS;
}


NTSTATUS SvgaUpdate(PVBOXWDDM_EXT_VMSVGA pSvga,
                    uint32_t u32X,
                    uint32_t u32Y,
                    uint32_t u32Width,
                    uint32_t u32Height)
{
    NTSTATUS Status = STATUS_SUCCESS;

    uint32_t cbSubmit =  sizeof(uint32_t)
                       + sizeof(SVGAFifoCmdUpdate);

    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        /** @todo Multi-monitor. */
        SvgaCmdUpdate(pvCmd, u32X, u32Y, u32Width, u32Height);
        SvgaCommit(pSvga, cbSubmit);
        SvgaFlush(pSvga);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaGenDefineCursor(PVBOXWDDM_EXT_VMSVGA pSvga,
                             uint32_t u32HotspotX, uint32_t u32HotspotY, uint32_t u32Width, uint32_t u32Height,
                             uint32_t u32AndMaskDepth, uint32_t u32XorMaskDepth,
                             void const *pvAndMask, uint32_t cbAndMask, void const *pvXorMask, uint32_t cbXorMask,
                             void *pvDst,
                             uint32_t cbDst,
                             uint32_t *pcbOut)
{
    RT_NOREF(pSvga);

    const uint32_t cbCmd =   sizeof(uint32_t)
                           + sizeof(SVGAFifoCmdDefineCursor)
                           + cbAndMask
                           + cbXorMask;

    const uint32_t cbRequired = cbCmd;
    if (pcbOut)
    {
        *pcbOut = cbRequired;
    }

    if (cbDst < cbRequired)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    SvgaCmdDefineCursor(pvDst, u32HotspotX, u32HotspotY, u32Width, u32Height,
                        u32AndMaskDepth, u32XorMaskDepth,
                        pvAndMask, cbAndMask, pvXorMask, cbXorMask);

    return STATUS_SUCCESS;
}

NTSTATUS SvgaDefineCursor(PVBOXWDDM_EXT_VMSVGA pSvga,
                          uint32_t u32HotspotX, uint32_t u32HotspotY, uint32_t u32Width, uint32_t u32Height,
                          uint32_t u32AndMaskDepth, uint32_t u32XorMaskDepth,
                          void const *pvAndMask, uint32_t cbAndMask, void const *pvXorMask, uint32_t cbXorMask)
{
    NTSTATUS Status = STATUS_SUCCESS;

    uint32_t cbSubmit = 0;
    SvgaGenDefineCursor(pSvga,
                        u32HotspotX, u32HotspotY, u32Width, u32Height,
                        u32AndMaskDepth, u32XorMaskDepth,
                        pvAndMask, cbAndMask, pvXorMask, cbXorMask,
                        NULL, 0, &cbSubmit);

    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        Status = SvgaGenDefineCursor(pSvga,
                                     u32HotspotX, u32HotspotY, u32Width, u32Height,
                                     u32AndMaskDepth, u32XorMaskDepth,
                                     pvAndMask, cbAndMask, pvXorMask, cbXorMask,
                                     pvCmd, cbSubmit, NULL);
        Assert(Status == STATUS_SUCCESS);
        SvgaCommit(pSvga, cbSubmit);
        SvgaFlush(pSvga);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS SvgaGenDefineAlphaCursor(PVBOXWDDM_EXT_VMSVGA pSvga,
                                  uint32_t u32HotspotX, uint32_t u32HotspotY, uint32_t u32Width, uint32_t u32Height,
                                  void const *pvImage, uint32_t cbImage,
                                  void *pvDst,
                                  uint32_t cbDst,
                                  uint32_t *pcbOut)
{
    RT_NOREF(pSvga);

    const uint32_t cbCmd =   sizeof(uint32_t)
                           + sizeof(SVGAFifoCmdDefineAlphaCursor)
                           + cbImage;

    const uint32_t cbRequired = cbCmd;
    if (pcbOut)
    {
        *pcbOut = cbRequired;
    }

    if (cbDst < cbRequired)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    SvgaCmdDefineAlphaCursor(pvDst, u32HotspotX, u32HotspotY, u32Width, u32Height,
                             pvImage, cbImage);

    return STATUS_SUCCESS;
}

NTSTATUS SvgaDefineAlphaCursor(PVBOXWDDM_EXT_VMSVGA pSvga,
                               uint32_t u32HotspotX, uint32_t u32HotspotY, uint32_t u32Width, uint32_t u32Height,
                               void const *pvImage, uint32_t cbImage)
{
    NTSTATUS Status = STATUS_SUCCESS;

    uint32_t cbSubmit = 0;
    SvgaGenDefineAlphaCursor(pSvga,
                             u32HotspotX, u32HotspotY, u32Width, u32Height,
                             pvImage, cbImage,
                             NULL, 0, &cbSubmit);

    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        Status = SvgaGenDefineAlphaCursor(pSvga,
                                          u32HotspotX, u32HotspotY, u32Width, u32Height,
                                          pvImage, cbImage,
                                          pvCmd, cbSubmit, NULL);
        Assert(Status == STATUS_SUCCESS);
        SvgaCommit(pSvga, cbSubmit);
        SvgaFlush(pSvga);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}


NTSTATUS SvgaGenDefineGMRFB(PVBOXWDDM_EXT_VMSVGA pSvga,
                            uint32_t u32Offset,
                            uint32_t u32BytesPerLine,
                            void *pvDst,
                            uint32_t cbDst,
                            uint32_t *pcbOut)
{
    RT_NOREF(pSvga);

    const uint32_t cbCmd =   sizeof(uint32_t)
                           + sizeof(SVGAFifoCmdDefineGMRFB);

    const uint32_t cbRequired = cbCmd;
    if (pcbOut)
    {
        *pcbOut = cbRequired;
    }

    if (cbDst < cbRequired)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    SvgaCmdDefineGMRFB(pvDst, u32Offset, u32BytesPerLine);

    return STATUS_SUCCESS;
}

NTSTATUS SvgaDefineGMRFB(PVBOXWDDM_EXT_VMSVGA pSvga,
                         uint32_t u32Offset,
                         uint32_t u32BytesPerLine,
                         bool fForce)
{
    NTSTATUS Status = STATUS_SUCCESS;

    ExAcquireFastMutex(&pSvga->SvgaMutex);
    if (   !fForce
        && pSvga->lastGMRFB.u32Offset == u32Offset
        && pSvga->lastGMRFB.u32BytesPerLine == u32BytesPerLine)
    {
        ExReleaseFastMutex(&pSvga->SvgaMutex);
        return VINF_SUCCESS;
    }
    ExReleaseFastMutex(&pSvga->SvgaMutex);

    uint32_t cbSubmit = 0;
    SvgaGenDefineGMRFB(pSvga, u32Offset, u32BytesPerLine,
                       NULL, 0, &cbSubmit);

    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        Status = SvgaGenDefineGMRFB(pSvga, u32Offset, u32BytesPerLine,
                                    pvCmd, cbSubmit, NULL);
        Assert(Status == STATUS_SUCCESS);
        SvgaCommit(pSvga, cbSubmit);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Status == STATUS_SUCCESS)
    {
        ExAcquireFastMutex(&pSvga->SvgaMutex);
        pSvga->lastGMRFB.u32Offset = u32Offset;
        pSvga->lastGMRFB.u32BytesPerLine = u32BytesPerLine;
        ExReleaseFastMutex(&pSvga->SvgaMutex);
    }

    return Status;
}


/* SVGA Guest Memory Region (GMR). Memory known for both host and guest.
 * There can be many (hundreds) of regions, therefore use AVL tree.
 */
typedef struct GAWDDMREGION
{
    /* Key is GMR id (equal to u32GmrId). */
    AVLU32NODECORE Core;
    /* Pointer to a graphics context device (PVBOXWDDM_DEVICE) the GMR is associated with. */
    void      *pvOwner;
    /* The ring-3 mapping memory object handle (from mob). */
    RTR0MEMOBJ MapObjR3;
    RTR3PTR    pvR3;
    /* A corresponding MOB, which provides the GMR id and RTR0MEMOBJ for the region memory. */
    PVMSVGAMOB pMob;
    /* The allocated size in pages. */
    uint32_t   u32NumPages;
    /* Physical addresses of the pages. */
    RTHCPHYS   aPhys[1];
} GAWDDMREGION;


/* Allocate memory pages and the corresponding mob.
 */
static NTSTATUS gmrAllocMemory(VBOXWDDM_EXT_VMSVGA *pSvga, GAWDDMREGION *pRegion, uint32_t u32NumPages)
{
    NTSTATUS Status;

    /* Allocate memory. */
    RTR0MEMOBJ MemObj;
    int rc = RTR0MemObjAllocPageTag(&MemObj, u32NumPages << PAGE_SHIFT,
                                    false /* executable R0 mapping */, "VMSVGAGMR");
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        if (!RTR0MemObjWasZeroInitialized(MemObj))
            RT_BZERO(RTR0MemObjAddress(MemObj), (size_t)u32NumPages << PAGE_SHIFT);

        /* Allocate corresponding mob. */
        Status = SvgaMobCreate(pSvga, &pRegion->pMob, u32NumPages, 0);
        Assert(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
        {
            Status = SvgaMobSetMemObj(pRegion->pMob, MemObj);
            Assert(NT_SUCCESS(Status));
            if (NT_SUCCESS(Status))
                return STATUS_SUCCESS;
        }

        if (   pRegion->pMob
            && pRegion->pMob->hMemObj == NIL_RTR0MEMOBJ)
        {
            /* The memory object has not been assigned to the mob yet. Clean up the local object.
             * Otherwise the caller will clean up.
             */
            int rc2 = RTR0MemObjFree(MemObj, false);
            AssertRC(rc2);
        }
    }
    else
        AssertFailedStmt(Status = STATUS_INSUFFICIENT_RESOURCES);

    return Status;
}

/* Initialize the GMR to be ready for reporting to the host.
 */
static NTSTATUS gmrInit(VBOXWDDM_EXT_VMSVGA *pSvga, GAWDDMREGION *pRegion, void *pvOwner, uint32_t u32NumPages)
{
    NTSTATUS Status = gmrAllocMemory(pSvga, pRegion, u32NumPages);
    if (NT_SUCCESS(Status))
    {
        int rc = RTR0MemObjMapUser(&pRegion->MapObjR3, pRegion->pMob->hMemObj, (RTR3PTR)-1, 0,
                                   RTMEM_PROT_WRITE | RTMEM_PROT_READ, NIL_RTR0PROCESS);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            uint32_t iPage;
            for (iPage = 0; iPage < u32NumPages; ++iPage)
                pRegion->aPhys[iPage] = RTR0MemObjGetPagePhysAddr(pRegion->pMob->hMemObj, iPage);

            pRegion->pvR3 = RTR0MemObjAddressR3(pRegion->MapObjR3);

            pRegion->pvOwner = pvOwner;
            pRegion->u32NumPages = u32NumPages;
        }
        else
            AssertFailedStmt(Status = STATUS_INSUFFICIENT_RESOURCES);
    }

    return Status;
}

/* Send GMR creation commands to the host.
 */
static NTSTATUS gmrReportToHost(VBOXWDDM_EXT_VMSVGA *pSvga, GAWDDMREGION *pRegion)
{
    /*
     * Issue SVGA_CMD_DEFINE_GMR2 + SVGA_CMD_REMAP_GMR2 + SVGA_3D_CMD_DEFINE_GB_MOB64.
     */
    uint32_t const cbPPNArray = pRegion->u32NumPages * sizeof(uint64_t);

    uint32_t cbSubmit = sizeof(uint32_t) + sizeof(SVGAFifoCmdDefineGMR2);
    cbSubmit += sizeof(uint32_t) + sizeof(SVGAFifoCmdRemapGMR2) + cbPPNArray;
    if (RT_BOOL(pSvga->u32Caps & SVGA_CAP_DX))
        cbSubmit += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdDefineGBMob64);

    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (!pvCmd)
        return STATUS_INSUFFICIENT_RESOURCES;

    uint8_t *pu8Cmd = (uint8_t *)pvCmd;

    uint32_t *pu32CmdId;

    pu32CmdId = (uint32_t *)pu8Cmd;
    *pu32CmdId = SVGA_CMD_DEFINE_GMR2;
    pu8Cmd += sizeof(*pu32CmdId);

    {
    SVGAFifoCmdDefineGMR2 *pCmd = (SVGAFifoCmdDefineGMR2 *)pu8Cmd;
    pCmd->gmrId = VMSVGAMOB_ID(pRegion->pMob);
    pCmd->numPages = pRegion->u32NumPages;
    pu8Cmd += sizeof(*pCmd);
    }

    pu32CmdId = (uint32_t *)pu8Cmd;
    *pu32CmdId = SVGA_CMD_REMAP_GMR2;
    pu8Cmd += sizeof(*pu32CmdId);

    {
    SVGAFifoCmdRemapGMR2 *pCmd = (SVGAFifoCmdRemapGMR2 *)pu8Cmd;
    pCmd->gmrId = VMSVGAMOB_ID(pRegion->pMob);
    pCmd->flags = SVGA_REMAP_GMR2_PPN64;
    pCmd->offsetPages = 0;
    pCmd->numPages = pRegion->u32NumPages;
    pu8Cmd += sizeof(*pCmd);
    }

    uint64_t *paPPN64 = (uint64_t *)pu8Cmd;
    for (uint32_t iPage = 0; iPage < pRegion->u32NumPages; ++iPage)
    {
        RTHCPHYS const Phys = pRegion->aPhys[iPage];
        paPPN64[iPage] = Phys >> PAGE_SHIFT;
    }
    pu8Cmd += cbPPNArray;

    if (RT_BOOL(pSvga->u32Caps & SVGA_CAP_DX))
    {
        SVGA3dCmdHeader *pHdr;

        pHdr = (SVGA3dCmdHeader *)pu8Cmd;
        pHdr->id   = SVGA_3D_CMD_DEFINE_GB_MOB64;
        pHdr->size = sizeof(SVGA3dCmdDefineGBMob64);
        pu8Cmd += sizeof(*pHdr);

        {
        SVGA3dCmdDefineGBMob64 *pCmd = (SVGA3dCmdDefineGBMob64 *)pu8Cmd;
        pCmd->mobid       = VMSVGAMOB_ID(pRegion->pMob);
        pCmd->ptDepth     = pRegion->pMob->gbo.enmMobFormat;
        pCmd->base        = pRegion->pMob->gbo.base;
        pCmd->sizeInBytes = pRegion->pMob->gbo.cbGbo;
        pu8Cmd += sizeof(*pCmd);
        }
    }

    Assert((uintptr_t)pu8Cmd - (uintptr_t)pvCmd == cbSubmit);
    SvgaCommit(pSvga, (uintptr_t)pu8Cmd - (uintptr_t)pvCmd);

    return STATUS_SUCCESS;
}

/* Destroy exising region.
 */
static NTSTATUS gmrDestroy(VBOXWDDM_EXT_VMSVGA *pSvga, GAWDDMREGION *pRegion)
{
    AssertReturn(pRegion, STATUS_INVALID_PARAMETER);

    /* Mapping must be freed prior to the mob destruction. Otherwise, due to a race condition,
     * SvgaMobFree could free the mapping in a system worker thread after DPC, which would not
     * work obviously, because the mapping was created for another process.
     */
    if (pRegion->MapObjR3 != NIL_RTR0MEMOBJ)
    {
        int rc = RTR0MemObjFree(pRegion->MapObjR3, false);
        AssertRC(rc);
        pRegion->MapObjR3 = NIL_RTR0MEMOBJ;
    }

    /* Issue commands to delete the gmr. */
    uint32_t cbRequired = 0;
    SvgaMobDestroy(pSvga, pRegion->pMob, NULL, 0, &cbRequired);
    cbRequired += sizeof(uint32_t) + sizeof(SVGAFifoCmdDefineGMR2);

    void *pvCmd = SvgaCmdBufReserve(pSvga, cbRequired, SVGA3D_INVALID_ID);
    AssertReturn(pvCmd, STATUS_INSUFFICIENT_RESOURCES);

    uint8_t *pu8Cmd = (uint8_t *)pvCmd;
    uint32_t *pu32CmdId;

    /* Undefine GMR: SVGA_CMD_DEFINE_GMR2 with numPages = 0 */
    pu32CmdId = (uint32_t *)pu8Cmd;
    *pu32CmdId = SVGA_CMD_DEFINE_GMR2;
    pu8Cmd += sizeof(*pu32CmdId);

    SVGAFifoCmdDefineGMR2 *pCmd = (SVGAFifoCmdDefineGMR2 *)pu8Cmd;
    pCmd->gmrId = VMSVGAMOB_ID(pRegion->pMob);
    pCmd->numPages = 0;
    pu8Cmd += sizeof(*pCmd);

    uint32_t cbCmd = 0;
    NTSTATUS Status = SvgaMobDestroy(pSvga, pRegion->pMob, pu8Cmd,
                                     cbRequired - ((uintptr_t)pu8Cmd - (uintptr_t)pvCmd),
                                     &cbCmd);
    AssertReturn(NT_SUCCESS(Status), Status);
    pu8Cmd += cbCmd;

    Assert(((uintptr_t)pu8Cmd - (uintptr_t)pvCmd) == cbRequired);
    SvgaCmdBufCommit(pSvga, ((uintptr_t)pu8Cmd - (uintptr_t)pvCmd));

    /* The mob will be deleted in DPC routine after host reports completion of the above commands. */
    pRegion->pMob = NULL;

#ifdef DEBUG
    ASMAtomicDecU32(&pSvga->cAllocatedGmrs);
    ASMAtomicSubU32(&pSvga->cAllocatedGmrPages, pRegion->u32NumPages);
#endif
    GaMemFree(pRegion);
    return STATUS_SUCCESS;
}

typedef struct GAREGIONSDESTROYCTX
{
    void *pvOwner;
    uint32_t cMaxIds;
    uint32_t cIds;
    uint32_t au32Ids[1]; /* [cMaxIds] */
} GAREGIONSDESTROYCTX;

static DECLCALLBACK(int) gaRegionsDestroyCb(PAVLU32NODECORE pNode, void *pvUser)
{
    GAWDDMREGION *pRegion = (GAWDDMREGION *)pNode;
    GAREGIONSDESTROYCTX *pCtx = (GAREGIONSDESTROYCTX *)pvUser;

    if (   pCtx->pvOwner == NULL
        || (uintptr_t)pCtx->pvOwner == (uintptr_t)pRegion->pvOwner)
    {
        AssertReturn(pCtx->cIds < pCtx->cMaxIds, -1);
        pCtx->au32Ids[pCtx->cIds++] = VMSVGAMOB_ID(pRegion->pMob);
    }
    return 0;
}

/* Destroy all regions of a particular owner.
 */
void SvgaRegionsDestroy(VBOXWDDM_EXT_VMSVGA *pSvga,
                        void *pvOwner)
{
    const uint32_t cbCtx = RT_UOFFSETOF(GAREGIONSDESTROYCTX, au32Ids) + pSvga->u32GmrMaxIds * sizeof(uint32_t);
    GAREGIONSDESTROYCTX *pCtx = (GAREGIONSDESTROYCTX *)GaMemAlloc(cbCtx);
    AssertReturnVoid(pCtx);

    pCtx->pvOwner = pvOwner;
    pCtx->cMaxIds = pSvga->u32GmrMaxIds;
    pCtx->cIds = 0;

    ExAcquireFastMutex(&pSvga->SvgaMutex);
    /* Fetch GMR ids associated with this device. */
    RTAvlU32DoWithAll(&pSvga->GMRTree, 0, gaRegionsDestroyCb, pCtx);
    ExReleaseFastMutex(&pSvga->SvgaMutex);

    /* Free all found GMRs. */
    for (uint32_t i = 0; i < pCtx->cIds; i++)
    {
        ExAcquireFastMutex(&pSvga->SvgaMutex);
        GAWDDMREGION *pRegion = (GAWDDMREGION *)RTAvlU32Remove(&pSvga->GMRTree, pCtx->au32Ids[i]);
        ExReleaseFastMutex(&pSvga->SvgaMutex);

        if (pRegion)
        {
            Assert(VMSVGAMOB_ID(pRegion->pMob) == pCtx->au32Ids[i]);
            GALOG(("Deallocate gmrId %d, pv %p, aPhys[0] %RHp\n",
                   VMSVGAMOB_ID(pRegion->pMob), pRegion->pvR3, pRegion->aPhys[0]));

            gmrDestroy(pSvga, pRegion);
        }
    }

    GaMemFree(pCtx);
}

NTSTATUS SvgaRegionDestroy(VBOXWDDM_EXT_VMSVGA *pSvga,
                           uint32_t u32GmrId)
{
    AssertReturn(u32GmrId <= pSvga->u32GmrMaxIds, STATUS_INVALID_PARAMETER);

    GALOG(("[%p] gmrId %d\n", pSvga, u32GmrId));

    ExAcquireFastMutex(&pSvga->SvgaMutex);

    GAWDDMREGION *pRegion = (GAWDDMREGION *)RTAvlU32Remove(&pSvga->GMRTree, u32GmrId);

    ExReleaseFastMutex(&pSvga->SvgaMutex);

    AssertReturn(pRegion, STATUS_INVALID_PARAMETER);

    Assert(VMSVGAMOB_ID(pRegion->pMob) == u32GmrId);
    GALOG(("Freed gmrId %d, pv %p, aPhys[0] %RHp\n",
           VMSVGAMOB_ID(pRegion->pMob), pRegion->pvR3, pRegion->aPhys[0]));

    return gmrDestroy(pSvga, pRegion);
}

NTSTATUS SvgaRegionUserAddressAndSize(VBOXWDDM_EXT_VMSVGA *pSvga,
                                      uint32_t u32GmrId,
                                      uint64_t *pu64UserAddress,
                                      uint32_t *pu32Size)
{
    AssertReturn(u32GmrId <= pSvga->u32GmrMaxIds, STATUS_INVALID_PARAMETER);

    GALOG(("[%p] gmrId %d\n", pSvga, u32GmrId));

    ExAcquireFastMutex(&pSvga->SvgaMutex);

    GAWDDMREGION *pRegion = (GAWDDMREGION *)RTAvlU32Get(&pSvga->GMRTree, u32GmrId);

    ExReleaseFastMutex(&pSvga->SvgaMutex);

    AssertReturn(pRegion, STATUS_INVALID_PARAMETER);

    Assert(VMSVGAMOB_ID(pRegion->pMob) == u32GmrId);
    GALOG(("Get gmrId %d, UserAddress 0x%p\n",
           VMSVGAMOB_ID(pRegion->pMob), pRegion->pvR3));
    *pu64UserAddress = (uintptr_t)pRegion->pvR3;
    *pu32Size = pRegion->u32NumPages * PAGE_SIZE;
    return STATUS_SUCCESS;
}

NTSTATUS SvgaRegionCreate(VBOXWDDM_EXT_VMSVGA *pSvga,
                          void *pvOwner,
                          uint32_t u32NumPages,
                          uint32_t *pu32GmrId,
                          uint64_t *pu64UserAddress)
{
    AssertReturn(u32NumPages > 0 && u32NumPages <= pSvga->u32GmrMaxPages, STATUS_INVALID_PARAMETER);

    GALOG(("[%p] %d pages\n", pSvga, u32NumPages));

    NTSTATUS Status;

    uint32_t const cbAlloc = RT_UOFFSETOF(GAWDDMREGION, aPhys) + u32NumPages * sizeof(RTHCPHYS);
    GAWDDMREGION *pRegion = (GAWDDMREGION *)GaMemAllocZero(cbAlloc);
    if (pRegion)
    {
        /* Region id and VGPU10+ mobid are the same. So a mob is always allocated for the gmr.
         * The mob provides an id and, if SVGA_CAP_DX is available, is reported to the host on VGPU10.
         *
         * Allocate memory and init pRegion fields.
         */
        Status = gmrInit(pSvga, pRegion, pvOwner, u32NumPages);
        Assert(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
        {
            if (VMSVGAMOB_ID(pRegion->pMob) < pSvga->u32GmrMaxIds)
            {
                GALOG(("Allocated gmrId %d, pv %p, aPhys[0] %RHp\n",
                       VMSVGAMOB_ID(pRegion->pMob), pRegion->pvR3, pRegion->aPhys[0]));

                Status = gmrReportToHost(pSvga, pRegion);
                Assert(NT_SUCCESS(Status));
                if (NT_SUCCESS(Status))
                {
                    pRegion->Core.Key = VMSVGAMOB_ID(pRegion->pMob);

                    ExAcquireFastMutex(&pSvga->SvgaMutex);
                    RTAvlU32Insert(&pSvga->GMRTree, &pRegion->Core);
                    ExReleaseFastMutex(&pSvga->SvgaMutex);

                    *pu32GmrId = VMSVGAMOB_ID(pRegion->pMob);
                    *pu64UserAddress = (uint64_t)pRegion->pvR3;

#ifdef DEBUG
                    ASMAtomicIncU32(&pSvga->cAllocatedGmrs);
                    ASMAtomicAddU32(&pSvga->cAllocatedGmrPages, pRegion->u32NumPages);
#endif
                    /* Everything OK. */
                    return STATUS_SUCCESS;
                }
            }
            else
                AssertFailedStmt(Status = STATUS_INSUFFICIENT_RESOURCES);
        }

        /* Clean up on failure. */
        if (pRegion->MapObjR3 != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pRegion->MapObjR3, false);
            AssertRC(rc);
            pRegion->MapObjR3 = NIL_RTR0MEMOBJ;
        }

        SvgaMobFree(pSvga, pRegion->pMob);
        pRegion->pMob = NULL;

        GaMemFree(pRegion);
    }
    else
        AssertFailedStmt(Status = STATUS_INSUFFICIENT_RESOURCES);

    return Status;
}

NTSTATUS SvgaDXContextCreate(PVBOXWDDM_EXT_VMSVGA pSvga,
                             uint32_t u32Cid)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /*
     * Issue SVGA_3D_CMD_DX_DEFINE_CONTEXT.
     */
    uint32_t cbSubmit =   sizeof(SVGA3dCmdHeader)
                        + sizeof(SVGA3dCmdDXDefineContext);
    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pvCmd;
        SVGA3dCmdDXDefineContext *pCommand = (SVGA3dCmdDXDefineContext *)&pHeader[1];

        pHeader->id = SVGA_3D_CMD_DX_DEFINE_CONTEXT;
        pHeader->size = sizeof(SVGA3dCmdDXDefineContext);
        pCommand->cid = u32Cid;

        SvgaCommit(pSvga, cbSubmit);
    }
    else
        Status = STATUS_INSUFFICIENT_RESOURCES;

    return Status;
}

NTSTATUS SvgaDXContextDestroy(PVBOXWDDM_EXT_VMSVGA pSvga,
                              uint32_t u32Cid)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /*
     * Issue SVGA_3D_CMD_DX_DESTROY_CONTEXT.
     */
    uint32_t cbSubmit =   sizeof(SVGA3dCmdHeader)
                        + sizeof(SVGA3dCmdDXDestroyContext);
    void *pvCmd = SvgaReserve(pSvga, cbSubmit);
    if (pvCmd)
    {
        SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pvCmd;
        SVGA3dCmdDXDestroyContext *pCommand = (SVGA3dCmdDXDestroyContext *)&pHeader[1];

        pHeader->id = SVGA_3D_CMD_DX_DESTROY_CONTEXT;
        pHeader->size = sizeof(SVGA3dCmdDXDestroyContext);
        pCommand->cid = u32Cid;

        SvgaCommit(pSvga, cbSubmit);
    }
    else
        Status = STATUS_INSUFFICIENT_RESOURCES;

    return Status;
}

/*
 *
 * Guest Backed Objects.
 *
 */

void SvgaGboFree(VMSVGAGBO *pGbo)
{
    if (pGbo->hMemObjPT != NIL_RTR0MEMOBJ)
    {
        int rc = RTR0MemObjFree(pGbo->hMemObjPT, true);
        AssertRC(rc);
        pGbo->hMemObjPT = NIL_RTR0MEMOBJ;
    }
    memset(pGbo, 0, sizeof(*pGbo));
}

NTSTATUS SvgaGboInit(VMSVGAGBO *pGbo, uint32_t cPages)
{
    /*
     * Calculate how many pages are needed to describe the gbo.
     * Use 64 bit mob format for 32 bit driver too in order to simplify the code for now.
     */
    uint32_t const cPageEntriesPerPage = PAGE_SIZE / sizeof(PPN64);
    if (cPages == 1)
    {
        pGbo->cPTPages = 0;
        pGbo->enmMobFormat = SVGA3D_MOBFMT_PTDEPTH64_0;
    }
    else if (cPages <= cPageEntriesPerPage)
    {
        pGbo->cPTPages = 1;
        pGbo->enmMobFormat = SVGA3D_MOBFMT_PTDEPTH64_1;
    }
    else if (cPages <= cPageEntriesPerPage * cPageEntriesPerPage)
    {
        uint32_t const cLevel1Pages =
            (cPages + cPageEntriesPerPage - 1) / cPageEntriesPerPage;
        pGbo->cPTPages = 1 + cLevel1Pages; /* One level 2 page and level 1 pages. */
        pGbo->enmMobFormat = SVGA3D_MOBFMT_PTDEPTH64_2;
    }
    else
        AssertFailedReturn(STATUS_INVALID_PARAMETER);

    if (pGbo->cPTPages)
    {
        int rc = RTR0MemObjAllocPageTag(&pGbo->hMemObjPT, pGbo->cPTPages * PAGE_SIZE,
                                        false /* executable R0 mapping */, "VMSVGAGBO");
        AssertRCReturn(rc, STATUS_INSUFFICIENT_RESOURCES);

        if (pGbo->enmMobFormat == SVGA3D_MOBFMT_PTDEPTH64_2)
        {
            /* Store the page numbers of level 1 pages into the level 2 page.
             * Skip the level 2 page at index 0.
             */
            PPN64 *paPpn = (PPN64 *)RTR0MemObjAddress(pGbo->hMemObjPT);
            for (unsigned i = 1; i < pGbo->cPTPages; ++i)
                paPpn[i - 1] = RTR0MemObjGetPagePhysAddr(pGbo->hMemObjPT, i) >> PAGE_SHIFT;
        }
    }
    else
        pGbo->hMemObjPT = NIL_RTR0MEMOBJ;

    pGbo->base = UINT64_C(~0); /* base will be assigned by SvgaGboFillPageTable* */
    pGbo->cbGbo = cPages << PAGE_SHIFT;
    return STATUS_SUCCESS;
}


NTSTATUS SvgaGboFillPageTableForMDL(PVMSVGAGBO pGbo,
                                    PMDL pMdl,
                                    uint32_t MdlOffset)
{
    PPFN_NUMBER paMdlPfn = &MmGetMdlPfnArray(pMdl)[MdlOffset];
    if (pGbo->enmMobFormat == SVGA3D_MOBFMT_PTDEPTH64_0)
        pGbo->base = paMdlPfn[0];
    else
    {
        /* The first of pages is alway the base. It is either the level 2 page or the single level 1 page */
        pGbo->base = RTR0MemObjGetPagePhysAddr(pGbo->hMemObjPT, 0) >> PAGE_SHIFT;

        PPN64 *paPpn = (PPN64 *)RTR0MemObjAddress(pGbo->hMemObjPT);
        PPN64 *paPpnMdlPfn;
        if (pGbo->enmMobFormat == SVGA3D_MOBFMT_PTDEPTH64_2)
            paPpnMdlPfn = &paPpn[PAGE_SIZE / sizeof(PPN64)]; /* Level 1 pages follow the level 2 page. */
        else if (pGbo->enmMobFormat == SVGA3D_MOBFMT_PTDEPTH64_1)
            paPpnMdlPfn = paPpn;
        else
            AssertFailedReturn(STATUS_INVALID_PARAMETER);

        /* Store Mdl page numbers into the level 1 description pages. */
        for (unsigned i = 0; i < pGbo->cbGbo >> PAGE_SHIFT; ++i)
            paPpnMdlPfn[i] = paMdlPfn[i];
    }
    return STATUS_SUCCESS;
}


NTSTATUS SvgaGboFillPageTableForMemObj(PVMSVGAGBO pGbo,
                                       RTR0MEMOBJ hMemObj)
{
    if (pGbo->enmMobFormat == SVGA3D_MOBFMT_PTDEPTH64_0)
        pGbo->base = RTR0MemObjGetPagePhysAddr(hMemObj, 0) >> PAGE_SHIFT;
    else
    {
        /* The first of pages is alway the base. It is either the level 2 page or the single level 1 page */
        pGbo->base = RTR0MemObjGetPagePhysAddr(pGbo->hMemObjPT, 0) >> PAGE_SHIFT;

        PPN64 *paPpn = (PPN64 *)RTR0MemObjAddress(pGbo->hMemObjPT);
        PPN64 *paPpnGbo;
        if (pGbo->enmMobFormat == SVGA3D_MOBFMT_PTDEPTH64_2)
            paPpnGbo = &paPpn[PAGE_SIZE / sizeof(PPN64)]; /* Level 1 pages follow the level 2 page. */
        else if (pGbo->enmMobFormat == SVGA3D_MOBFMT_PTDEPTH64_1)
            paPpnGbo = paPpn;
        else
            AssertFailedReturn(STATUS_INVALID_PARAMETER);

        /* Store page numbers into the level 1 description pages. */
        for (unsigned i = 0; i < pGbo->cbGbo >> PAGE_SHIFT; ++i)
            paPpnGbo[i] = RTR0MemObjGetPagePhysAddr(hMemObj, i) >> PAGE_SHIFT;
    }
    return STATUS_SUCCESS;
}


/*
 *
 * Memory OBjects.
 *
 */

static NTSTATUS svgaMobAlloc(VBOXWDDM_EXT_VMSVGA *pSvga,
                             PVMSVGAMOB *ppMob)
{
    GALOG(("[%p]\n", pSvga));

    NTSTATUS Status;
    AssertCompile(NIL_RTR0MEMOBJ == 0);

    *ppMob = (PVMSVGAMOB)GaMemAllocZero(sizeof(VMSVGAMOB));
    AssertReturn(*ppMob, STATUS_INSUFFICIENT_RESOURCES);

    Status = SvgaMobIdAlloc(pSvga, &VMSVGAMOB_ID(*ppMob));
    AssertReturnStmt(NT_SUCCESS(Status), GaMemFree(*ppMob), STATUS_INSUFFICIENT_RESOURCES);

    KIRQL OldIrql;
    KeAcquireSpinLock(&pSvga->MobSpinLock, &OldIrql);
    bool fInserted = RTAvlU32Insert(&pSvga->MobTree, &(*ppMob)->core);
    KeReleaseSpinLock(&pSvga->MobSpinLock, OldIrql);
    Assert(fInserted); RT_NOREF(fInserted);

    GALOG(("mobid = %u\n", VMSVGAMOB_ID(*ppMob)));
    return STATUS_SUCCESS;
}

void SvgaMobFree(VBOXWDDM_EXT_VMSVGA *pSvga,
                 PVMSVGAMOB pMob)
{
    GALOG(("[%p] %p\n", pSvga, pMob));

    if (pMob)
    {
        GALOG(("mobid = %u\n", VMSVGAMOB_ID(pMob)));

        KIRQL OldIrql;
        KeAcquireSpinLock(&pSvga->MobSpinLock, &OldIrql);
        RTAvlU32Remove(&pSvga->MobTree, pMob->core.Key);
        KeReleaseSpinLock(&pSvga->MobSpinLock, OldIrql);

#ifdef DEBUG
        ASMAtomicSubU32(&pSvga->cAllocatedMobPages, pMob->gbo.cbGbo / PAGE_SIZE);
        ASMAtomicDecU32(&pSvga->cAllocatedMobs);
#endif

        SvgaGboFree(&pMob->gbo);

        if (pMob->hMemObj != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pMob->hMemObj, true);
            AssertRC(rc);
            pMob->hMemObj = NIL_RTR0MEMOBJ;
        }

        NTSTATUS Status = SvgaMobIdFree(pSvga, VMSVGAMOB_ID(pMob));
        Assert(NT_SUCCESS(Status)); RT_NOREF(Status);
        GaMemFree(pMob);
    }
}

PVMSVGAMOB SvgaMobQuery(VBOXWDDM_EXT_VMSVGA *pSvga,
                        uint32_t mobid)
{
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSvga->MobSpinLock, &OldIrql);
    PVMSVGAMOB pMob = (PVMSVGAMOB)RTAvlU32Get(&pSvga->MobTree, mobid);
    KeReleaseSpinLock(&pSvga->MobSpinLock, OldIrql);

    GALOG(("[%p] mobid = %u -> %p\n", pSvga, mobid, pMob));
    return pMob;
}


static NTSTATUS svgaMobCreate(VBOXWDDM_EXT_VMSVGA *pSvga,
                              PVMSVGAMOB *ppMob,
                              uint32_t cMobPages,
                              HANDLE hAllocation)
{
    PVMSVGAMOB pMob;
    NTSTATUS Status = svgaMobAlloc(pSvga, &pMob);
    AssertReturn(NT_SUCCESS(Status), Status);

    Status = SvgaGboInit(&pMob->gbo, cMobPages);
    AssertReturnStmt(NT_SUCCESS(Status), SvgaMobFree(pSvga, pMob), Status);

    pMob->hAllocation = hAllocation;
    *ppMob = pMob;

#ifdef DEBUG
    ASMAtomicIncU32(&pSvga->cAllocatedMobs);
    ASMAtomicAddU32(&pSvga->cAllocatedMobPages, cMobPages);
#endif

    return STATUS_SUCCESS;
}


NTSTATUS SvgaMobCreate(VBOXWDDM_EXT_VMSVGA *pSvga,
                       PVMSVGAMOB *ppMob,
                       uint32_t cMobPages,
                       HANDLE hAllocation)
{
    /* This function also performs one time initialization of miniport MOB. */
    if (RT_LIKELY(pSvga->pMiniportMob != NULL))
    { /* likely */ }
    else
    {
        NTSTATUS Status = svgaMobCreate(pSvga, &pSvga->pMiniportMob, (uint32_t)(RTR0MemObjSize(pSvga->hMemObjMiniportMob) / PAGE_SIZE), 0);
        if (NT_SUCCESS(Status))
        {
            Status = SvgaMobSetMemObj(pSvga->pMiniportMob, pSvga->hMemObjMiniportMob);
            if (NT_SUCCESS(Status))
            {
                void *pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_DEFINE_GB_MOB64, sizeof(SVGA3dCmdDefineGBMob64), SVGA3D_INVALID_ID);
                if (pvCmd)
                {
                    SVGA3dCmdDefineGBMob64 *pCmd = (SVGA3dCmdDefineGBMob64 *)pvCmd;
                    pCmd->mobid       = VMSVGAMOB_ID(pSvga->pMiniportMob);
                    pCmd->ptDepth     = pSvga->pMiniportMob->gbo.enmMobFormat;
                    pCmd->base        = pSvga->pMiniportMob->gbo.base;
                    pCmd->sizeInBytes = pSvga->pMiniportMob->gbo.cbGbo;
                    SvgaCmdBufCommit(pSvga, sizeof(*pCmd));

                }
                else
                    AssertFailedReturn(STATUS_INSUFFICIENT_RESOURCES);
            }
            else
                AssertFailedReturn(Status);
        }
        else
            AssertFailedReturn(Status);
    }

    return svgaMobCreate(pSvga, ppMob, cMobPages, hAllocation);
}

NTSTATUS SvgaMobSetMemObj(PVMSVGAMOB pMob,
                          RTR0MEMOBJ hMemObj)
{
    NTSTATUS Status = SvgaGboFillPageTableForMemObj(&pMob->gbo, hMemObj);
    if (NT_SUCCESS(Status))
        pMob->hMemObj = hMemObj;
    return Status;
}


NTSTATUS SvgaCOTNotifyId(VBOXWDDM_EXT_VMSVGA *pSvga,
                         PVMSVGACONTEXT pSvgaContext,
                         SVGACOTableType enmType,
                         uint32_t id)
{
    uint32_t idxCOTable;
    if (enmType < SVGA_COTABLE_MAX)
        idxCOTable = enmType;
    else if (enmType >= VBSVGA_COTABLE_MIN && enmType < VBSVGA_COTABLE_MAX)
        idxCOTable = SVGA_COTABLE_MAX + (enmType - VBSVGA_COTABLE_MIN);
    else
        AssertFailedReturn(STATUS_INVALID_PARAMETER);

    PVMSVGACOT pCOT = &pSvgaContext->aCOT[idxCOTable];

    if (id < pCOT->cEntries)
        return STATUS_SUCCESS; /* Still large enough. */

    AssertReturn(id < SVGA_COTABLE_MAX_IDS, STATUS_INVALID_PARAMETER);

    /* Allocate a new larger mob and inform the host. */
    static uint32_t const s_acbEntry[] =
    {
        sizeof(SVGACOTableDXRTViewEntry),
        sizeof(SVGACOTableDXDSViewEntry),
        sizeof(SVGACOTableDXSRViewEntry),
        sizeof(SVGACOTableDXElementLayoutEntry),
        sizeof(SVGACOTableDXBlendStateEntry),
        sizeof(SVGACOTableDXDepthStencilEntry),
        sizeof(SVGACOTableDXRasterizerStateEntry),
        sizeof(SVGACOTableDXSamplerEntry),
        sizeof(SVGACOTableDXStreamOutputEntry),
        sizeof(SVGACOTableDXQueryEntry),
        sizeof(SVGACOTableDXShaderEntry),
        sizeof(SVGACOTableDXUAViewEntry),
        /* VirtualBox Context Object Tables */
        sizeof(VBSVGACOTableDXVideoProcessorEntry),
        sizeof(VBSVGACOTableDXVideoDecoderOutputViewEntry),
        sizeof(VBSVGACOTableDXVideoDecoderEntry),
        sizeof(VBSVGACOTableDXVideoProcessorInputViewEntry),
        sizeof(VBSVGACOTableDXVideoProcessorOutputViewEntry),
    };
    AssertCompile(RT_ELEMENTS(pSvgaContext->aCOT) == RT_ELEMENTS(s_acbEntry));

    uint32_t cbRequired = (id + 1) * s_acbEntry[idxCOTable];
    cbRequired = RT_ALIGN_32(cbRequired, PAGE_SIZE);

    /* Try to double the current size. */
    uint32_t cbCOT = pCOT->cEntries ? pCOT->cEntries * s_acbEntry[idxCOTable] : PAGE_SIZE;
    while (cbRequired > cbCOT)
        cbCOT *= 2;

    /* Allocate pages for the new COTable. */
    RTR0MEMOBJ hMemObjCOT;
    int rc = RTR0MemObjAllocPageTag(&hMemObjCOT, cbCOT, false /* executable R0 mapping */, "VMSVGACOT");
    AssertRCReturn(rc, STATUS_INSUFFICIENT_RESOURCES);

    /* Allocate a new mob. */
    PVMSVGAMOB pMob;
    NTSTATUS Status = SvgaMobCreate(pSvga, &pMob, cbCOT >> PAGE_SHIFT, 0);
    AssertReturnStmt(NT_SUCCESS(Status),
                     RTR0MemObjFree(hMemObjCOT, true),
                     Status);

    Status = SvgaMobSetMemObj(pMob, hMemObjCOT);
    AssertReturnStmt(NT_SUCCESS(Status),
                     SvgaMobFree(pSvga, pMob); RTR0MemObjFree(hMemObjCOT, true),
                     Status);

    /* Emit commands. */
    void *pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_DEFINE_GB_MOB64, sizeof(SVGA3dCmdDefineGBMob64), SVGA3D_INVALID_ID);
    if (pvCmd)
    {
        SVGA3dCmdDefineGBMob64 *pCmd = (SVGA3dCmdDefineGBMob64 *)pvCmd;
        pCmd->mobid       = VMSVGAMOB_ID(pMob);
        pCmd->ptDepth     = pMob->gbo.enmMobFormat;
        pCmd->base        = pMob->gbo.base;
        pCmd->sizeInBytes = pMob->gbo.cbGbo;
        SvgaCmdBufCommit(pSvga, sizeof(*pCmd));
    }
    else
        AssertFailedReturnStmt(SvgaMobFree(pSvga, pMob); RTR0MemObjFree(hMemObjCOT, true),
                               STATUS_INSUFFICIENT_RESOURCES);

    if (pCOT->cEntries == 0)
    {
        /* Set the mob for COTable. */
        pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_DX_SET_COTABLE, sizeof(SVGA3dCmdDXSetCOTable), SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SVGA3dCmdDXSetCOTable *pCmd = (SVGA3dCmdDXSetCOTable *)pvCmd;
            pCmd->cid              = pSvgaContext->u32Cid;
            pCmd->mobid            = VMSVGAMOB_ID(pMob);
            pCmd->type             = enmType;
            pCmd->validSizeInBytes = pCOT->cEntries * s_acbEntry[idxCOTable];
            SvgaCmdBufCommit(pSvga, sizeof(*pCmd));
        }
        else
            AssertFailedReturnStmt(SvgaMobFree(pSvga, pMob); RTR0MemObjFree(hMemObjCOT, true),
                                   STATUS_INSUFFICIENT_RESOURCES);
    }
    else
    {
        /* Grow COTable and delete old mob. */
        pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_DX_GROW_COTABLE, sizeof(SVGA3dCmdDXGrowCOTable), SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SVGA3dCmdDXGrowCOTable *pCmd = (SVGA3dCmdDXGrowCOTable *)pvCmd;
            pCmd->cid              = pSvgaContext->u32Cid;
            pCmd->mobid            = VMSVGAMOB_ID(pMob);
            pCmd->type             = enmType;
            pCmd->validSizeInBytes = pCOT->cEntries * s_acbEntry[idxCOTable];
            SvgaCmdBufCommit(pSvga, sizeof(*pCmd));
        }
        else
            AssertFailedReturnStmt(SvgaMobFree(pSvga, pMob); RTR0MemObjFree(hMemObjCOT, true),
                                   STATUS_INSUFFICIENT_RESOURCES);

        uint32_t cbCmdRequired = 0;
        SvgaMobDestroy(pSvga, pCOT->pMob, NULL, 0, &cbCmdRequired);
        pvCmd = SvgaCmdBufReserve(pSvga, cbCmdRequired, SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SvgaMobDestroy(pSvga, pCOT->pMob, pvCmd, cbCmdRequired, &cbCmdRequired);
            SvgaCmdBufCommit(pSvga, cbCmdRequired);
        }

        pCOT->pMob = NULL;
    }

    SvgaCmdBufFlush(pSvga);

    pCOT->pMob = pMob;
    pCOT->cEntries = cbCOT / s_acbEntry[idxCOTable];

    return STATUS_SUCCESS;
}



/*
 * Place mob destruction commands into the buffer and add the mob to the deferred destruction list.
 *
 * Makes sure that the MOB, in particular the mobid, is deallocated by the guest after the MOB deletion
 * has been completed by the host.
 *
 * SVGA_3D_CMD_DESTROY_GB_MOB can be submitted to the host either in the miniport command buffer
 * (VMSVGACBSTATE::pCBCurrent) or in a paging buffer due to DXGK_OPERATION_UNMAP_APERTURE_SEGMENT operation.
 * These two ways are not synchronized. Therefore it is possible that the guest deletes a mob for an aperture segment
 * in a paging buffer then allocates the same mobid and sends SVGA_3D_CMD_DEFINE_GB_MOB64 to the host for a COTable
 * before the paging buffer is sent to the host.
 *
 * The driver uses SVGA_3D_CMD_DX_MOB_FENCE_64 command to notify the driver that the host had deleted a mob
 * and frees deleted mobs in the DPC routine
 */
NTSTATUS SvgaMobDestroy(VBOXWDDM_EXT_VMSVGA *pSvga,
                        PVMSVGAMOB pMob,
                        void *pvCmd,
                        uint32_t cbReserved,
                        uint32_t *pcbCmd)
{
    uint32_t cbRequired = sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdDestroyGBMob)
                        + sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdDXMobFence64);

    *pcbCmd = cbRequired;
    if (cbReserved < cbRequired)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    uint8_t *pu8Cmd = (uint8_t *)pvCmd;
    SVGA3dCmdHeader *pHdr;

    pHdr = (SVGA3dCmdHeader *)pu8Cmd;
    pHdr->id   = SVGA_3D_CMD_DESTROY_GB_MOB;
    pHdr->size = sizeof(SVGA3dCmdDestroyGBMob);
    pu8Cmd += sizeof(*pHdr);

    {
    SVGA3dCmdDestroyGBMob *pCmd = (SVGA3dCmdDestroyGBMob *)pu8Cmd;
    pCmd->mobid = VMSVGAMOB_ID(pMob);
    pu8Cmd += sizeof(*pCmd);
    }

    pMob->u64MobFence = ASMAtomicIncU64(&pSvga->u64MobFence);

    pHdr = (SVGA3dCmdHeader *)pu8Cmd;
    pHdr->id   = SVGA_3D_CMD_DX_MOB_FENCE_64;
    pHdr->size = sizeof(SVGA3dCmdDXMobFence64);
    pu8Cmd += sizeof(*pHdr);

    {
    SVGA3dCmdDXMobFence64 *pCmd = (SVGA3dCmdDXMobFence64 *)pu8Cmd;
    pCmd->value = pMob->u64MobFence;
    pCmd->mobId = VMSVGAMOB_ID(pSvga->pMiniportMob);
    pCmd->mobOffset = RT_OFFSETOF(VMSVGAMINIPORTMOB, u64MobFence);
    pu8Cmd += sizeof(*pCmd);
    }

    /* Add the mob to the deferred destruction queue. */
    KIRQL OldIrql;
    SvgaHostObjectsLock(pSvga, &OldIrql);
    RTListAppend(&pSvga->listMobDeferredDestruction, &pMob->node);
    SvgaHostObjectsUnlock(pSvga, OldIrql);

    Assert((uintptr_t)pu8Cmd - (uintptr_t)pvCmd == cbRequired);

    return STATUS_SUCCESS;
}
