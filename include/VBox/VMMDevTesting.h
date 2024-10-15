/* $Id: VMMDevTesting.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMMDev - Testing Extensions.
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
 */

#ifndef VBOX_INCLUDED_VMMDevTesting_h
#define VBOX_INCLUDED_VMMDevTesting_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>


/** @defgroup grp_vmmdev_testing    VMM Device Testing
 * @ingroup grp_vmmdev
 * @{
 */

/** The base address of the MMIO range used for testing.
 * @remarks This used to be at 0x101000 but moved to 0xdf000 so that it would
 *          work better with prototype NEM code.  This also means enabling A20
 *          is not a requirement. */
#define VMMDEV_TESTING_MMIO_BASE        UINT32_C(0x000df000)
/** The size of the MMIO range used for testing.  */
#define VMMDEV_TESTING_MMIO_SIZE        UINT32_C(0x00001000)

/** MMIO offset: The NOP register - 1248 RW. */
#define VMMDEV_TESTING_MMIO_OFF_NOP         (0x000)
/** MMIO offset: The go-to-ring-3-NOP register - 1248 RW. */
#define VMMDEV_TESTING_MMIO_OFF_NOP_R3      (0x008)
/** MMIO offset: The readback registers - 64 bytes of read/write "memory". */
#define VMMDEV_TESTING_MMIO_OFF_READBACK    (0x040)
/** MMIO offset: Readback register view that always goes to ring-3. */
#define VMMDEV_TESTING_MMIO_OFF_READBACK_R3 (0x080)
/** The size of the MMIO readback registers. */
#define VMMDEV_TESTING_READBACK_SIZE        (0x40)

/** Default address of VMMDEV_TESTING_MMIO_OFF_NOP. */
#define VMMDEV_TESTING_MMIO_NOP             (VMMDEV_TESTING_MMIO_BASE + VMMDEV_TESTING_MMIO_OFF_NOP)
/** Default address of VMMDEV_TESTING_MMIO_OFF_NOP_R3. */
#define VMMDEV_TESTING_MMIO_NOP_R3          (VMMDEV_TESTING_MMIO_BASE + VMMDEV_TESTING_MMIO_OFF_NOP_R3)
/** Default address of VMMDEV_TESTING_MMIO_OFF_READBACK. */
#define VMMDEV_TESTING_MMIO_READBACK        (VMMDEV_TESTING_MMIO_BASE + VMMDEV_TESTING_MMIO_OFF_READBACK)
/** Default address of VMMDEV_TESTING_MMIO_OFF_READBACK_R3. */
#define VMMDEV_TESTING_MMIO_READBACK_R3     (VMMDEV_TESTING_MMIO_BASE + VMMDEV_TESTING_MMIO_OFF_READBACK_R3)

/** The real mode selector to use. */
#define VMMDEV_TESTING_MMIO_RM_SEL          0xdf00
/** Calculate the real mode offset of a MMIO register. */
#define VMMDEV_TESTING_MMIO_RM_OFF(val)     ((val) - VMMDEV_TESTING_MMIO_BASE)
/** Calculate the real mode offset of a MMIO register offset. */
#define VMMDEV_TESTING_MMIO_RM_OFF2(off)    (off)

