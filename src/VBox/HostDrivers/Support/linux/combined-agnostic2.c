/* $Id: combined-agnostic2.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include "common/string/RTStrCat.c"
#undef LOG_GROUP
#include "common/string/RTStrCopy.c"
#undef LOG_GROUP
#include "common/string/RTStrCopyEx.c"
#undef LOG_GROUP
#include "common/string/RTStrCopyP.c"
#undef LOG_GROUP
#include "common/string/RTStrEnd.c"
#undef LOG_GROUP
#include "common/string/RTStrNCmp.c"
#undef LOG_GROUP
#include "common/string/RTStrNLen.c"
#undef LOG_GROUP
#include "common/string/stringalloc.c"
#undef LOG_GROUP
#include "common/string/strformat.c"
#undef LOG_GROUP
#include "common/string/RTStrFormat.c"
#undef LOG_GROUP
#include "common/string/strformatnum.c"
#undef LOG_GROUP
#include "common/string/strformattype.c"
#undef LOG_GROUP
#include "common/string/strprintf.c"
#undef LOG_GROUP
#include "common/string/strprintf-ellipsis.c"
#undef LOG_GROUP
#include "common/string/strprintf2.c"
#undef LOG_GROUP
#include "common/string/strprintf2-ellipsis.c"
#undef LOG_GROUP
#include "common/string/strtonum.c"
#undef LOG_GROUP
#include "common/table/avlpv.c"
#undef LOG_GROUP
#include "common/time/time.c"
#undef LOG_GROUP
#include "generic/RTAssertShouldPanic-generic.c"
#undef LOG_GROUP
#include "generic/RTLogWriteStdErr-stub-generic.c"
#undef LOG_GROUP
#include "generic/RTLogWriteStdOut-stub-generic.c"
#undef LOG_GROUP
#include "generic/RTLogWriteUser-generic.c"
#undef LOG_GROUP
#include "generic/RTMpGetArraySize-generic.c"
#undef LOG_GROUP
#include "generic/RTMpGetCoreCount-generic.c"
#undef LOG_GROUP
#include "generic/RTSemEventWait-2-ex-generic.c"
#undef LOG_GROUP
#include "generic/RTSemEventWaitNoResume-2-ex-generic.c"
#undef LOG_GROUP
#include "generic/RTSemEventMultiWait-2-ex-generic.c"
#undef LOG_GROUP
#include "generic/RTSemEventMultiWaitNoResume-2-ex-generic.c"
#undef LOG_GROUP
#include "generic/RTTimerCreate-generic.c"
#undef LOG_GROUP
#include "generic/errvars-generic.c"
#undef LOG_GROUP
#include "generic/mppresent-generic.c"
#undef LOG_GROUP
#include "generic/system-page-size-generic.c"
#undef LOG_GROUP
#include "generic/uuid-generic.c"
#undef LOG_GROUP
#include "VBox/log-vbox.c"
#undef LOG_GROUP
#include "VBox/RTLogWriteVmm-amd64-x86.c"

#ifdef RT_ARCH_AMD64
# undef LOG_GROUP
# include "common/alloc/heapsimple.c"
#endif

