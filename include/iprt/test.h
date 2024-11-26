/** @file
 * IPRT - Testcase Framework.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_test_h
#define IPRT_INCLUDED_test_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/stdarg.h>
#include <iprt/assert.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_test       RTTest - Testcase Framework.
 * @ingroup grp_rt
 * @{
 */

/** A test handle. */
typedef R3PTRTYPE(struct RTTESTINT *) RTTEST;
/** A pointer to a test handle. */
typedef RTTEST *PRTTEST;
/** A const pointer to a test handle. */
typedef RTTEST const *PCRTTEST;

/** A NIL Test handle. */
#define NIL_RTTEST  ((RTTEST)0)

/**
 * Test message importance level.
 */
typedef enum RTTESTLVL
{
    /** Invalid 0. */
    RTTESTLVL_INVALID = 0,
    /** Message should always be printed. */
    RTTESTLVL_ALWAYS,
    /** Failure message. */
    RTTESTLVL_FAILURE,
    /** Sub-test banner. */
    RTTESTLVL_SUB_TEST,
    /** Info message. */
    RTTESTLVL_INFO,
    /** Debug message. */
    RTTESTLVL_DEBUG,
    /** The last (invalid). */
    RTTESTLVL_END
} RTTESTLVL;


/**
 * Creates a test instance.
 *
 * @returns IPRT status code.
 * @param   pszTest     The test name.
 * @param   phTest      Where to store the test instance handle.
 */
RTR3DECL(int) RTTestCreate(const char *pszTest, PRTTEST phTest);

/**
 * Creates a test instance for a child process.
 *
 * This differs from RTTestCreate in that it disabled result reporting to file
 * and pipe in order to avoid producing invalid XML.
 *
 * @returns IPRT status code.
 * @param   pszTest     The test name.
 * @param   phTest      Where to store the test instance handle.
 */
RTR3DECL(int) RTTestCreateChild(const char *pszTest, PRTTEST phTest);

/** @name RTTEST_C_XXX - Flags for RTTestCreateEx.
 * @{ */
/** Whether to check the IPRT_TEST_XXX variables when constructing the
 * instance.  The following environment variables get checks:
 *
 *      - IPRT_TEST_MAX_LEVEL:      String value indicating which level.
 *        The env. var. is applied if the program specified the default level
 *        (by passing RTTESTLVL_INVALID).
 *
 *      - IPRT_TEST_PIPE:           The native pipe/fifo handle to write XML
 *        results to.
 *        The env. var. is applied if iNativeTestPipe is -1.
 *
 *      - IPRT_TEST_FILE:           Path to file/named-pipe/fifo/whatever to
 *        write XML results to.
 *        The env. var. is applied if the program specified a NULL path, it is
 *        not applied if the program hands us an empty string.
 *
 *      - IPRT_TEST_OMIT_TOP_TEST:  If present, this makes the XML output omit
 *        the top level test element.
 *        The env. var is applied when present.
 *
 */
#define RTTEST_C_USE_ENV                RT_BIT(0)
/** Whether to omit the top test in the XML. */
#define RTTEST_C_XML_OMIT_TOP_TEST      RT_BIT(1)
/** Whether to delay the top test XML element until testing commences. */
#define RTTEST_C_XML_DELAY_TOP_TEST     RT_BIT(2)
/** Whether to try install the test instance in the test TLS slot.  Setting
 * this flag is incompatible with using the RTTestIXxxx variant of the API. */
#define RTTEST_C_NO_TLS                 RT_BIT(3)
/** Don't report to the pipe (IPRT_TEST_PIPE or other).   */
#define RTTEST_C_NO_XML_REPORTING_PIPE  RT_BIT(4)
/** Don't report to the results file (IPRT_TEST_FILE or other).   */
#define RTTEST_C_NO_XML_REPORTING_FILE  RT_BIT(4)
/** No XML reporting to pipes, file or anything.
 * Child processes may want to use this so they don't garble the output of
 * the main test process. */
#define RTTEST_C_NO_XML_REPORTING       (RTTEST_C_NO_XML_REPORTING_PIPE | RTTEST_C_NO_XML_REPORTING_FILE)
/** Mask containing the valid bits. */
#define RTTEST_C_VALID_MASK             UINT32_C(0x0000003f)
/** @} */


/**
 * Creates a test instance.
 *
 * @returns IPRT status code.
 * @param   pszTest         The test name.
 * @param   fFlags          Flags, see RTTEST_C_XXX.
 * @param   enmMaxLevel     The max message level.  Use RTTESTLVL_INVALID for
 *                          the default output level or one from the
 *                          environment.  If specified, the environment variable
 *                          will not be able to override it.
 * @param   iNativeTestPipe Native handle to a test pipe. -1 if not interested.
 * @param   pszXmlFile      The XML output file name. If NULL the environment
 *                          may be used.  To selectively avoid that, pass an
 *                          empty string.
 * @param   phTest          Where to store the test instance handle.
 *
 * @note    At the moment, we don't fail if @a pszXmlFile or @a iNativeTestPipe
 *          fails to open.  This may change later.
 */
RTR3DECL(int) RTTestCreateEx(const char *pszTest, uint32_t fFlags, RTTESTLVL enmMaxLevel,
                             RTHCINTPTR iNativeTestPipe, const char *pszXmlFile, PRTTEST phTest);

/**
 * Initializes IPRT and creates a test instance.
 *
 * Typical usage is:
 * @code
    int main(int argc, char **argv)
    {
        RTTEST hTest;
        int rc = RTTestInitAndCreate("tstSomething", &hTest);
        if (rc)
            return rc;
        ...
    }
   @endcode
 *
 * @returns RTEXITCODE_SUCCESS on success.  On failure an error message is
 *          printed and a suitable exit code is return.
 *
 * @param   pszTest     The test name.
 * @param   phTest      Where to store the test instance handle.
 */
RTR3DECL(RTEXITCODE) RTTestInitAndCreate(const char *pszTest, PRTTEST phTest);

/**
 * Variant of RTTestInitAndCreate that includes IPRT init flags and argument
 * vectors.
 *
 * @returns RTEXITCODE_SUCCESS on success.  On failure an error message is
 *          printed and a suitable exit code is return.
 *
 * @param   cArgs       Pointer to the argument count.
 * @param   ppapszArgs  Pointer to the argument vector pointer.
 * @param   fRtInit     Flags, see RTR3INIT_XXX.
 * @param   pszTest     The test name.
 * @param   phTest      Where to store the test instance handle.
 */
RTR3DECL(RTEXITCODE) RTTestInitExAndCreate(int cArgs, char ***ppapszArgs, uint32_t fRtInit, const char *pszTest, PRTTEST phTest);

