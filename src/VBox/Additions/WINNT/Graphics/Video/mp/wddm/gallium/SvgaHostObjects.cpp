/* $Id: SvgaHostObjects.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - VMSVGA host object accounting.
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

#define GALOG_GROUP GALOG_GROUP_HOSTOBJECTS

#include "Svga.h"
#include "SvgaFifo.h"

#include <iprt/asm.h>

/*
 * Host objects are resources which created or allocated by the guest on the host.
 *
 * The purpose of the host objects is to:
 * 1) make sure that a host resource is not deallocated by the driver
 *    while it is still being used by the guest.
 * 2) store additional information about an object, for example a shared sid
 *    for the surfaces.
 *
 * Currently this applies only to the SVGA surfaces. The user mode driver can
 * submit a command buffer which use a surface and then delete the surface, because
 * the surface is not needed anymore.
 * The miniport driver check command buffers and adds a reference for each surface.
 * When a surface is deleted it will still be referenced by the command buffer and
 * will be deleted only when the buffer is processed by the host.
 */

/** Return a host object with the given key.
 *
 * @param pSvga    Device instance.
 * @param pAvlTree Map between the keys and the host object pointers.
 * @param u32Key   Host object key.
 * @return The pointer to the host object with increased reference counter.
 */
static SVGAHOSTOBJECT *svgaHostObjectQuery(VBOXWDDM_EXT_VMSVGA *pSvga,
                                           AVLU32TREE *pAvlTree,
                                           uint32_t u32Key)
{
    KIRQL OldIrql;
    SvgaHostObjectsLock(pSvga, &OldIrql);
    SVGAHOSTOBJECT *pHO = (SVGAHOSTOBJECT *)RTAvlU32Get(pAvlTree, u32Key);
    if (pHO)
       ASMAtomicIncU32(&pHO->cRefs);
    SvgaHostObjectsUnlock(pSvga, OldIrql);
    return pHO;
}

/** Release a host object and delete it if the reference counter goes to zero.
 *
 * @param pHO      Host object which reference counter is to be decreased.
 * @param pAvlTree Map between the keys and the host object pointers.
 * @return NT status.
 */
static NTSTATUS svgaHostObjectRelease(SVGAHOSTOBJECT *pHO,
                                      AVLU32TREE *pAvlTree)
{
    uint32_t const c = ASMAtomicDecU32(&pHO->cRefs);
    if (c > 0)
    {
        /* Do not delete the object. */
        return STATUS_SUCCESS;
    }

    /* Delete the object. */
    VBOXWDDM_EXT_VMSVGA *pSvga = pHO->pSvga;
    uint32_t const u32Key = pHO->u.avl.core.Key;

    KIRQL OldIrql;
    SvgaHostObjectsLock(pSvga, &OldIrql);

    SVGAHOSTOBJECT *pHORemoved = (SVGAHOSTOBJECT *)RTAvlU32Remove(pAvlTree, u32Key);

    SvgaHostObjectsUnlock(pSvga, OldIrql);

    NTSTATUS Status;
    if (pHORemoved == pHO)
    {
        if (KeGetCurrentIrql() <= APC_LEVEL)
        {
            /* Need to write to the FIFO which uses FastMutex, i.e. incompatible with DISPATCH_LEVEL+. */
            Status = pHO->pfnHostObjectDestroy
                   ? pHO->pfnHostObjectDestroy(pHO)
                   : STATUS_SUCCESS;
            GaMemFree(pHO);
        }
        else
        {
            /* This can (rarely) happen if the Dpc routine deletes a surface via SvgaRenderComplete */
            SvgaHostObjectsLock(pSvga, &OldIrql);

            pHO->u.list.u32Key = pHO->u.avl.core.Key;
            RTListAppend(&pSvga->DeletedHostObjectsList, &pHO->u.list.node);

            SvgaHostObjectsUnlock(pSvga, OldIrql);

            GALOG(("Pending object sid=%u\n", pHO->u.list.u32Key));
            Status = STATUS_SUCCESS;
        }
    }
    else
    {
        AssertFailed();
        Status = STATUS_INVALID_PARAMETER; /* Internal error. Should never happen. */
    }
    return Status;
}

