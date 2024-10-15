/* $Id: sysfs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Linux sysfs access.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_linux_sysfs_h
#define IPRT_INCLUDED_linux_sysfs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/stdarg.h>

#include <sys/types.h> /* for dev_t */

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_linux_sysfs    RTLinuxSysfs - Linux sysfs
 * @ingroup grp_rt
 * @{
 */

/**
 * Checks if a sysfs file (or directory, device, symlink, whatever) exists.
 *
 * @returns true if the sysfs object exists.
 *          false otherwise or if an error occurred.
 * @param   pszFormat   The name format, either absolute or relative to "/sys/".
 * @param   va          The format args.
 */
RTDECL(bool) RTLinuxSysFsExistsV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Checks if a sysfs object (directory, device, symlink, whatever) exists.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if the sysfs object exists.
 * @retval  VERR_FILE_NOT_FOUND if the sysfs object does not exist.
 * @param   pszFormat   The name format, either absolute or relative to "/sys/".
 * @param   va          The format args.
 */
RTDECL(int) RTLinuxSysFsExistsExV(const char *pszFormat, va_list va)  RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Checks if a sysfs file (or directory, device, symlink, whatever) exists.
 *
 * @returns true if the sysfs object exists.
 *          false otherwise or if an error occurred.
 * @param   pszFormat   The name format, either absolute or relative to "/sys/".
 * @param   ...         The format args.
 */
RTDECL(bool) RTLinuxSysFsExists(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

/**
 * Checks if a sysfs object (directory, device, symlink, whatever) exists.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if the sysfs object exists.
 * @retval  VERR_FILE_NOT_FOUND if the sysfs object does not exist.
 * @param   pszFormat   The name format, either absolute or relative to "/sys/".
 * @param   ...         The format args.
 */
RTDECL(int) RTLinuxSysFsExistsEx(const char *pszFormat, ...)  RT_IPRT_FORMAT_ATTR(1, 2);

/**
 * Opens a sysfs file for reading.
 *
 * @returns IPRT status code.
 * @param   phFile      Where to store the file handle on success.
 * @param   pszFormat   The name format, either absolute or relative to "/sys/".
 * @param   va          The format args.
 *
 * @note Close the file using RTFileClose().
 */
RTDECL(int) RTLinuxSysFsOpenV(PRTFILE phFile, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Opens a sysfs file - extended version.
 *
 * @returns IPRT status code.
 * @param   phFile      Where to store the file handle on success.
 * @param   fOpen       Open flags, see RTFileOpen().
 * @param   pszFormat   The name format, either absolute or relative to "/sys/".
 * @param   va          The format args.
 */
RTDECL(int) RTLinuxSysFsOpenExV(PRTFILE phFile, uint64_t fOpen, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Opens a sysfs file.
 *
 * @returns IPRT status code.
 * @param   phFile      Where to store the file handle on success.
 * @param   pszFormat   The name format, either absolute or relative to "/sys/".
 * @param   ...         The format args.
 *
 * @note Close the file using RTFileClose().
 */
RTDECL(int) RTLinuxSysFsOpen(PRTFILE phFile, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Opens a sysfs file - extended version.
 *
 * @returns IPRT status code.
 * @param   phFile      Where to store the file handle on success.
 * @param   fOpen       Open flags, see RTFileOpen().
 * @param   pszFormat   The name format, either absolute or relative to "/sys/".
 * @param   ...         The format args.
 */
RTDECL(int) RTLinuxSysFsOpenEx(PRTFILE phFile, uint64_t fOpen, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Reads a string from a file opened with RTLinuxSysFsOpen or RTLinuxSysFsOpenV.
 *
 * Expects to read the whole file, mind, and will return VERR_BUFFER_OVERFLOW if
 * that is not possible with the given buffer size.
 *
 * @returns IPRT status code.
 * @param   hFile       The file descriptor returned by RTLinuxSysFsOpen or RTLinuxSysFsOpenV.
 * @param   pszBuf      Where to store the string.
 * @param   cchBuf      The size of the buffer. Must be at least 2 bytes.
 * @param   pcchRead    Where to store the amount of characters read on success - optional.
 */
RTDECL(int) RTLinuxSysFsReadStr(RTFILE hFile, char *pszBuf, size_t cchBuf, size_t *pcchRead);

/**
 * Writes a string to a file opened with RTLinuxSysFsOpenEx or RTLinuxSysFsOpenExV for writing.
 *
 * @returns IPRT status code.
 * @param   hFile       The file descriptor returned by RTLinuxSysFsOpenEx or RTLinuxSysFsOpenExV.
 * @param   pszBuf      The string to write.
 * @param   cchBuf      The length of the string to write - if 0 is given
 *                      the string length is determined before writing it including the zero terminator.
 * @param   pcchWritten Where to store the amount of characters written on success - optional.
 */
RTDECL(int) RTLinuxSysFsWriteStr(RTFILE hFile, const char *pszBuf, size_t cchBuf, size_t *pcchWritten);

/**
 * Reads the remainder of a file opened with RTLinuxSysFsOpen or
 * RTLinuxSysFsOpenV.
 *
 * @returns IPRT status code.
 * @param   hFile       The file descriptor returned by RTLinuxSysFsOpen or RTLinuxSysFsOpenV.
 * @param   pvBuf       Where to store the bits from the file.
 * @param   cbBuf       The size of the buffer.
 * @param   pcbRead     Where to return the number of bytes read.  Optional.
 */
RTDECL(int) RTLinuxSysFsReadFile(RTFILE hFile, void *pvBuf, size_t cbBuf, size_t *pcbRead);

/**
 * Writes the given buffer to a file opened with RTLinuxSysFsOpenEx or
 * RTLinuxSysFsOpenExV.
 *
 * @returns IPRT status code.
 * @param   hFile       The file descriptor returned by RTLinuxSysFsOpenEx or RTLinuxSysFsOpenExV.
 * @param   pvBuf       The data to write.
 * @param   cbBuf       The size of the buffer.
 * @param   pcbWritten  Where to return the number of bytes read.  Optional.
 */
RTDECL(int) RTLinuxSysFsWriteFile(RTFILE hFile, void *pvBuf, size_t cbBuf, size_t *pcbWritten);

/**
 * Reads a number from a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The number base, 0 for autodetect.
 * @param   pi64        Where to store the 64-bit signed on success.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   va          Format args.
 */
RTDECL(int) RTLinuxSysFsReadIntFileV(unsigned uBase, int64_t *pi64, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Reads a number from a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The number base, 0 for autodetect.
 * @param   pi64        Where to store the 64-bit signed on success.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   ...         Format args.
 */
RTDECL(int) RTLinuxSysFsReadIntFile(unsigned uBase, int64_t *pi64, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Writes an unsigned 8-bit number to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The base format to write the number. Passing 16 here for
 *                      example writes the number as a hexadecimal string with 0x prepended.
 * @param   u8          The number to write.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   va          Format args.
 */
RTDECL(int) RTLinuxSysFsWriteU8FileV(unsigned uBase, uint8_t u8, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Writes an unsigned 8-bit number to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The base format to write the number. Passing 16 here for
 *                      example writes the number as a hexadecimal string with 0x prepended.
 * @param   u8          The number to write.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   ...         Format args.
 */
RTDECL(int) RTLinuxSysFsWriteU8File(unsigned uBase, uint8_t u8, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Writes an unsigned 16-bit number to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The base format to write the number. Passing 16 here for
 *                      example writes the number as a hexadecimal string with 0x prepended.
 * @param   u16         The number to write.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   va          Format args.
 */
RTDECL(int) RTLinuxSysFsWriteU16FileV(unsigned uBase, uint16_t u16, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Writes an unsigned 16-bit number to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The base format to write the number. Passing 16 here for
 *                      example writes the number as a hexadecimal string with 0x prepended.
 * @param   u16         The number to write.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   ...         Format args.
 */
RTDECL(int) RTLinuxSysFsWriteU16File(unsigned uBase, uint16_t u16, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Writes an unsigned 32-bit number to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The base format to write the number. Passing 16 here for
 *                      example writes the number as a hexadecimal string with 0x prepended.
 * @param   u32         The number to write.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   va          Format args.
 */
RTDECL(int) RTLinuxSysFsWriteU32FileV(unsigned uBase, uint32_t u32, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Writes an unsigned 8-bit number to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The base format to write the number. Passing 16 here for
 *                      example writes the number as a hexadecimal string with 0x prepended.
 * @param   u32         The number to write.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   ...         Format args.
 */
RTDECL(int) RTLinuxSysFsWriteU32File(unsigned uBase, uint32_t u32, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Writes an unsigned 64-bit number to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The base format to write the number. Passing 16 here for
 *                      example writes the number as a hexadecimal string with 0x prepended.
 * @param   u64         The number to write.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   va          Format args.
 */
RTDECL(int) RTLinuxSysFsWriteU64FileV(unsigned uBase, uint64_t u64, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Writes an unsigned 8-bit number to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   uBase       The base format to write the number. Passing 16 here for
 *                      example writes the number as a hexadecimal string with 0x prepended.
 * @param   u64         The number to write.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   ...         Format args.
 */
RTDECL(int) RTLinuxSysFsWriteU64File(unsigned uBase, uint32_t u64, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Reads a device number from a sysfs file.
 *
 * @returns IPRT status code.
 * @param   pDevNum     Where to store the device number on success.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   va          Format args.
 */
RTDECL(int) RTLinuxSysFsReadDevNumFileV(dev_t *pDevNum, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Reads a device number from a sysfs file.
 *
 * @returns IPRT status code.
 * @param   pDevNum     Where to store the device number on success.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   ...         Format args.
 */
RTDECL(int) RTLinuxSysFsReadDevNumFile(dev_t *pDevNum, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Reads a string from a sysfs file.
 *
 * If the file contains a newline, we only return the text up until there.  This
 * differs from the RTLinuxSysFsReadStr() behaviour.
 *
 * @returns IPRT status code.
 * @param   pszBuf      Where to store the path element.  Must be at least two
 *                      characters, but a longer buffer would be advisable.
 * @param   cchBuf      The size of the buffer pointed to by @a pszBuf.
 * @param   pcchRead    Where to store the amount of characters read on success - optional.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   va          Format args.
 */
RTDECL(int) RTLinuxSysFsReadStrFileV(char *pszBuf, size_t cchBuf, size_t *pcchRead, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(4, 0);

/**
 * Reads a string from a sysfs file.  If the file contains a newline, we only
 * return the text up until there.
 *
 * @returns IPRT status code.
 * @param   pszBuf      Where to store the path element.  Must be at least two
 *                      characters, but a longer buffer would be advisable.
 * @param   cchBuf      The size of the buffer pointed to by @a pszBuf.
 * @param   pcchRead    Where to store the amount of characters read on success - optional.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   ...         Format args.
 */
RTDECL(int) RTLinuxSysFsReadStrFile(char *pszBuf, size_t cchBuf, size_t *pcchRead, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(4, 5);

/**
 * Writes a string to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   pszBuf      The string to write.
 * @param   cchBuf      The size of the buffer pointed to by @a pszBuf.
 * @param   pcchWritten Where to store the amount of characters written on success - optional.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   va          Format args.
 */
RTDECL(int) RTLinuxSysFsWriteStrFileV(const char *pszBuf, size_t cchBuf, size_t *pcchWritten, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(4, 0);

/**
 * Writes a string to a sysfs file.
 *
 * @returns IPRT status code.
 * @param   pszBuf      The string to write.
 * @param   cchBuf      The size of the buffer pointed to by @a pszBuf.
 * @param   pcchWritten Where to store the amount of characters written on success - optional.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   ...         Format args.
 */
RTDECL(int) RTLinuxSysFsWriteStrFile(const char *pszBuf, size_t cchBuf, size_t *pcchWritten, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(4, 5);

/**
 * Reads the last element of the path of the file pointed to by the symbolic
 * link specified.
 *
 * This is needed at least to get the name of the driver associated with a
 * device, where pszFormat should be the "driver" link in the devices sysfs
 * directory.
 *
 * @returns IPRT status code.
 * @param   pszBuf      Where to store the path element.  Must be at least two
 *                      characters, but a longer buffer would be advisable.
 * @param   cchBuf      The size of the buffer pointed to by @a pszBuf.
 * @param   pchBuf      Where to store the length of the returned string on success - optional.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   va           Format args.
 */
RTDECL(int) RTLinuxSysFsGetLinkDestV(char *pszBuf, size_t cchBuf, size_t *pchBuf, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(4, 0);

/**
 * Reads the last element of the path of the file pointed to by the symbolic
 * link specified.
 *
 * This is needed at least to get the name of the driver associated with a
 * device, where pszFormat should be the "driver" link in the devices sysfs
 * directory.
 *
 * @returns IPRT status code.
 * @param   pszBuf      Where to store the path element.  Must be at least two
 *                      characters, but a longer buffer would be advisable.
 * @param   cchBuf      The size of the buffer pointed to by @a pszBuf.
 * @param   pchBuf      Where to store the length of the returned string on success - optional.
 * @param   pszFormat   The filename format, either absolute or relative to "/sys/".
 * @param   ...         Format args.
 */
RTDECL(int) RTLinuxSysFsGetLinkDest(char *pszBuf, size_t cchBuf, size_t *pchBuf, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(4, 5);

/**
 * Check the path of a device node under /dev, given the device number and a
 * pattern and store the path into @a pszBuf.
 *
 * @returns IPRT status code.
 * @retval  VERR_FILE_NOT_FOUND if no matching device node could be found.
 * @param   DevNum         The device number to search for.
 * @param   fMode          The type of device - only RTFS_TYPE_DEV_CHAR and
 *                         RTFS_TYPE_DEV_BLOCK are valid values.
 * @param   pszBuf         Where to store the path.
 * @param   cchBuf         The size of the buffer.
 * @param   pszPattern     The expected path format of the device node, either
 *                         absolute or relative to "/dev".
 * @param   va             Format args.
 */
RTDECL(int) RTLinuxCheckDevicePathV(dev_t DevNum, RTFMODE fMode, char *pszBuf, size_t cchBuf,
                                    const char *pszPattern, va_list va) RT_IPRT_FORMAT_ATTR(5, 0);

/**
 * Check the path of a device node under /dev, given the device number and a
 * pattern and store the path into @a pszBuf.
 *
 * @returns IPRT status code.
 * @retval  VERR_FILE_NOT_FOUND if no matching device node could be found.
 * @param   DevNum          The device number to search for
 * @param   fMode           The type of device - only RTFS_TYPE_DEV_CHAR and
 *                          RTFS_TYPE_DEV_BLOCK are valid values
 * @param   pszBuf          Where to store the path.
 * @param   cchBuf          The size of the buffer.
 * @param   pszPattern      The expected path format of the device node, either
 *                          absolute or relative to "/dev".
 * @param   ...             Format args.
 */
RTDECL(int) RTLinuxCheckDevicePath(dev_t DevNum, RTFMODE fMode, char *pszBuf, size_t cchBuf,
                                   const char *pszPattern, ...) RT_IPRT_FORMAT_ATTR(5, 6);

/**
 * Constructs the path of a sysfs file from the format parameters passed,
 * prepending "/sys/" if the path is relative.
 *
 * @returns IPRT status code.
 * @param   pszPath    Where to write the path.
 * @param   cbPath     The size of the buffer pointed to by @a pszPath.
 * @param   pszFormat  The name format, either absolute or relative to "/sys/".
 * @param   va         The format args.
 */
RTDECL(int) RTLinuxConstructPathV(char *pszPath, size_t cbPath, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Constructs the path of a sysfs file from the format parameters passed,
 * prepending "/sys/" if the path is relative.
 *
 * @returns IPRT status code.
 * @param   pszPath    Where to write the path.
 * @param   cbPath     The size of the buffer pointed to by @a pszPath.
 * @param   pszFormat  The name format, either absolute or relative to "/sys/".
 * @param   ...        The format args.
 */
RTDECL(int) RTLinuxConstructPath(char *pszPath, size_t cbPath, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_linux_sysfs_h */

