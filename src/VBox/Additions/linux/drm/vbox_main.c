/* $Id: vbox_main.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Additions Linux kernel video driver
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
 * This file is based on ast_main.c
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
 * Authors: Dave Airlie <airlied@redhat.com>,
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */
#include "vbox_drv.h"
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>

#if RTLNX_VER_MIN(6,3,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99)
# include <drm/drm_modeset_helper.h>
#endif

#include <VBoxVideoGuest.h>
#include <VBoxVideoVBE.h>

#include "hgsmi_channels.h"

static void vbox_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct vbox_framebuffer *vbox_fb = to_vbox_framebuffer(fb);

	if (vbox_fb->obj)
#if RTLNX_VER_MIN(5,9,0) || RTLNX_RHEL_MIN(8,4) || RTLNX_SUSE_MAJ_PREREQ(15,3)
		drm_gem_object_put(vbox_fb->obj);
#else
		drm_gem_object_put_unlocked(vbox_fb->obj);
#endif

	drm_framebuffer_cleanup(fb);
	kfree(fb);
}

void vbox_enable_accel(struct vbox_private *vbox)
{
	unsigned int i;
	struct VBVABUFFER *vbva;

	if (!vbox->vbva_info || !vbox->vbva_buffers) {
		/* Should never happen... */
		DRM_ERROR("vboxvideo: failed to set up VBVA.\n");
		return;
	}

	for (i = 0; i < vbox->num_crtcs; ++i) {
		if (vbox->vbva_info[i].pVBVA)
			continue;

		vbva = (void __force *)vbox->vbva_buffers +
			i * VBVA_MIN_BUFFER_SIZE;
		if (!VBoxVBVAEnable(&vbox->vbva_info[i],
				 vbox->guest_pool, vbva, i)) {
			/* very old host or driver error. */
			DRM_ERROR("vboxvideo: vbva_enable failed\n");
			return;
		}
	}
}

void vbox_disable_accel(struct vbox_private *vbox)
{
	unsigned int i;

	for (i = 0; i < vbox->num_crtcs; ++i)
		VBoxVBVADisable(&vbox->vbva_info[i], vbox->guest_pool, i);
}

void vbox_report_caps(struct vbox_private *vbox)
{
	u32 caps = VBVACAPS_DISABLE_CURSOR_INTEGRATION |
		   VBVACAPS_IRQ | VBVACAPS_USE_VBVA_ONLY;

	if (vbox->initial_mode_queried)
		caps |= VBVACAPS_VIDEO_MODE_HINTS;

	VBoxHGSMISendCapsInfo(vbox->guest_pool, caps);
}

/**
 * Send information about dirty rectangles to VBVA.  If necessary we enable
 * VBVA first, as this is normally disabled after a change of master in case
 * the new master does not send dirty rectangle information (is this even
 * allowed?)
 */
void vbox_framebuffer_dirty_rectangles(struct drm_framebuffer *fb,
				       struct drm_clip_rect *rects,
				       unsigned int num_rects)
{
	struct vbox_private *vbox = fb->dev->dev_private;
	struct drm_crtc *crtc;
	unsigned int i;

	/* The user can send rectangles, we do not need the timer. */
	vbox->need_refresh_timer = false;
	mutex_lock(&vbox->hw_mutex);
	list_for_each_entry(crtc, &fb->dev->mode_config.crtc_list, head) {
		if (CRTC_FB(crtc) != fb)
			continue;

		for (i = 0; i < num_rects; ++i) {
			VBVACMDHDR cmd_hdr;
			unsigned int crtc_id = to_vbox_crtc(crtc)->crtc_id;

			if ((rects[i].x1 > crtc->x + crtc->hwmode.hdisplay) ||
			    (rects[i].y1 > crtc->y + crtc->hwmode.vdisplay) ||
			    (rects[i].x2 < crtc->x) ||
			    (rects[i].y2 < crtc->y))
				continue;

			cmd_hdr.x = (s16)rects[i].x1;
			cmd_hdr.y = (s16)rects[i].y1;
			cmd_hdr.w = (u16)rects[i].x2 - rects[i].x1;
			cmd_hdr.h = (u16)rects[i].y2 - rects[i].y1;

			if (!VBoxVBVABufferBeginUpdate(&vbox->vbva_info[crtc_id],
						      vbox->guest_pool))
				continue;

			VBoxVBVAWrite(&vbox->vbva_info[crtc_id], vbox->guest_pool,
				   &cmd_hdr, sizeof(cmd_hdr));
			VBoxVBVABufferEndUpdate(&vbox->vbva_info[crtc_id]);
		}
	}
	mutex_unlock(&vbox->hw_mutex);
}

