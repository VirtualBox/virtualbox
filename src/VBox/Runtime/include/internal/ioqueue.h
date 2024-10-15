/* $Id: ioqueue.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal RTIoQueue header.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_INTERNAL_ioqueue_h
#define IPRT_INCLUDED_INTERNAL_ioqueue_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/ioqueue.h>

RT_C_DECLS_BEGIN

/** The standard file I/O queue provider using synchronous file access. */
extern RTDATADECL(RTIOQUEUEPROVVTABLE const) g_RTIoQueueStdFileProv;
#ifndef RT_OS_OS2
/** The file I/O queue provider using the RTFileAio API. */
extern RTDATADECL(RTIOQUEUEPROVVTABLE const) g_RTIoQueueAioFileProv;
#endif
#if defined(RT_OS_LINUX)
/** The file I/O queue provider using the recently added io_uring interface when
 * available on the host. */
extern RTDATADECL(RTIOQUEUEPROVVTABLE const) g_RTIoQueueLnxIoURingProv;
#endif

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_ioqueue_h */

