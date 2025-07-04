/* $Id: SUPDrv-linux.mod.c 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * VBoxDrv - The VirtualBox Support Driver - Autogenerated Linux code.
 *
 * This is checked in to assist syntax checking the module.
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

#include "SUPDrvInternal.h" /* for KBUILD_STR */
#include "the-linux-kernel.h"
#include <linux/vermagic.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

#undef unix
struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = __stringify(KBUILD_MODNAME),
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
};

static const struct modversion_info ____versions[]
__attribute_used__
__attribute__((section("__versions"))) = {
	{        0, "cleanup_module" },
	{        0, "init_module" },
	{        0, "struct_module" },
	{        0, "strpbrk" },
	{        0, "__kmalloc" },
	{        0, "mem_map" },
	{        0, "vmalloc" },
	{        0, "malloc_sizes" },
	{        0, "vfree" },
	{        0, "change_page_attr" },
	{        0, "__might_sleep" },
	{        0, "remap_page_range" },
	{        0, "__alloc_pages" },
	{        0, "printk" },
	{        0, "__PAGE_KERNEL" },
	{        0, "rwsem_wake" },
	{        0, "copy_to_user" },
	{        0, "preempt_schedule" },
	{        0, "contig_page_data" },
	{        0, "do_mmap_pgoff" },
	{        0, "find_vma" },
	{        0, "kmem_cache_alloc" },
	{        0, "__free_pages" },
	{        0, "do_munmap" },
	{        0, "get_user_pages" },
	{        0, "vsnprintf" },
	{        0, "kfree" },
	{        0, "memcpy" },
	{        0, "put_page" },
	{        0, "__up_wakeup" },
	{        0, "__down_failed" },
	{        0, "copy_from_user" },
	{        0, "rwsem_down_read_failed" },
};

static const char __module_depends[]
__attribute_used__
__attribute__((section(".modinfo"))) =
"depends=";