/** Release a host object with the specified key.
 *
 * @param pSvga    Device instance.
 * @param pAvlTree Map between the keys and the host object pointers.
 * @param u32Key   Host object key.
 * @return NT status.
 */
static NTSTATUS svgaHostObjectUnref(VBOXWDDM_EXT_VMSVGA *pSvga,
                                    AVLU32TREE *pAvlTree,
                                    uint32_t u32Key)
{
    SVGAHOSTOBJECT *pHO = svgaHostObjectQuery(pSvga, pAvlTree, u32Key);
    AssertPtrReturn(pHO, STATUS_INVALID_PARAMETER);
    ASMAtomicDecU32(&pHO->cRefs); /* Undo svgaHostObjectQuery */
    return svgaHostObjectRelease(pHO, pAvlTree);
}

/** Destroy the deleted objects which could not be destroyed at DISPATCH_LEVEL.
 *
 * @param pSvga    Device instance.
 * @return NT status.
 */
static NTSTATUS svgaHostObjectsProcessPending(VBOXWDDM_EXT_VMSVGA *pSvga)
{
    KIRQL OldIrql;
    SvgaHostObjectsLock(pSvga, &OldIrql);

    RTLISTANCHOR DeletedHostObjectsList;
    RTListMove(&DeletedHostObjectsList, &pSvga->DeletedHostObjectsList);

    SvgaHostObjectsUnlock(pSvga, OldIrql);

    if (RTListIsEmpty(&DeletedHostObjectsList)) /* likely */
        return STATUS_SUCCESS;

    GALOG(("Deleting pending objects\n"));

    SVGAHOSTOBJECT *pIter, *pNext;
    RTListForEachSafe(&DeletedHostObjectsList, pIter, pNext, SVGAHOSTOBJECT, u.list.node)
    {
        int32_t const c = pIter->cRefs;
        if (c == 0)
        {
            if (pIter->pfnHostObjectDestroy)
                pIter->pfnHostObjectDestroy(pIter);
            GaMemFree(pIter);
        }
        else
        {
            GALOGREL(32, ("WDDM: Deleted host object in use: cRefs %d, Key %u\n", c, pIter->u.list.u32Key));
            AssertFailed();
        }
    }

    GALOG(("Deleting pending objects done\n"));
    return STATUS_SUCCESS;
}

/** Initialize a host object.
*
 * Init the fields and add the object to the AVL tree.
 *
 * @param pSvga    Device instance.
 * @param pHO      New host object.
 * @param uType    What kind of an object.
 * @param u32Key   Host object key.
 * @param pfnHostObjectDestroy Destructor.
 * @return NT status.
 */
static NTSTATUS svgaHostObjectInit(VBOXWDDM_EXT_VMSVGA *pSvga,
                                   SVGAHOSTOBJECT *pHO,
                                   uint32_t uType,
                                   uint32_t u32Key,
                                   PFNHostObjectDestroy pfnHostObjectDestroy)
{
    AVLU32TREE *pAvlTree;
    switch (uType)
    {
        case SVGA_HOST_OBJECT_SURFACE: pAvlTree = &pSvga->SurfaceTree; break;
        default:
            AssertFailed();
            return STATUS_INVALID_PARAMETER;
    }

    pHO->u.avl.core.Key       = u32Key;
    pHO->cRefs                = 1;
    pHO->uType                = uType;
    pHO->pSvga                = pSvga;
    pHO->pfnHostObjectDestroy = pfnHostObjectDestroy;

    KIRQL OldIrql;
    SvgaHostObjectsLock(pSvga, &OldIrql);

    if (RTAvlU32Insert(pAvlTree, &pHO->u.avl.core))
    {
        SvgaHostObjectsUnlock(pSvga, OldIrql);
        return STATUS_SUCCESS;
    }

    SvgaHostObjectsUnlock(pSvga, OldIrql);
    return STATUS_NOT_SUPPORTED;
}