static int vbox_user_framebuffer_dirty(struct drm_framebuffer *fb,
				       struct drm_file *file_priv,
				       unsigned int flags, unsigned int color,
				       struct drm_clip_rect *rects,
				       unsigned int num_rects)
{
	vbox_framebuffer_dirty_rectangles(fb, rects, num_rects);

	return 0;
}

static const struct drm_framebuffer_funcs vbox_fb_funcs = {
	.destroy = vbox_user_framebuffer_destroy,
	.dirty = vbox_user_framebuffer_dirty,
};

int vbox_framebuffer_init(struct drm_device *dev,
			  struct vbox_framebuffer *vbox_fb,
#if RTLNX_VER_MIN(4,5,0) || RTLNX_RHEL_MAJ_PREREQ(7,3)
			  const struct DRM_MODE_FB_CMD *mode_cmd,
#else
			  struct DRM_MODE_FB_CMD *mode_cmd,
#endif
			  struct drm_gem_object *obj)
{
	int ret;

#if RTLNX_VER_MIN(4,11,0) || RTLNX_RHEL_MAJ_PREREQ(7,5)
	drm_helper_mode_fill_fb_struct(dev, &vbox_fb->base, mode_cmd);
#else
	drm_helper_mode_fill_fb_struct(&vbox_fb->base, mode_cmd);
#endif
	vbox_fb->obj = obj;
	ret = drm_framebuffer_init(dev, &vbox_fb->base, &vbox_fb_funcs);
	if (ret) {
		DRM_ERROR("framebuffer init failed %d\n", ret);
		return ret;
	}

	return 0;
}

static struct drm_framebuffer *vbox_user_framebuffer_create(
		struct drm_device *dev,
		struct drm_file *filp,
#if RTLNX_VER_MIN(4,5,0) || RTLNX_RHEL_MAJ_PREREQ(7,3)
		const struct drm_mode_fb_cmd2 *mode_cmd)
#else
		struct drm_mode_fb_cmd2 *mode_cmd)
#endif
{
	struct drm_gem_object *obj;
	struct vbox_framebuffer *vbox_fb;
	int ret = -ENOMEM;

#if RTLNX_VER_MIN(4,7,0) || RTLNX_RHEL_MAJ_PREREQ(7,4)
	obj = drm_gem_object_lookup(filp, mode_cmd->handles[0]);
#else
	obj = drm_gem_object_lookup(dev, filp, mode_cmd->handles[0]);
#endif
	if (!obj)
		return ERR_PTR(-ENOENT);

	vbox_fb = kzalloc(sizeof(*vbox_fb), GFP_KERNEL);
	if (!vbox_fb)
		goto err_unref_obj;

	ret = vbox_framebuffer_init(dev, vbox_fb, mode_cmd, obj);
	if (ret)
		goto err_free_vbox_fb;

	return &vbox_fb->base;

err_free_vbox_fb:
	kfree(vbox_fb);
err_unref_obj:
#if RTLNX_VER_MIN(5,9,0) || RTLNX_RHEL_MIN(8,4) || RTLNX_SUSE_MAJ_PREREQ(15,3)
	drm_gem_object_put(obj);
#else
	drm_gem_object_put_unlocked(obj);
#endif
	return ERR_PTR(ret);
}

static const struct drm_mode_config_funcs vbox_mode_funcs = {
	.fb_create = vbox_user_framebuffer_create,
};

