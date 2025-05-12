/* $Id: vbox_fb.c 109499 2025-05-12 12:21:43Z vadim.galitsyn@oracle.com $ */
/** @file
 * VirtualBox Additions Linux kernel video driver
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
 * This file is based on ast_fb.c
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
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>

#include "vbox_drv.h"

#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>

#include <VBoxVideo.h>

#if RTLNX_VER_MIN(6,2,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99)
# define VBOX_FBDEV_INFO(_helper) _helper.info
#else
# define VBOX_FBDEV_INFO(_helper) _helper.fbdev
#endif

#if RTLNX_VER_MAX(4,7,0) && !RTLNX_RHEL_MAJ_PREREQ(7,4)
/**
 * Tell the host about dirty rectangles to update.
 */
static void vbox_dirty_update(struct vbox_fbdev *fbdev,
			      int x, int y, int width, int height)
{
	struct drm_gem_object *obj;
	struct vbox_bo *bo;
	int ret = -EBUSY;
	bool store_for_later = false;
	int x2, y2;
	unsigned long flags;
	struct drm_clip_rect rect;

	obj = fbdev->afb.obj;
	bo = gem_to_vbox_bo(obj);

	/*
	 * try and reserve the BO, if we fail with busy
	 * then the BO is being moved and we should
	 * store up the damage until later.
	 */
	if (drm_can_sleep())
		ret = vbox_bo_reserve(bo, true);
	if (ret) {
		if (ret != -EBUSY)
			return;

		store_for_later = true;
	}

	x2 = x + width - 1;
	y2 = y + height - 1;
	spin_lock_irqsave(&fbdev->dirty_lock, flags);

	if (fbdev->y1 < y)
		y = fbdev->y1;
	if (fbdev->y2 > y2)
		y2 = fbdev->y2;
	if (fbdev->x1 < x)
		x = fbdev->x1;
	if (fbdev->x2 > x2)
		x2 = fbdev->x2;

	if (store_for_later) {
		fbdev->x1 = x;
		fbdev->x2 = x2;
		fbdev->y1 = y;
		fbdev->y2 = y2;
		spin_unlock_irqrestore(&fbdev->dirty_lock, flags);
		return;
	}

	fbdev->x1 = INT_MAX;
	fbdev->y1 = INT_MAX;
	fbdev->x2 = 0;
	fbdev->y2 = 0;

	spin_unlock_irqrestore(&fbdev->dirty_lock, flags);

	/*
	 * Not sure why the original code subtracted 1 here, but I will keep
	 * it that way to avoid unnecessary differences.
	 */
	rect.x1 = x;
	rect.x2 = x2 + 1;
	rect.y1 = y;
	rect.y2 = y2 + 1;
	vbox_framebuffer_dirty_rectangles(&fbdev->afb.base, &rect, 1);

	vbox_bo_unreserve(bo);
}
#endif /* RTLNX_VER_MAX(4,7,0) && !RTLNX_RHEL_MAJ_PREREQ(7,4) */

#ifdef CONFIG_FB_DEFERRED_IO
# if RTLNX_VER_MAX(4,7,0) && !RTLNX_RHEL_MAJ_PREREQ(7,4)
static void drm_fb_helper_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
	struct vbox_fbdev *fbdev = info->par;
	unsigned long start, end, min, max;
	struct page *page;
	int y1, y2;

	min = ULONG_MAX;
	max = 0;
	list_for_each_entry(page, pagelist, lru) {
		start = page->index << PAGE_SHIFT;
		end = start + PAGE_SIZE - 1;
		min = min(min, start);
		max = max(max, end);
	}

	if (min < max) {
		y1 = min / info->fix.line_length;
		y2 = (max / info->fix.line_length) + 1;
		DRM_INFO("%s: Calling dirty update: 0, %d, %d, %d\n",
			 __func__, y1, info->var.xres, y2 - y1 - 1);
		vbox_dirty_update(fbdev, 0, y1, info->var.xres, y2 - y1 - 1);
	}
}
# endif

static struct fb_deferred_io vbox_defio = {
	.delay = HZ / 30,
	.deferred_io = drm_fb_helper_deferred_io,
};
#endif /* CONFIG_FB_DEFERRED_IO */

#if RTLNX_VER_MAX(4,3,0) && !RTLNX_RHEL_MAJ_PREREQ(7,3)
static void drm_fb_helper_sys_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct vbox_fbdev *fbdev = info->par;

	sys_fillrect(info, rect);
	vbox_dirty_update(fbdev, rect->dx, rect->dy, rect->width, rect->height);
}

