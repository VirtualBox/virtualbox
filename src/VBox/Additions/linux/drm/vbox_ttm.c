/* $Id: vbox_ttm.c 109139 2025-04-09 11:15:45Z vadim.galitsyn@oracle.com $ */
/** @file
 * VirtualBox Additions Linux kernel video driver
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
 * This file is based on ast_ttm.c
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 *
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com>
 */
#include "vbox_drv.h"

#if RTLNX_VER_MIN(6,3,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_MAJ_PREREQ(9,3)
# include <drm/ttm/ttm_tt.h>
#endif

#if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_MAJ_PREREQ(8,5)
# include <drm/drm_gem.h>
# include <drm/drm_gem_ttm_helper.h>
# include <drm/drm_gem_vram_helper.h>
#else
# include <drm/ttm/ttm_page_alloc.h>
#endif

#if RTLNX_VER_MIN(5,14,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
# include <drm/ttm/ttm_range_manager.h>
#endif

#if RTLNX_VER_MAX(3,18,0) && !RTLNX_RHEL_MAJ_PREREQ(7,2)
#define PLACEMENT_FLAGS(placement) (placement)
#else
#define PLACEMENT_FLAGS(placement) ((placement).flags)
#endif


#if RTLNX_VER_MIN(5,13,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
static inline struct vbox_private *vbox_bdev(struct ttm_device *bd)
#else
static inline struct vbox_private *vbox_bdev(struct ttm_bo_device *bd)
#endif
{
	return container_of(bd, struct vbox_private, ttm.bdev);
}

#if RTLNX_VER_MAX(5,0,0) && !RTLNX_RHEL_MAJ_PREREQ(7,7) && !RTLNX_RHEL_MAJ_PREREQ(8,1)
static int vbox_ttm_mem_global_init(struct drm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void vbox_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

/**
 * Adds the vbox memory manager object/structures to the global memory manager.
 */
static int vbox_ttm_global_init(struct vbox_private *vbox)
{
	struct drm_global_reference *global_ref;
	int ret;

#if RTLNX_VER_MAX(5,0,0)
	global_ref = &vbox->ttm.mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &vbox_ttm_mem_global_init;
	global_ref->release = &vbox_ttm_mem_global_release;
	ret = drm_global_item_ref(global_ref);
	if (ret) {
		DRM_ERROR("Failed setting up TTM memory subsystem.\n");
		return ret;
	}

	vbox->ttm.bo_global_ref.mem_glob = vbox->ttm.mem_global_ref.object;
#endif
	global_ref = &vbox->ttm.bo_global_ref.ref;
	global_ref->global_type = DRM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;

	ret = drm_global_item_ref(global_ref);
	if (ret) {
		DRM_ERROR("Failed setting up TTM BO subsystem.\n");
#if RTLNX_VER_MAX(5,0,0)
		drm_global_item_unref(&vbox->ttm.mem_global_ref);
#endif
		return ret;
	}

	return 0;
}

/**
 * Removes the vbox memory manager object from the global memory manager.
 */
static void vbox_ttm_global_release(struct vbox_private *vbox)
{
	drm_global_item_unref(&vbox->ttm.bo_global_ref.ref);
	drm_global_item_unref(&vbox->ttm.mem_global_ref);
}
#endif

static void vbox_bo_ttm_destroy(struct ttm_buffer_object *tbo)
{
	struct vbox_bo *bo;

	bo = container_of(tbo, struct vbox_bo, bo);

	drm_gem_object_release(&bo->gem);
	kfree(bo);
}

static bool vbox_ttm_bo_is_vbox_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &vbox_bo_ttm_destroy)
		return true;

	return false;
}

#if RTLNX_VER_MAX(5,10,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
static int
vbox_bo_init_mem_type(struct ttm_bo_device *bdev, u32 type,
		      struct ttm_mem_type_manager *man)
{
	switch (type) {
	case TTM_PL_SYSTEM:
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
		man->func = &ttm_bo_manager_func;
		man->flags = TTM_MEMTYPE_FLAG_FIXED | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned int)type);
		return -EINVAL;
	}

	return 0;
}
#endif

