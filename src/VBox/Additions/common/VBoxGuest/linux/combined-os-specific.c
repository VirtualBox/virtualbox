/* $Id: combined-os-specific.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuest - Combine a bunch of OS specific sources into one compile unit.
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


#include "the-linux-kernel.h"

#include "r0drv/linux/alloc-r0drv-linux.c"
#include "r0drv/linux/assert-r0drv-linux.c"
#include "r0drv/linux/initterm-r0drv-linux.c"
#include "r0drv/linux/memobj-r0drv-linux.c"
#include "r0drv/linux/memuserkernel-r0drv-linux.c"
#include "r0drv/linux/mp-r0drv-linux.c"
#include "r0drv/linux/mpnotification-r0drv-linux.c"
#include "r0drv/linux/process-r0drv-linux.c"
#include "r0drv/linux/semevent-r0drv-linux.c"
#include "r0drv/linux/semeventmulti-r0drv-linux.c"
#include "r0drv/linux/semfastmutex-r0drv-linux.c"
#include "r0drv/linux/semmutex-r0drv-linux.c"
#include "r0drv/linux/spinlock-r0drv-linux.c"
#include "r0drv/linux/thread-r0drv-linux.c"
#include "r0drv/linux/thread2-r0drv-linux.c"
#undef LOG_GROUP
#include "r0drv/linux/time-r0drv-linux.c"
#include "r0drv/linux/timer-r0drv-linux.c"
#include "r0drv/linux/RTLogWriteDebugger-r0drv-linux.c"
#include "common/err/RTErrConvertFromErrno.c"
#include "common/err/RTErrConvertToErrno.c"