static DECLCALLBACK(int) svgaHostObjectsDestroyCb(PAVLU32NODECORE pNode, void *pvUser)
{
    RT_NOREF(pvUser);

    SVGAHOSTOBJECT *pHO = (SVGAHOSTOBJECT *)pNode;
    if (pHO->pfnHostObjectDestroy)
        pHO->pfnHostObjectDestroy(pHO);
    GaMemFree(pHO);

    return 0;
}

NTSTATUS SvgaHostObjectsCleanup(VBOXWDDM_EXT_VMSVGA *pSvga)
{
    if (pSvga->SurfaceTree)
    {
        RTAvlU32Destroy(&pSvga->SurfaceTree, svgaHostObjectsDestroyCb, pSvga);
    }

    return svgaHostObjectsProcessPending(pSvga);
}


/*
 * SVGA surfaces.
 */

/** Surface object destructor.
 *
 * @param pHO      The corresponding host object.
 * @return NT status.
 */
static DECLCALLBACK(NTSTATUS) svgaSurfaceObjectDestroy(SVGAHOSTOBJECT *pHO)
{
    AssertPtrReturn(pHO, STATUS_INVALID_PARAMETER);

    VBOXWDDM_EXT_VMSVGA *pSvga = pHO->pSvga;
    uint32_t const u32Sid = pHO->u.avl.core.Key;

    SURFACEOBJECT *pSO = (SURFACEOBJECT *)pHO;

    /* Delete the surface. */
    GALOG(("deleted sid=%u\n", u32Sid));

    if (pSO->mobid != SVGA3D_INVALID_ID)
    {
        void *pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_BIND_GB_SURFACE, sizeof(SVGA3dCmdBindGBSurface), SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pvCmd;
            pCmd->sid = u32Sid;
            pCmd->mobid = SVGA3D_INVALID_ID;
            SvgaCmdBufCommit(pSvga, sizeof(SVGA3dCmdBindGBSurface));
        }
    }

    NTSTATUS Status = SvgaSurfaceDestroy(pSvga, u32Sid);
    if (NT_SUCCESS(Status))
    {
        /* Do not free the id, if the host surface deletion has failed. */
        SvgaSurfaceIdFree(pSvga, u32Sid);
    }

    return Status;
}

/** Return a SVGA surface object with the given surface id.
 *
 * @param pSvga    The device instance.
 * @param u32Sid   The surface id.
 * @return The pointer to the surface object with increased reference counter.
 */
SURFACEOBJECT *SvgaSurfaceObjectQuery(VBOXWDDM_EXT_VMSVGA *pSvga,
                                      uint32_t u32Sid)
{
    return (SURFACEOBJECT *)svgaHostObjectQuery(pSvga, &pSvga->SurfaceTree, u32Sid);
}

/** Release a surface object and delete it if the reference counter goes to zero.
 *
 * @param pSO      Surface object which reference counter is to be decreased.
 * @return NT status.
 */
NTSTATUS SvgaSurfaceObjectRelease(SURFACEOBJECT *pSO)
{
    VBOXWDDM_EXT_VMSVGA *pSvga = pSO->ho.pSvga;
    return svgaHostObjectRelease(&pSO->ho, &pSvga->SurfaceTree);
}

/** Release a surface object with the specified key.
 *
 * @param pSvga    Device instance.
 * @param u32Sid   Surface id.
 * @return NT status.
 */
NTSTATUS SvgaSurfaceUnref(VBOXWDDM_EXT_VMSVGA *pSvga,
                          uint32_t u32Sid)
{
    GALOG(("sid=%u\n", u32Sid));
    return svgaHostObjectUnref(pSvga, &pSvga->SurfaceTree, u32Sid);
}