static void drm_fb_helper_sys_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct vbox_fbdev *fbdev = info->par;

	sys_copyarea(info, area);
	vbox_dirty_update(fbdev, area->dx, area->dy, area->width, area->height);
}

static void drm_fb_helper_sys_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct vbox_fbdev *fbdev = info->par;

	sys_imageblit(info, image);
	vbox_dirty_update(fbdev, image->dx, image->dy, image->width,
			  image->height);
}
#endif /* RTLNX_VER_MAX(4,3,0) && !RTLNX_RHEL_MAJ_PREREQ(7,3) */

static struct fb_ops vboxfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
#if RTLNX_VER_MIN(6,5,0) || RTLNX_RHEL_RANGE(9,4, 9,99) || RTLNX_SUSE_MAJ_PREREQ(15, 6)
	.fb_read    = fb_sys_read,
	.fb_write   = fb_sys_write,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
	.fb_mmap = NULL,
#else
	.fb_fillrect = drm_fb_helper_sys_fillrect,
	.fb_copyarea = drm_fb_helper_sys_copyarea,
	.fb_imageblit = drm_fb_helper_sys_imageblit,
#endif
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

static int vboxfb_create_object(struct vbox_fbdev *fbdev,
				struct DRM_MODE_FB_CMD *mode_cmd,
				struct drm_gem_object **gobj_p)
{
	struct drm_device *dev = fbdev->helper.dev;
	u32 size;
	struct drm_gem_object *gobj;
#if RTLNX_VER_MAX(3,3,0)
	u32 pitch = mode_cmd->pitch;
#else
	u32 pitch = mode_cmd->pitches[0];
#endif

	int ret;

	size = pitch * mode_cmd->height;
	ret = vbox_gem_create(dev, size, true, &gobj);
	if (ret)
		return ret;

	*gobj_p = gobj;

	return 0;
}

#if RTLNX_VER_MAX(4,3,0) && !RTLNX_RHEL_MAJ_PREREQ(7,3)
static struct fb_info *drm_fb_helper_alloc_fbi(struct drm_fb_helper *helper)
{
	struct fb_info *info;
	struct vbox_fbdev *fbdev =
	    container_of(helper, struct vbox_fbdev, helper);
	struct drm_device *dev = fbdev->helper.dev;
	struct device *device = &dev->pdev->dev;

	info = framebuffer_alloc(0, device);
	if (!info)
		return ERR_PTR(-ENOMEM);
	fbdev->helper.fbdev = info;

	if (fb_alloc_cmap(&info->cmap, 256, 0))
		return ERR_PTR(-ENOMEM);

	info->apertures = alloc_apertures(1);
	if (!info->apertures)
		return ERR_PTR(-ENOMEM);

	return info;
}
#endif

int vboxfb_create(struct drm_fb_helper *helper,
			 struct drm_fb_helper_surface_size *sizes)
{
	struct vbox_fbdev *fbdev =
	    container_of(helper, struct vbox_fbdev, helper);
	struct drm_device *dev = fbdev->helper.dev;
	struct DRM_MODE_FB_CMD mode_cmd;
	struct drm_framebuffer *fb;
	struct fb_info *info;
	struct drm_gem_object *gobj;
	struct vbox_bo *bo;
	int size, ret;
	u32 pitch;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	pitch = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);
#if RTLNX_VER_MAX(3,3,0)
	mode_cmd.bpp = sizes->surface_bpp;
	mode_cmd.depth = sizes->surface_depth;
	mode_cmd.pitch = pitch;
#else
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);
	mode_cmd.pitches[0] = pitch;
#endif

	size = pitch * mode_cmd.height;

	ret = vboxfb_create_object(fbdev, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon backing object %d\n", ret);
		return ret;
	}

	ret = vbox_framebuffer_init(dev, &fbdev->afb, &mode_cmd, gobj);
	if (ret)
		return ret;

	bo = gem_to_vbox_bo(gobj);

	ret = vbox_bo_reserve(bo, false);
	if (ret)
		return ret;

	ret = vbox_bo_pin(bo, VBOX_MEM_TYPE_VRAM, NULL);
	if (ret) {
		vbox_bo_unreserve(bo);
		return ret;
	}

