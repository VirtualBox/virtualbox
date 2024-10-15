/* $Id: vboxvideo_drm.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions - vboxvideo DRM module.
 * FreeBSD kernel OpenGL module.
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
 * SPDX-License-Identifier: GPL-3.0-only
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * tdfx_drv.c -- tdfx driver -*- linux-c -*-
 * Created: Thu Oct  7 10:38:32 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Daryll Strauss <daryll@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "dev/drm/drmP.h"
#include "dev/drm/drm_pciids.h"

#define DRIVER_AUTHOR                   "Oracle Corporation"
#define DRIVER_NAME                     "vboxvideo"
#define DRIVER_DESC                     "VirtualBox DRM"
#define DRIVER_DATE                     "20090317"
#define DRIVER_MAJOR                    1
#define DRIVER_MINOR                    0
#define DRIVER_PATCHLEVEL               0

/** @todo Take PCI IDs from VBox/param.h; VBOX_VESA_VENDORID,
 *        VBOX_VESA_DEVICEID. */
#define vboxvideo_PCI_IDS           { 0x80ee, 0xbeef, 0, "VirtualBox Video" }, \
                                    { 0, 0, 0, NULL }

static drm_pci_id_list_t vboxvideo_pciidlist[] = {
	vboxvideo_PCI_IDS
};

static void vboxvideo_configure(struct drm_device *dev)
{
#if __FreeBSD_version >= 702000
	dev->driver->buf_priv_size	= 1; /* No dev_priv */

	dev->driver->max_ioctl		= 0;

	dev->driver->name		= DRIVER_NAME;
	dev->driver->desc		= DRIVER_DESC;
	dev->driver->date		= DRIVER_DATE;
	dev->driver->major		= DRIVER_MAJOR;
	dev->driver->minor		= DRIVER_MINOR;
	dev->driver->patchlevel		= DRIVER_PATCHLEVEL;
#else
	dev->driver.buf_priv_size	= 1; /* No dev_priv */

	dev->driver.max_ioctl		= 0;

	dev->driver.name		= DRIVER_NAME;
	dev->driver.desc		= DRIVER_DESC;
	dev->driver.date		= DRIVER_DATE;
	dev->driver.major		= DRIVER_MAJOR;
	dev->driver.minor		= DRIVER_MINOR;
	dev->driver.patchlevel		= DRIVER_PATCHLEVEL;
#endif
}

static int
vboxvideo_probe(device_t kdev)
{
	return drm_probe(kdev, vboxvideo_pciidlist);
}

static int
vboxvideo_attach(device_t kdev)
{
	struct drm_device *dev = device_get_softc(kdev);

#if __FreeBSD_version >= 702000
	dev->driver = malloc(sizeof(struct drm_driver_info), DRM_MEM_DRIVER,
	    M_WAITOK | M_ZERO);
#else
	bzero(&dev->driver, sizeof(struct drm_driver_info));
#endif

	vboxvideo_configure(dev);

	return drm_attach(kdev, vboxvideo_pciidlist);
}

static int
vboxvideo_detach(device_t kdev)
{
	struct drm_device *dev = device_get_softc(kdev);
	int ret;

	ret = drm_detach(kdev);

#if __FreeBSD_version >= 702000
	free(dev->driver, DRM_MEM_DRIVER);
#endif

	return ret;
}

static device_method_t vboxvideo_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		vboxvideo_probe),
	DEVMETHOD(device_attach,	vboxvideo_attach),
	DEVMETHOD(device_detach,	vboxvideo_detach),

	{ 0, 0 }
};

static driver_t vboxvideo_driver = {
	"drm",
	vboxvideo_methods,
	sizeof(struct drm_device)
};

extern devclass_t drm_devclass;
#if __FreeBSD_version >= 700010
DRIVER_MODULE(vboxvideo, vgapci, vboxvideo_driver, drm_devclass, 0, 0);
#else
DRIVER_MODULE(vboxvideo, pci, vboxvideo_driver, drm_devclass, 0, 0);
#endif
MODULE_DEPEND(vboxvideo, drm, 1, 1, 1);