NTSTATUS SvgaSurfaceCreate(VBOXWDDM_EXT_VMSVGA *pSvga,
                           GASURFCREATE *pCreateParms,
                           GASURFSIZE *paSizes,
                           uint32_t cSizes,
                           uint32_t *pu32Sid)
{
    NTSTATUS Status = svgaHostObjectsProcessPending(pSvga);
    AssertReturn(Status == STATUS_SUCCESS, Status);

    /* A surface must have dimensions. */
    AssertReturn(cSizes >= 1, STATUS_INVALID_PARAMETER);

    /* Number of faces ('cFaces') is specified as the number of the first non-zero elements in the
     * 'pCreateParms->mip_levels' array.
     * Since only plain surfaces (cFaces == 1) and cubemaps (cFaces == 6) are supported
     * (see also SVGA3dCmdDefineSurface definition in svga3d_reg.h), we ignore anything else.
     */
    uint32_t cRemainingSizes = cSizes;
    uint32_t cFaces = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(pCreateParms->mip_levels); ++i)
    {
        if (pCreateParms->mip_levels[i] == 0)
            break;

        /* Can't have too many mip levels. */
        AssertReturn(pCreateParms->mip_levels[i] <= pSvga->u32MaxTextureLevels, STATUS_INVALID_PARAMETER);

        /* All SVGA3dSurfaceFace structures must have the same value of numMipLevels field */
        AssertReturn(pCreateParms->mip_levels[i] == pCreateParms->mip_levels[0], STATUS_INVALID_PARAMETER);

        /* The miplevels count value can't be greater than the number of remaining elements in the paSizes array. */
        AssertReturn(pCreateParms->mip_levels[i] <= cRemainingSizes, STATUS_INVALID_PARAMETER);
        cRemainingSizes -= pCreateParms->mip_levels[i];

        ++cFaces;
    }
    for (uint32_t i = cFaces; i < RT_ELEMENTS(pCreateParms->mip_levels); ++i)
        AssertReturn(pCreateParms->mip_levels[i] == 0, STATUS_INVALID_PARAMETER);

    /* cFaces must be 6 for a cubemap and 1 otherwise. */
    AssertReturn(cFaces == (uint32_t)((pCreateParms->flags & SVGA3D_SURFACE_CUBEMAP) ? 6 : 1), STATUS_INVALID_PARAMETER);

    /* Sum of pCreateParms->mip_levels[i] must be equal to cSizes. */
    AssertReturn(cRemainingSizes == 0, STATUS_INVALID_PARAMETER);

    SURFACEOBJECT *pSO = (SURFACEOBJECT *)GaMemAllocZero(sizeof(SURFACEOBJECT));
    if (pSO)
    {
        uint32_t u32Sid;
        Status = SvgaSurfaceIdAlloc(pSvga, &u32Sid);
        if (NT_SUCCESS(Status))
        {
            Status = SvgaSurfaceDefine(pSvga, pCreateParms, paSizes, cSizes, u32Sid);
            if (NT_SUCCESS(Status))
            {
                pSO->u32SharedSid = u32Sid; /* Initially. The user mode driver can change this for shared surfaces. */
                pSO->mobid = SVGA3D_INVALID_ID;

                Status = svgaHostObjectInit(pSvga, &pSO->ho, SVGA_HOST_OBJECT_SURFACE, u32Sid, svgaSurfaceObjectDestroy);
                if (NT_SUCCESS(Status))
                {
                    *pu32Sid = u32Sid;

                    GALOG(("created sid=%u\n", u32Sid));
                    return STATUS_SUCCESS;
                }

                AssertFailed();

                /*
                 * Cleanup on error.
                 */
                SvgaSurfaceDestroy(pSvga, u32Sid);
            }
            SvgaSurfaceIdFree(pSvga, u32Sid);
        }
        GaMemFree(pSO);
    }
    else
        Status = STATUS_INSUFFICIENT_RESOURCES;

    return Status;
}