static void
vbox_bo_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *pl)
{
	struct vbox_bo *vboxbo = vbox_bo(bo);

	if (!vbox_ttm_bo_is_vbox_bo(bo))
		return;

	vbox_ttm_placement(vboxbo, VBOX_MEM_TYPE_SYSTEM);
	*pl = vboxbo->placement;
}

#if RTLNX_VER_MAX(5,14,0) && !RTLNX_RHEL_RANGE(8,6, 8,99)
static int vbox_bo_verify_access(struct ttm_buffer_object *bo,
				 struct file *filp)
{
	return 0;
}
#endif

#if RTLNX_VER_MAX(5,10,0) && !RTLNX_RHEL_RANGE(8,5, 8,99)
static int vbox_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				   struct ttm_mem_reg *mem)
{
	struct vbox_private *vbox = vbox_bdev(bdev);
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];

	mem->bus.addr = NULL;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	mem->bus.is_iomem = false;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		/* system memory */
		return 0;
	case TTM_PL_VRAM:
		mem->bus.offset = mem->start << PAGE_SHIFT;
		mem->bus.base = pci_resource_start(vbox->dev->pdev, 0);
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
#else
# if RTLNX_VER_MAX(5,13,0) && !RTLNX_RHEL_RANGE(8,6, 8,99)
static int vbox_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				   struct ttm_resource *mem)
# else /* > 5.13.0 */
static int vbox_ttm_io_mem_reserve(struct ttm_device *bdev,
				   struct ttm_resource *mem)
