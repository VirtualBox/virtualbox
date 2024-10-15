/* $Id: VBoxMouseLog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Mouse drivers, logging helper
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
 */

#ifndef GA_INCLUDED_SRC_WINNT_Mouse_common_VBoxMouseLog_h
#define GA_INCLUDED_SRC_WINNT_Mouse_common_VBoxMouseLog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/log.h>
#include <iprt/assert.h>

#define VBOX_MOUSE_LOG_NAME "VBoxMouse"

/* Uncomment to show file/line info in the log */
/*#define VBOX_MOUSE_LOG_SHOWLINEINFO*/

#define VBOX_MOUSE_LOG_PREFIX_FMT VBOX_MOUSE_LOG_NAME"::"LOG_FN_FMT": "
#define VBOX_MOUSE_LOG_PREFIX_PARMS __PRETTY_FUNCTION__

#ifdef VBOX_MOUSE_LOG_SHOWLINEINFO
# define VBOX_MOUSE_LOG_SUFFIX_FMT " (%s:%d)\n"
# define VBOX_MOUSE_LOG_SUFFIX_PARMS ,__FILE__, __LINE__
#else
# define VBOX_MOUSE_LOG_SUFFIX_FMT "\n"
# define VBOX_MOUSE_LOG_SUFFIX_PARMS
#endif

#define _LOGMSG(_logger, _a)                                                \
    do                                                                      \
    {                                                                       \
        _logger((VBOX_MOUSE_LOG_PREFIX_FMT, VBOX_MOUSE_LOG_PREFIX_PARMS));  \
        _logger(_a);                                                        \
        _logger((VBOX_MOUSE_LOG_SUFFIX_FMT  VBOX_MOUSE_LOG_SUFFIX_PARMS));  \
    } while (0)

#if 1 /* Exclude yourself if you're not keen on this. */
# define BREAK_WARN() AssertFailed()
#else
# define BREAK_WARN() do {} while(0)
#endif

#define WARN(_a)                                                                  \
    do                                                                            \
    {                                                                             \
        Log((VBOX_MOUSE_LOG_PREFIX_FMT"WARNING! ", VBOX_MOUSE_LOG_PREFIX_PARMS)); \
        Log(_a);                                                                  \
        Log((VBOX_MOUSE_LOG_SUFFIX_FMT VBOX_MOUSE_LOG_SUFFIX_PARMS));             \
        BREAK_WARN(); \
    } while (0)

#define LOG(_a) _LOGMSG(Log, _a)
#define LOGREL(_a) _LOGMSG(LogRel, _a)
#define LOGF(_a) _LOGMSG(LogFlow, _a)
#define LOGF_ENTER() LOGF(("ENTER"))
#define LOGF_LEAVE() LOGF(("LEAVE"))

#endif /* !GA_INCLUDED_SRC_WINNT_Mouse_common_VBoxMouseLog_h */

