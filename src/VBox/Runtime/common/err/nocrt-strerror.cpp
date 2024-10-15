/* $Id: nocrt-strerror.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - Convert errno value to string.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define IPRT_NO_CRT_FOR_3RD_PARTY
#include "internal/nocrt.h"
#include <iprt/nocrt/string.h>
#include <iprt/nocrt/errno.h>
#include <iprt/assert.h>
#include <iprt/log.h>


#undef strerror
const char *RT_NOCRT(strerror)(int iErrNo)
{
    /*
     * Process error codes.
     *
     * (Use a switch and not a table since the numbers vary among compilers
     * and OSes. So we let the compiler switch optimizer handle speed issues.)
     *
     * This switch is arranged like the Linux i386 errno.h! This switch is mirrored
     * by RTErrConvertToErrno and RTErrConvertFromErrno.
     */
    switch (iErrNo)
    {                                                                           /* Linux number */
        case 0: return "no error";
#ifdef EPERM
        RT_CASE_RET_STR(EPERM);                      /*   1 */
#endif
#ifdef ENOENT
        RT_CASE_RET_STR(ENOENT);
#endif
#ifdef ESRCH
        RT_CASE_RET_STR(ESRCH);
#endif
#ifdef EINTR
        RT_CASE_RET_STR(EINTR);
#endif
#ifdef EIO
        RT_CASE_RET_STR(EIO);
#endif
#ifdef ENXIO
        RT_CASE_RET_STR(ENXIO); /** @todo fix this duplicate error */
#endif
#ifdef E2BIG
        RT_CASE_RET_STR(E2BIG);
#endif
#ifdef ENOEXEC
        RT_CASE_RET_STR(ENOEXEC);
#endif
#ifdef EBADF
        RT_CASE_RET_STR(EBADF);
#endif
#ifdef ECHILD
        RT_CASE_RET_STR(ECHILD);                  /*  10 */ /** @todo fix duplicate error */
#endif
#ifdef EAGAIN
        RT_CASE_RET_STR(EAGAIN);
#endif
#ifdef ENOMEM
        RT_CASE_RET_STR(ENOMEM);
#endif
#ifdef EACCES
        RT_CASE_RET_STR(EACCES); /** @todo fix duplicate error */
#endif
#ifdef EFAULT
        RT_CASE_RET_STR(EFAULT);
#endif
#ifdef ENOTBLK
        RT_CASE_RET_STR(ENOTBLK);
#endif
#ifdef EBUSY
        RT_CASE_RET_STR(EBUSY);
#endif
#ifdef EEXIST
        RT_CASE_RET_STR(EEXIST);
#endif
#ifdef EXDEV
        RT_CASE_RET_STR(EXDEV);
#endif
#ifdef ENODEV
        RT_CASE_RET_STR(ENODEV); /** @todo fix duplicate error */
#endif
#ifdef ENOTDIR
        RT_CASE_RET_STR(ENOTDIR);                     /*  20 */
#endif
#ifdef EISDIR
        RT_CASE_RET_STR(EISDIR);
#endif
#ifdef EINVAL
        RT_CASE_RET_STR(EINVAL);
#endif
#ifdef ENFILE
        RT_CASE_RET_STR(ENFILE); /** @todo fix duplicate error */
#endif
#ifdef EMFILE
        RT_CASE_RET_STR(EMFILE);
#endif
#ifdef ENOTTY
        RT_CASE_RET_STR(ENOTTY);
#endif
#ifdef ETXTBSY
        RT_CASE_RET_STR(ETXTBSY);
#endif
#ifdef EFBIG
        RT_CASE_RET_STR(EFBIG);
#endif
#ifdef ENOSPC
        RT_CASE_RET_STR(ENOSPC);
#endif
#ifdef ESPIPE
        RT_CASE_RET_STR(ESPIPE);
#endif
#ifdef EROFS
        RT_CASE_RET_STR(EROFS);                      /*  30 */
#endif
#ifdef EMLINK
         RT_CASE_RET_STR(EMLINK);
#endif
#ifdef EPIPE
        RT_CASE_RET_STR(EPIPE);
#endif
#ifdef EDOM
        RT_CASE_RET_STR(EDOM);  /** @todo fix duplicate error */
#endif
#ifdef ERANGE
        RT_CASE_RET_STR(ERANGE);  /** @todo fix duplicate error */
#endif
#ifdef EDEADLK
        RT_CASE_RET_STR(EDEADLK);
#endif
#ifdef ENAMETOOLONG
        RT_CASE_RET_STR(ENAMETOOLONG);
#endif
#ifdef ENOLCK
        RT_CASE_RET_STR(ENOLCK);
#endif
#ifdef ENOSYS /** @todo map this differently on solaris. */
        RT_CASE_RET_STR(ENOSYS);
#endif
#ifdef ENOTEMPTY
        RT_CASE_RET_STR(ENOTEMPTY);
#endif
#ifdef ELOOP
        RT_CASE_RET_STR(ELOOP);                  /*  40 */
#endif
        //41??
#ifdef ENOMSG
        RT_CASE_RET_STR(ENOMSG);
#endif
#ifdef EIDRM
        RT_CASE_RET_STR(EIDRM);
#endif
#ifdef ECHRNG
        RT_CASE_RET_STR(ECHRNG);
#endif
#ifdef EL2NSYNC
        RT_CASE_RET_STR(EL2NSYNC);
#endif
#ifdef EL3HLT
        RT_CASE_RET_STR(EL3HLT);
#endif
#ifdef EL3RST
        RT_CASE_RET_STR(EL3RST);
#endif
#ifdef ELNRNG
        RT_CASE_RET_STR(ELNRNG);
#endif
#ifdef EUNATCH
        RT_CASE_RET_STR(EUNATCH);
#endif
#ifdef ENOCSI
        RT_CASE_RET_STR(ENOCSI);
#endif
#ifdef EL2HLT
        RT_CASE_RET_STR(EL2HLT);
#endif
#ifdef EBADE
        RT_CASE_RET_STR(EBADE);
#endif
#ifdef EBADR
        RT_CASE_RET_STR(EBADR);
#endif
#ifdef EXFULL
        RT_CASE_RET_STR(EXFULL);
#endif
#ifdef ENOANO
        RT_CASE_RET_STR(ENOANO);
#endif
#ifdef EBADRQC
        RT_CASE_RET_STR(EBADRQC);
#endif
#ifdef EBADSLT
        RT_CASE_RET_STR(EBADSLT);
#endif
        //case 58:
#ifdef EBFONT
        RT_CASE_RET_STR(EBFONT);
#endif
#ifdef ENOSTR
        RT_CASE_RET_STR(ENOSTR);
#endif
#ifdef ENODATA
        RT_CASE_RET_STR(ENODATA);
#endif
#ifdef ETIME
        RT_CASE_RET_STR(ETIME);
#endif
#ifdef ENOSR
        RT_CASE_RET_STR(ENOSR);
#endif
#ifdef ENONET
        RT_CASE_RET_STR(ENONET);
#endif
#ifdef ENOPKG
        RT_CASE_RET_STR(ENOPKG);
#endif
#ifdef EREMOTE
        RT_CASE_RET_STR(EREMOTE);
#endif
#ifdef ENOLINK
        RT_CASE_RET_STR(ENOLINK);
#endif
#ifdef EADV
        RT_CASE_RET_STR(EADV);
#endif
#ifdef ESRMNT
        RT_CASE_RET_STR(ESRMNT);
#endif
#ifdef ECOMM
        RT_CASE_RET_STR(ECOMM);
#endif
#ifdef EPROTO
        RT_CASE_RET_STR(EPROTO);
#endif
#ifdef EMULTIHOP
        RT_CASE_RET_STR(EMULTIHOP);
#endif
#ifdef EDOTDOT
        RT_CASE_RET_STR(EDOTDOT);
#endif
#ifdef EBADMSG
        RT_CASE_RET_STR(EBADMSG);
#endif
#ifdef EOVERFLOW
        RT_CASE_RET_STR(EOVERFLOW); /** @todo fix duplicate error? */
#endif
#ifdef ENOTUNIQ
        RT_CASE_RET_STR(ENOTUNIQ);
#endif
#ifdef EBADFD
        RT_CASE_RET_STR(EBADFD); /** @todo fix duplicate error? */
#endif
#ifdef EREMCHG
        RT_CASE_RET_STR(EREMCHG);
#endif
#ifdef ELIBACC
        RT_CASE_RET_STR(ELIBACC);
#endif
#ifdef ELIBBAD
        RT_CASE_RET_STR(ELIBBAD);
#endif
#ifdef ELIBSCN
        RT_CASE_RET_STR(ELIBSCN);
#endif
#ifdef ELIBMAX
        RT_CASE_RET_STR(ELIBMAX);
#endif
#ifdef ELIBEXEC
        RT_CASE_RET_STR(ELIBEXEC);
#endif
#ifdef EILSEQ
        RT_CASE_RET_STR(EILSEQ);
#endif
#ifdef ERESTART
        RT_CASE_RET_STR(ERESTART);/** @todo fix duplicate error?*/
#endif
#ifdef ESTRPIPE
        RT_CASE_RET_STR(ESTRPIPE);
#endif
#ifdef EUSERS
        RT_CASE_RET_STR(EUSERS);
#endif
#ifdef ENOTSOCK
        RT_CASE_RET_STR(ENOTSOCK);
#endif
#ifdef EDESTADDRREQ
        RT_CASE_RET_STR(EDESTADDRREQ);
#endif
#ifdef EMSGSIZE
        RT_CASE_RET_STR(EMSGSIZE);
#endif
#ifdef EPROTOTYPE
        RT_CASE_RET_STR(EPROTOTYPE);
#endif
#ifdef ENOPROTOOPT
        RT_CASE_RET_STR(ENOPROTOOPT);
#endif
#ifdef EPROTONOSUPPORT
        RT_CASE_RET_STR(EPROTONOSUPPORT);
#endif
#ifdef ESOCKTNOSUPPORT
        RT_CASE_RET_STR(ESOCKTNOSUPPORT);
#endif
#ifdef EOPNOTSUPP /** @todo map this differently on solaris. */
        RT_CASE_RET_STR(EOPNOTSUPP);
#endif
#ifdef EPFNOSUPPORT
        RT_CASE_RET_STR(EPFNOSUPPORT);
#endif
#ifdef EAFNOSUPPORT
        RT_CASE_RET_STR(EAFNOSUPPORT);
#endif
#ifdef EADDRINUSE
        RT_CASE_RET_STR(EADDRINUSE);
#endif
#ifdef EADDRNOTAVAIL
        RT_CASE_RET_STR(EADDRNOTAVAIL);
#endif
#ifdef ENETDOWN
        RT_CASE_RET_STR(ENETDOWN);
#endif
#ifdef ENETUNREACH
        RT_CASE_RET_STR(ENETUNREACH);
#endif
#ifdef ENETRESET
        RT_CASE_RET_STR(ENETRESET);
#endif
#ifdef ECONNABORTED
        RT_CASE_RET_STR(ECONNABORTED);
#endif
#ifdef ECONNRESET
        RT_CASE_RET_STR(ECONNRESET);
#endif
#ifdef ENOBUFS
        RT_CASE_RET_STR(ENOBUFS);
#endif
#ifdef EISCONN
        RT_CASE_RET_STR(EISCONN);
#endif
#ifdef ENOTCONN
        RT_CASE_RET_STR(ENOTCONN);
#endif
#ifdef ESHUTDOWN
        RT_CASE_RET_STR(ESHUTDOWN);
#endif
#ifdef ETOOMANYREFS
        RT_CASE_RET_STR(ETOOMANYREFS);
#endif
#ifdef ETIMEDOUT
        RT_CASE_RET_STR(ETIMEDOUT);
#endif
#ifdef ECONNREFUSED
        RT_CASE_RET_STR(ECONNREFUSED);
#endif
#ifdef EHOSTDOWN
        RT_CASE_RET_STR(EHOSTDOWN);
#endif
#ifdef EHOSTUNREACH
        RT_CASE_RET_STR(EHOSTUNREACH);
#endif
#ifdef EALREADY
# if !defined(ENOLCK) || (EALREADY != ENOLCK)
        RT_CASE_RET_STR(EALREADY);
# endif
#endif
#ifdef EINPROGRESS
# if !defined(ENODEV) || (EINPROGRESS != ENODEV)
        RT_CASE_RET_STR(EINPROGRESS);
# endif
#endif
#ifdef ESTALE
        RT_CASE_RET_STR(ESTALE); /* 116: Stale NFS file handle */
#endif
#ifdef EUCLEAN
        RT_CASE_RET_STR(EUCLEAN);
#endif
#ifdef ENOTNAM
        RT_CASE_RET_STR(ENOTNAM);
#endif
#ifdef ENAVAIL
        RT_CASE_RET_STR(ENAVAIL);
#endif
#ifdef EISNAM
        RT_CASE_RET_STR(EISNAM);
#endif
#ifdef EREMOTEIO
        RT_CASE_RET_STR(EREMOTEIO);
#endif
#ifdef EDQUOT
        RT_CASE_RET_STR(EDQUOT); /** @todo fix duplicate error */
#endif
#ifdef ENOMEDIUM
        RT_CASE_RET_STR(ENOMEDIUM);
#endif
#ifdef EMEDIUMTYPE
        RT_CASE_RET_STR(EMEDIUMTYPE);
#endif
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
        RT_CASE_RET_STR(EWOULDBLOCK);
#endif

        /* Non-linux */

#ifdef EPROCLIM
        RT_CASE_RET_STR(EPROCLIM);
#endif
#ifdef EDOOFUS
# if EDOOFUS != EINVAL
        RT_CASE_RET_STR(EDOOFUS);
# endif
#endif
#ifdef ENOTSUP
# ifndef EOPNOTSUPP
        RT_CASE_RET_STR(ENOTSUP);
# else
#  if ENOTSUP != EOPNOTSUPP
        RT_CASE_RET_STR(ENOTSUP);
#  endif
# endif
#endif
        default:
            AssertLogRelMsgFailedReturn(("Unhandled error code %d\n", iErrNo), "unknown-errno-value");
    }
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(strerror);