# endif /* > 5.13.0 */
{
	struct vbox_private *vbox = vbox_bdev(bdev);
	mem->bus.addr = NULL;
	mem->bus.offset = 0;
# if RTLNX_VER_MAX(5,12,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
	mem->size = mem->num_pages << PAGE_SHIFT;
# endif
	mem->start = 0;
	mem->bus.is_iomem = false;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		/* system memory */
		return 0;
	case TTM_PL_VRAM:
# if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
		mem->bus.caching = ttm_write_combined;
# endif
# if RTLNX_VER_MIN(5,10,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
		mem->bus.offset = (mem->start << PAGE_SHIFT) + pci_resource_start(VBOX_DRM_TO_PCI_DEV(vbox->dev), 0);
# else
		mem->bus.offset = mem->start << PAGE_SHIFT;
		mem->start = pci_resource_start(VBOX_DRM_TO_PCI_DEV(vbox->dev), 0);
# endif
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
#endif



#if RTLNX_VER_MIN(5,13,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
static void vbox_ttm_io_mem_free(struct ttm_device *bdev,
				 struct ttm_resource *mem)
{
}
#elif RTLNX_VER_MIN(5,10,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
static void vbox_ttm_io_mem_free(struct ttm_bo_device *bdev,
				 struct ttm_resource *mem)
{
}
#else
static void vbox_ttm_io_mem_free(struct ttm_bo_device *bdev,
				 struct ttm_mem_reg *mem)
{
}
#endif

#if RTLNX_VER_MIN(5,13,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
static void vbox_ttm_tt_destroy(struct ttm_device *bdev, struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}
#elif RTLNX_VER_MIN(5,10,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
static void vbox_ttm_tt_destroy(struct ttm_bo_device *bdev, struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}
#else
static void vbox_ttm_backend_destroy(struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_backend_func vbox_tt_backend_func = {
	.destroy = &vbox_ttm_backend_destroy,
};
#endif

#if RTLNX_VER_MAX(4,17,0) && !RTLNX_RHEL_MAJ_PREREQ(7,6) && !RTLNX_SUSE_MAJ_PREREQ(15,1) && !RTLNX_SUSE_MAJ_PREREQ(12,5)
static struct ttm_tt *vbox_ttm_tt_create(struct ttm_bo_device *bdev,
					 unsigned long size,
					 u32 page_flags,
					 struct page *dummy_read_page)
#else
static struct ttm_tt *vbox_ttm_tt_create(struct ttm_buffer_object *bo,
										 u32 page_flags)
#endif
{
	struct ttm_tt *tt;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt)
		return NULL;

#if RTLNX_VER_MAX(5,10,0) && !RTLNX_RHEL_RANGE(8,5, 8,99)
	tt->func = &vbox_tt_backend_func;
#endif
#if RTLNX_VER_MIN(5,19,0) || RTLNX_RHEL_RANGE(8,8, 8,99) || RTLNX_RHEL_RANGE(9,2, 9,99) || RTLNX_SUSE_MAJ_PREREQ(15,5)
	if (ttm_tt_init(tt, bo, page_flags, ttm_write_combined, 0)) {
#elif RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
	if (ttm_tt_init(tt, bo, page_flags, ttm_write_combined)) {
#elif RTLNX_VER_MIN(4,17,0) || RTLNX_RHEL_MAJ_PREREQ(7,6) || RTLNX_SUSE_MAJ_PREREQ(15,1) || RTLNX_SUSE_MAJ_PREREQ(12,5)
	if (ttm_tt_init(tt, bo, page_flags)) {
#else
	if (ttm_tt_init(tt, bdev, size, page_flags, dummy_read_page)) {
#endif

		kfree(tt);
		return NULL;
	}

	return tt;
}

#if RTLNX_VER_MAX(4,17,0)
# if RTLNX_VER_MAX(4,16,0) && !RTLNX_RHEL_MAJ_PREREQ(7,6) && !RTLNX_SUSE_MAJ_PREREQ(15,1) && !RTLNX_SUSE_MAJ_PREREQ(12,5)
static int vbox_ttm_tt_populate(struct ttm_tt *ttm)
{
	return ttm_pool_populate(ttm);
}
# else
static int vbox_ttm_tt_populate(struct ttm_tt *ttm,
				struct ttm_operation_ctx *ctx)
{
	return ttm_pool_populate(ttm, ctx);
}
# endif

static void vbox_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	ttm_pool_unpopulate(ttm);
}
#endif

#if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
static int vbox_bo_move(struct ttm_buffer_object *bo, bool evict,
	struct ttm_operation_ctx *ctx, struct ttm_resource *new_mem,
	struct ttm_place *hop)
{
# if RTLNX_VER_MIN(6,4,0) || RTLNX_RHEL_RANGE(9,7, 9,99)
	if (!bo->resource)
	{
		if (new_mem->mem_type != TTM_PL_SYSTEM)
		{
			hop->mem_type = TTM_PL_SYSTEM;
			hop->flags = TTM_PL_FLAG_TEMPORARY;
			return -EMULTIHOP;
		}
		ttm_bo_move_null(bo, new_mem);
		return 0;
	}
# endif
	return ttm_bo_move_memcpy(bo, ctx, new_mem);
}
#endif

#if RTLNX_VER_MIN(5,13,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
static struct ttm_device_funcs vbox_bo_driver = {
#else /* < 5.13.0 */
static struct ttm_bo_driver vbox_bo_driver = {
#endif /* < 5.13.0 */
	.ttm_tt_create = vbox_ttm_tt_create,
#if RTLNX_VER_MIN(5,10,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
	.ttm_tt_destroy = vbox_ttm_tt_destroy,
#endif
#if RTLNX_VER_MAX(4,17,0)
	.ttm_tt_populate = vbox_ttm_tt_populate,
	.ttm_tt_unpopulate = vbox_ttm_tt_unpopulate,
#endif
#if RTLNX_VER_MAX(5,10,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
	.init_mem_type = vbox_bo_init_mem_type,
#endif
#if RTLNX_VER_MIN(4,10,0) || RTLNX_RHEL_MAJ_PREREQ(7,4)
	.eviction_valuable = ttm_bo_eviction_valuable,
#endif
	.evict_flags = vbox_bo_evict_flags,
#if RTLNX_VER_MAX(5,14,0) && !RTLNX_RHEL_RANGE(8,6, 8,99)
	.verify_access = vbox_bo_verify_access,
#endif
	.io_mem_reserve = &vbox_ttm_io_mem_reserve,
	.io_mem_free = &vbox_ttm_io_mem_free,
#if RTLNX_VER_MIN(4,12,0) || RTLNX_RHEL_MAJ_PREREQ(7,5)
# if RTLNX_VER_MAX(4,16,0) && !RTLNX_RHEL_MAJ_PREREQ(7,6) && !RTLNX_SUSE_MAJ_PREREQ(15,1) && !RTLNX_SUSE_MAJ_PREREQ(12,5)
	.io_mem_pfn = ttm_bo_default_io_mem_pfn,
# endif
#endif
#if (RTLNX_VER_RANGE(4,7,0,  4,11,0) || RTLNX_RHEL_MAJ_PREREQ(7,4)) && !RTLNX_RHEL_MAJ_PREREQ(7,5)
	.lru_tail = &ttm_bo_default_lru_tail,
	.swap_lru_tail = &ttm_bo_default_swap_lru_tail,
#endif
#if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
	.move = &vbox_bo_move,
#endif
};

int vbox_mm_init(struct vbox_private *vbox)
{
	int ret;
	struct drm_device *dev = vbox->dev;
#if RTLNX_VER_MIN(5,13,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	struct ttm_device *bdev = &vbox->ttm.bdev;
#else
	struct ttm_bo_device *bdev = &vbox->ttm.bdev;
#endif

#if RTLNX_VER_MAX(5,0,0) && !RTLNX_RHEL_MAJ_PREREQ(7,7) && !RTLNX_RHEL_MAJ_PREREQ(8,1)
	ret = vbox_ttm_global_init(vbox);
	if (ret)
		return ret;
#endif
#if RTLNX_VER_MIN(5,13,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	ret = ttm_device_init(&vbox->ttm.bdev,
#else
	ret = ttm_bo_device_init(&vbox->ttm.bdev,
#endif
#if RTLNX_VER_MAX(5,0,0) && !RTLNX_RHEL_MAJ_PREREQ(7,7) && !RTLNX_RHEL_MAJ_PREREQ(8,1)
				 vbox->ttm.bo_global_ref.ref.object,
#endif
				 &vbox_bo_driver,
#if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
				 dev->dev,
#endif
#if RTLNX_VER_MIN(3,15,0) || RTLNX_RHEL_MAJ_PREREQ(7,1)
				 dev->anon_inode->i_mapping,
#endif
#if RTLNX_VER_MIN(5,5,0) || RTLNX_RHEL_MIN(8,3) || RTLNX_SUSE_MAJ_PREREQ(15,3)
				 dev->vma_offset_manager,
#elif RTLNX_VER_MAX(5,2,0) && !RTLNX_RHEL_MAJ_PREREQ(8,2)
				 DRM_FILE_PAGE_OFFSET,
#endif
#if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
				 false,
#endif
				 true);
	if (ret) {
		DRM_ERROR("Error initialising bo driver; %d\n", ret);
#if RTLNX_VER_MAX(5,0,0) && !RTLNX_RHEL_MAJ_PREREQ(7,7) && !RTLNX_RHEL_MAJ_PREREQ(8,1)
		goto err_ttm_global_release;
#else
		return ret;
#endif
	}

#if RTLNX_VER_MIN(5,10,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
	ret = ttm_range_man_init(bdev, TTM_PL_VRAM, false,
			     vbox->available_vram_size >> PAGE_SHIFT);
#else
	ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM,
			     vbox->available_vram_size >> PAGE_SHIFT);
#endif
	if (ret) {
		DRM_ERROR("Failed ttm VRAM init: %d\n", ret);
		goto err_device_release;
	}

#ifdef DRM_MTRR_WC
	vbox->fb_mtrr = drm_mtrr_add(pci_resource_start(VBOX_DRM_TO_PCI_DEV(dev), 0),
				     pci_resource_len(VBOX_DRM_TO_PCI_DEV(dev), 0),
				     DRM_MTRR_WC);
#else
	vbox->fb_mtrr = arch_phys_wc_add(pci_resource_start(VBOX_DRM_TO_PCI_DEV(dev), 0),
					 pci_resource_len(VBOX_DRM_TO_PCI_DEV(dev), 0));
#endif
	return 0;

err_device_release:
#if RTLNX_VER_MIN(5,13,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	ttm_device_fini(&vbox->ttm.bdev);
#else
	ttm_bo_device_release(&vbox->ttm.bdev);
#endif
#if RTLNX_VER_MAX(5,0,0) && !RTLNX_RHEL_MAJ_PREREQ(7,7) && !RTLNX_RHEL_MAJ_PREREQ(8,1)
err_ttm_global_release:
	vbox_ttm_global_release(vbox);
#endif
	return ret;
}

void vbox_mm_fini(struct vbox_private *vbox)
{
#ifdef DRM_MTRR_WC
	drm_mtrr_del(vbox->fb_mtrr,
		     pci_resource_start(VBOX_DRM_TO_PCI_DEV(vbox->dev), 0),
		     pci_resource_len(VBOX_DRM_TO_PCI_DEV(vbox->dev), 0), DRM_MTRR_WC);
#else
	arch_phys_wc_del(vbox->fb_mtrr);
#endif
#if RTLNX_VER_MIN(5,13,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	ttm_device_fini(&vbox->ttm.bdev);
#else
	ttm_bo_device_release(&vbox->ttm.bdev);
#endif
#if RTLNX_VER_MAX(5,0,0) && !RTLNX_RHEL_MAJ_PREREQ(7,7) && !RTLNX_RHEL_MAJ_PREREQ(8,1)
	vbox_ttm_global_release(vbox);
#endif
}

void vbox_ttm_placement(struct vbox_bo *bo, u32 mem_type)
{
	u32 c = 0;
#if RTLNX_VER_MAX(3,18,0) && !RTLNX_RHEL_MAJ_PREREQ(7,2)
	bo->placement.fpfn = 0;
	bo->placement.lpfn = 0;
#else
	unsigned int i;
#endif

	bo->placement.placement = bo->placements;
#if RTLNX_VER_MAX(6,9,0) && !RTLNX_RHEL_MAJ_PREREQ(9,5)
	bo->placement.busy_placement = bo->placements;
#endif

	if (mem_type & VBOX_MEM_TYPE_VRAM) {
#if RTLNX_VER_MIN(6,9,0)
		bo->placements[c].mem_type = TTM_PL_VRAM;
		PLACEMENT_FLAGS(bo->placements[c++]) = TTM_PL_FLAG_DESIRED;
#elif RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
		bo->placements[c].mem_type = TTM_PL_VRAM;
		PLACEMENT_FLAGS(bo->placements[c++]) = 0;
#elif RTLNX_VER_MIN(5,10,0)
		bo->placements[c].mem_type = TTM_PL_VRAM;
		PLACEMENT_FLAGS(bo->placements[c++]) =
		    TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED;
#else
		PLACEMENT_FLAGS(bo->placements[c++]) =
		    TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_VRAM;
#endif
	}
	if (mem_type & VBOX_MEM_TYPE_SYSTEM) {

#if RTLNX_VER_MIN(6,9,0)
		bo->placements[c].mem_type = TTM_PL_SYSTEM;
		PLACEMENT_FLAGS(bo->placements[c++]) = TTM_PL_FLAG_DESIRED;
#elif RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
		bo->placements[c].mem_type = TTM_PL_SYSTEM;
		PLACEMENT_FLAGS(bo->placements[c++]) = 0;
#elif RTLNX_VER_MIN(5,10,0)
		bo->placements[c].mem_type = TTM_PL_SYSTEM;
		PLACEMENT_FLAGS(bo->placements[c++]) =
		    TTM_PL_MASK_CACHING;
#else
		PLACEMENT_FLAGS(bo->placements[c++]) =
		    TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
#endif
	}
	if (!c) {
#if RTLNX_VER_MIN(6,9,0)
		bo->placements[c].mem_type = TTM_PL_SYSTEM;
		PLACEMENT_FLAGS(bo->placements[c++]) = TTM_PL_FLAG_DESIRED;
#elif RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
		bo->placements[c].mem_type = TTM_PL_SYSTEM;
		PLACEMENT_FLAGS(bo->placements[c++]) = 0;
#elif RTLNX_VER_MIN(5,10,0)
		bo->placements[c].mem_type = TTM_PL_SYSTEM;
		PLACEMENT_FLAGS(bo->placements[c++]) =
		    TTM_PL_MASK_CACHING;
#else
		PLACEMENT_FLAGS(bo->placements[c++]) =
		    TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
#endif
	}

	bo->placement.num_placement = c;
#if RTLNX_VER_MAX(6,9,0) && !RTLNX_RHEL_MAJ_PREREQ(9,5)
	bo->placement.num_busy_placement = c;
#endif

#if RTLNX_VER_MIN(3,18,0) || RTLNX_RHEL_MAJ_PREREQ(7,2)
	for (i = 0; i < c; ++i) {
		bo->placements[i].fpfn = 0;
		bo->placements[i].lpfn = 0;
	}
#endif
}

#if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
static const struct drm_gem_object_funcs vbox_drm_gem_object_funcs = {
	.free   = vbox_gem_free_object,
	.print_info = drm_gem_ttm_print_info,
# if RTLNX_VER_MIN(6,5,0)
	.vmap   = drm_gem_ttm_vmap,
	.vunmap = drm_gem_ttm_vunmap,
# endif
# if RTLNX_VER_MIN(5,14,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	.mmap = drm_gem_ttm_mmap,
# endif
};
#endif

int vbox_bo_create(struct drm_device *dev, int size, int align,
		   u32 flags, struct vbox_bo **pvboxbo)
{
	struct vbox_private *vbox = dev->dev_private;
	struct vbox_bo *vboxbo;
#if RTLNX_VER_MAX(5,13,0) && !RTLNX_RHEL_RANGE(8,6, 8,99)
	size_t acc_size;
#endif
	int ret;

	vboxbo = kzalloc(sizeof(*vboxbo), GFP_KERNEL);
	if (!vboxbo)
		return -ENOMEM;

	ret = drm_gem_object_init(dev, &vboxbo->gem, size);
	if (ret)
		goto err_free_vboxbo;

#if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
	if (!vboxbo->gem.funcs) {
		vboxbo->gem.funcs = &vbox_drm_gem_object_funcs;
	}
#endif
	vboxbo->bo.bdev = &vbox->ttm.bdev;
#if RTLNX_VER_MAX(3,15,0) && !RTLNX_RHEL_MAJ_PREREQ(7,1)
	vboxbo->bo.bdev->dev_mapping = dev->dev_mapping;
#endif

	vbox_ttm_placement(vboxbo, VBOX_MEM_TYPE_SYSTEM);

#if RTLNX_VER_MAX(5,13,0) && !RTLNX_RHEL_RANGE(8,6, 8,99)
	acc_size = ttm_bo_dma_acc_size(&vbox->ttm.bdev, size,
				       sizeof(struct vbox_bo));
#endif

#if RTLNX_VER_MIN(5,14,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	/* Initialization of the following was removed from DRM stack
	 * in 5.14, so we need to do it manually. */
	vboxbo->bo.base.funcs = &vbox_drm_gem_object_funcs;
	kref_init(&vboxbo->bo.base.refcount);
	vboxbo->bo.base.size = size;
	vboxbo->bo.base.dev = dev;
	dma_resv_init(&vboxbo->bo.base._resv);
	drm_vma_node_reset(&vboxbo->bo.base.vma_node);
#endif

#if RTLNX_VER_MIN(6,1,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99) || RTLNX_SUSE_MAJ_PREREQ(15,5)
	ret = ttm_bo_init_validate(&vbox->ttm.bdev, &vboxbo->bo,
#else
	ret = ttm_bo_init(&vbox->ttm.bdev, &vboxbo->bo, size,
#endif /* < 6.1.0 */
			  ttm_bo_type_device, &vboxbo->placement,
#if RTLNX_VER_MAX(4,17,0) && !RTLNX_RHEL_MAJ_PREREQ(7,6) && !RTLNX_SUSE_MAJ_PREREQ(15,1) && !RTLNX_SUSE_MAJ_PREREQ(12,5)
			  align >> PAGE_SHIFT, false, NULL, acc_size,
#elif RTLNX_VER_MAX(5,13,0) && !RTLNX_RHEL_RANGE(8,6, 8,99) /* < 5.13.0, < RHEL(8.6, 8.99) */
			  align >> PAGE_SHIFT, false, acc_size,
#else /* > 5.13.0 */
			  align >> PAGE_SHIFT, false,
#endif /* > 5.13.0 */
#if RTLNX_VER_MIN(3,18,0) || RTLNX_RHEL_MAJ_PREREQ(7,2)
			  NULL, NULL, vbox_bo_ttm_destroy);
#else
			  NULL, vbox_bo_ttm_destroy);
#endif
	if (ret)
	{
		/* In case of failure, ttm_bo_init() supposed to call
		 * vbox_bo_ttm_destroy() which in turn will free @vboxbo. */
		goto err_exit;
	}

	*pvboxbo = vboxbo;

	return 0;

err_free_vboxbo:
	kfree(vboxbo);
err_exit:
	return ret;
}

static inline u64 vbox_bo_gpu_offset(struct vbox_bo *bo)
{
#if RTLNX_VER_MIN(5,14,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	return bo->bo.resource->start << PAGE_SHIFT;
#elif RTLNX_VER_MIN(5,9,0) || RTLNX_RHEL_MIN(8,4) || RTLNX_SUSE_MAJ_PREREQ(15,3)
	return bo->bo.mem.start << PAGE_SHIFT;
#else
	return bo->bo.offset;
#endif
}

int vbox_bo_pin(struct vbox_bo *bo, u32 mem_type, u64 *gpu_addr)
{
#if RTLNX_VER_MIN(4,16,0) || RTLNX_RHEL_MAJ_PREREQ(7,6) || RTLNX_SUSE_MAJ_PREREQ(15,1) || RTLNX_SUSE_MAJ_PREREQ(12,5)
	struct ttm_operation_ctx ctx = { false, false };
#endif
	int ret;
#if RTLNX_VER_MAX(5,11,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
	int i;
#endif

	if (bo->pin_count) {
		bo->pin_count++;
		if (gpu_addr)
			*gpu_addr = vbox_bo_gpu_offset(bo);

		return 0;
	}

	vbox_ttm_placement(bo, mem_type);

#if RTLNX_VER_MAX(5,11,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
	for (i = 0; i < bo->placement.num_placement; i++)
		PLACEMENT_FLAGS(bo->placements[i]) |= TTM_PL_FLAG_NO_EVICT;
#endif

#if RTLNX_VER_MAX(4,16,0) && !RTLNX_RHEL_MAJ_PREREQ(7,6) && !RTLNX_SUSE_MAJ_PREREQ(15,1) && !RTLNX_SUSE_MAJ_PREREQ(12,5)
	ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
#else
	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);
#endif
	if (ret)
		return ret;

	bo->pin_count = 1;

#if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
	ttm_bo_pin(&bo->bo);
#endif

	if (gpu_addr)
		*gpu_addr = vbox_bo_gpu_offset(bo);

	return 0;
}

int vbox_bo_unpin(struct vbox_bo *bo)
{
#if RTLNX_VER_MIN(4,16,0) || RTLNX_RHEL_MAJ_PREREQ(7,6) || RTLNX_SUSE_MAJ_PREREQ(15,1) || RTLNX_SUSE_MAJ_PREREQ(12,5)
# if RTLNX_VER_MAX(5,11,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
	struct ttm_operation_ctx ctx = { false, false };
# endif
#endif
	int ret = 0;
#if RTLNX_VER_MAX(5,11,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
	int i;
#endif

	if (!bo->pin_count) {
		DRM_ERROR("unpin bad %p\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;

#if RTLNX_VER_MAX(5,11,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
	for (i = 0; i < bo->placement.num_placement; i++)
		PLACEMENT_FLAGS(bo->placements[i]) &= ~TTM_PL_FLAG_NO_EVICT;
#endif

#if RTLNX_VER_MAX(4,16,0) && !RTLNX_RHEL_MAJ_PREREQ(7,6) && !RTLNX_SUSE_MAJ_PREREQ(15,1) && !RTLNX_SUSE_MAJ_PREREQ(12,5)
	ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
#elif RTLNX_VER_MAX(5,11,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);
#endif
	if (ret)
		return ret;

#if RTLNX_VER_MIN(5,11,0) || RTLNX_RHEL_RANGE(8,5, 8,99)
	ttm_bo_unpin(&bo->bo);
#endif

	return 0;
}

#if RTLNX_VER_MAX(5,11,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
/*
 * Move a vbox-owned buffer object to system memory if no one else has it
 * pinned.  The caller must have pinned it previously, and this call will
 * release the caller's pin.
 */
int vbox_bo_push_sysram(struct vbox_bo *bo)
{
# if RTLNX_VER_MIN(4,16,0) || RTLNX_RHEL_MAJ_PREREQ(7,6) || RTLNX_SUSE_MAJ_PREREQ(15,1) || RTLNX_SUSE_MAJ_PREREQ(12,5)
	struct ttm_operation_ctx ctx = { false, false };
# endif
	int i, ret;

	if (!bo->pin_count) {
		DRM_ERROR("unpin bad %p\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;

	if (bo->kmap.virtual)
		ttm_bo_kunmap(&bo->kmap);

	vbox_ttm_placement(bo, VBOX_MEM_TYPE_SYSTEM);

	for (i = 0; i < bo->placement.num_placement; i++)
		PLACEMENT_FLAGS(bo->placements[i]) |= TTM_PL_FLAG_NO_EVICT;

# if RTLNX_VER_MAX(4,16,0) && !RTLNX_RHEL_MAJ_PREREQ(7,6) && !RTLNX_SUSE_MAJ_PREREQ(15,1) && !RTLNX_SUSE_MAJ_PREREQ(12,5)
	ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
# else
	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);
# endif
	if (ret) {
		DRM_ERROR("pushing to VRAM failed\n");
		return ret;
	}

	return 0;
}
#endif

int vbox_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct vbox_private *vbox;
	int ret = -EINVAL;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return -EINVAL;

	file_priv = filp->private_data;
	vbox = file_priv->minor->dev->dev_private;

#if RTLNX_VER_MIN(5,14,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	(void)vbox;
	if (drm_dev_is_unplugged(file_priv->minor->dev))
		return -ENODEV;
	ret = drm_gem_mmap(filp, vma);
#else
	ret = ttm_bo_mmap(filp, vma, &vbox->ttm.bdev);
#endif
	return ret;
}