/** The base port of the I/O range used for testing. */
#define VMMDEV_TESTING_IOPORT_BASE      0x0510
/** The number of I/O ports reserved for testing. */
#define VMMDEV_TESTING_IOPORT_COUNT     0x0010
/** The NOP I/O port - 1,2,4 RW. */
#define VMMDEV_TESTING_IOPORT_NOP       (VMMDEV_TESTING_IOPORT_BASE + 0)
/** The low nanosecond timestamp - 4 RO.  */
#define VMMDEV_TESTING_IOPORT_TS_LOW    (VMMDEV_TESTING_IOPORT_BASE + 1)
/** The high nanosecond timestamp - 4 RO.  Read this after the low one!  */
#define VMMDEV_TESTING_IOPORT_TS_HIGH   (VMMDEV_TESTING_IOPORT_BASE + 2)
/** Command register usually used for preparing the data register - 4/2 WO. */
#define VMMDEV_TESTING_IOPORT_CMD       (VMMDEV_TESTING_IOPORT_BASE + 3)
/** Data register which use depends on the current command - 1s, 4 WO. */
#define VMMDEV_TESTING_IOPORT_DATA      (VMMDEV_TESTING_IOPORT_BASE + 4)
/** The go-to-ring-3-NOP I/O port - 1,2,4 RW. */
#define VMMDEV_TESTING_IOPORT_NOP_R3    (VMMDEV_TESTING_IOPORT_BASE + 5)
/** Take the VMMDev lock in arrival context and return - 1,2,4 RW.
 * Writing configures counter action by a thread taking the lock to trigger
 * contention:
 *  - bits 15:0: Number of microseconds thread should hold lock.
 *  - bits 31:16: Number of microseconds thread should wait before locking
 *    again. */
#define VMMDEV_TESTING_IOPORT_LOCKED_LO (VMMDEV_TESTING_IOPORT_BASE + 6)
/** Take the VMMDev lock in arrival context and return - 1,2,4 RW.
 * Writing configures counter action by a thread taking the lock to trigger
 * contention:
 *  - bits 19:0: Number of kilo (1024) ticks the EMT should hold lock.
 *  - bits 25:20: Reserved, must be zero.
 *  - bit 26: Thread takes lock in shared mode when set, exclusive when clear.
 *  - bit 27: EMT takes lock in shared mode when set, exclusive when clear.
 *  - bit 28: Use read/write critical section when set, device section if clear.
 *  - bit 29: EMT passes VINF_SUCCESS as rcBusy when set.
 *  - bit 30: Makes thread poke all EMTs before release lock.
 *  - bit 31: Enables the thread. */
#define VMMDEV_TESTING_IOPORT_LOCKED_HI (VMMDEV_TESTING_IOPORT_BASE + 7)

/** @name Commands.
 * @{ */
/** Initialize test, sending name (zero terminated string). (RTTestCreate) */
#define VMMDEV_TESTING_CMD_INIT         UINT32_C(0xcab1e000)
/** Test done, sending 32-bit total error count with it. (RTTestSummaryAndDestroy) */
#define VMMDEV_TESTING_CMD_TERM         UINT32_C(0xcab1e001)
/** Start a new sub-test, sending name (zero terminated string). (RTTestSub) */
#define VMMDEV_TESTING_CMD_SUB_NEW      UINT32_C(0xcab1e002)
/** Sub-test is done, sending 32-bit error count for it. (RTTestSubDone) */
#define VMMDEV_TESTING_CMD_SUB_DONE     UINT32_C(0xcab1e003)
/** Report a failure, sending reason (zero terminated string). (RTTestFailed) */
#define VMMDEV_TESTING_CMD_FAILED       UINT32_C(0xcab1e004)
/** Report a value, sending the 64-bit value (2x4), the 32-bit unit (4), and
 * finally the name (zero terminated string).  (RTTestValue) */
#define VMMDEV_TESTING_CMD_VALUE        UINT32_C(0xcab1e005)
/** Report a failure, sending reason (zero terminated string). (RTTestSkipped) */
#define VMMDEV_TESTING_CMD_SKIPPED      UINT32_C(0xcab1e006)
/** Report a value found in a VMM register, sending a string on the form
 * "value-name:register-name". */
#define VMMDEV_TESTING_CMD_VALUE_REG    UINT32_C(0xcab1e007)
/** Print string, sending a string including newline. (RTTestPrintf) */
#define VMMDEV_TESTING_CMD_PRINT        UINT32_C(0xcab1e008)
/** Query a config value, sending a 16-bit word (VMMDEV_TESTING_CFG_XXX) to the
 * DATA port and reading back the result. */
