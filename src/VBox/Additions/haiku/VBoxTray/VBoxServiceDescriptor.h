/* $Id: VBoxServiceDescriptor.h 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxGuestServiceDescriptor, Haiku Guest Additions, header.
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
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
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
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GA_INCLUDED_SRC_haiku_VBoxTray_VBoxServiceDescriptor_h
#define GA_INCLUDED_SRC_haiku_VBoxTray_VBoxServiceDescriptor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <Handler.h>

class VBoxShClService : public BHandler
{
    public:
        VBoxShClService();
        virtual ~VBoxShClService();
};

/* The shared clipboard service prototypes. */
int vbtrShClInit(const VBOXTRAYSVCENV *pEnv, void **ppInstance, bool *pfStartThread);
unsigned __stdcall VBoxShClThread(void *pInstance);
void vbtrShClDestroy(const VBOXTRAYSVCENV *pEnv, void *pInstance);

#endif /* !GA_INCLUDED_SRC_haiku_VBoxTray_VBoxServiceDescriptor_h */