/**
 * Destroys a test instance previously created by RTTestCreate.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. NIL_RTTEST is ignored.
 */
RTR3DECL(int) RTTestDestroy(RTTEST hTest);

/**
 * Changes the default test instance for the calling thread.
 *
 * @returns IPRT status code.
 *
 * @param   hNewDefaultTest The new default test. NIL_RTTEST is fine.
 * @param   phOldTest       Where to store the old test handle. Optional.
 */
RTR3DECL(int) RTTestSetDefault(RTTEST hNewDefaultTest, PRTTEST phOldTest);

/**
 * Changes the test case name.
 *
 * @returns IRPT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszName     The new test case name.  Empty string is not accepted,
 *                      nor are strings longer than 127 chars.  Keep it short
 *                      but descriptive.
 */
RTR3DECL(int) RTTestChangeName(RTTEST hTest, const char *pszName);

/**
 * Allocate a block of guarded memory.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   cb          The amount of memory to allocate.
 * @param   cbAlign     The alignment of the returned block.
 * @param   fHead       Head or tail optimized guard.
 * @param   ppvUser     Where to return the pointer to the block.
 */
RTR3DECL(int) RTTestGuardedAlloc(RTTEST hTest, size_t cb, uint32_t cbAlign, bool fHead, void **ppvUser);

/**
 * Allocates a block of guarded memory where the guarded is immediately after
 * the user memory.
 *
 * @returns Pointer to the allocated memory. NULL on failure.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   cb          The amount of memory to allocate.
 */
RTR3DECL(void *) RTTestGuardedAllocTail(RTTEST hTest, size_t cb);

/**
 * Allocates a block of guarded memory where the guarded is right in front of
 * the user memory.
 *
 * @returns Pointer to the allocated memory. NULL on failure.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   cb          The amount of memory to allocate.
 */
RTR3DECL(void *) RTTestGuardedAllocHead(RTTEST hTest, size_t cb);

/**
 * Frees a block of guarded memory.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pv          The memory. NULL is ignored.
 */
RTR3DECL(int) RTTestGuardedFree(RTTEST hTest, void *pv);

/**
 * Test vprintf making sure the output starts on a new line.
 *
 * @returns Number of chars printed.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   enmLevel    Message importance level.
 * @param   pszFormat   The message.
 * @param   va          Arguments.
 */
