/* $Id: vbox_drv.h 109499 2025-05-12 12:21:43Z vadim.galitsyn@oracle.com $ */
/** @file
 * VirtualBox Additions Linux kernel video driver
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
 * This file is based on ast_drv.h
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
 *          Hans de Goede <hdegoede@redhat.com>
 */

#ifndef GA_INCLUDED_SRC_linux_drm_vbox_drv_h
#define GA_INCLUDED_SRC_linux_drm_vbox_drv_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <linux/version.h>

/* iprt/linux/version.h copy - start */
/** @def RTLNX_VER_MIN
 * Evaluates to true if the linux kernel version is equal or higher to the
 * one specfied. */
#define RTLNX_VER_MIN(a_Major, a_Minor, a_Patch) \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(a_Major, a_Minor, a_Patch))

/** @def RTLNX_VER_MAX
 * Evaluates to true if the linux kernel version is less to the one specfied
 * (exclusive). */
#define RTLNX_VER_MAX(a_Major, a_Minor, a_Patch) \
    (LINUX_VERSION_CODE < KERNEL_VERSION(a_Major, a_Minor, a_Patch))

/** @def RTLNX_VER_RANGE
 * Evaluates to true if the linux kernel version is equal or higher to the given
 * minimum version and less (but not equal) to the maximum version (exclusive). */
#define RTLNX_VER_RANGE(a_MajorMin, a_MinorMin, a_PatchMin,  a_MajorMax, a_MinorMax, a_PatchMax) \
    (   LINUX_VERSION_CODE >= KERNEL_VERSION(a_MajorMin, a_MinorMin, a_PatchMin) \
     && LINUX_VERSION_CODE <  KERNEL_VERSION(a_MajorMax, a_MinorMax, a_PatchMax) )


/** @def RTLNX_RHEL_MIN
 * Require a minium RedHat release.
 * @param a_iMajor      The major release number (RHEL_MAJOR).
 * @param a_iMinor      The minor release number (RHEL_MINOR).
 * @sa RTLNX_RHEL_MAX, RTLNX_RHEL_RANGE, RTLNX_RHEL_MAJ_PREREQ
 */
#if defined(RHEL_MAJOR) && defined(RHEL_MINOR)
# define RTLNX_RHEL_MIN(a_iMajor, a_iMinor) \
     ((RHEL_MAJOR) > (a_iMajor) || ((RHEL_MAJOR) == (a_iMajor) && (RHEL_MINOR) >= (a_iMinor)))
#else
# define RTLNX_RHEL_MIN(a_iMajor, a_iMinor) (0)
#endif

/** @def RTLNX_RHEL_MAX
 * Require a maximum RedHat release, true for all RHEL versions below it.
 * @param a_iMajor      The major release number (RHEL_MAJOR).
 * @param a_iMinor      The minor release number (RHEL_MINOR).
 * @sa RTLNX_RHEL_MIN, RTLNX_RHEL_RANGE, RTLNX_RHEL_MAJ_PREREQ
 */
#if defined(RHEL_MAJOR) && defined(RHEL_MINOR)
# define RTLNX_RHEL_MAX(a_iMajor, a_iMinor) \
     ((RHEL_MAJOR) < (a_iMajor) || ((RHEL_MAJOR) == (a_iMajor) && (RHEL_MINOR) < (a_iMinor)))
#else
# define RTLNX_RHEL_MAX(a_iMajor, a_iMinor) (0)
#endif

/** @def RTLNX_RHEL_RANGE
 * Check that it's a RedHat kernel in the given version range.
 * The max version is exclusive, the minimum inclusive.
 * @sa RTLNX_RHEL_MIN, RTLNX_RHEL_MAX, RTLNX_RHEL_MAJ_PREREQ
 */
#if defined(RHEL_MAJOR) && defined(RHEL_MINOR)
# define RTLNX_RHEL_RANGE(a_iMajorMin, a_iMinorMin,  a_iMajorMax, a_iMinorMax) \
     (RTLNX_RHEL_MIN(a_iMajorMin, a_iMinorMin) && RTLNX_RHEL_MAX(a_iMajorMax, a_iMinorMax))
