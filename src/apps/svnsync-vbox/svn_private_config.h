/* $Id: svn_private_config.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 */
/*
 * svn_private_config.h
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */




#ifndef SVN_PRIVATE_CONFIG_H
#define SVN_PRIVATE_CONFIG_H
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* The version of Berkeley DB we want */
#define SVN_FS_WANT_DB_MAJOR    4
#define SVN_FS_WANT_DB_MINOR    0
#define SVN_FS_WANT_DB_PATCH    14


/* Path separator for local filesystem */
#define SVN_PATH_LOCAL_SEPARATOR '/'

/* Name of system's null device */
#define SVN_NULL_DEVICE_NAME "/dev/null"

/* Link fs fs library into the fs library */
#define SVN_LIBSVN_FS_LINKS_FS_FS

/* Link local repos access library to client */
#define SVN_LIBSVN_CLIENT_LINKS_RA_LOCAL

/* Link pipe repos access library to client */
#define SVN_LIBSVN_CLIENT_LINKS_RA_SVN

/* Defined to be the path to the installed binaries */
#define SVN_BINDIR "/usr/local/bin"



/* The default FS back-end type */
#define DEFAULT_FS_TYPE "fsfs"


/* Define to the Python/C API format character suitable for apr_int64_t */
#define SVN_APR_INT64_T_PYCFMT "L"

/* Setup gettext macros */
#define N_(x) x
#define PACKAGE_NAME "subversion"

#ifdef ENABLE_NLS
#define SVN_LOCALE_RELATIVE_PATH "../share/locale"
#include <locale.h>
#include <libintl.h>
#define _(x) dgettext(PACKAGE_NAME, x)
#else
#define _(x) (x)
#define gettext(x) (x)
#define dgettext(domain,x) (x)
#endif

#endif /* SVN_PRIVATE_CONFIG_H */