RTR3DECL(int) RTTestPrintfNlV(RTTEST hTest, RTTESTLVL enmLevel, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Test printf making sure the output starts on a new line.
 *
 * @returns Number of chars printed.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   enmLevel    Message importance level.
 * @param   pszFormat   The message.
 * @param   ...         Arguments.
 */
RTR3DECL(int) RTTestPrintfNl(RTTEST hTest, RTTESTLVL enmLevel, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Test vprintf, makes sure lines are prefixed and so forth.
 *
 * @returns Number of chars printed.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   enmLevel    Message importance level.
 * @param   pszFormat   The message.
 * @param   va          Arguments.
 */
RTR3DECL(int) RTTestPrintfV(RTTEST hTest, RTTESTLVL enmLevel, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Test printf, makes sure lines are prefixed and so forth.
 *
 * @returns Number of chars printed.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   enmLevel    Message importance level.
 * @param   pszFormat   The message.
 * @param   ...         Arguments.
 */
RTR3DECL(int) RTTestPrintf(RTTEST hTest, RTTESTLVL enmLevel, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Prints the test banner.
 *
 * @returns Number of chars printed.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 */
RTR3DECL(int) RTTestBanner(RTTEST hTest);

/**
 * Summaries the test, destroys the test instance and return an exit code.
 *
 * @returns Test program exit code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 */
RTR3DECL(RTEXITCODE) RTTestSummaryAndDestroy(RTTEST hTest);

/**
 * Skips the test, destroys the test instance and return an exit code.
 *
 * @returns Test program exit code.
 * @param   hTest           The test handle. If NIL_RTTEST we'll use the one
 *                          associated with the calling thread.
 * @param   pszReasonFmt    Text explaining why, optional (NULL).
 * @param   va              Arguments for the reason format string.
 */
RTR3DECL(RTEXITCODE) RTTestSkipAndDestroyV(RTTEST hTest, const char *pszReasonFmt, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Skips the test, destroys the test instance and return an exit code.
 *
 * @returns Test program exit code.
 * @param   hTest           The test handle. If NIL_RTTEST we'll use the one
 *                          associated with the calling thread.
 * @param   pszReasonFmt    Text explaining why, optional (NULL).
 * @param   ...             Arguments for the reason format string.
 */
RTR3DECL(RTEXITCODE) RTTestSkipAndDestroy(RTTEST hTest, const char *pszReasonFmt, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Starts a sub-test.
 *
 * This will perform an implicit RTTestSubDone() call if that has not been done
 * since the last RTTestSub call.
 *
 * @returns Number of chars printed.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszSubTest  The sub-test name.
 */
RTR3DECL(int) RTTestSub(RTTEST hTest, const char *pszSubTest);

/**
 * Format string version of RTTestSub.
 *
 * See RTTestSub for details.
 *
 * @returns Number of chars printed.
 * @param   hTest           The test handle. If NIL_RTTEST we'll use the one
 *                          associated with the calling thread.
 * @param   pszSubTestFmt   The sub-test name format string.
 * @param   ...             Arguments.
 */
RTR3DECL(int) RTTestSubF(RTTEST hTest, const char *pszSubTestFmt, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Format string version of RTTestSub.
 *
 * See RTTestSub for details.
 *
 * @returns Number of chars printed.
 * @param   hTest           The test handle. If NIL_RTTEST we'll use the one
 *                          associated with the calling thread.
 * @param   pszSubTestFmt   The sub-test name format string.
 * @param   va              Arguments.
 */
RTR3DECL(int) RTTestSubV(RTTEST hTest, const char *pszSubTestFmt, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Completes a sub-test.
 *
 * @returns Number of chars printed, negative numbers are IPRT error codes.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 */
RTR3DECL(int) RTTestSubDone(RTTEST hTest);

/**
 * Starts a sub-sub-test.
 *
 * This will perform an implicit RTTestSubSubDone() call if that has not been
 * done since the last RTTestSubSub() call.
 *
 * @returns Number of chars printed.
 * @param   hTest           The test handle. If NIL_RTTEST we'll use the one
 *                          associated with the calling thread.
 * @param   pszSubSubTest   The sub-sub-test name.
 */
RTR3DECL(int) RTTestSubSub(RTTEST hTest, const char *pszSubSubTest);

/**
 * Format string version of RTTestSubSub().
 *
 * See RTTestSubSub() for details.
 *
 * @returns Number of chars printed.
 * @param   hTest               The test handle. If NIL_RTTEST we'll use the one
 *                              associated with the calling thread.
 * @param   pszSubSubTestFmt    The sub-sub-test name format string.
 * @param   ...                 Arguments.
 */
RTR3DECL(int) RTTestSubSubF(RTTEST hTest, const char *pszSubSubTestFmt, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Format string version of RTTestSubSub().
 *
 * See RTTestSubSub() for details.
 *
 * @returns Number of chars printed.
 * @param   hTest               The test handle. If NIL_RTTEST we'll use the one
 *                              associated with the calling thread.
 * @param   pszSubSubTestFmt    The sub-sub-test name format string.
 * @param   va                  Arguments.
 */
RTR3DECL(int) RTTestSubSubV(RTTEST hTest, const char *pszSubSubTestFmt, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Completes a sub-sub-test.
 *
 * @returns Number of chars printed, negative numbers are IPRT error codes.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 */
RTR3DECL(int) RTTestSubSubDone(RTTEST hTest);

/**
 * Prints an extended PASSED message, optional.
 *
 * This does not conclude the (sub-)sub-test, it could be used to report the
 * passing of a sub-sub-to-the-power-of-N-test.
 *
 * @returns Number of chars printed, negative numbers are IPRT error codes.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message. No trailing newline.
 * @param   va          The arguments.
 */
RTR3DECL(int) RTTestPassedV(RTTEST hTest, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Prints an extended PASSED message, optional.
 *
 * This does not conclude the (sub-)sub-test, it could be used to report the
 * passing of a sub-sub-to-the-power-of-N-test.
 *
 * @returns Number of chars printed, negative numbers are IPRT error codes.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message. No trailing newline.
 * @param   ...         The arguments.
 */
RTR3DECL(int) RTTestPassed(RTTEST hTest, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Marks the current test as 'SKIPPED' and optionally displays a message
 * explaining why.
 *
 * @returns Number of chars printed, negative numbers are IPRT error codes.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message. No trailing newline.  Can be NULL or empty.
 * @param   ...         The arguments.
 */
RTR3DECL(int) RTTestSkipped(RTTEST hTest, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(2, 3);

/**
 * Marks the current test as 'SKIPPED' and optionally displays a message
 * explaining why.
 *
 * @returns Number of chars printed, negative numbers are IPRT error codes.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message. No trailing newline.  Can be NULL or empty.
 * @param   va          The arguments.
 */
RTR3DECL(int) RTTestSkippedV(RTTEST hTest, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(2, 0);


/**
 * Value units.
 *
 * @remarks This is an interface where we have to be binary compatible with both
 *          older versions of this header and other components using the same
 *          contant values.
 * @remarks When adding a new item:
 *              - Always add at the end of the list.
 *              - Add it to rtTestUnitName in r3/test.cpp.
 *              - Add it as VMMDEV_TESTING_UNIT_ in include/VBox/VMMDevTesting.h.
 *              - Add it to g_aszBs2TestUnitNames in
 *                ValidationKit/bootsectors/bootsector2-common-routines.mac.
 *              - Add it to g_aszBs3TestUnitNames in bs3kit/bs3-cmn-TestData.c.
 *              - Add it to ValidationKit/common/constants/valueunit.py both as
 *                a constant (strip RTTESTUNIT_) and as a name (same as what
 *                rtTestUnitName returns) for mapping.  Testmanager must be
 *                updated.
 *              - Add it to Value.kdBestByUnit in ValidationKit/analysis/reader.py.
 */
typedef enum RTTESTUNIT
{
    /** The customary invalid zero value. */
    RTTESTUNIT_INVALID = 0,

    RTTESTUNIT_PCT,                             /**< Percentage (10^-2). */
    RTTESTUNIT_BYTES,                           /**< Bytes. */
    RTTESTUNIT_BYTES_PER_SEC,                   /**< Bytes per second. */
    RTTESTUNIT_KILOBYTES,                       /**< Kilobytes. */
    RTTESTUNIT_KILOBYTES_PER_SEC,               /**< Kilobytes per second. */
    RTTESTUNIT_MEGABYTES,                       /**< Megabytes. */
    RTTESTUNIT_MEGABYTES_PER_SEC,               /**< Megabytes per second. */
    RTTESTUNIT_PACKETS,                         /**< Packets. */
    RTTESTUNIT_PACKETS_PER_SEC,                 /**< Packets per second. */
    RTTESTUNIT_FRAMES,                          /**< Frames. */
    RTTESTUNIT_FRAMES_PER_SEC,                  /**< Frames per second. */
    RTTESTUNIT_OCCURRENCES,                     /**< Occurrences. */
    RTTESTUNIT_OCCURRENCES_PER_SEC,             /**< Occurrences per second. */
    RTTESTUNIT_CALLS,                           /**< Calls. */
    RTTESTUNIT_CALLS_PER_SEC,                   /**< Calls per second. */
    RTTESTUNIT_ROUND_TRIP,                      /**< Round trips. */
    RTTESTUNIT_SECS,                            /**< Seconds. */
    RTTESTUNIT_MS,                              /**< Milliseconds. */
    RTTESTUNIT_NS,                              /**< Nanoseconds. */
    RTTESTUNIT_NS_PER_CALL,                     /**< Nanoseconds per call. */
    RTTESTUNIT_NS_PER_FRAME,                    /**< Nanoseconds per frame. */
    RTTESTUNIT_NS_PER_OCCURRENCE,               /**< Nanoseconds per occurrence. */
    RTTESTUNIT_NS_PER_PACKET,                   /**< Nanoseconds per frame. */
    RTTESTUNIT_NS_PER_ROUND_TRIP,               /**< Nanoseconds per round trip. */
    RTTESTUNIT_INSTRS,                          /**< Instructions. */
    RTTESTUNIT_INSTRS_PER_SEC,                  /**< Instructions per second. */
    RTTESTUNIT_NONE,                            /**< No unit. */
    RTTESTUNIT_PP1K,                            /**< Parts per thousand (10^-3). */
    RTTESTUNIT_PP10K,                           /**< Parts per ten thousand (10^-4). */
    RTTESTUNIT_PPM,                             /**< Parts per million (10^-6). */
    RTTESTUNIT_PPB,                             /**< Parts per billion (10^-9). */
    RTTESTUNIT_TICKS,                           /**< CPU ticks. */
    RTTESTUNIT_TICKS_PER_CALL,                  /**< CPU ticks per call. */
    RTTESTUNIT_TICKS_PER_OCCURENCE,             /**< CPU ticks per occurence. */
    RTTESTUNIT_PAGES,                           /**< Page count. */
    RTTESTUNIT_PAGES_PER_SEC,                   /**< Pages per second. */
    RTTESTUNIT_TICKS_PER_PAGE,                  /**< CPU ticks per page. */
    RTTESTUNIT_NS_PER_PAGE,                     /**< Nanoseconds per page. */
    RTTESTUNIT_PS,                              /**< Picoseconds. */
    RTTESTUNIT_PS_PER_CALL,                     /**< Picoseconds per call. */
    RTTESTUNIT_PS_PER_FRAME,                    /**< Picoseconds per frame. */
    RTTESTUNIT_PS_PER_OCCURRENCE,               /**< Picoseconds per occurrence. */
    RTTESTUNIT_PS_PER_PACKET,                   /**< Picoseconds per frame. */
    RTTESTUNIT_PS_PER_ROUND_TRIP,               /**< Picoseconds per round trip. */
    RTTESTUNIT_PS_PER_PAGE,                     /**< Picoseconds per page. */

    /** The end of valid units. */
    RTTESTUNIT_END
} RTTESTUNIT;
AssertCompile(RTTESTUNIT_INSTRS      == 0x19);
AssertCompile(RTTESTUNIT_NONE        == 0x1b);
AssertCompile(RTTESTUNIT_NS_PER_PAGE == 0x26);
AssertCompile(RTTESTUNIT_PS_PER_PAGE == 0x2d);

/**
 * Report a named test result value.
 *
 * This is typically used for benchmarking but can be used for other purposes
 * like reporting limits of some implementation.  The value gets associated with
 * the current (sub-)sub-test, the name must be unique within the
 * (sub-)sub-test.
 *
 * @returns IPRT status code.
 *
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszName     The value name.
 * @param   u64Value    The value.
 * @param   enmUnit     The value unit.
 */
RTR3DECL(int) RTTestValue(RTTEST hTest, const char *pszName, uint64_t u64Value, RTTESTUNIT enmUnit);

/**
 * Same as RTTestValue, except that the name is now a format string.
 *
 * @returns IPRT status code.
 *
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   u64Value    The value.
 * @param   enmUnit     The value unit.
 * @param   pszNameFmt  The value name format string.
 * @param   ...         String arguments.
 */
RTR3DECL(int) RTTestValueF(RTTEST hTest, uint64_t u64Value, RTTESTUNIT enmUnit,
                           const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR(4, 5);

/**
 * Same as RTTestValue, except that the name is now a format string.
 *
 * @returns IPRT status code.
 *
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   u64Value    The value.
 * @param   enmUnit     The value unit.
 * @param   pszNameFmt  The value name format string.
 * @param   va          String arguments.
 */
RTR3DECL(int) RTTestValueV(RTTEST hTest, uint64_t u64Value, RTTESTUNIT enmUnit,
                           const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR(4, 0);

/**
 * Increments the error counter.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 */
RTR3DECL(int) RTTestErrorInc(RTTEST hTest);

/**
 * Get the current error count.
 *
 * @returns The error counter, UINT32_MAX if no valid test handle.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 */
RTR3DECL(uint32_t) RTTestErrorCount(RTTEST hTest);

/**
 * Get the error count of the current sub-test.
 *
 * @returns The error counter, UINT32_MAX if no valid test handle.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 */
RTR3DECL(uint32_t) RTTestSubErrorCount(RTTEST hTest);

/**
 * Get the error count of the current sub-sub-test.
 *
 * @returns The error counter, UINT32_MAX if no valid test handle.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 */
RTR3DECL(uint32_t) RTTestSubSubErrorCount(RTTEST hTest);

/**
 * Increments the error counter and prints a failure message.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message. No trailing newline.
 * @param   va          The arguments.
 */
RTR3DECL(int) RTTestFailedV(RTTEST hTest, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Increments the error counter and prints a failure message.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message. No trailing newline.
 * @param   ...         The arguments.
 */
RTR3DECL(int) RTTestFailed(RTTEST hTest, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Same as RTTestPrintfV with RTTESTLVL_FAILURE.
 *
 * @returns Number of chars printed.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message.
 * @param   va          Arguments.
 */
RTR3DECL(int) RTTestFailureDetailsV(RTTEST hTest, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Same as RTTestPrintf with RTTESTLVL_FAILURE.
 *
 * @returns Number of chars printed.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message.
 * @param   ...         Arguments.
 */
RTR3DECL(int) RTTestFailureDetails(RTTEST hTest, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Sets error context info to be printed with the first failure.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message, no trailing newline.  NULL to clear the
 *                      context message.
 * @param   va          The arguments.
 */
RTR3DECL(int) RTTestErrContextV(RTTEST hTest, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Sets error context info to be printed with the first failure.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @param   pszFormat   The message, no trailing newline.  NULL to clear the
 *                      context message.
 * @param   ...         The arguments.
 */
RTR3DECL(int) RTTestErrContext(RTTEST hTest, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Disables and shuts up assertions.
 *
 * Max 8 nestings.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 * @sa      RTAssertSetMayPanic, RTAssertSetQuiet.
 */
RTR3DECL(int) RTTestDisableAssertions(RTTEST hTest);

/**
 * Restores the previous call to RTTestDisableAssertions.
 *
 * @returns IPRT status code.
 * @param   hTest       The test handle. If NIL_RTTEST we'll use the one
 *                      associated with the calling thread.
 */
RTR3DECL(int) RTTestRestoreAssertions(RTTEST hTest);


/** @def RTTEST_CHECK
 * Check whether a boolean expression holds true.
 *
 * If the expression is false, call RTTestFailed giving the line number and expression.
 *
 * @param   hTest       The test handle.
 * @param   expr        The expression to evaluate.
 */
#define RTTEST_CHECK(hTest, expr) \
    do { if (!(expr)) { \
            RTTestFailed((hTest), "line %u: %s", __LINE__, #expr); \
         } \
    } while (0)
/** @def RTTEST_CHECK_RET
 * Check whether a boolean expression holds true, returns on false.
 *
 * If the expression is false, call RTTestFailed giving the line number and
 * expression, then return @a rcRet.
 *
 * @param   hTest       The test handle.
 * @param   expr        The expression to evaluate.
 * @param   rcRet       What to return on failure.
 */
#define RTTEST_CHECK_RET(hTest, expr, rcRet) \
    do { if (!(expr)) { \
            RTTestFailed((hTest), "line %u: %s", __LINE__, #expr); \
            return (rcRet); \
         } \
    } while (0)
/** @def RTTEST_CHECK_RETV
 * Check whether a boolean expression holds true, returns void on false.
 *
 * If the expression is false, call RTTestFailed giving the line number and
 * expression, then return void.
 *
 * @param   hTest       The test handle.
 * @param   expr        The expression to evaluate.
 */
#define RTTEST_CHECK_RETV(hTest, expr) \
    do { if (!(expr)) { \
            RTTestFailed((hTest), "line %u: %s", __LINE__, #expr); \
            return; \
         } \
    } while (0)
/** @def RTTEST_CHECK_BREAK
 * Check whether a boolean expression holds true.
 *
 * If the expression is false, call RTTestFailed giving the line number and
 * expression, then break.
 *
 * @param   hTest       The test handle.
 * @param   expr        The expression to evaluate.
 */
#define RTTEST_CHECK_BREAK(hTest, expr) \
    if (!(expr)) { \
        RTTestFailed((hTest), "line %u: %s", __LINE__, #expr); \
        break; \
    } else do {} while (0)


/** @def RTTEST_CHECK_MSG
 * Check whether a boolean expression holds true.
 *
 * If the expression is false, call RTTestFailed giving the line number and expression.
 *
 * @param   hTest           The test handle.
 * @param   expr            The expression to evaluate.
 * @param   DetailsArgs     Argument list for RTTestFailureDetails, including
 *                          parenthesis.
 */
#define RTTEST_CHECK_MSG(hTest, expr, DetailsArgs) \
    do { if (!(expr)) { \
            RTTestFailed((hTest), "line %u: %s", __LINE__, #expr); \
            RTTestFailureDetails DetailsArgs; \
         } \
    } while (0)
/** @def RTTEST_CHECK_MSG_RET
 * Check whether a boolean expression holds true, returns on false.
 *
 * If the expression is false, call RTTestFailed giving the line number and expression.
 *
 * @param   hTest           The test handle.
 * @param   expr            The expression to evaluate.
 * @param   DetailsArgs     Argument list for RTTestFailureDetails, including
 *                          parenthesis.
 * @param   rcRet           What to return on failure.
 */
#define RTTEST_CHECK_MSG_RET(hTest, expr, DetailsArgs, rcRet) \
    do { if (!(expr)) { \
            RTTestFailed((hTest), "line %u: %s", __LINE__, #expr); \
            RTTestFailureDetails DetailsArgs; \
            return (rcRet); \
         } \
    } while (0)
/** @def RTTEST_CHECK_MSG_RETV
 * Check whether a boolean expression holds true, returns void on false.
 *
 * If the expression is false, call RTTestFailed giving the line number and expression.
 *
 * @param   hTest           The test handle.
 * @param   expr            The expression to evaluate.
 * @param   DetailsArgs     Argument list for RTTestFailureDetails, including
 *                          parenthesis.
 */
#define RTTEST_CHECK_MSG_RETV(hTest, expr, DetailsArgs) \
    do { if (!(expr)) { \
            RTTestFailed((hTest), "line %u: %s", __LINE__, #expr); \
            RTTestFailureDetails DetailsArgs; \
            return; \
         } \
    } while (0)


/** @def RTTEST_CHECK_RC
 * Check whether an expression returns a specific IPRT style status code.
 *
 * If a different status code is return, call RTTestFailed giving the line
 * number, expression, actual and expected status codes.
 *
 * @param   hTest           The test handle.
 * @param   rcExpr          The expression resulting in an IPRT status code.
 * @param   rcExpect        The expected return code. This may be referenced
 *                          more than once by the macro.
 */
#define RTTEST_CHECK_RC(hTest, rcExpr, rcExpect) \
    do { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) { \
            RTTestFailed((hTest), "line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
        } \
    } while (0)
/** @def RTTEST_CHECK_RC_RET
 * Check whether an expression returns a specific IPRT style status code.
 *
 * If a different status code is return, call RTTestFailed giving the line
 * number, expression, actual and expected status codes, then return.
 *
 * @param   hTest           The test handle.
 * @param   rcExpr          The expression resulting in an IPRT status code.
 *                          This will be assigned to a local rcCheck variable
 *                          that can be used as return value.
 * @param   rcExpect        The expected return code. This may be referenced
 *                          more than once by the macro.
 * @param   rcRet           The return code.
 */
#define RTTEST_CHECK_RC_RET(hTest, rcExpr, rcExpect, rcRet) \
    do { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) { \
            RTTestFailed((hTest), "line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
            return (rcRet); \
        } \
    } while (0)
/** @def RTTEST_CHECK_RC_RETV
 * Check whether an expression returns a specific IPRT style status code.
 *
 * If a different status code is return, call RTTestFailed giving the line
 * number, expression, actual and expected status codes, then return.
 *
 * @param   hTest           The test handle.
 * @param   rcExpr          The expression resulting in an IPRT status code.
 * @param   rcExpect        The expected return code. This may be referenced
 *                          more than once by the macro.
 */
#define RTTEST_CHECK_RC_RETV(hTest, rcExpr, rcExpect) \
    do { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) { \
            RTTestFailed((hTest), "line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
            return; \
        } \
    } while (0)
/** @def RTTEST_CHECK_RC_BREAK
 * Check whether an expression returns a specific IPRT style status code.
 *
 * If a different status code is return, call RTTestFailed giving the line
 * number, expression, actual and expected status codes, then break.
 *
 * @param   hTest           The test handle.
 * @param   rcExpr          The expression resulting in an IPRT status code.
 * @param   rcExpect        The expected return code. This may be referenced
 *                          more than once by the macro.
 */
#define RTTEST_CHECK_RC_BREAK(hTest, rcExpr, rcExpect) \
    if (1) { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) { \
            RTTestFailed((hTest), "line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
            break; \
        } \
    } else do {} while (0)


/** @def RTTEST_CHECK_RC_OK
 * Check whether a IPRT style status code indicates success.
 *
 * If the status indicates failure, call RTTestFailed giving the line number,
 * expression and status code.
 *
 * @param   hTest           The test handle.
 * @param   rcExpr          The expression resulting in an IPRT status code.
 */
#define RTTEST_CHECK_RC_OK(hTest, rcExpr) \
    do { \
        int rcCheck = (rcExpr); \
        if (RT_FAILURE(rcCheck)) { \
            RTTestFailed((hTest), "line %u: %s: %Rrc", __LINE__, #rcExpr, rcCheck); \
        } \
    } while (0)
/** @def RTTEST_CHECK_RC_OK_BREAK
 * Check whether a IPRT style status code indicates success.
 *
 * If the status indicates failure, call RTTestFailed giving the line number,
 * expression and status code, then break.
 *
 * @param   hTest           The test handle.
 * @param   rcExpr          The expression resulting in an IPRT status code.
 *                          This will be assigned to a local rcCheck variable
 *                          that can be used as return value.
 */
#define RTTEST_CHECK_RC_OK_BREAK(hTest, rcExpr) \
    do { \
        int rcCheck = (rcExpr); \
        if (RT_FAILURE(rcCheck)) { \
            RTTestFailed((hTest), "line %u: %s: %Rrc", __LINE__, #rcExpr, rcCheck); \
            break; \
        } \
    } while (0)
/** @def RTTEST_CHECK_RC_OK_RET
 * Check whether a IPRT style status code indicates success.
 *
 * If the status indicates failure, call RTTestFailed giving the line number,
 * expression and status code, then return with the specified value.
 *
 * @param   hTest           The test handle.
 * @param   rcExpr          The expression resulting in an IPRT status code.
 *                          This will be assigned to a local rcCheck variable
 *                          that can be used as return value.
 * @param   rcRet           The return code.
 */
#define RTTEST_CHECK_RC_OK_RET(hTest, rcExpr, rcRet) \
    do { \
        int rcCheck = (rcExpr); \
        if (RT_FAILURE(rcCheck)) { \
            RTTestFailed((hTest), "line %u: %s: %Rrc", __LINE__, #rcExpr, rcCheck); \
            return (rcRet); \
        } \
    } while (0)
/** @def RTTEST_CHECK_RC_OK_RETV
 * Check whether a IPRT style status code indicates success.
 *
 * If the status indicates failure, call RTTestFailed giving the line number,
 * expression and status code, then return.
 *
 * @param   hTest           The test handle.
 * @param   rcExpr          The expression resulting in an IPRT status code.
 */
#define RTTEST_CHECK_RC_OK_RETV(hTest, rcExpr) \
    do { \
        int rcCheck = (rcExpr); \
        if (RT_FAILURE(rcCheck)) { \
            RTTestFailed((hTest), "line %u: %s: %Rrc", __LINE__, #rcExpr, rcCheck); \
            return; \
        } \
    } while (0)




/** @name Implicit Test Handle API Variation
 * The test handle is retrieved from the test TLS entry of the calling thread.
 * @{
 */

/**
 * Test vprintf, makes sure lines are prefixed and so forth.
 *
 * @returns Number of chars printed.
 * @param   enmLevel    Message importance level.
 * @param   pszFormat   The message.
 * @param   va          Arguments.
 */
RTR3DECL(int) RTTestIPrintfV(RTTESTLVL enmLevel, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Test printf, makes sure lines are prefixed and so forth.
 *
 * @returns Number of chars printed.
 * @param   enmLevel    Message importance level.
 * @param   pszFormat   The message.
 * @param   ...         Arguments.
 */
RTR3DECL(int) RTTestIPrintf(RTTESTLVL enmLevel, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Starts a sub-test.
 *
 * This will perform an implicit RTTestSubDone() call if that has not been done
 * since the last RTTestSub call.
 *
 * @returns Number of chars printed.
 * @param   pszSubTest  The sub-test name.
 */
RTR3DECL(int) RTTestISub(const char *pszSubTest);

/**
 * Format string version of RTTestSub.
 *
 * See RTTestSub for details.
 *
 * @returns Number of chars printed.
 * @param   pszSubTestFmt   The sub-test name format string.
 * @param   ...             Arguments.
 */
RTR3DECL(int) RTTestISubF(const char *pszSubTestFmt, ...) RT_IPRT_FORMAT_ATTR(1, 2);

/**
 * Format string version of RTTestSub.
 *
 * See RTTestSub for details.
 *
 * @returns Number of chars printed.
 * @param   pszSubTestFmt   The sub-test name format string.
 * @param   va              Arguments.
 */
RTR3DECL(int) RTTestISubV(const char *pszSubTestFmt, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Completes a sub-test (and any outstanding sub-sub-test).
 *
 * @returns Number of chars printed.
 */
RTR3DECL(int) RTTestISubDone(void);

/**
 * Starts a sub-sub-test.
 *
 * This will perform an implicit RTTestSubSubDone() call if that has not been
 * done since the last RTTestSubSub call.
 *
 * @returns Number of chars printed.
 * @param   pszSubSubTest   The sub-sub-test name.
 */
RTR3DECL(int) RTTestISubSub(const char *pszSubSubTest);

/**
 * Format string version of RTTestSubSub.
 *
 * See RTTestSubSub for details.
 *
 * @returns Number of chars printed.
 * @param   pszSubSubTestFmt    The sub-sub-test name format string.
 * @param   ...                 Arguments.
 */
RTR3DECL(int) RTTestISubSubF(const char *pszSubSubTestFmt, ...) RT_IPRT_FORMAT_ATTR(1, 2);

/**
 * Format string version of RTTestSubSub.
 *
 * See RTTestSubSub for details.
 *
 * @returns Number of chars printed.
 * @param   pszSubSubTestFmt    The sub-sub-test name format string.
 * @param   va                  Arguments.
 */
RTR3DECL(int) RTTestISubSubV(const char *pszSubSubTestFmt, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Completes a sub-sub-test.
 *
 * @returns Number of chars printed.
 */
RTR3DECL(int) RTTestISubSubDone(void);

/**
 * Prints an extended PASSED message, optional.
 *
 * This does not conclude the (sub-)sub-test, it could be used to report the
 * passing of a sub-sub-to-the-power-of-N-test.
 *
 * @returns IPRT status code.
 * @param   pszFormat   The message. No trailing newline.
 * @param   va          The arguments.
 */
RTR3DECL(int) RTTestIPassedV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Prints an extended PASSED message, optional.
 *
 * This does not conclude the (sub-)sub-test, it could be used to report the
 * passing of a sub-sub-to-the-power-of-N-test.
 *
 * @returns IPRT status code.
 * @param   pszFormat   The message. No trailing newline.
 * @param   ...         The arguments.
 */
RTR3DECL(int) RTTestIPassed(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

/**
 * Report a named test result value.
 *
 * This is typically used for benchmarking but can be used for other purposes
 * like reporting limits of some implementation.  The value gets associated with
 * the current (sub-)sub-test, the name must be unique within the
 * (sub-)sub-test.
 *
 * @returns IPRT status code.
 *
 * @param   pszName     The value name.
 * @param   u64Value    The value.
 * @param   enmUnit     The value unit.
 */
RTR3DECL(int) RTTestIValue(const char *pszName, uint64_t u64Value, RTTESTUNIT enmUnit);

/**
 * Same as RTTestValue, except that the name is now a format string.
 *
 * @returns IPRT status code.
 *
 * @param   u64Value    The value.
 * @param   enmUnit     The value unit.
 * @param   pszNameFmt  The value name format string.
 * @param   ...         String arguments.
 */
RTR3DECL(int) RTTestIValueF(uint64_t u64Value, RTTESTUNIT enmUnit, const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Same as RTTestValue, except that the name is now a format string.
 *
 * @returns IPRT status code.
 *
 * @param   u64Value    The value.
 * @param   enmUnit     The value unit.
 * @param   pszNameFmt  The value name format string.
 * @param   va          String arguments.
 */
RTR3DECL(int) RTTestIValueV(uint64_t u64Value, RTTESTUNIT enmUnit, const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Increments the error counter.
 *
 * @returns IPRT status code.
 */
RTR3DECL(int) RTTestIErrorInc(void);

/**
 * Get the current error count.
 *
 * @returns The error counter, UINT32_MAX if no valid test handle.
 */
RTR3DECL(uint32_t) RTTestIErrorCount(void);

/**
 * Increments the error counter and prints a failure message.
 *
 * @returns IPRT status code.
 * @param   pszFormat   The message. No trailing newline.
 * @param   va          The arguments.
 */
RTR3DECL(int) RTTestIFailedV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Increments the error counter and prints a failure message.
 *
 * @returns IPRT status code.
 * @param   pszFormat   The message. No trailing newline.
 * @param   ...         The arguments.
 */
RTR3DECL(int) RTTestIFailed(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

/**
 * Increments the error counter, prints a failure message and returns the
 * specified status code.
 *
 * This is mainly a convenience method for saving vertical space in the source
 * code.
 *
 * @returns @a rcRet
 * @param   rcRet       The IPRT status code to return.
 * @param   pszFormat   The message. No trailing newline.
 * @param   va          The arguments.
 */
RTR3DECL(int) RTTestIFailedRcV(int rcRet, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Increments the error counter, prints a failure message and returns the
 * specified status code.
 *
 * This is mainly a convenience method for saving vertical space in the source
 * code.
 *
 * @returns @a rcRet
 * @param   rcRet       The IPRT status code to return.
 * @param   pszFormat   The message. No trailing newline.
 * @param   ...         The arguments.
 */
RTR3DECL(int) RTTestIFailedRc(int rcRet, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3);

/**
 * Same as RTTestIPrintfV with RTTESTLVL_FAILURE.
 *
 * @returns Number of chars printed.
 * @param   pszFormat   The message.
 * @param   va          Arguments.
 */
RTR3DECL(int) RTTestIFailureDetailsV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Same as RTTestIPrintf with RTTESTLVL_FAILURE.
 *
 * @returns Number of chars printed.
 * @param   pszFormat   The message.
 * @param   ...         Arguments.
 */
RTR3DECL(int) RTTestIFailureDetails(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

/**
 * Sets error context info to be printed with the first failure.
 *
 * @returns IPRT status code.
 * @param   pszFormat   The message, no trailing newline.  NULL to clear the
 *                      context message.
 * @param   va          The arguments.
 */
RTR3DECL(int) RTTestIErrContextV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Sets error context info to be printed with the first failure.
 *
 * @returns IPRT status code.
 * @param   pszFormat   The message, no trailing newline.  NULL to clear the
 *                      context message.
 * @param   ...         The arguments.
 */
RTR3DECL(int) RTTestIErrContext(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

/**
 * Disables and shuts up assertions.
 *
 * Max 8 nestings.
 *
 * @returns IPRT status code.
 * @sa      RTAssertSetMayPanic, RTAssertSetQuiet.
 */
RTR3DECL(int) RTTestIDisableAssertions(void);

/**
 * Restores the previous call to RTTestDisableAssertions.
 *
 * @returns IPRT status code.
 */
RTR3DECL(int) RTTestIRestoreAssertions(void);


/** @def RTTESTI_CHECK
 * Check whether a boolean expression holds true.
 *
 * If the expression is false, call RTTestIFailed giving the line number and
 * expression.
 *
 * @param   expr        The expression to evaluate.
 */
#define RTTESTI_CHECK(expr) \
    do { if (!(expr)) { \
            RTTestIFailed("line %u: %s", __LINE__, #expr); \
         } \
    } while (0)
/** @def RTTESTI_CHECK_RET
 * Check whether a boolean expression holds true, returns on false.
 *
 * If the expression is false, call RTTestIFailed giving the line number and
 * expression, then return @a rcRet.
 *
 * @param   expr        The expression to evaluate.
 * @param   rcRet       What to return on failure.
 */
#define RTTESTI_CHECK_RET(expr, rcRet) \
    do { if (!(expr)) { \
            RTTestIFailed("line %u: %s", __LINE__, #expr); \
            return (rcRet); \
         } \
    } while (0)
/** @def RTTESTI_CHECK_RETV
 * Check whether a boolean expression holds true, returns void on false.
 *
 * If the expression is false, call RTTestIFailed giving the line number and
 * expression, then return void.
 *
 * @param   expr        The expression to evaluate.
 */
#define RTTESTI_CHECK_RETV(expr) \
    do { if (!(expr)) { \
            RTTestIFailed("line %u: %s", __LINE__, #expr); \
            return; \
         } \
    } while (0)
/** @def RTTESTI_CHECK_BREAK
 * Check whether a boolean expression holds true, returns void on false.
 *
 * If the expression is false, call RTTestIFailed giving the line number and
 * expression, then break.
 *
 * @param   expr        The expression to evaluate.
 */
#define RTTESTI_CHECK_BREAK(expr) \
    if (!(expr)) { \
        RTTestIFailed("line %u: %s", __LINE__, #expr); \
        break; \
    } else do {} while (0)


/** @def RTTESTI_CHECK_MSG
 * Check whether a boolean expression holds true.
 *
 * If the expression is false, call RTTestIFailed giving the line number and
 * expression.
 *
 * @param   expr            The expression to evaluate.
 * @param   DetailsArgs     Argument list for RTTestIFailureDetails, including
 *                          parenthesis.
 */
#define RTTESTI_CHECK_MSG(expr, DetailsArgs) \
    do { if (!(expr)) { \
            RTTestIFailed("line %u: %s", __LINE__, #expr); \
            RTTestIFailureDetails DetailsArgs; \
         } \
    } while (0)
/** @def RTTESTI_CHECK_MSG_BREAK
 * Check whether a boolean expression holds true, returns on false.
 *
 * If the expression is false, call RTTestIFailed giving the line number and
 * expression.
 *
 * @param   expr            The expression to evaluate.
 * @param   DetailsArgs     Argument list for RTTestIFailureDetails, including
 *                          parenthesis.
 */
#define RTTESTI_CHECK_MSG_BREAK(expr, DetailsArgs) \
    if (!(expr)) { \
        RTTestIFailed("line %u: %s", __LINE__, #expr); \
        RTTestIFailureDetails DetailsArgs; \
        break; \
    } else do {} while (0)
/** @def RTTESTI_CHECK_MSG_RET
 * Check whether a boolean expression holds true, returns on false.
 *
 * If the expression is false, call RTTestIFailed giving the line number and
 * expression.
 *
 * @param   expr            The expression to evaluate.
 * @param   DetailsArgs     Argument list for RTTestIFailureDetails, including
 *                          parenthesis.
 * @param   rcRet           What to return on failure.
 */
#define RTTESTI_CHECK_MSG_RET(expr, DetailsArgs, rcRet) \
    do { if (!(expr)) { \
            RTTestIFailed("line %u: %s", __LINE__, #expr); \
            RTTestIFailureDetails DetailsArgs; \
            return (rcRet); \
         } \
    } while (0)
/** @def RTTESTI_CHECK_MSG_RETV
 * Check whether a boolean expression holds true, returns void on false.
 *
 * If the expression is false, call RTTestIFailed giving the line number and
 * expression.
 *
 * @param   expr            The expression to evaluate.
 * @param   DetailsArgs     Argument list for RTTestIFailureDetails, including
 *                          parenthesis.
 */
#define RTTESTI_CHECK_MSG_RETV(expr, DetailsArgs) \
    do { if (!(expr)) { \
            RTTestIFailed("line %u: %s", __LINE__, #expr); \
            RTTestIFailureDetails DetailsArgs; \
            return; \
         } \
    } while (0)

/** @def RTTESTI_CHECK_RC
 * Check whether an expression returns a specific IPRT style status code.
 *
 * If a different status code is return, call RTTestIFailed giving the line
 * number, expression, actual and expected status codes.
 *
 * @param   rcExpr          The expression resulting in an IPRT status code.
 * @param   rcExpect        The expected return code. This may be referenced
 *                          more than once by the macro.
 */
#define RTTESTI_CHECK_RC(rcExpr, rcExpect) \
    do { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) { \
            RTTestIFailed("line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
        } \
    } while (0)
/** @def RTTESTI_CHECK_RC_RET
 * Check whether an expression returns a specific IPRT style status code.
 *
 * If a different status code is return, call RTTestIFailed giving the line
 * number, expression, actual and expected status codes, then return.
 *
 * @param   rcExpr          The expression resulting in an IPRT status code.
 *                          This will be assigned to a local rcCheck variable
 *                          that can be used as return value.
 * @param   rcExpect        The expected return code. This may be referenced
 *                          more than once by the macro.
 * @param   rcRet           The return code.
 */
#define RTTESTI_CHECK_RC_RET(rcExpr, rcExpect, rcRet) \
    do { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) { \
            RTTestIFailed("line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
            return (rcRet); \
        } \
    } while (0)
/** @def RTTESTI_CHECK_RC_RETV
 * Check whether an expression returns a specific IPRT style status code.
 *
 * If a different status code is return, call RTTestIFailed giving the line
 * number, expression, actual and expected status codes, then return.
 *
 * @param   rcExpr          The expression resulting in an IPRT status code.
 * @param   rcExpect        The expected return code. This may be referenced
 *                          more than once by the macro.
 */
#define RTTESTI_CHECK_RC_RETV(rcExpr, rcExpect) \
    do { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) { \
            RTTestIFailed("line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
            return; \
        } \
    } while (0)
/** @def RTTESTI_CHECK_RC_BREAK
 * Check whether an expression returns a specific IPRT style status code.
 *
 * If a different status code is return, call RTTestIFailed giving the line
 * number, expression, actual and expected status codes, then break.
 *
 * @param   rcExpr          The expression resulting in an IPRT status code.
 * @param   rcExpect        The expected return code. This may be referenced
 *                          more than once by the macro.
 */
#define RTTESTI_CHECK_RC_BREAK(rcExpr, rcExpect) \
    if (1) { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) { \
            RTTestIFailed("line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
            break; \
        } \
    } else do {} while (0)
/** @def RTTESTI_CHECK_RC_OK
 * Check whether a IPRT style status code indicates success.
 *
 * If the status indicates failure, call RTTestIFailed giving the line number,
 * expression and status code.
 *
 * @param   rcExpr          The expression resulting in an IPRT status code.
 */
#define RTTESTI_CHECK_RC_OK(rcExpr) \
    do { \
        int rcCheck = (rcExpr); \
        if (RT_FAILURE(rcCheck)) { \
            RTTestIFailed("line %u: %s: %Rrc", __LINE__, #rcExpr, rcCheck); \
        } \
    } while (0)
/** @def RTTESTI_CHECK_RC_OK_BREAK
 * Check whether a IPRT style status code indicates success.
 *
 * If a different status code is return, call RTTestIFailed giving the line
 * number, expression, actual and expected status codes, then break.
 *
 * @param   rcExpr          The expression resulting in an IPRT status code.
 */
#define RTTESTI_CHECK_RC_OK_BREAK(rcExpr) \
    do { \
        int rcCheck = (rcExpr); \
        if (RT_FAILURE(rcCheck)) { \
            RTTestIFailed("line %u: %s: %Rrc", __LINE__, #rcExpr, rcCheck); \
            break; \
        } \
    } while (0)
/** @def RTTESTI_CHECK_RC_OK_RET
 * Check whether a IPRT style status code indicates success.
 *
 * If the status indicates failure, call RTTestIFailed giving the line number,
 * expression and status code, then return with the specified value.
 *
 * @param   rcExpr          The expression resulting in an IPRT status code.
 *                          This will be assigned to a local rcCheck variable
 *                          that can be used as return value.
 * @param   rcRet           The return code.
 */
#define RTTESTI_CHECK_RC_OK_RET(rcExpr, rcRet) \
    do { \
        int rcCheck = (rcExpr); \
        if (RT_FAILURE(rcCheck)) { \
            RTTestIFailed("line %u: %s: %Rrc", __LINE__, #rcExpr, rcCheck); \
            return (rcRet); \
        } \
    } while (0)
/** @def RTTESTI_CHECK_RC_OK_RETV
 * Check whether a IPRT style status code indicates success.
 *
 * If the status indicates failure, call RTTestIFailed giving the line number,
 * expression and status code, then return.
 *
 * @param   rcExpr          The expression resulting in an IPRT status code.
 */
#define RTTESTI_CHECK_RC_OK_RETV(rcExpr) \
    do { \
        int rcCheck = (rcExpr); \
        if (RT_FAILURE(rcCheck)) { \
            RTTestIFailed("line %u: %s: %Rrc", __LINE__, #rcExpr, rcCheck); \
            return; \
        } \
    } while (0)

/** @} */


/** @}  */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_test_h */

