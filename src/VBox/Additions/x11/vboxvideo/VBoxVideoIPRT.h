/* $Id: VBoxVideoIPRT.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

/* In builds inside of the VirtualBox source tree we override the default
 * VBoxVideoIPRT.h using -include, therefore this define must match the one
 * there. */

#ifndef VBOX_INCLUDED_Graphics_VBoxVideoIPRT_h
#define VBOX_INCLUDED_Graphics_VBoxVideoIPRT_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

# include "VBoxVideoErr.h"

#ifndef __cplusplus
typedef enum
{
    false = 0,
    true
} bool;
# define RT_C_DECLS_BEGIN
# define RT_C_DECLS_END
#else
# define RT_C_DECLS_BEGIN extern "C" {
# define RT_C_DECLS_END }
#endif

#if defined(IN_XF86_MODULE) && !defined(NO_ANSIC)
# ifdef __cplusplus
/* xf86Module.h redefines this. */
#  define NULL 0
# endif
RT_C_DECLS_BEGIN
# include "xf86_ansic.h"
RT_C_DECLS_END
#endif  /* defined(IN_XF86_MODULE) && !defined(NO_ANSIC) */
#define __STDC_LIMIT_MACROS  /* define *INT*_MAX on C++ too. */
#include "compiler.h"  /* Can pull in <sdtint.h>.  Must come after xf86_ansic.h on XFree86. */
#include <X11/Xfuncproto.h>
#include <stdint.h>
#if defined(IN_XF86_MODULE) && !defined(NO_ANSIC)
# ifndef offsetof
#  define offsetof(type, member) ( (int)(uintptr_t)&( ((type *)(void *)0)->member) )
# endif
#else  /* !(defined(IN_XF86_MODULE) && !defined(NO_ANSIC)) */
# include <stdarg.h>
# include <stddef.h>
# include <string.h>
#endif  /* !(defined(IN_XF86_MODULE) && !defined(NO_ANSIC)) */

/* XFree86 (and newer Xfuncproto.h) do not have these.  Not that I care much for micro-optimisations
 * in most cases anyway. */
#ifndef _X_LIKELY
# define _X_LIKELY(x) (x)
#endif
#ifndef _X_UNLIKELY
# define _X_UNLIKELY(x) (x)
#endif

RT_C_DECLS_BEGIN
extern int RTASSERTVAR[1];
RT_C_DECLS_END

#define AssertCompile(expr) \
    extern int RTASSERTVAR[1] __attribute__((__unused__)), \
    RTASSERTVAR[(expr) ? 1 : 0] __attribute__((__unused__))
#define AssertCompileSize(type, size) \
    AssertCompile(sizeof(type) == (size))
#define AssertPtrNullReturnVoid(a) do { } while(0)

#if !defined(IN_XF86_MODULE) && defined(DEBUG)
# include <assert.h>
# define Assert assert
# define AssertFailed() assert(0)
# define AssertMsg(expr, msg) \
  do { \
      if (!(expr)) xf86ErrorF msg; \
      assert((expr)); \
  } while (0)
# define AssertPtr assert
# define AssertPtrReturn(pv, rcRet) do { assert(pv); if (pv) {} else return(rcRet); } while(0)
# define AssertRC(expr) assert (!expr)
#else
# define Assert(expr) do { } while(0)
# define AssertFailed() do { } while(0)
# define AssertMsg(expr, msg) do { } while(0)
# define AssertPtr(ptr) do { } while(0)
# define AssertPtrReturn(pv, rcRet) do { if (pv) {} else return(rcRet); } while(0)
# define AssertRC(expr) do { } while(0)
#endif

#define DECLCALLBACK(a_RetType) a_RetType
#define DECLCALLBACKTYPE(a_RetType, a_Name, a_Args) a_RetType a_Name a_Args
#define DECLCALLBACKMEMBER(a_RetType, a_Name, a_Args) a_RetType (*a_Name) a_Args
#if __GNUC__ >= 4
# define DECLHIDDEN(type) __attribute__((visibility("hidden"))) type
#else
# define DECLHIDDEN(type) type
#endif
#define DECLINLINE(type) static __inline__ type

#define _1K 1024
#define ASMCompilerBarrier mem_barrier
#define RT_BIT(bit)                             ( 1U << (bit) )
#define RT_BOOL(Value)                          ( !!(Value) )
#define RT_BZERO(pv, cb)    do { memset((pv), 0, cb); } while (0)
#define RT_CLAMP(Value, Min, Max)               ( (Value) > (Max) ? (Max) : (Value) < (Min) ? (Min) : (Value) )
#define RT_ELEMENTS(aArray)                     ( sizeof(aArray) / sizeof((aArray)[0]) )
#define RTIOPORT unsigned short
#define RT_NOREF(...)       (void)(__VA_ARGS__)
#define RT_OFFSETOF(type, member) offsetof(type, member)
#define RT_UOFFSETOF(type, member) offsetof(type, member)
#define RT_ZERO(Obj)        RT_BZERO(&(Obj), sizeof(Obj))
#define RT_VALID_PTR(ptr)  (   (uintptr_t)(ptr) + 0x1000U >= 0x2000U )
#ifndef INT16_C
# define INT16_C(Value) (Value)
#endif
#ifndef UINT16_C
# define UINT16_C(Value) (Value)
#endif
#ifndef INT32_C
# define INT32_C(Value) (Value ## U)
#endif
#ifndef UINT32_C
# define UINT32_C(Value) (Value ## U)
#endif
#define RT_UNTRUSTED_GUEST
#define RT_UNTRUSTED_VOLATILE_GUEST  volatile
#define RT_UNTRUSTED_HOST
#define RT_UNTRUSTED_VOLATILE_HOST   volatile
#define RT_UNTRUSTED_HSTGST
#define RT_UNTRUSTED_VOLATILE_HSTGST volatile

#define likely _X_LIKELY
#define unlikely _X_UNLIKELY

/**
 * A point in a two dimentional coordinate system.
 */
typedef struct RTPOINT
{
    /** X coordinate. */
    int32_t     x;
    /** Y coordinate. */
    int32_t     y;
} RTPOINT;

/**
 * Rectangle data type, double point.
 */
typedef struct RTRECT
{
    /** left X coordinate. */
    int32_t     xLeft;
    /** top Y coordinate. */
    int32_t     yTop;
    /** right X coordinate. (exclusive) */
    int32_t     xRight;
    /** bottom Y coordinate. (exclusive) */
    int32_t     yBottom;
} RTRECT;

/**
 * Rectangle data type, point + size.
 */
typedef struct RTRECT2
{
    /** X coordinate.
     * Unless stated otherwise, this is the top left corner. */
    int32_t     x;
    /** Y coordinate.
     * Unless stated otherwise, this is the top left corner.  */
    int32_t     y;
    /** The width.
     * Unless stated otherwise, this is to the right of (x,y) and will not
     * be a negative number. */
    int32_t     cx;
    /** The height.
     * Unless stated otherwise, this is down from (x,y) and will not be a
     * negative number. */
    int32_t     cy;
} RTRECT2;

/**
 * The size of a rectangle.
 */
typedef struct RTRECTSIZE
{
    /** The width (along the x-axis). */
    uint32_t    cx;
    /** The height (along the y-axis). */
    uint32_t    cy;
} RTRECTSIZE;

/** @name Port I/O helpers
 * @{ */

/** Write an 8-bit value to an I/O port. */
#define VBVO_PORT_WRITE_U8(Port, Value) \
    outb(Port, Value)
/** Write a 16-bit value to an I/O port. */
#define VBVO_PORT_WRITE_U16(Port, Value) \
    outw(Port, Value)
/** Write a 32-bit value to an I/O port. */
#define VBVO_PORT_WRITE_U32(Port, Value) \
    outl(Port, Value)
/** Read an 8-bit value from an I/O port. */
#define VBVO_PORT_READ_U8(Port) \
    inb(Port)
/** Read a 16-bit value from an I/O port. */
#define VBVO_PORT_READ_U16(Port) \
    inw(Port)
/** Read a 32-bit value from an I/O port. */
#define VBVO_PORT_READ_U32(Port) \
    inl(Port)

/** @}  */

#endif /* !VBOX_INCLUDED_Graphics_VBoxVideoIPRT_h */