#if RTLNX_VER_MAX(4,0,0) && !RTLNX_RHEL_MAJ_PREREQ(7,3)
# define pci_iomap_range(dev, bar, offset, maxlen) \
	ioremap(pci_resource_start(dev, bar) + (offset), maxlen)
#endif

/**
 * Tell the host about the views.  This design originally targeted the
 * Windows XP driver architecture and assumed that each screen would
 * have a dedicated frame buffer with the command buffer following it,
 * the whole being a "view".  The host works out which screen a command
 * buffer belongs to by checking whether it is in the first view, then
 * whether it is in the second and so on.  The first match wins.  We
 * cheat around this by making the first view be the managed memory
 * plus the first command buffer, the second the same plus the second
 * buffer and so on.
 */
static int vbox_set_views(struct vbox_private *vbox)
{
	VBVAINFOVIEW *p;
	int i;

	p = VBoxHGSMIBufferAlloc(vbox->guest_pool, sizeof(*p),
			       HGSMI_CH_VBVA, VBVA_INFO_VIEW);
	if (!p)
		return -ENOMEM;

	for (i = 0; i < vbox->num_crtcs; ++i) {
		p->u32ViewIndex = i;
		p->u32ViewOffset = 0;
		p->u32ViewSize = vbox->available_vram_size +
			i * VBVA_MIN_BUFFER_SIZE;
		p->u32MaxScreenSize = vbox->available_vram_size;

		VBoxHGSMIBufferSubmit(vbox->guest_pool, p);
	}

	VBoxHGSMIBufferFree(vbox->guest_pool, p);

	return 0;
}

static int vbox_accel_init(struct vbox_private *vbox)
{
	unsigned int i, ret;

	vbox->vbva_info = devm_kcalloc(vbox->dev->dev, vbox->num_crtcs,
				       sizeof(*vbox->vbva_info), GFP_KERNEL);
	if (!vbox->vbva_info)
		return -ENOMEM;

	/* Take a command buffer for each screen from the end of usable VRAM. */
	vbox->available_vram_size -= vbox->num_crtcs * VBVA_MIN_BUFFER_SIZE;

	vbox->vbva_buffers = pci_iomap_range(VBOX_DRM_TO_PCI_DEV(vbox->dev), 0,
					     vbox->available_vram_size,
					     vbox->num_crtcs *
					     VBVA_MIN_BUFFER_SIZE);
	if (!vbox->vbva_buffers)
		return -ENOMEM;

	for (i = 0; i < vbox->num_crtcs; ++i)
		VBoxVBVASetupBufferContext(&vbox->vbva_info[i],
					  vbox->available_vram_size +
					  i * VBVA_MIN_BUFFER_SIZE,
					  VBVA_MIN_BUFFER_SIZE);

	vbox_enable_accel(vbox);
	ret = vbox_set_views(vbox);
	if (ret)
		goto err_pci_iounmap;

	return 0;

err_pci_iounmap:
	pci_iounmap(VBOX_DRM_TO_PCI_DEV(vbox->dev), vbox->vbva_buffers);
	return ret;
}

static void vbox_accel_fini(struct vbox_private *vbox)
{
	vbox_disable_accel(vbox);
	pci_iounmap(VBOX_DRM_TO_PCI_DEV(vbox->dev), vbox->vbva_buffers);
}

/** Do we support the 4.3 plus mode hint reporting interface? */
static bool have_hgsmi_mode_hints(struct vbox_private *vbox)
{
	u32 have_hints, have_cursor;
	int ret;

	ret = VBoxQueryConfHGSMI(vbox->guest_pool,
			       VBOX_VBVA_CONF32_MODE_HINT_REPORTING,
			       &have_hints);
	if (ret)
		return false;

	ret = VBoxQueryConfHGSMI(vbox->guest_pool,
			       VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING,
			       &have_cursor);
	if (ret)
		return false;

	return have_hints == VINF_SUCCESS && have_cursor == VINF_SUCCESS;
}