#else
# define RTLNX_RHEL_RANGE(a_iMajorMin, a_iMinorMin,  a_iMajorMax, a_iMinorMax)  (0)
#endif

/** @def RTLNX_RHEL_MAJ_PREREQ
 * Require a minimum minor release number for the given RedHat release.
 * @param a_iMajor      RHEL_MAJOR must _equal_ this.
 * @param a_iMinor      RHEL_MINOR must be greater or equal to this.
 * @sa RTLNX_RHEL_MIN, RTLNX_RHEL_MAX
 */
#if defined(RHEL_MAJOR) && defined(RHEL_MINOR)
# define RTLNX_RHEL_MAJ_PREREQ(a_iMajor, a_iMinor) ((RHEL_MAJOR) == (a_iMajor) && (RHEL_MINOR) >= (a_iMinor))
#else
# define RTLNX_RHEL_MAJ_PREREQ(a_iMajor, a_iMinor) (0)
#endif


/** @def RTLNX_SUSE_MAJ_PREREQ
 * Require a minimum minor release number for the given SUSE release.
 * @param a_iMajor      CONFIG_SUSE_VERSION must _equal_ this.
 * @param a_iMinor      CONFIG_SUSE_PATCHLEVEL must be greater or equal to this.
 */
#if defined(CONFIG_SUSE_VERSION) && defined(CONFIG_SUSE_PATCHLEVEL)
# define RTLNX_SUSE_MAJ_PREREQ(a_iMajor, a_iMinor) ((CONFIG_SUSE_VERSION) == (a_iMajor) && (CONFIG_SUSE_PATCHLEVEL) >= (a_iMinor))
#else
# define RTLNX_SUSE_MAJ_PREREQ(a_iMajor, a_iMinor) (0)
#endif
/* iprt/linux/version.h copy - end */

#if RTLNX_VER_MAX(4,5,0)
# include <linux/types.h>
# include <linux/spinlock_types.h>
#endif

#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/pci.h>


#if RTLNX_VER_MAX(3,14,0) || RTLNX_RHEL_MAJ_PREREQ(7,1)
#define U8_MAX          ((u8)~0U)
#define S8_MAX          ((s8)(U8_MAX>>1))
#define S8_MIN          ((s8)(-S8_MAX - 1))
#define U16_MAX         ((u16)~0U)
#define S16_MAX         ((s16)(U16_MAX>>1))
#define S16_MIN         ((s16)(-S16_MAX - 1))
#define U32_MAX         ((u32)~0U)
#define S32_MAX         ((s32)(U32_MAX>>1))
#define S32_MIN         ((s32)(-S32_MAX - 1))
#define U64_MAX         ((u64)~0ULL)
#define S64_MAX         ((s64)(U64_MAX>>1))
#define S64_MIN         ((s64)(-S64_MAX - 1))
#endif

#if RTLNX_VER_MIN(5,5,0) || RTLNX_RHEL_MIN(8,3) || RTLNX_SUSE_MAJ_PREREQ(15,3)
# include <drm/drm_file.h>
# include <drm/drm_drv.h>
# include <drm/drm_device.h>
# include <drm/drm_ioctl.h>
# include <drm/drm_fourcc.h>
# if RTLNX_VER_MAX(5,15,0) && !RTLNX_RHEL_RANGE(8,7, 8,99) && !RTLNX_RHEL_MAJ_PREREQ(9,1) && !RTLNX_SUSE_MAJ_PREREQ(15,5)
#  include <drm/drm_irq.h>
# endif
# include <drm/drm_vblank.h>
#else /* < 5.5.0 || RHEL < 8.3 || SLES < 15-SP3 */
# include <drm/drmP.h>
#endif
#if RTLNX_VER_MIN(4,11,0) || RTLNX_RHEL_MAJ_PREREQ(7,5)
# include <drm/drm_encoder.h>
#endif
#include <drm/drm_fb_helper.h>
#if RTLNX_VER_MIN(3,18,0) || RTLNX_RHEL_MAJ_PREREQ(7,2)
# include <drm/drm_gem.h>
#endif

