/* $Id: file.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal RTFile header.
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

#ifndef IPRT_INCLUDED_INTERNAL_file_h
#define IPRT_INCLUDED_INTERNAL_file_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/file.h>

RT_C_DECLS_BEGIN

/**
 * Adjusts and validates the flags.
 *
 * The adjustments are made according to the wishes specified using the RTFileSetForceFlags API.
 *
 * @returns IPRT status code.
 * @param   pfOpen      Pointer to the user specified flags on input.
 *                      Updated on successful return.
 * @internal
 */
int rtFileRecalcAndValidateFlags(uint64_t *pfOpen);


/**
 * Internal interface for getting the RTFILE handle of stdin, stdout or stderr.
 *
 * This interface will not be exposed and is purely for internal IPRT use.
 *
 * @returns Handle or NIL_RTFILE.
 *
 * @param   enmStdHandle    The standard handle.
 */
RTFILE rtFileGetStandard(RTHANDLESTD enmStdHandle);

#ifdef RT_OS_WINDOWS
/**
 * Helper for converting RTFILE_O_XXX to the various NtCreateFile flags.
 *
 * @returns IPRT status code
 * @param   fOpen               The RTFILE_O_XXX flags to convert.
 * @param   pfDesiredAccess     Where to return the desired access mask.
 * @param   pfObjAttribs        Where to return the NT object attributes.
 * @param   pfFileAttribs       Where to return the file attributes (create).
 * @param   pfShareAccess       Where to return the file sharing access mask.
 * @param   pfCreateDisposition Where to return the file create disposition.
 * @param   pfCreateOptions     Where to return the file open/create options.
 */
DECLHIDDEN(int) rtFileNtValidateAndConvertFlags(uint64_t fOpen, uint32_t *pfDesiredAccess, uint32_t *pfObjAttribs,
                                                uint32_t *pfFileAttribs, uint32_t *pfShareAccess, uint32_t *pfCreateDisposition,
                                                uint32_t *pfCreateOptions);

#endif

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_file_h */