/**
 * Our refresh timer call-back.  Only used for guests without dirty rectangle
 * support.
 */
static void vbox_refresh_timer(struct work_struct *work)
{
	struct vbox_private *vbox = container_of(work, struct vbox_private,
												 refresh_work.work);
	bool have_unblanked = false;
	struct drm_crtc *crtci;

	if (!vbox->need_refresh_timer)
		return;
	list_for_each_entry(crtci, &vbox->dev->mode_config.crtc_list, head) {
		struct vbox_crtc *vbox_crtc = to_vbox_crtc(crtci);
		if (crtci->enabled && !vbox_crtc->blanked)
			have_unblanked = true;
	}
	if (!have_unblanked)
		return;
	/* This forces a full refresh. */
	vbox_enable_accel(vbox);
	/* Schedule the next timer iteration. */
	schedule_delayed_work(&vbox->refresh_work, VBOX_REFRESH_PERIOD);
}

static bool vbox_check_supported(u16 id)
{
	u16 dispi_id;

	vbox_write_ioport(VBE_DISPI_INDEX_ID, id);
	dispi_id = inw(VBE_DISPI_IOPORT_DATA);

	return dispi_id == id;
}

/**
 * Set up our heaps and data exchange buffers in VRAM before handing the rest
 * to the memory manager.
 */
static int vbox_hw_init(struct vbox_private *vbox)
{
	int ret = -ENOMEM;

	vbox->full_vram_size = inl(VBE_DISPI_IOPORT_DATA);
	vbox->any_pitch = vbox_check_supported(VBE_DISPI_ID_ANYX);

	DRM_INFO("VRAM %08x\n", vbox->full_vram_size);

	/* Map guest-heap at end of vram */
	vbox->guest_heap =
	    pci_iomap_range(VBOX_DRM_TO_PCI_DEV(vbox->dev), 0, GUEST_HEAP_OFFSET(vbox),
			    GUEST_HEAP_SIZE);
	if (!vbox->guest_heap)
		return -ENOMEM;

	/* Create guest-heap mem-pool use 2^4 = 16 byte chunks */
	vbox->guest_pool = gen_pool_create(4, -1);
	if (!vbox->guest_pool)
		goto err_unmap_guest_heap;

	ret = gen_pool_add_virt(vbox->guest_pool,
				(unsigned long)vbox->guest_heap,
				GUEST_HEAP_OFFSET(vbox),
				GUEST_HEAP_USABLE_SIZE, -1);
	if (ret)
		goto err_destroy_guest_pool;

	/* Reduce available VRAM size to reflect the guest heap. */
	vbox->available_vram_size = GUEST_HEAP_OFFSET(vbox);
	/* Linux drm represents monitors as a 32-bit array. */
	VBoxQueryConfHGSMI(vbox->guest_pool, VBOX_VBVA_CONF32_MONITOR_COUNT,
			 &vbox->num_crtcs);
	vbox->num_crtcs = clamp_t(u32, vbox->num_crtcs, 1, VBOX_MAX_SCREENS);

	if (!have_hgsmi_mode_hints(vbox)) {
		ret = -ENOTSUPP;
		goto err_destroy_guest_pool;
	}

	vbox->last_mode_hints = devm_kcalloc(vbox->dev->dev, vbox->num_crtcs,
					     sizeof(VBVAMODEHINT),
					     GFP_KERNEL);
	if (!vbox->last_mode_hints) {
		ret = -ENOMEM;
		goto err_destroy_guest_pool;
	}

	ret = vbox_accel_init(vbox);
	if (ret)
		goto err_destroy_guest_pool;

	/* Set up the refresh timer for users which do not send dirty rectangles. */
	INIT_DELAYED_WORK(&vbox->refresh_work, vbox_refresh_timer);

	return 0;

err_destroy_guest_pool:
	gen_pool_destroy(vbox->guest_pool);
err_unmap_guest_heap:
	pci_iounmap(VBOX_DRM_TO_PCI_DEV(vbox->dev), vbox->guest_heap);
	return ret;
}

