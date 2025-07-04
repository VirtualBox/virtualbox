/* $Id: fsw_efi.h 109526 2025-05-14 10:34:03Z alexander.eichner@oracle.com $ */
/** @file
 * fsw_efi.h - EFI host environment header.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
 * ---------------------------------------------------------------------------
 * This code is based on:
 *
 * Copyright (c) 2006 Christoph Pfisterer
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _FSW_EFI_H_
#define _FSW_EFI_H_

#include "fsw_core.h"


/**
 * EFI Host: Private per-volume structure.
 */

typedef struct {
    UINT64                      Signature;      //!< Used to identify this structure

    EFI_FILE_IO_INTERFACE       FileSystem;     //!< Published EFI protocol interface structure

    EFI_HANDLE                  Handle;         //!< The device handle the protocol is attached to
    EFI_DISK_IO                 *DiskIo;        //!< The Disk I/O protocol we use for disk access
    UINT32                      MediaId;        //!< The media ID from the Block I/O protocol
    EFI_STATUS                  LastIOStatus;   //!< Last status from Disk I/O

    struct fsw_volume           *vol;           //!< FSW volume structure

} FSW_VOLUME_DATA;

/** Signature for the volume structure. */
#define FSW_VOLUME_DATA_SIGNATURE  EFI_SIGNATURE_32 ('f', 's', 'w', 'V')
/** Access macro for the volume structure. */
#define FSW_VOLUME_FROM_FILE_SYSTEM(a)  CR (a, FSW_VOLUME_DATA, FileSystem, FSW_VOLUME_DATA_SIGNATURE)

/**
 * EFI Host: Private structure for a EFI_FILE interface.
 */

typedef struct {
    UINT64                      Signature;      //!< Used to identify this structure

    EFI_FILE                    FileHandle;     //!< Published EFI protocol interface structure

    UINTN                       Type;           //!< File type used for dispatching
    struct fsw_shandle          shand;          //!< FSW handle for this file

} FSW_FILE_DATA;

/** File type: regular file. */
#define FSW_EFI_FILE_TYPE_FILE  (0)
/** File type: directory. */
#define FSW_EFI_FILE_TYPE_DIR   (1)

/** Signature for the file handle structure. */
#define FSW_FILE_DATA_SIGNATURE    EFI_SIGNATURE_32 ('f', 's', 'w', 'F')
/** Access macro for the file handle structure. */
#define FSW_FILE_FROM_FILE_HANDLE(a)  CR (a, FSW_FILE_DATA, FileHandle, FSW_FILE_DATA_SIGNATURE)


//
// Library functions
//

VOID fsw_efi_decode_time(OUT EFI_TIME *EfiTime, IN UINT32 UnixTime);

UINTN fsw_efi_strsize(struct fsw_string *s);
VOID fsw_efi_strcpy(CHAR16 *Dest, struct fsw_string *src);


#endif
