## @file
# UnitTestFrameworkPkg DSC file used to build host-based unit tests that are
# always expected to fail to demonstrate the format of the log file and reports
# when failures occur.
#
# For Google Test based unit tests, in order to see full log of errors from the
# sanitizer, the Google Test handling of exceptions must be disabled by either
# setting the environment variable GTEST_CATCH_EXCEPTIONS=0 or passing
# --gtest-catch-exceptions=0 on the command line when executing unit tests.
#
# Copyright (c) 2024, Intel Corporation. All rights reserved.<BR>
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

[Defines]
  PLATFORM_NAME           = UnitTestFrameworkPkgHostTest
  PLATFORM_GUID           = C7F97D6D-54AC-45A9-8197-CC99B20CC7EC
  PLATFORM_VERSION        = 0.1
  DSC_SPECIFICATION       = 0x00010005
  OUTPUT_DIRECTORY        = Build/UnitTestFrameworkPkg/HostTestExpectFail
  SUPPORTED_ARCHITECTURES = IA32|X64
  BUILD_TARGETS           = NOOPT
  SKUID_IDENTIFIER        = DEFAULT

!include UnitTestFrameworkPkg/UnitTestFrameworkPkgHost.dsc.inc

[PcdsPatchableInModule]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x1F

[Components]
  #
  # Build HOST_APPLICATIONs that test the SampleUnitTest
  #
  UnitTestFrameworkPkg/Test/GoogleTest/Sample/SampleGoogleTestExpectFail/SampleGoogleTestHostExpectFail.inf
  UnitTestFrameworkPkg/Test/UnitTest/Sample/SampleUnitTestExpectFail/SampleUnitTestHostExpectFail.inf

  #
  # Disable warning for divide by zero to pass build of unit tests
  # that generate a divide by zero exception.
  #
  UnitTestFrameworkPkg/Test/GoogleTest/Sample/SampleGoogleTestGenerateException/SampleGoogleTestHostGenerateException.inf {
    <BuildOptions>
      MSFT:*_*_*_CC_FLAGS = /wd4723
  }
  UnitTestFrameworkPkg/Test/UnitTest/Sample/SampleUnitTestGenerateException/SampleUnitTestHostGenerateException.inf {
    <BuildOptions>
      MSFT:*_*_*_CC_FLAGS = /wd4723
  }

  #
  # Unit tests that perform illegal actions that are caught by a sanitizer
  #
  UnitTestFrameworkPkg/Test/UnitTest/Sample/SampleUnitTestDoubleFree/SampleUnitTestDoubleFree.inf
  UnitTestFrameworkPkg/Test/UnitTest/Sample/SampleUnitTestBufferOverflow/SampleUnitTestBufferOverflow.inf
  UnitTestFrameworkPkg/Test/UnitTest/Sample/SampleUnitTestBufferUnderflow/SampleUnitTestBufferUnderflow.inf
  UnitTestFrameworkPkg/Test/UnitTest/Sample/SampleUnitTestNullAddress/SampleUnitTestNullAddress.inf
  UnitTestFrameworkPkg/Test/UnitTest/Sample/SampleUnitTestInvalidAddress/SampleUnitTestInvalidAddress.inf