static void vbox_hw_fini(struct vbox_private *vbox)
{
	vbox->need_refresh_timer = false;
	cancel_delayed_work(&vbox->refresh_work);
	vbox_accel_fini(vbox);
	gen_pool_destroy(vbox->guest_pool);
	pci_iounmap(VBOX_DRM_TO_PCI_DEV(vbox->dev), vbox->guest_heap);
}

#if RTLNX_VER_MIN(4,19,0) || RTLNX_RHEL_MIN(8,3)
int vbox_driver_load(struct drm_device *dev)
#else
int vbox_driver_load(struct drm_device *dev, unsigned long flags)
#endif
{
	struct vbox_private *vbox;
	int ret = 0;

	if (!vbox_check_supported(VBE_DISPI_ID_HGSMI))
		return -ENODEV;

	vbox = devm_kzalloc(dev->dev, sizeof(*vbox), GFP_KERNEL);
	if (!vbox)
		return -ENOMEM;

	dev->dev_private = vbox;
	vbox->dev = dev;

	mutex_init(&vbox->hw_mutex);

	ret = vbox_hw_init(vbox);
	if (ret)
		return ret;

	ret = vbox_mm_init(vbox);
	if (ret)
		goto err_hw_fini;

	drm_mode_config_init(dev);

	dev->mode_config.funcs = (void *)&vbox_mode_funcs;
	dev->mode_config.min_width = 64;
	dev->mode_config.min_height = 64;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.max_width = VBE_DISPI_MAX_XRES;
	dev->mode_config.max_height = VBE_DISPI_MAX_YRES;

	ret = vbox_mode_init(dev);
	if (ret)
		goto err_drm_mode_cleanup;

	ret = vbox_irq_init(vbox);
	if (ret)
		goto err_mode_fini;

	ret = vbox_fbdev_init(dev);
	if (ret)
		goto err_irq_fini;

	return 0;

err_irq_fini:
	vbox_irq_fini(vbox);
err_mode_fini:
	vbox_mode_fini(dev);
err_drm_mode_cleanup:
	drm_mode_config_cleanup(dev);
	vbox_mm_fini(vbox);
err_hw_fini:
	vbox_hw_fini(vbox);
	return ret;
}

#if RTLNX_VER_MIN(4,11,0) || RTLNX_RHEL_MAJ_PREREQ(7,5)
void vbox_driver_unload(struct drm_device *dev)
#else
int vbox_driver_unload(struct drm_device *dev)
#endif
{
	struct vbox_private *vbox = dev->dev_private;

	vbox_fbdev_fini(dev);
	vbox_irq_fini(vbox);
	vbox_mode_fini(dev);
	drm_mode_config_cleanup(dev);
	vbox_mm_fini(vbox);
	vbox_hw_fini(vbox);
#if RTLNX_VER_MAX(4,11,0) && !RTLNX_RHEL_MAJ_PREREQ(7,5)
	return 0;
#endif
}

/**
 * @note this is described in the DRM framework documentation.  AST does not
 * have it, but we get an oops on driver unload if it is not present.
 */
void vbox_driver_lastclose(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;

#if RTLNX_VER_MIN(3,16,0) || RTLNX_RHEL_MAJ_PREREQ(7,1)
	if (vbox->fbdev)
		drm_fb_helper_restore_fbdev_mode_unlocked(&vbox->fbdev->helper);
#else
	drm_modeset_lock_all(dev);
	if (vbox->fbdev)
		drm_fb_helper_restore_fbdev_mode(&vbox->fbdev->helper);
	drm_modeset_unlock_all(dev);
#endif
}

int vbox_gem_create(struct drm_device *dev,
		    u32 size, bool iskernel, struct drm_gem_object **obj)
{
	struct vbox_bo *vboxbo;
	int ret;

	*obj = NULL;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
	{
		DRM_ERROR("bad size\n");
		return -EINVAL;
	}

	ret = vbox_bo_create(dev, size, 0, 0, &vboxbo);
	if (ret) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("failed to allocate GEM object\n");
		DRM_ERROR("failed to allocate GEM (%d)\n", ret);
		return ret;
	}

	*obj = &vboxbo->gem;

	return 0;
}