#if RTLNX_VER_MIN(5,14,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	ret = ttm_bo_kmap(&bo->bo, 0, VBOX_BO_RESOURCE_NUM_PAGES(bo->bo.resource), &bo->kmap);
#elif RTLNX_VER_MIN(5,12,0) || RTLNX_RHEL_MAJ_PREREQ(8,5)
	ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.mem.num_pages, &bo->kmap);
#else
	ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
#endif

	vbox_bo_unreserve(bo);
	if (ret) {
		DRM_ERROR("failed to kmap fbcon\n");
		return ret;
	}

#if RTLNX_VER_MIN(6,2,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99)
	info = drm_fb_helper_alloc_info(helper);
#else
	info = drm_fb_helper_alloc_fbi(helper);
#endif
	if (IS_ERR(info))
		return -PTR_ERR(info);

	info->par = fbdev;

	fbdev->size = size;

	fb = &fbdev->afb.base;
	fbdev->helper.fb = fb;

	strcpy(info->fix.id, "vboxdrmfb");

	/*
	 * The last flag forces a mode set on VT switches even if the kernel
	 * does not think it is needed.
	 */
#if RTLNX_VER_MIN(6,6,0) || RTLNX_SUSE_MAJ_PREREQ(15, 6) || RTLNX_RHEL_RANGE(9,5, 9,99)
	info->flags = FBINFO_MISC_ALWAYS_SETPAR;
#else
	info->flags = FBINFO_DEFAULT | FBINFO_MISC_ALWAYS_SETPAR;
#endif
	info->fbops = &vboxfb_ops;

#if RTLNX_VER_MAX(6,3,0) && !RTLNX_RHEL_RANGE(8,9, 8,99) && !RTLNX_RHEL_RANGE(9,3, 9,99)
	/*
	 * This seems to be done for safety checking that the framebuffer
	 * is not registered twice by different drivers.
	 */
	info->apertures->ranges[0].base = pci_resource_start(VBOX_DRM_TO_PCI_DEV(dev), 0);
	info->apertures->ranges[0].size = pci_resource_len(VBOX_DRM_TO_PCI_DEV(dev), 0);
#endif

#if RTLNX_VER_MIN(5,2,0) || RTLNX_RHEL_MAJ_PREREQ(8,2)
        /*
         * The corresponding 5.2-rc1 Linux DRM kernel changes have been
         * also backported to older RedHat based 4.18.0 Linux kernels.
         */
	drm_fb_helper_fill_info(info, &fbdev->helper, sizes);
#elif RTLNX_VER_MIN(4,11,0) || RTLNX_RHEL_MAJ_PREREQ(7, 5)
	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);
#else
	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
#endif
#if RTLNX_VER_MAX(5,2,0) && !RTLNX_RHEL_MAJ_PREREQ(8,2)
	drm_fb_helper_fill_var(info, &fbdev->helper, sizes->fb_width,
			       sizes->fb_height);
#endif

#if RTLNX_VER_MIN(6,5,0)
	info->screen_buffer = (char *)bo->kmap.virtual;
	info->fix.smem_start = page_to_phys(vmalloc_to_page(bo->kmap.virtual));
#endif
	info->screen_base = (char __iomem *)bo->kmap.virtual;
	info->screen_size = size;

#ifdef CONFIG_FB_DEFERRED_IO
# if RTLNX_VER_MIN(5,19,0) || RTLNX_RHEL_RANGE(8,8, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99) || RTLNX_SUSE_MAJ_PREREQ(15,5)
	info->fix.smem_len = info->screen_size;
# endif
	info->fbdefio = &vbox_defio;
# if RTLNX_VER_MIN(5,19,0)
	ret = fb_deferred_io_init(info);
	if (ret)
	{
		DRM_ERROR("failed to initialize deferred io: %d\n", ret);
		return ret;
	}
# endif
	fb_deferred_io_init(info);
#endif

	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	DRM_DEBUG_KMS("allocated %dx%d\n", fb->width, fb->height);

	return 0;
}

#if RTLNX_VER_MAX(6,15,0)
static struct drm_fb_helper_funcs vbox_fb_helper_funcs = {
	.fb_probe = vboxfb_create,
};
#endif

#if RTLNX_VER_MAX(4,3,0) && !RTLNX_RHEL_MAJ_PREREQ(7,3)
static void drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
	if (fb_helper && fb_helper->fbdev)
		unregister_framebuffer(fb_helper->fbdev);
}
#endif