#if RTLNX_VER_MIN(6,3,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99)
# include <drm/ttm/ttm_bo.h>
#else
# include <drm/ttm/ttm_bo_api.h>
# include <drm/ttm/ttm_bo_driver.h>
#endif
#include <drm/ttm/ttm_placement.h>
#if RTLNX_VER_MAX(5,13,0) && !RTLNX_RHEL_RANGE(8,6, 8,99)
# include <drm/ttm/ttm_memory.h>
#endif
#if RTLNX_VER_MAX(5,12,0) && !RTLNX_RHEL_MAJ_PREREQ(8,5)
# include <drm/ttm/ttm_module.h>
#endif
#if RTLNX_VER_MIN(5,10,0)
# include <drm/ttm/ttm_resource.h>
#endif

#if RTLNX_VER_MIN(6,0,0) || RTLNX_RHEL_RANGE(8,8, 8,99) || RTLNX_RHEL_MAJ_PREREQ(9,2) || RTLNX_SUSE_MAJ_PREREQ(15,5)
# include <drm/drm_framebuffer.h>
#endif

#include "vboxvideo_guest.h"
#include "vboxvideo_vbe.h"
#include "hgsmi_ch_setup.h"

#include "product-generated.h"

#if RTLNX_VER_MAX(4,12,0) && !RTLNX_RHEL_MAJ_PREREQ(7,5)
static inline void drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	drm_gem_object_unreference_unlocked(obj);
}
#endif

#if RTLNX_VER_MAX(4,12,0) && !RTLNX_RHEL_MAJ_PREREQ(7,5)
static inline void drm_gem_object_put(struct drm_gem_object *obj)
{
	drm_gem_object_unreference(obj);
}
#endif

#define DRIVER_AUTHOR       VBOX_VENDOR

#define DRIVER_NAME         "vboxvideo"
#define DRIVER_DESC         VBOX_PRODUCT " Graphics Card"
#define DRIVER_DATE         "20130823"

#define DRIVER_MAJOR        1
#define DRIVER_MINOR        0
#define DRIVER_PATCHLEVEL   0

#define VBOX_MAX_CURSOR_WIDTH  64
#define VBOX_MAX_CURSOR_HEIGHT 64
#define CURSOR_PIXEL_COUNT (VBOX_MAX_CURSOR_WIDTH * VBOX_MAX_CURSOR_HEIGHT)
#define CURSOR_DATA_SIZE (CURSOR_PIXEL_COUNT * 4 + CURSOR_PIXEL_COUNT / 8)

#define VBOX_MAX_SCREENS  32

#define GUEST_HEAP_OFFSET(vbox) ((vbox)->full_vram_size - \
				 VBVA_ADAPTER_INFORMATION_SIZE)
#define GUEST_HEAP_SIZE   VBVA_ADAPTER_INFORMATION_SIZE
#define GUEST_HEAP_USABLE_SIZE (VBVA_ADAPTER_INFORMATION_SIZE - \
				sizeof(HGSMIHOSTFLAGS))
#define HOST_FLAGS_OFFSET GUEST_HEAP_USABLE_SIZE

/** Field "pdev" of struct drm_device was removed in 5.14. This macro
 * transparently handles this change. Input argument is a pointer
 * to struct drm_device. */
#if RTLNX_VER_MIN(5,14,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
# define VBOX_DRM_TO_PCI_DEV(_dev) to_pci_dev(_dev->dev)
#else
# define VBOX_DRM_TO_PCI_DEV(_dev) _dev->pdev
#endif

/** Field "num_pages" of struct ttm_resource was renamed to "size" in 6.2 and
 * now represents number of bytes. This macro handles this change. Input
 * argument is a pointer to struct ttm_resource. */
