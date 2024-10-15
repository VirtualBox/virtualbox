/* $Id: VBoxDTraceLibCWrappers.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDTraceTLibCWrappers.h - IPRT wrappers/fake for lib C stuff.
 *
 * Contributed by: bird
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from http://www.virtualbox.org.
 *
 * The contents of this file are subject to the terms of the Common
 * Development and Distribution License Version 1.0 (CDDL) only, as it
 * comes in the "COPYING.CDDL" file of the VirtualBox distribution.
 *
 * SPDX-License-Identifier: CDDL-1.0
 */

#ifndef VBOX_INCLUDED_SRC_VBoxDTrace_include_VBoxDTraceLibCWrappers_h
#define VBOX_INCLUDED_SRC_VBoxDTrace_include_VBoxDTraceLibCWrappers_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifdef RT_OS_WINDOWS
# include <process.h>
#else
# include <sys/types.h>
# include <limits.h>        /* Workaround for syslimit.h bug in gcc 4.8.3 on gentoo. */
# ifdef RT_OS_DARWIN
#  include <sys/syslimits.h> /* PATH_MAX */
# elif !defined(RT_OS_SOLARIS) && !defined(RT_OS_FREEBSD)
#  include <syslimits.h>    /* PATH_MAX */
# endif
# include <libgen.h>        /* basename */
# include <unistd.h>
# include <strings.h>       /* bzero & bcopy.*/
#endif

#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/param.h>
#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/time.h>


#undef gethrtime
#define gethrtime()                RTTimeNanoTS()
#undef strcasecmp
#define strcasecmp(a_psz1, a_psz2) RTStrICmp(a_psz1, a_psz2)
#undef strncasecmp
#define strncasecmp(a_psz1, a_psz2, a_cch) RTStrNICmp(a_psz1, a_psz2, a_cch)
#undef strlcpy
#define strlcpy(a_pszDst, a_pszSrc, a_cbDst) ((void)RTStrCopy(a_pszDst, a_cbDst, a_pszSrc))

#undef assert
#define assert(expr)               Assert(expr)

#undef PATH_MAX
#define PATH_MAX                    RTPATH_MAX

#undef getpid
#define getpid                      RTProcSelf

#undef basename
#define basename(a_pszPath)         RTPathFilename(a_pszPath)

#undef malloc
#define malloc(a_cb)                RTMemAlloc(a_cb)
#undef calloc
#define calloc(a_cItems, a_cb)      RTMemAllocZ((size_t)(a_cb) * (a_cItems))
#undef realloc
#define realloc(a_pvOld, a_cbNew)   RTMemRealloc(a_pvOld, a_cbNew)
#undef free
#define free(a_pv)                  RTMemFree(a_pv)

/* Not using RTStrDup and RTStrNDup here because the allocation won't be freed
   by RTStrFree and thus may cause trouble when using the efence. */
#undef strdup
#define strdup(a_psz)               ((char *)RTMemDup(a_psz, strlen(a_psz) + 1))
#undef strndup
#define strndup(a_psz, a_cchMax)    ((char *)RTMemDupEx(a_psz, RTStrNLen(a_psz, a_cchMax), 1))

/* For various stupid reasons, these are duplicated in VBoxDTraceTypes.h. */
#undef bcopy
#define bcopy(a_pSrc, a_pDst, a_cb) ((void)memmove(a_pDst, a_pSrc, a_cb))
#undef bzero
#define bzero(a_pDst, a_cb)         ((void)memset(a_pDst, 0, a_cb))
#undef bcmp
#define bcmp(a_p1, a_p2, a_cb)      (memcmp(a_p1, a_p2, a_cb))

#endif /* !VBOX_INCLUDED_SRC_VBoxDTrace_include_VBoxDTraceLibCWrappers_h */