void vbox_fbdev_fini(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;
	struct vbox_fbdev *fbdev = vbox->fbdev;
	struct vbox_framebuffer *afb = &fbdev->afb;

#ifdef CONFIG_FB_DEFERRED_IO
	if (VBOX_FBDEV_INFO(fbdev->helper) && VBOX_FBDEV_INFO(fbdev->helper)->fbdefio)
		fb_deferred_io_cleanup(VBOX_FBDEV_INFO(fbdev->helper));
#endif

#if RTLNX_VER_MIN(6,2,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99)
	drm_fb_helper_unregister_info(&fbdev->helper);
#else
	drm_fb_helper_unregister_fbi(&fbdev->helper);
#endif

	if (afb->obj) {
		struct vbox_bo *bo = gem_to_vbox_bo(afb->obj);

		if (!vbox_bo_reserve(bo, false)) {
			if (bo->kmap.virtual)
				ttm_bo_kunmap(&bo->kmap);
			/*
			 * QXL does this, but is it really needed before
			 * freeing?
			 */
			if (bo->pin_count)
				vbox_bo_unpin(bo);
			vbox_bo_unreserve(bo);
		}
#if RTLNX_VER_MIN(5,9,0) || RTLNX_RHEL_MIN(8,4) || RTLNX_SUSE_MAJ_PREREQ(15,3)
		drm_gem_object_put(afb->obj);
#else
		drm_gem_object_put_unlocked(afb->obj);
#endif
		afb->obj = NULL;
	}
	drm_fb_helper_fini(&fbdev->helper);

#if RTLNX_VER_MIN(3,9,0)
	drm_framebuffer_unregister_private(&afb->base);
#endif
	drm_framebuffer_cleanup(&afb->base);
}

int vbox_fbdev_init(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;
	struct vbox_fbdev *fbdev;
	int ret = 0;

	fbdev = devm_kzalloc(dev->dev, sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	vbox->fbdev = fbdev;
	spin_lock_init(&fbdev->dirty_lock);

#if RTLNX_VER_MIN(6,15,0)
	drm_fb_helper_prepare(dev, &fbdev->helper, 32, NULL);
#elif RTLNX_VER_MIN(6,3,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99)
	drm_fb_helper_prepare(dev, &fbdev->helper, 32, &vbox_fb_helper_funcs);
#elif RTLNX_VER_MIN(3,17,0) || RTLNX_RHEL_MIN(7,2)
	drm_fb_helper_prepare(dev, &fbdev->helper, &vbox_fb_helper_funcs);
#else
	fbdev->helper.funcs = &vbox_fb_helper_funcs;
#endif

#if RTLNX_VER_MIN(5,7,0) || RTLNX_RHEL_MIN(8,4) || RTLNX_SUSE_MAJ_PREREQ(15,3)
        ret = drm_fb_helper_init(dev, &fbdev->helper);
#elif RTLNX_VER_MIN(4,11,0) || RTLNX_RHEL_MAJ_PREREQ(7,5)
	ret = drm_fb_helper_init(dev, &fbdev->helper, vbox->num_crtcs);
#else /* < 4.11.0 */
	ret =
	    drm_fb_helper_init(dev, &fbdev->helper, vbox->num_crtcs,
			       vbox->num_crtcs);
#endif
	if (ret)
		return ret;

#if RTLNX_VER_MAX(5,7,0) && !RTLNX_RHEL_MAJ_PREREQ(8,4) && !RTLNX_SUSE_MAJ_PREREQ(15,3)
	ret = drm_fb_helper_single_add_all_connectors(&fbdev->helper);
	if (ret)
		goto err_fini;
#endif

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

#if RTLNX_VER_MIN(6,3,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99)
	ret = drm_fb_helper_initial_config(&fbdev->helper);
#else
	ret = drm_fb_helper_initial_config(&fbdev->helper, 32);
#endif
	if (ret)
		goto err_fini;

	return 0;

err_fini:
	drm_fb_helper_fini(&fbdev->helper);
	return ret;
}

void vbox_fbdev_set_base(struct vbox_private *vbox, unsigned long gpu_addr)
{
	struct fb_info *fbdev = VBOX_FBDEV_INFO(vbox->fbdev->helper);

#if RTLNX_VER_MIN(6,3,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99)
	fbdev->fix.smem_start = pci_resource_start(VBOX_DRM_TO_PCI_DEV(vbox->fbdev->helper.dev), 0) + gpu_addr;
#else
	fbdev->fix.smem_start = fbdev->apertures->ranges[0].base + gpu_addr;
#endif
	fbdev->fix.smem_len = vbox->available_vram_size - gpu_addr;
}