int vbox_dumb_create(struct drm_file *file,
		     struct drm_device *dev, struct drm_mode_create_dumb *args)
{
	int ret;
	struct drm_gem_object *gobj;
	u32 handle;

	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	ret = vbox_gem_create(dev, args->size, false, &gobj);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file, gobj, &handle);
#if RTLNX_VER_MIN(5,9,0) || RTLNX_RHEL_MIN(8,4) || RTLNX_SUSE_MAJ_PREREQ(15,3)
	drm_gem_object_put(gobj);
#else
	drm_gem_object_put_unlocked(gobj);
#endif
	if (ret)
		return ret;

	args->handle = handle;

	return 0;
}

#if RTLNX_VER_MAX(3,12,0) && !RTLNX_RHEL_MAJ_PREREQ(7,3)
int vbox_dumb_destroy(struct drm_file *file,
		      struct drm_device *dev, u32 handle)
{
	return drm_gem_handle_delete(file, handle);
}
#endif

#if RTLNX_VER_MAX(4,19,0) && !RTLNX_RHEL_MAJ_PREREQ(7,7) && !RTLNX_RHEL_MAJ_PREREQ(8,1) && !RTLNX_SUSE_MAJ_PREREQ(15,1) && !RTLNX_SUSE_MAJ_PREREQ(12,5)
static void ttm_bo_put(struct ttm_buffer_object *bo)
{
	ttm_bo_unref(&bo);
}
#endif

void vbox_gem_free_object(struct drm_gem_object *obj)
{
	struct vbox_bo *vbox_bo = gem_to_vbox_bo(obj);

#if RTLNX_VER_MIN(5,14,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	/* Starting from kernel 5.14, there is a warning appears in dmesg
	 * on attempt to desroy pinned buffer object. Make sure it is unpinned. */
	while (vbox_bo->bo.pin_count)
	{
		int ret;
		ret = vbox_bo_unpin(vbox_bo);
		if (ret)
		{
			DRM_ERROR("unable to unpin buffer object\n");
			break;
		}
	}
#endif

	ttm_bo_put(&vbox_bo->bo);
}

static inline u64 vbox_bo_mmap_offset(struct vbox_bo *bo)
{
#if RTLNX_VER_MIN(5,4,0) || RTLNX_RHEL_MIN(8,3) || RTLNX_SUSE_MAJ_PREREQ(15,3)
        return drm_vma_node_offset_addr(&bo->bo.base.vma_node);
#elif RTLNX_VER_MAX(3,12,0) && !RTLNX_RHEL_MAJ_PREREQ(7,0)
	return bo->bo.addr_space_offset;
#else
	return drm_vma_node_offset_addr(&bo->bo.vma_node);
#endif /* >= 5.4.0 */
}

int
vbox_dumb_mmap_offset(struct drm_file *file,
		      struct drm_device *dev,
		      u32 handle, u64 *offset)
{
	struct drm_gem_object *obj;
	int ret = 0;
	struct vbox_bo *bo;

	mutex_lock(&dev->struct_mutex);
#if RTLNX_VER_MIN(4,7,0) || RTLNX_RHEL_MAJ_PREREQ(7,4)
	obj = drm_gem_object_lookup(file, handle);
#else
	obj = drm_gem_object_lookup(dev, file, handle);
#endif
	if (!obj) {
		ret = -ENOENT;
		goto out_unlock;
	}

	bo = gem_to_vbox_bo(obj);
	*offset = vbox_bo_mmap_offset(bo);

#if RTLNX_VER_MIN(5,14,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
	ret = drm_vma_node_allow(&bo->bo.base.vma_node, file);
	if (ret)
	{
		DRM_ERROR("unable to grant previladges to user");
	}
#endif

	drm_gem_object_put(obj);

out_unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}