#if RTLNX_VER_MIN(6,2,0) || RTLNX_RHEL_RANGE(8,9, 8,99) || RTLNX_RHEL_RANGE(9,3, 9,99)
# define VBOX_BO_RESOURCE_NUM_PAGES(_resource) PFN_UP(_resource->size)
#else
# define VBOX_BO_RESOURCE_NUM_PAGES(_resource) _resource->num_pages
#endif

/** How frequently we refresh if the guest is not providing dirty rectangles. */
#define VBOX_REFRESH_PERIOD (HZ / 2)

#if RTLNX_VER_MAX(3,13,0) && !RTLNX_RHEL_MAJ_PREREQ(7,2)
static inline void *devm_kcalloc(struct device *dev, size_t n, size_t size,
				 gfp_t flags)
{
    return devm_kzalloc(dev, n * size, flags);
}
#endif

struct vbox_fbdev;

struct vbox_private {
	struct drm_device *dev;

	u8 __iomem *guest_heap;
	u8 __iomem *vbva_buffers;
	struct gen_pool *guest_pool;
	struct VBVABUFFERCONTEXT *vbva_info;
	bool any_pitch;
	u32 num_crtcs;
	/** Amount of available VRAM, including space used for buffers. */
	u32 full_vram_size;
	/** Amount of available VRAM, not including space used for buffers. */
	u32 available_vram_size;
	/** Array of structures for receiving mode hints. */
	VBVAMODEHINT *last_mode_hints;

	struct vbox_fbdev *fbdev;

	int fb_mtrr;

	struct {
#if RTLNX_VER_MAX(5,0,0) && !RTLNX_RHEL_MAJ_PREREQ(7,7) && !RTLNX_RHEL_MAJ_PREREQ(8,1)
		struct drm_global_reference mem_global_ref;
		struct ttm_bo_global_ref bo_global_ref;
#endif
#if RTLNX_VER_MIN(5,13,0) || RTLNX_RHEL_RANGE(8,6, 8,99)
		struct ttm_device bdev;
#else
		struct ttm_bo_device bdev;
#endif
		bool mm_initialised;
	} ttm;

	struct mutex hw_mutex; /* protects modeset and accel/vbva accesses */
	/**
	 * We decide whether or not user-space supports display hot-plug
	 * depending on whether they react to a hot-plug event after the initial
	 * mode query.
	 */
	bool initial_mode_queried;
	/**
	 * Do we know that the current user can send us dirty rectangle information?
	 * If not, do periodic refreshes until we do know.
	 */
	bool need_refresh_timer;
	/**
	 * As long as the user is not sending us dirty rectangle information,
	 * refresh the whole screen at regular intervals.
	 */
	struct delayed_work refresh_work;
	struct work_struct hotplug_work;
	u32 input_mapping_width;
	u32 input_mapping_height;
	/**
	 * Is user-space using an X.Org-style layout of one large frame-buffer
	 * encompassing all screen ones or is the fbdev console active?
	 */
	bool single_framebuffer;
	u32 cursor_width;
	u32 cursor_height;
	u32 cursor_hot_x;
	u32 cursor_hot_y;
	size_t cursor_data_size;
	u8 cursor_data[CURSOR_DATA_SIZE];
};

#undef CURSOR_PIXEL_COUNT
#undef CURSOR_DATA_SIZE

#if RTLNX_VER_MIN(4,19,0) || RTLNX_RHEL_MIN(8,3)
int vbox_driver_load(struct drm_device *dev);
#else
int vbox_driver_load(struct drm_device *dev, unsigned long flags);
#endif
#if RTLNX_VER_MIN(4,11,0) || RTLNX_RHEL_MAJ_PREREQ(7,5)
void vbox_driver_unload(struct drm_device *dev);
#else
int vbox_driver_unload(struct drm_device *dev);
#endif
void vbox_driver_lastclose(struct drm_device *dev);

struct vbox_gem_object;