#define VMMDEV_TESTING_CMD_QUERY_CFG    UINT32_C(0xcab1e009)
/** Start a new sub-sub-test, sending name (zero terminated string). (RTTestSubSub) */
#define VMMDEV_TESTING_CMD_SUBSUB_NEW   UINT32_C(0xcab1e00a)
/** Sub-sub-test is done, sending 32-bit error count for it. (RTTestSubSubDone) */
#define VMMDEV_TESTING_CMD_SUBSUB_DONE  UINT32_C(0xcab1e00b)

/** The magic part of the command. */
#define VMMDEV_TESTING_CMD_MAGIC        UINT32_C(0xcab1e000)
/** The magic part of the command. */
#define VMMDEV_TESTING_CMD_MAGIC_MASK   UINT32_C(0xffffff00)
/** The magic high word automatically supplied to 16-bit CMD writes. */
#define VMMDEV_TESTING_CMD_MAGIC_HI_WORD UINT32_C(0xcab10000)
/** @} */

/** @name Value units
 * @note Same as RTTESTUNIT, see rules here for adding new units.
 * @{ */
#define VMMDEV_TESTING_UNIT_PCT                 UINT8_C(0x01)   /**< Percentage. */
#define VMMDEV_TESTING_UNIT_BYTES               UINT8_C(0x02)   /**< Bytes. */
#define VMMDEV_TESTING_UNIT_BYTES_PER_SEC       UINT8_C(0x03)   /**< Bytes per second. */
#define VMMDEV_TESTING_UNIT_KILOBYTES           UINT8_C(0x04)   /**< Kilobytes. */
#define VMMDEV_TESTING_UNIT_KILOBYTES_PER_SEC   UINT8_C(0x05)   /**< Kilobytes per second. */
#define VMMDEV_TESTING_UNIT_MEGABYTES           UINT8_C(0x06)   /**< Megabytes. */
#define VMMDEV_TESTING_UNIT_MEGABYTES_PER_SEC   UINT8_C(0x07)   /**< Megabytes per second. */
#define VMMDEV_TESTING_UNIT_PACKETS             UINT8_C(0x08)   /**< Packets. */
#define VMMDEV_TESTING_UNIT_PACKETS_PER_SEC     UINT8_C(0x09)   /**< Packets per second. */
#define VMMDEV_TESTING_UNIT_FRAMES              UINT8_C(0x0a)   /**< Frames. */
#define VMMDEV_TESTING_UNIT_FRAMES_PER_SEC      UINT8_C(0x0b)   /**< Frames per second. */
#define VMMDEV_TESTING_UNIT_OCCURRENCES         UINT8_C(0x0c)   /**< Occurrences. */
#define VMMDEV_TESTING_UNIT_OCCURRENCES_PER_SEC UINT8_C(0x0d)   /**< Occurrences per second. */
#define VMMDEV_TESTING_UNIT_CALLS               UINT8_C(0x0e)   /**< Calls. */
#define VMMDEV_TESTING_UNIT_CALLS_PER_SEC       UINT8_C(0x0f)   /**< Calls per second. */
#define VMMDEV_TESTING_UNIT_ROUND_TRIP          UINT8_C(0x10)   /**< Round trips. */
#define VMMDEV_TESTING_UNIT_SECS                UINT8_C(0x11)   /**< Seconds. */
#define VMMDEV_TESTING_UNIT_MS                  UINT8_C(0x12)   /**< Milliseconds. */
#define VMMDEV_TESTING_UNIT_NS                  UINT8_C(0x13)   /**< Nanoseconds. */
#define VMMDEV_TESTING_UNIT_NS_PER_CALL         UINT8_C(0x14)   /**< Nanoseconds per call. */
#define VMMDEV_TESTING_UNIT_NS_PER_FRAME        UINT8_C(0x15)   /**< Nanoseconds per frame. */
#define VMMDEV_TESTING_UNIT_NS_PER_OCCURRENCE   UINT8_C(0x16)   /**< Nanoseconds per occurrence. */
#define VMMDEV_TESTING_UNIT_NS_PER_PACKET       UINT8_C(0x17)   /**< Nanoseconds per frame. */
#define VMMDEV_TESTING_UNIT_NS_PER_ROUND_TRIP   UINT8_C(0x18)   /**< Nanoseconds per round trip. */
#define VMMDEV_TESTING_UNIT_INSTRS              UINT8_C(0x19)   /**< Instructions. */
#define VMMDEV_TESTING_UNIT_INSTRS_PER_SEC      UINT8_C(0x1a)   /**< Instructions per second. */
#define VMMDEV_TESTING_UNIT_NONE                UINT8_C(0x1b)   /**< No unit. */
#define VMMDEV_TESTING_UNIT_PP1K                UINT8_C(0x1c)   /**< Parts per thousand (10^-3). */
#define VMMDEV_TESTING_UNIT_PP10K               UINT8_C(0x1d)   /**< Parts per ten thousand (10^-4). */
#define VMMDEV_TESTING_UNIT_PPM                 UINT8_C(0x1e)   /**< Parts per million (10^-6). */
#define VMMDEV_TESTING_UNIT_PPB                 UINT8_C(0x1f)   /**< Parts per billion (10^-9). */
#define VMMDEV_TESTING_UNIT_TICKS               UINT8_C(0x20)   /**< CPU ticks. */
#define VMMDEV_TESTING_UNIT_TICKS_PER_CALL      UINT8_C(0x21)   /**< CPU ticks per call. */
#define VMMDEV_TESTING_UNIT_TICKS_PER_OCCURENCE UINT8_C(0x22)   /**< CPU ticks per occurence. */
#define VMMDEV_TESTING_UNIT_PAGES               UINT8_C(0x23)   /**< Page count. */
#define VMMDEV_TESTING_UNIT_PAGES_PER_SEC       UINT8_C(0x24)   /**< Pages per second. */
#define VMMDEV_TESTING_UNIT_TICKS_PER_PAGE      UINT8_C(0x25)   /**< CPU ticks per page. */
#define VMMDEV_TESTING_UNIT_NS_PER_PAGE         UINT8_C(0x26)   /**< Nanoseconds per page. */
#define VMMDEV_TESTING_UNIT_PS                  UINT8_C(0x27)   /**< Picoseconds. */
#define VMMDEV_TESTING_UNIT_PS_PER_CALL         UINT8_C(0x28)   /**< Picoseconds per call. */
#define VMMDEV_TESTING_UNIT_PS_PER_FRAME        UINT8_C(0x29)   /**< Picoseconds per frame. */
#define VMMDEV_TESTING_UNIT_PS_PER_OCCURRENCE   UINT8_C(0x2a)   /**< Picoseconds per occurrence. */
#define VMMDEV_TESTING_UNIT_PS_PER_PACKET       UINT8_C(0x2b)   /**< Picoseconds per frame. */
#define VMMDEV_TESTING_UNIT_PS_PER_ROUND_TRIP   UINT8_C(0x2c)   /**< Picoseconds per round trip. */
#define VMMDEV_TESTING_UNIT_PS_PER_PAGE         UINT8_C(0x2d)   /**< Picoseconds per page. */
/** @}  */

