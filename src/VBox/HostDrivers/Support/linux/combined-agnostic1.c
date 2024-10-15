/* $Id: combined-agnostic1.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SUPDrv - Combine a bunch of OS agnostic sources into one compile unit.
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

#define LOG_GROUP LOG_GROUP_DEFAULT
#include "internal/iprt.h"
#include <VBox/log.h>

#undef LOG_GROUP
#include "r0drv/alloc-r0drv.c"
#undef LOG_GROUP
#include "r0drv/initterm-r0drv.c"
#undef LOG_GROUP
#include "r0drv/memobj-r0drv.c"
#undef LOG_GROUP
#include "r0drv/mpnotification-r0drv.c"
#undef LOG_GROUP
#include "r0drv/powernotification-r0drv.c"
#undef LOG_GROUP
#include "r0drv/generic/semspinmutex-r0drv-generic.c"
#undef LOG_GROUP
#include "common/alloc/alloc.c"
#undef LOG_GROUP
#include "common/checksum/crc32.c"
#undef LOG_GROUP
#include "common/checksum/ipv4.c"
#undef LOG_GROUP
#include "common/checksum/ipv6.c"
#undef LOG_GROUP
#include "common/err/errinfo.c"
#undef LOG_GROUP
#include "common/log/log.c"
#undef LOG_GROUP
#include "common/log/logellipsis.c"
#undef LOG_GROUP
#include "common/log/logrel.c"
#undef LOG_GROUP
#include "common/log/logrelellipsis.c"
#undef LOG_GROUP
#include "common/log/logcom.c"
#undef LOG_GROUP
#include "common/log/logformat.c"
#undef LOG_GROUP
#include "common/log/RTLogCreateEx.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg1Weak.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2Add.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2AddWeak.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2AddWeakV.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2Weak.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2WeakV.c"
#undef LOG_GROUP
#include "common/misc/assert.c"
#undef LOG_GROUP
#include "common/misc/handletable.c"
#undef LOG_GROUP
#include "common/misc/handletablectx.c"
#undef LOG_GROUP
#include "common/misc/thread.c"

