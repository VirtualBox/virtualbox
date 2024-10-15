/* $Id: vbox-libhal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * Module to dynamically load libhal and libdbus and load all symbols
 * which are needed by VirtualBox.
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

#ifndef MAIN_INCLUDED_vbox_libhal_h
#define MAIN_INCLUDED_vbox_libhal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <stdint.h>

#define LIB_HAL "libhal.so.1"

/** Types from the dbus and hal header files which we need.  These are taken more or less
    verbatim from the DBus and Hal public interface header files. */
struct DBusError
{
    const char *name;
    const char *message;
    unsigned int dummy1 : 1; /**< placeholder */
    unsigned int dummy2 : 1; /**< placeholder */
    unsigned int dummy3 : 1; /**< placeholder */
    unsigned int dummy4 : 1; /**< placeholder */
    unsigned int dummy5 : 1; /**< placeholder */
    void *padding1; /**< placeholder */
};
struct DBusConnection;
typedef struct DBusConnection DBusConnection;
typedef uint32_t dbus_bool_t;
typedef enum { DBUS_BUS_SESSON, DBUS_BUS_SYSTEM, DBUS_BUS_STARTER } DBusBusType;
struct LibHalContext_s;
typedef struct LibHalContext_s LibHalContext;

/** The following are the symbols which we need from libdbus and libhal. */
extern void (*gDBusErrorInit)(DBusError *);
extern DBusConnection *(*gDBusBusGet)(DBusBusType, DBusError *);
extern void (*gDBusErrorFree)(DBusError *);
extern void (*gDBusConnectionUnref)(DBusConnection *);
extern LibHalContext *(*gLibHalCtxNew)(void);
extern dbus_bool_t (*gLibHalCtxSetDBusConnection)(LibHalContext *, DBusConnection *);
extern dbus_bool_t (*gLibHalCtxInit)(LibHalContext *, DBusError *);
extern char **(*gLibHalFindDeviceStringMatch)(LibHalContext *, const char *, const char *, int *,
               DBusError *);
extern char *(*gLibHalDeviceGetPropertyString)(LibHalContext *, const char *, const char *,
                                              DBusError *);
extern void (*gLibHalFreeString)(char *);
extern void (*gLibHalFreeStringArray)(char **);
extern dbus_bool_t (*gLibHalCtxShutdown)(LibHalContext *, DBusError *);
extern dbus_bool_t (*gLibHalCtxFree)(LibHalContext *);

extern bool gLibHalCheckPresence(void);

#endif /* !MAIN_INCLUDED_vbox_libhal_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
