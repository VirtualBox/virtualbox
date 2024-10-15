/* $Id: VBoxDbgLog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Logging helper
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

#ifndef VBOX_INCLUDED_SRC_win_VBoxDbgLog_h
#define VBOX_INCLUDED_SRC_win_VBoxDbgLog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_DBG_LOG_NAME
# error VBOX_DBG_LOG_NAME should be defined!
#endif

/* Uncomment to show file/line info in the log */
/*#define VBOX_DBG_LOG_SHOWLINEINFO*/

#define VBOX_DBG_LOG_PREFIX_FMT VBOX_DBG_LOG_NAME"::"LOG_FN_FMT": "
#define VBOX_DBG_LOG_PREFIX_PARMS __PRETTY_FUNCTION__

#ifdef VBOX_DBG_LOG_SHOWLINEINFO
# define VBOX_DBG_LOG_SUFFIX_FMT " (%s:%d)\n"
# define VBOX_DBG_LOG_SUFFIX_PARMS ,__FILE__, __LINE__
#else
# define VBOX_DBG_LOG_SUFFIX_FMT "\n"
# define VBOX_DBG_LOG_SUFFIX_PARMS
#endif

#ifdef DEBUG_misha
# define BP_WARN() AssertFailed()
#else
# define BP_WARN() do { } while (0)
#endif

#define _LOGMSG_EXACT(_logger, _a)                                          \
    do                                                                      \
    {                                                                       \
        _logger(_a);                                                        \
    } while (0)

#define _LOGMSG(_logger, _a)                                                \
    do                                                                      \
    {                                                                       \
        _logger((VBOX_DBG_LOG_PREFIX_FMT, VBOX_DBG_LOG_PREFIX_PARMS));  \
        _logger(_a);                                                        \
        _logger((VBOX_DBG_LOG_SUFFIX_FMT  VBOX_DBG_LOG_SUFFIX_PARMS));  \
    } while (0)

/* we can not print paged strings to RT logger, do it this way */
#define _LOGMSG_STR(_logger, _a, _f) do {\
        int _i = 0; \
        _logger(("\"")); \
        for (;(_a)[_i];++_i) { \
            _logger(("%"_f, (_a)[_i])); \
        }\
        _logger(("\"\n")); \
    } while (0)

#define _LOGMSG_USTR(_logger, _a) do {\
        int _i = 0; \
        _logger(("\"")); \
        for (;_i<(_a)->Length/2;++_i) { \
            _logger(("%c", (_a)->Buffer[_i])); \
        }\
        _logger(("\"\n")); \
    } while (0)

#define WARN_NOBP(_a)                                                          \
    do                                                                            \
    {                                                                             \
        Log((VBOX_DBG_LOG_PREFIX_FMT"WARNING! ", VBOX_DBG_LOG_PREFIX_PARMS)); \
        Log(_a);                                                                  \
        Log((VBOX_DBG_LOG_SUFFIX_FMT VBOX_DBG_LOG_SUFFIX_PARMS));             \
    } while (0)

#define WARN(_a)                                                                  \
    do                                                                            \
    {                                                                             \
        WARN_NOBP(_a);                                                         \
        BP_WARN();                                                             \
    } while (0)

#define ASSERT_WARN(_a, _w) do {\
        if(!(_a)) { \
            WARN(_w); \
        }\
    } while (0)

#define LOG(_a) _LOGMSG(Log, _a)
#define LOGREL(_a) _LOGMSG(LogRel, _a)
#define LOGF(_a) _LOGMSG(LogFlow, _a)
#define LOGF_ENTER() LOGF(("ENTER"))
#define LOGF_LEAVE() LOGF(("LEAVE"))
#define LOG_EXACT(_a) _LOGMSG_EXACT(Log, _a)
#define LOGREL_EXACT(_a) _LOGMSG_EXACT(LogRel, _a)
/* we can not print paged strings to RT logger, do it this way */
#define LOG_STRA(_a) do {\
        _LOGMSG_STR(Log, _a, "c"); \
    } while (0)
#define LOG_STRW(_a) do {\
        _LOGMSG_STR(Log, _a, "c"); \
    } while (0)
#define LOG_USTR(_a) do {\
        _LOGMSG_USTR(Log, _a); \
    } while (0)
#define LOGREL_STRA(_a) do {\
        _LOGMSG_STR(LogRel, _a, "c"); \
    } while (0)
#define LOGREL_STRW(_a) do {\
        _LOGMSG_STR(LogRel, _a, "c"); \
    } while (0)
#define LOGREL_USTR(_a) do {\
        _LOGMSG_USTR(LogRel, _a); \
    } while (0)


#endif /* !VBOX_INCLUDED_SRC_win_VBoxDbgLog_h */