/** What the NOP accesses returns. */
#define VMMDEV_TESTING_NOP_RET                  UINT32_C(0x64726962) /* bird */

/** What follows the data provided by a VMMDEV_TESTING_CMD_QUERY_CFG.
 * This can be used to verify that a config query was successful.  The value
 * will not be split up if the read is 16-bit or 8-bit, subsequent reads (of
 * any size) will return zero. */
#define VMMDEV_TESTING_QUERY_CFG_OKAY_TAIL      UINT32_C(0x79616b4f) /* Okay */

/** @name Low and High Locking Control Dwords
 * @{ */
/** Low Locking Control: Thread lock hold interval in microseconds. */
#define VMMDEV_TESTING_LOCKED_LO_HOLD_MASK      UINT32_C(0x0000ffff)
/** Low Locking Control: Thread wait time in microseconds between locking
 *  attempts. */
#define VMMDEV_TESTING_LOCKED_LO_WAIT_MASK      UINT32_C(0xffff0000)
/** Low Locking Control: Thread wait time shift count. */
#define VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT     16
/** High Locking Control: Kilo (1024) ticks the EMT should hold the lock.  */
#define VMMDEV_TESTING_LOCKED_HI_TICKS_MASK     UINT32_C(0x000fffff)
/** High Locking Control: Must be zero. */
#define VMMDEV_TESTING_LOCKED_HI_MBZ_MASK       UINT32_C(0x03f00000)
/** High Locking Control: Thread takes lock in shared mode when set, exclusive
 *  when clear.  */
