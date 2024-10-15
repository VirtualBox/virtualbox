/* $Id: server.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * Common header for XPCOM server and its module counterpart
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
 */

#ifndef MAIN_INCLUDED_SRC_src_server_xpcom_server_h
#define MAIN_INCLUDED_SRC_src_server_xpcom_server_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/com.h>

#include <VBox/version.h>

/**
 * IPC name used to resolve the client ID of the server.
 */
#define VBOXSVC_IPC_NAME "VBoxSVC-" VBOX_VERSION_STRING


/**
 * Tag for the file descriptor passing for the daemonizing control.
 */
#define VBOXSVC_STARTUP_PIPE_NAME "vboxsvc:startup-pipe"

#endif /* !MAIN_INCLUDED_SRC_src_server_xpcom_server_h */