#ifndef VGA_PORT_HGSMI_HOST
#define VGA_PORT_HGSMI_HOST             0x3b0
#define VGA_PORT_HGSMI_GUEST            0x3d0
#endif

struct vbox_connector {
	struct drm_connector base;
	char name[32];
	struct vbox_crtc *vbox_crtc;
	struct {
		u32 width;
		u32 height;
		bool disconnected;
	} mode_hint;
};

struct vbox_crtc {
	struct drm_crtc base;
	bool blanked;
	bool disconnected;
	unsigned int crtc_id;
	u32 fb_offset;
	bool cursor_enabled;
	u32 x_hint;
	u32 y_hint;
};

struct vbox_encoder {
	struct drm_encoder base;
};

struct vbox_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

struct vbox_fbdev {
	struct drm_fb_helper helper;
	struct vbox_framebuffer afb;
	int size;
	struct ttm_bo_kmap_obj mapping;
	int x1, y1, x2, y2;	/* dirty rect */
	spinlock_t dirty_lock;
};

#define to_vbox_crtc(x) container_of(x, struct vbox_crtc, base)
#define to_vbox_connector(x) container_of(x, struct vbox_connector, base)
#define to_vbox_encoder(x) container_of(x, struct vbox_encoder, base)
#define to_vbox_framebuffer(x) container_of(x, struct vbox_framebuffer, base)

int vbox_mode_init(struct drm_device *dev);
void vbox_mode_fini(struct drm_device *dev);

#if RTLNX_VER_MAX(3,3,0)
# define DRM_MODE_FB_CMD drm_mode_fb_cmd
#else
# define DRM_MODE_FB_CMD drm_mode_fb_cmd2
#endif

#if RTLNX_VER_MAX(3,15,0) && !RTLNX_RHEL_MAJ_PREREQ(7,1)
# define CRTC_FB(crtc) ((crtc)->fb)
#else
# define CRTC_FB(crtc) ((crtc)->primary->fb)
#endif

void vbox_enable_accel(struct vbox_private *vbox);
void vbox_disable_accel(struct vbox_private *vbox);
void vbox_report_caps(struct vbox_private *vbox);

void vbox_framebuffer_dirty_rectangles(struct drm_framebuffer *fb,
				       struct drm_clip_rect *rects,
				       unsigned int num_rects);

int vbox_framebuffer_init(struct drm_device *dev,
			  struct vbox_framebuffer *vbox_fb,
#if RTLNX_VER_MIN(4,5,0) || RTLNX_RHEL_MAJ_PREREQ(7,3)
			  const struct DRM_MODE_FB_CMD *mode_cmd,
#else
			  struct DRM_MODE_FB_CMD *mode_cmd,
#endif
			  struct drm_gem_object *obj);

int vboxfb_create(struct drm_fb_helper *helper, struct drm_fb_helper_surface_size *sizes);

int vbox_fbdev_init(struct drm_device *dev);
void vbox_fbdev_fini(struct drm_device *dev);
void vbox_fbdev_set_base(struct vbox_private *vbox, unsigned long gpu_addr);

struct vbox_bo {
	struct ttm_buffer_object bo;
	struct ttm_placement placement;
	struct ttm_bo_kmap_obj kmap;
	struct drm_gem_object gem;
#if RTLNX_VER_MAX(3,18,0) && !RTLNX_RHEL_MAJ_PREREQ(7,2)
	u32 placements[3];
#else
	struct ttm_place placements[3];
#endif
	int pin_count;
};

#define gem_to_vbox_bo(gobj) container_of((gobj), struct vbox_bo, gem)

static inline struct vbox_bo *vbox_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct vbox_bo, bo);
}

#define to_vbox_obj(x) container_of(x, struct vbox_gem_object, base)

int vbox_dumb_create(struct drm_file *file,
		     struct drm_device *dev,
		     struct drm_mode_create_dumb *args);
#if RTLNX_VER_MAX(3,12,0) && !RTLNX_RHEL_MAJ_PREREQ(7,3)
int vbox_dumb_destroy(struct drm_file *file,
		      struct drm_device *dev, u32 handle);