#define VMMDEV_TESTING_LOCKED_HI_THREAD_SHARED  UINT32_C(0x04000000)
/** High Locking Control: EMT takes lock in shared mode when set, exclusive
 *  when clear.  */
#define VMMDEV_TESTING_LOCKED_HI_EMT_SHARED     UINT32_C(0x08000000)
/** High Locking Control: Use read/write critical section instead of regular. */
#define VMMDEV_TESTING_LOCKED_HI_TYPE_RW        UINT32_C(0x10000000)
/** High Locking Control: EMT takes lock with rcBusy set to VINF_SUCCESS. */
#define VMMDEV_TESTING_LOCKED_HI_BUSY_SUCCESS   UINT32_C(0x20000000)
/** High Locking Control: Thread pokes EMTs before releasing lock. */
#define VMMDEV_TESTING_LOCKED_HI_POKE           UINT32_C(0x40000000)
/** High Locking Control: Thread enabled. */
#define VMMDEV_TESTING_LOCKED_HI_ENABLED        UINT32_C(0x80000000)
/** @} */

/** @name VMMDEV_TESTING_CFG_XXX - Configuration values that can be queried.
 * @{ */
/** Generic 32-bit value \#0 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD0            UINT16_C(0x0000)
/** Generic 32-bit value \#1 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD1            UINT16_C(0x0001)
/** Generic 32-bit value \#2 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD2            UINT16_C(0x0002)
/** Generic 32-bit value \#3 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD3            UINT16_C(0x0003)
/** Generic 32-bit value \#4 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD4            UINT16_C(0x0004)
/** Generic 32-bit value \#5 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD5            UINT16_C(0x0005)
/** Generic 32-bit value \#6 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD6            UINT16_C(0x0006)
/** Generic 32-bit value \#7 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD7            UINT16_C(0x0007)
/** Generic 32-bit value \#8 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD8            UINT16_C(0x0008)
/** Generic 32-bit value \#9 - testcase defined meaning. */
#define VMMDEV_TESTING_CFG_DWORD9            UINT16_C(0x0009)

/** Boolean (8-bit): Running in NEM on Linux? */
#define VMMDEV_TESTING_CFG_IS_NEM_LINUX      UINT16_C(0x0100)
/** Boolean (8-bit): Running in NEM on Windows? */
#define VMMDEV_TESTING_CFG_IS_NEM_WINDOWS    UINT16_C(0x0101)
/** Boolean (8-bit): Running in NEM on Darwin? */
#define VMMDEV_TESTING_CFG_IS_NEM_DARWIN     UINT16_C(0x0102)
/** Unsigned (16-bit): The override value for g_cBs3ThresholdNativeRecompiler.
 * A value of zero means it is not configured and the default should be used. */
#define VMMDEV_TESTING_CFG_THRESHOLD_NATIVE_RECOMPILER  UINT16_C(0x0103)

/** @} */

/** @} */

#endif /* !VBOX_INCLUDED_VMMDevTesting_h */