static NTSTATUS svgaGBSurfaceDefine(VBOXWDDM_EXT_VMSVGA *pSvga,
                                    uint32_t u32Sid,
                                    SVGAGBSURFCREATE *pCreateParms,
                                    uint32_t mobid)
{
    void *pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_DEFINE_GB_SURFACE_V4, sizeof(SVGA3dCmdDefineGBSurface_v4), SVGA3D_INVALID_ID);
    if (pvCmd)
    {
        SVGA3dCmdDefineGBSurface_v4 *pCmd = (SVGA3dCmdDefineGBSurface_v4 *)pvCmd;
        pCmd->sid              = u32Sid;
        pCmd->surfaceFlags     = pCreateParms->s.flags;
        pCmd->format           = pCreateParms->s.format;
        pCmd->numMipLevels     = pCreateParms->s.numMipLevels;
        pCmd->multisampleCount = pCreateParms->s.sampleCount;
        pCmd->autogenFilter    = SVGA3D_TEX_FILTER_NONE;
        pCmd->size             = pCreateParms->s.size;
        pCmd->arraySize        = pCreateParms->s.numFaces;
        pCmd->bufferByteStride = 0;
        SvgaCmdBufCommit(pSvga, sizeof(SVGA3dCmdDefineGBSurface_v4));
    }
    else
        AssertFailedReturn(STATUS_INSUFFICIENT_RESOURCES);

    pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_BIND_GB_SURFACE, sizeof(SVGA3dCmdBindGBSurface), SVGA3D_INVALID_ID);
    if (pvCmd)
    {
        SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pvCmd;
        pCmd->sid = u32Sid;
        pCmd->mobid = mobid;
        SvgaCmdBufCommit(pSvga, sizeof(SVGA3dCmdBindGBSurface));
    }
    else
        AssertFailedReturn(STATUS_INSUFFICIENT_RESOURCES);

    return STATUS_SUCCESS;
}


static void svgaGBSurfaceDestroy(VBOXWDDM_EXT_VMSVGA *pSvga, uint32_t u32Sid)
{
    SvgaSurfaceDestroy(pSvga, u32Sid);
}


NTSTATUS SvgaGBSurfaceCreate(VBOXWDDM_EXT_VMSVGA *pSvga,
                             void *pvOwner,
                             SVGAGBSURFCREATE *pCreateParms)
{
    NTSTATUS Status = svgaHostObjectsProcessPending(pSvga);
    AssertReturn(Status == STATUS_SUCCESS, Status);

    GALOGG(GALOG_GROUP_SVGA, ("gmrid = %u\n", pCreateParms->gmrid));

    uint32_t cbGB = 0;
    uint64_t u64UserAddress = 0;
    /* Allocate GMR, if not already supplied. */
    if (pCreateParms->gmrid == SVGA3D_INVALID_ID)
    {
        uint32_t u32NumPages = (pCreateParms->cbGB + PAGE_SIZE - 1) >> PAGE_SHIFT;
        Status = SvgaRegionCreate(pSvga, pvOwner, u32NumPages, &pCreateParms->gmrid, &u64UserAddress);
        AssertReturn(NT_SUCCESS(Status), Status);
        cbGB = u32NumPages * PAGE_SIZE;
    }
    else
    {
        Status = SvgaRegionUserAddressAndSize(pSvga, pCreateParms->gmrid, &u64UserAddress, &cbGB);
        AssertReturn(NT_SUCCESS(Status), Status);
    }

    SURFACEOBJECT *pSO = (SURFACEOBJECT *)GaMemAllocZero(sizeof(SURFACEOBJECT));
    if (pSO)
    {
        uint32_t u32Sid;
        Status = SvgaSurfaceIdAlloc(pSvga, &u32Sid);
        if (NT_SUCCESS(Status))
        {
            Status = svgaGBSurfaceDefine(pSvga, u32Sid, pCreateParms, pCreateParms->gmrid);
            if (NT_SUCCESS(Status))
            {
                pSO->u32SharedSid = u32Sid; /* Initially. The user mode driver can change this for shared surfaces. */
                pSO->mobid = pCreateParms->gmrid;

                Status = svgaHostObjectInit(pSvga, &pSO->ho, SVGA_HOST_OBJECT_SURFACE, u32Sid, svgaSurfaceObjectDestroy);
                if (NT_SUCCESS(Status))
                {
                    pCreateParms->cbGB = cbGB;
                    pCreateParms->u64UserAddress = u64UserAddress;
                    pCreateParms->u32Sid = u32Sid;

                    GALOG(("created sid=%u\n", u32Sid));
                    return STATUS_SUCCESS;
                }

                AssertFailed();

                /*
                 * Cleanup on error.
                 */
                svgaGBSurfaceDestroy(pSvga, u32Sid);
            }
            SvgaSurfaceIdFree(pSvga, u32Sid);
        }
        GaMemFree(pSO);
    }
    else
        Status = STATUS_INSUFFICIENT_RESOURCES;

    return Status;
}
