/* $Id: LoggingNew.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM - logging macros and function definitions, for new code.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_LoggingNew_h
#define MAIN_INCLUDED_LoggingNew_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef MAIN_INCLUDED_Logging_h
# error "You must include LoggingNew.h as the first include!"
#endif
#define MAIN_INCLUDED_Logging_h /* Prevent Logging.h from being included. */

#ifndef LOG_GROUP
# error "You must define LOG_GROUP immediately before including LoggingNew.h!"
#endif

#ifdef LOG_GROUP_MAIN_OVERRIDE
# error "Please, don't define LOG_GROUP_MAIN_OVERRIDE anymore!"
#endif

#include <VBox/log.h>


#ifndef VBOXSVC_LOG_DEFAULT
# define VBOXSVC_LOG_DEFAULT "all"
#endif

#ifndef VBOXSDS_LOG_DEFAULT
# define VBOXSDS_LOG_DEFAULT "all"
#endif

#endif /* !MAIN_INCLUDED_LoggingNew_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */

