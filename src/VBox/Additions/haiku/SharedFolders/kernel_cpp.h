/* $Id: kernel_cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Kernel C++, Haiku private.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku. C++ in the kernel.
 *
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */

#ifndef GA_INCLUDED_SRC_haiku_SharedFolders_kernel_cpp_h
#define GA_INCLUDED_SRC_haiku_SharedFolders_kernel_cpp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef __cplusplus

#include <new>
#include <stdlib.h>

#if _KERNEL_MODE || _LOADER_MODE

using namespace std;
extern const nothrow_t std::nothrow;

// We need new() versions we can use when also linking against libgcc.
// std::nothrow can't be used since it's defined in both libgcc and
// kernel_cpp.cpp.
typedef struct {} mynothrow_t;
extern const mynothrow_t mynothrow;


inline void *
operator new(size_t size) throw (std::bad_alloc)
{
        // we don't actually throw any exceptions, but we have to
        // keep the prototype as specified in <new>, or else GCC 3
        // won't like us
        return malloc(size);
}


inline void *
operator new[](size_t size) throw (std::bad_alloc)
{
        return malloc(size);
}


inline void *
operator new(size_t size, const std::nothrow_t &) throw ()
{
        return malloc(size);
}


inline void *
operator new[](size_t size, const std::nothrow_t &) throw ()
{
        return malloc(size);
}


inline void *
operator new(size_t size, const mynothrow_t &) throw ()
{
        return malloc(size);
}


inline void *
operator new[](size_t size, const mynothrow_t &) throw ()
{
        return malloc(size);
}


inline void
operator delete(void *ptr) throw ()
{
        free(ptr);
}


inline void
operator delete[](void *ptr) throw ()
{
        free(ptr);
}

#endif  // #if _KERNEL_MODE

#endif  // __cplusplus

#endif /* !GA_INCLUDED_SRC_haiku_SharedFolders_kernel_cpp_h */