#endif

void vbox_gem_free_object(struct drm_gem_object *obj);
int vbox_dumb_mmap_offset(struct drm_file *file,
			  struct drm_device *dev,
			  u32 handle, u64 *offset);

#define DRM_FILE_PAGE_OFFSET (0x10000000ULL >> PAGE_SHIFT)

int vbox_mm_init(struct vbox_private *vbox);
void vbox_mm_fini(struct vbox_private *vbox);

int vbox_bo_create(struct drm_device *dev, int size, int align,
		   u32 flags, struct vbox_bo **pvboxbo);

int vbox_gem_create(struct drm_device *dev,
		    u32 size, bool iskernel, struct drm_gem_object **obj);

#define VBOX_MEM_TYPE_VRAM   0x1
#define VBOX_MEM_TYPE_SYSTEM 0x2

int vbox_bo_pin(struct vbox_bo *bo, u32 mem_type, u64 *gpu_addr);
int vbox_bo_unpin(struct vbox_bo *bo);

static inline int vbox_bo_reserve(struct vbox_bo *bo, bool no_wait)
{
	int ret;

#if RTLNX_VER_MIN(4,7,0) || RTLNX_RHEL_MAJ_PREREQ(7,4)
	ret = ttm_bo_reserve(&bo->bo, true, no_wait, NULL);
#else
	ret = ttm_bo_reserve(&bo->bo, true, no_wait, false, 0);
#endif
	if (ret) {
		if (ret != -ERESTARTSYS && ret != -EBUSY)
			DRM_ERROR("reserve failed %p\n", bo);
		return ret;
	}
	return 0;
}

static inline void vbox_bo_unreserve(struct vbox_bo *bo)
{
	ttm_bo_unreserve(&bo->bo);
}

void vbox_ttm_placement(struct vbox_bo *bo, u32 mem_type);
int vbox_bo_push_sysram(struct vbox_bo *bo);
int vbox_mmap(struct file *filp, struct vm_area_struct *vma);

/* vbox_prime.c */
int vbox_gem_prime_pin(struct drm_gem_object *obj);
void vbox_gem_prime_unpin(struct drm_gem_object *obj);
struct sg_table *vbox_gem_prime_get_sg_table(struct drm_gem_object *obj);
#if RTLNX_VER_MAX(3,18,0) && !RTLNX_RHEL_MAJ_PREREQ(7,2)
struct drm_gem_object *vbox_gem_prime_import_sg_table(
	struct drm_device *dev, size_t size, struct sg_table *table);
#else
struct drm_gem_object *vbox_gem_prime_import_sg_table(
	struct drm_device *dev, struct dma_buf_attachment *attach,
	struct sg_table *table);
#endif
void *vbox_gem_prime_vmap(struct drm_gem_object *obj);
void vbox_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
#if RTLNX_VER_MAX(6,6,0) && !RTLNX_RHEL_RANGE(9,4, 9,99)
int vbox_gem_prime_mmap(struct drm_gem_object *obj,
			struct vm_area_struct *area);
#endif

/* vbox_irq.c */
int vbox_irq_init(struct vbox_private *vbox);
void vbox_irq_fini(struct vbox_private *vbox);
void vbox_report_hotplug(struct vbox_private *vbox);
irqreturn_t vbox_irq_handler(int irq, void *arg);

/* vbox_hgsmi.c */
void *hgsmi_buffer_alloc(struct gen_pool *guest_pool, size_t size,
			 u8 channel, u16 channel_info);
void hgsmi_buffer_free(struct gen_pool *guest_pool, void *buf);
int hgsmi_buffer_submit(struct gen_pool *guest_pool, void *buf);

static inline void vbox_write_ioport(u16 index, u16 data)
{
	outw(index, VBE_DISPI_IOPORT_INDEX);
	outw(data, VBE_DISPI_IOPORT_DATA);
}

#endif /* !GA_INCLUDED_SRC_linux_drm_vbox_drv_h */
