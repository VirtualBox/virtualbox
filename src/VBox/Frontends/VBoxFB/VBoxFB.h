/* $Id: VBoxFB.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox frontends - Framebuffer (FB, DirectFB), Main header file.
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

#ifndef VBOX_INCLUDED_SRC_VBoxFB_VBoxFB_h
#define VBOX_INCLUDED_SRC_VBoxFB_VBoxFB_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

// XPCOM headers
#include <nsIServiceManager.h>
#include <nsIComponentRegistrar.h>
#include <nsXPCOMGlue.h>
#include <nsMemory.h>
#include <nsIProgrammingLanguage.h>
#include <nsIFile.h>
#include <nsILocalFile.h>
#include <nsString.h>
#include <nsReadableUtils.h>
#include <VirtualBox_XPCOM.h>
#include <ipcIService.h>
#include <nsEventQueueUtils.h>
#include <ipcCID.h>
#include <ipcIDConnectService.h>
#define IPC_DCONNECTSERVICE_CONTRACTID \
    "@mozilla.org/ipc/dconnect-service;1"

#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/log.h>
#ifndef VBOX_WITH_XPCOM
# define VBOX_WITH_XPCOM
#endif
#include <VBox/com/com.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>

// DirectFB header
#include <directfb/directfb.h>

/**
 * Executes the passed in expression and verifies the return code.
 *
 * On failure a debug message is printed to stderr and the application will
 * abort with an fatal error.
 */
#define DFBCHECK(x...) \
    do { \
        DFBResult err = x; \
        if (err != DFB_OK) \
        { \
            fprintf(stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
            DirectFBErrorFatal(#x, err); \
        } \
    } while (0)

#include "Helper.h"

/**
 * Globals
 */
extern uint32_t g_useFixedVideoMode;
extern videoMode g_fixedVideoMode;
extern int g_scaleGuest;

#endif /* !VBOX_INCLUDED_SRC_VBoxFB_VBoxFB_h */
