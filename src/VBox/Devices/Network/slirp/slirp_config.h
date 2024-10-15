/* $Id: slirp_config.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - compile-time configuration.
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

/*
 * User definable configuration options
 */

/* Undefine if you don't want talk emulation */
#undef EMULATE_TALK

/* Define if you want the connection to be probed */
/* XXX Not working yet, so ignore this for now */
#undef PROBE_CONN

/* Define to 1 if you want KEEPALIVE timers */
#define DO_KEEPALIVE 0

/* Define to MAX interfaces you expect to use at once */
/* MAX_INTERFACES determines the max. TOTAL number of interfaces (SLIP and PPP) */
/* MAX_PPP_INTERFACES determines max. number of PPP interfaces */
#define MAX_INTERFACES 1
#define MAX_PPP_INTERFACES 1

/* Define if you want slirp's socket in /tmp */
/* XXXXXX Do this in ./configure */
#undef USE_TMPSOCKET

/* Define if you want slirp to use cfsetXspeed() on the terminal */
#undef DO_CFSETSPEED

/* Define this if you want slirp to write to the tty as fast as it can */
/* This should only be set if you are using load-balancing, slirp does a */
/* pretty good job on single modems already, and seting this will make */
/* interactive sessions less responsive */
/* XXXXX Talk about having fast modem as unit 0 */
#undef FULL_BOLT

/*
 * Define if you want slirp to use less CPU
 * You will notice a small lag in interactive sessions, but it's not that bad
 * Things like Netscape/ftp/etc. are completely unaffected
 * This is mainly for sysadmins who have many slirp users
 */
#undef USE_LOWCPU

/*********************************************************/
/*
 * Autoconf defined configuration options
 * You shouldn't need to touch any of these
 */

#ifdef _MSC_VER
#undef HAVE_UNISTD_H
#else
/* Define if you have unistd.h */
#define HAVE_UNISTD_H
#endif

/* Define if you have stdlib.h */
#define HAVE_STDLIB_H

/* Define if you have sys/ioctl.h */
#undef HAVE_SYS_IOCTL_H
#ifndef RT_OS_WINDOWS
# define HAVE_SYS_IOCTL_H
#endif

/* Define if you have sys/filio.h */
#undef HAVE_SYS_FILIO_H
#ifdef __APPLE__
#define HAVE_SYS_FILIO_H
#endif

/* Define according to how time.h should be included */
#define TIME_WITH_SYS_TIME 0
#undef HAVE_SYS_TIME_H

/* Define if you have sys/bitypes.h */
#undef HAVE_SYS_BITYPES_H

/* Define if the machine is big endian */
/*#undef WORDS_BIGENDIAN */

/* Define if you have readv */
#undef HAVE_READV

/* Define if iovec needs to be declared */
#undef DECLARE_IOVEC
#ifdef RT_OS_WINDOWS
# define DECLARE_IOVEC
#endif

/* Define if you have a POSIX.1 sys/wait.h */
#undef HAVE_SYS_WAIT_H

/* Define if you have sys/select.h */
#undef HAVE_SYS_SELECT_H
#ifndef RT_OS_WINDOWS
# define HAVE_SYS_SELECT_H
#endif

/* Define if you have strings.h */
#define HAVE_STRING_H

/* Define if you have arpa/inet.h */
#undef HAVE_ARPA_INET_H
#ifndef RT_OS_WINDOWS
# define HAVE_ARPA_INET_H
#endif

/* Define if you have sys/signal.h */
#undef HAVE_SYS_SIGNAL_H

/* Define if you have sys/stropts.h */
#undef HAVE_SYS_STROPTS_H

/* Define to whatever your compiler thinks inline should be */
#if defined(_MSC_VER) && !defined(__cplusplus)
# define inline _inline
#else
# define inline inline
#endif

/* Define to whatever your compiler thinks const should be */
#define const const

/* Define if you don't have u_int32_t etc. typedef'd */
#undef NEED_TYPEDEFS
#ifdef __sun__
#define NEED_TYPEDEFS
#endif

/* Define to sizeof(char) */
#define SIZEOF_CHAR 1

/* Define to sizeof(short) */
#define SIZEOF_SHORT 2

/* Define to sizeof(int) */
#define SIZEOF_INT 4

/* Define to sizeof(char *) */
#define HOST_LONG_BITS ARCH_BITS
#define SIZEOF_CHAR_P (HOST_LONG_BITS / 8)

/* Define if you have random() */
#undef HAVE_RANDOM

/* Define if you have srandom() */
#undef HAVE_SRANDOM

/* Define if you have inet_aton */
#undef HAVE_INET_ATON
#ifndef RT_OS_WINDOWS
# define HAVE_INET_ATON
#endif

/* Define if you have setenv */
#undef HAVE_SETENV

/* Define if you have index() */
#undef HAVE_INDEX

/* Define if you have bcmp() */
#undef HAVE_BCMP

/* Define if you have drand48 */
#undef HAVE_DRAND48

/* Define if you have memmove */
#define HAVE_MEMMOVE

/* Define if you have gethostid */
#undef HAVE_GETHOSTID
#ifdef RT_OS_OS2
# define HAVE_GETHOSTID
#endif

/* Define if you DON'T have unix-domain sockets */
#undef NO_UNIX_SOCKETS
#ifdef RT_OS_WINDOWS
# define NO_UNIX_SOCKETS
#endif

/* Define if gettimeofday only takes one argument */
#undef GETTIMEOFDAY_ONE_ARG

/* Define if you have revoke() */
#undef HAVE_REVOKE

/* Define if you have the sysv method of opening pty's (/dev/ptmx, etc.) */
#undef HAVE_GRANTPT

/* Define if you have fchmod */
#undef HAVE_FCHMOD

/* Define if you have <sys/type32.h> */
#undef HAVE_SYS_TYPES32_H
#ifdef RT_OS_SOLARIS
# define HAVE_SYS_TYPES32_H
#endif
