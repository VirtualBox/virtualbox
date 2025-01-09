/* $Id: VBoxVideoErr.h 107674 2025-01-09 18:12:18Z vadim.galitsyn@oracle.com $ */
/** @file
 * VirtualBox Video driver, common code - iprt and VirtualBox macros and
 * definitions.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VBOX_INCLUDED_Graphics_VBoxVideoErr_h
#define VBOX_INCLUDED_Graphics_VBoxVideoErr_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @name VirtualBox error macros
 * @{ */

#define VINF_SUCCESS                        0
#define VERR_INVALID_PARAMETER              (-2)
#define VERR_INVALID_POINTER                (-6)
#define VERR_NO_MEMORY                      (-8)
#define VERR_NOT_IMPLEMENTED                (-12)
#define VERR_INVALID_FUNCTION               (-36)
#define VERR_NOT_SUPPORTED                  (-37)
#define VERR_TOO_MUCH_DATA                  (-42)
#define VERR_NOT_FOUND                      (-78)
#define VERR_INVALID_STATE                  (-79)
#define VERR_OUT_OF_RESOURCES               (-80)
#define VERR_ALREADY_EXISTS                 (-105)
#define VERR_INTERNAL_ERROR                 (-225)

#define RT_SUCCESS_NP(rc)   ( (int)(rc) >= VINF_SUCCESS )
#define RT_SUCCESS(rc)      ( likely(RT_SUCCESS_NP(rc)) )
#define RT_FAILURE(rc)      ( unlikely(!RT_SUCCESS_NP(rc)) )

/** @}  */

/** @name VirtualBox assertions
 * @{ */

/* Unlike BUILD_BUG_ON(), these can be used outside of functions. */
extern int vbox_assert_var[1];
#define assert_compile(expr) \
    extern int vbox_assert_var[1] __attribute__((__unused__)), \
    vbox_assert_var[(expr) ? 1 : 0] __attribute__((__unused__))
#define assert_compile_size(type, size) \
    assert_compile(sizeof(type) == (size))
#define assert_ptr_return(ptr,ret) \
    do { if (unlikely(!(ptr))) { WARN_ON_ONCE(!(ptr)); return ret; } } while (0)

/** @}  */

/** @def RT_BCOPY_UNFORTIFIED
 * For copying a number of bytes from/to variable length structures.
 *
 * This is for working around false positives ("field-spanning writes") in the
 * linux kernel's fortified memcpy (v5.18+) when copying from/to
 * RT_FLEXIBLE_ARRAY fields and similar tricks going beyond the strict
 * definition of a target or source structure.
 *
 * @param   a_pDst          Pointer to the destination buffer.
 * @param   a_pSrc          Pointer to the source buffer.
 * @param   a_cbToCopy      Number of bytes to copy.
 * @see @bugref{10209}, @ticketref{21410}
 */
#if defined(RT_OS_LINUX) && defined(__KERNEL__)
/* The definition of RT_BCOPY_UNFORTIFIED macro originally comes from IPRT
 * header file. However, due to vboxvideo (Linux/DRM module) implementation,
 * those header files cannot be used directly. Therefore, let's duplicate this
 * definition here. Following vbox_drv.h header file is a part of
 * vboxvideo sources, so this code block is only intended to be used from there. */
# include "vbox_drv.h"
# if (RTLNX_VER_MIN(5,18,0) || RTLNX_RHEL_RANGE(9,3, 9,99)) \
  && !defined(__NO_FORTIFY) \
  && defined(__OPTIMIZE__) \
  && defined(CONFIG_FORTIFY_SOURCE)
#  define RT_BCOPY_UNFORTIFIED(a_pDst, a_pSrc, a_cbToCopy)  __underlying_memcpy((a_pDst), (a_pSrc), (a_cbToCopy))
# else
#  define RT_BCOPY_UNFORTIFIED(a_pDst, a_pSrc, a_cbToCopy)  memcpy((a_pDst), (a_pSrc), (a_cbToCopy))
# endif
#else  /* !RT_OS_LINUX && !__KERNEL__ */
# define RT_BCOPY_UNFORTIFIED(a_pDst, a_pSrc, a_cbToCopy)   memcpy((a_pDst), (a_pSrc), (a_cbToCopy))
#endif /* !RT_OS_LINUX && !__KERNEL__ */

#endif /* !VBOX_INCLUDED_Graphics_VBoxVideoErr_h */
