#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdUnitTest1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Unit Tests.
"""

__copyright__ = \
"""
Copyright (C) 2010-2024 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL), a copy of it is provided in the "COPYING.CDDL" file included
in the VirtualBox distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.

SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
"""
__version__ = "$Revision: 106320 $"


# Standard Python imports.
import os
import sys
import re


# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksValidationKitDir)

# Validation Kit imports.
from common     import utils;
from testdriver import base;
from testdriver import reporter;
from testdriver import vbox;
from testdriver import vboxcon;


class tdUnitTest1(vbox.TestDriver):
    """
    Unit Tests.
    """

    ## The temporary exclude list.
    ## @note This shall be empty before we release 4.3!
    kdTestCasesBuggyPerOs = {
        'darwin': {
            'testcase/tstX86-1': '',                                # 'FSTP M32R, ST0' fails; no idea why.
            'tstInt':                                 '>=7.0.0',    # Driverless package.
            'testcase/tstLow':                        '>=7.0.0',    # Driverless package.
            'testcase/tstPin':                        '>=7.0.0',    # Driverless package.
            'testcase/tstIntNet-1':                   '>=7.0.0',    # Driverless package.
            'testcase/tstRTR0DbgKrnlInfoDriver':      '>=7.0.0',    # Driverless package.
            'testcase/tstRTR0MemUserKernelDriver':    '>=7.0.0',    # Driverless package.
            'testcase/tstRTR0SemMutexDriver':         '>=7.0.0',    # Driverless package.
            'testcase/tstRTR0ThreadPreemptionDriver': '>=7.0.0',    # Driverless package.
            'testcase/tstRTR0TimerDriver':            '>=7.0.0',    # Driverless package.
            'testcase/tstDarwinKeyboard': '',                       # Fails for unknown reason.
            'testcase/tstVBoxAPIXPCOM': '',                         # Can't instantiate the VirtualBox object
                                                                    # (binary would need moving to the VirtualBox installation
                                                                    # directory, merely a compile time test anyway)
        },
        'darwin.arm64': {
            'testcase/tstRTDarwinMachKernel': '',       # Not supported on arm64 right now (and not required due to driverless).
            'testcase/tstAsmStructs': '',               # Fails on arm64 due to different sizes, also not required as there is no
                                                        # assembly code which needs to match with structs.
            'testcase/tstRTTime': '',                   # Needs more work first.
        },
        'linux': {
            'testcase/tstRTFileAio': '',                # See xTracker #8035.
        },
        'linux.amd64': {
            'testcase/tstLdr-4': '',        # failed: Failed to get bits for '/home/vbox/test/tmp/bin/testcase/tstLdrObjR0.r0'/0,
                                                        # rc=VERR_SYMBOL_VALUE_TOO_BIG. aborting test
        },
        'solaris': {
            'testcase/tstIntNet-1': '',                 # Fails opening rge0, probably a generic issue figuring which nic to use.
            'testcase/tstIprtList': '',                 # Crashes in the multithreaded test, I think.
            'testcase/tstRTCritSect': '',               # Fairness/whatever issue here.
            'testcase/tstRTR0MemUserKernelDriver': '',  # Failes when kernel to kernel buffers.
            'testcase/tstRTSemRW': '',                  # line 338: RTSemRWReleaseRead(hSemRW): got VERR_ACCESS_DENIED
            'testcase/tstRTStrAlloc': '',               # VERR_NO_STR_MEMORY!
            'testcase/tstRTFileQuerySize-1': '',        # VERR_DEV_IO_ERROR on /dev/null!
            'testcase/tstLow' : '',                     # VERR_NOT_SUPPORTED - allocating kernel memory with physical backing
                                                        # below 4GB (RTR0MemObjAllocLow) for running code (fExecutable=true)
                                                        # isn't implemented.
            'testcase/tstContiguous' : '',              # VERR_NOT_SUPPORTED - allocating kernel memory with contiguous physical
                                                        # backing below 4GB (RTR0MemObjAllocCont) for running code
                                                        # (fExecutable=true) isn't implemented.
            'tstPDMQueue' : ''                          # VERR_NOT_SUPPORTED - running without the support driver (vboxdrv) isn't
                                                        # supported on Solaris (VMCREATE_F_DRIVERLESS/SUPR3INIT_F_DRIVERLESS).
        },
        'solaris.amd64': {
            'testcase/tstLdr-4': '',        # failed: Failed to get bits for '/home/vbox/test/tmp/bin/testcase/tstLdrObjR0.r0'/0,
                                                        # rc=VERR_SYMBOL_VALUE_TOO_BIG. aborting test
        },
        'win': {
            'testcase/tstFile': '',                     # ??
            'testcase/tstIntNet-1': '',                 # possibly same issue as solaris.
            'testcase/tstMouseImpl': '',                # STATUS_ACCESS_VIOLATION
            'testcase/tstRTR0ThreadPreemptionDriver': '', # ??
            'testcase/tstRTPath': '<4.3.51r89894',
            'testcase/tstRTPipe': '',                   # ??
            'testcase/tstRTR0MemUserKernelDriver': '',  # ??
            'testcase/tstRTR0SemMutexDriver': '',       # ??
            'testcase/tstRTStrAlloc': '',               # ??
            'testcase/tstRTStrFormat': '',              # ??
            'testcase/tstRTSystemQueryOsInfo': '',      # ??
            'testcase/tstRTTemp': '',                   # ??
            'testcase/tstRTTime': '',                   # ??
            'testcase/tstTime-2': '',                   # Total time differs too much! ... delta=-10859859
            'testcase/tstTime-4': '',                   # Needs to be converted to DLL; ditto for tstTime-2.
            'testcase/tstUtf8': '',                     # ??
            'testcase/tstVMMR0CallHost-2': '',          # STATUS_STACK_OVERFLOW
            'testcase/tstX86-1': '',                    # Fails on win.x86.
            'tscpasswd': '',                            # ??
            'tstVMREQ': '',                             # ?? Same as darwin.x86?
        },
        'win.x86': {
            'testcase/tstRTR0TimerDriver': '',          # See xTracker #8041.
        }
    };

    kdTestCasesBuggy = {
        'testcase/tstGuestPropSvc': '',     # GET_NOTIFICATION fails on testboxlin5.de.oracle.com and others.
        'testcase/tstTimer': '',            # Sometimes fails on linux, not important atm.
        'testcase/tstGIP-2': '',            # 2015-09-10: Fails regularly. E.g. TestSetID 2744205 (testboxsh2),
                                            #             2743961 (wei01-b6kc-6). The responsible engineer should reenable
                                            #             it once it has been fixed.
    };

    ## The permanent exclude list.
    # @note Stripped of extensions!
    kdTestCasesBlackList = {
        'testcase/tstClipboardX11Smoke': '',            # (Old naming, deprecated) Needs X, not available on all test boxes.
        'testcase/tstClipboardGH-X11Smoke': '',         # (New name) Ditto.
        'testcase/tstClipboardHttpServerX11': '',       # Ditto.
        'testcase/tstClipboardMockHGCM': '',            # Ditto.
        'tstClipboardQt': '',                           # Is interactive and needs Qt, needed for Qt clipboard bugfixing.
        'testcase/tstClipboardQt': '',                  # In case it moves here.
        'tstDragAndDropQt': '',                         # Is interactive and needs Qt, needed for Qt drag'n drop bugfixing.
        'testcase/tstDragAndDropQt': '',                # In case it moves here.
        'testcase/tstFileLock': '',
        'testcase/tstDisasm-2': '',                     # without parameters it will disassembler 1GB starting from 0
        'testcase/tstFileAppendWin-1': '',
        'testcase/tstDir': '',                          # useless without parameters
        'testcase/tstDir-2': '',                        # useless without parameters
        'testcase/tstGlobalConfig': '',
        'testcase/tstHostHardwareLinux': '',            # must be killed with CTRL-C
        'testcase/tstHttp': '',                         # Talks to outside servers.
        'testcase/tstRTHttp': '',                       # parameters required
        'testcase/tstLdr-2': '',                        # parameters required
        'testcase/tstLdr-3': '',                        # parameters required
        'testcase/tstLdr': '',                          # parameters required
        'testcase/tstLdrLoad': '',                      # parameters required
        'testcase/tstMove': '',                         # parameters required
        'testcase/tstRTR0Timer': '',                    # loads 'tstRTR0Timer.r0'
        'testcase/tstRTR0ThreadDriver': '',             # loads 'tstRTR0Thread.r0'
        'testcase/tstRunTestcases': '',                 # that's a script like this one
        'testcase/tstRTReqPool': '',                    # fails sometimes, testcase buggy
        'testcase/tstRTS3': '',                         # parameters required
        'testcase/tstSDL': '',                          # graphics test
        'testcase/tstSupLoadModule': '',                # Needs parameters and vboxdrv access. Covered elsewhere.
        'testcase/tstSeamlessX11': '',                  # graphics test
        'testcase/tstTime-3': '',                       # parameters required
        'testcase/tstVBoxControl': '',                  # works only inside a guest
        'testcase/tstVDCopy': '',                       # parameters required
        'testcase/tstVDFill': '',                       # parameters required
        'tstAnimate': '',                               # parameters required
        'testcase/tstAPI': '',                          # user interaction required
        'tstCollector': '',                             # takes forever
        'testcase/tstHeadless': '',                     # parameters required
        'tstHeadless': '',                              # parameters required
        'tstMicroRC': '',                               # required by tstMicro
        'tstVBoxDbg': '',                               # interactive test
        'testcase/tstTestServMgr': '',                  # some strange xpcom18a4 test, does not work
        'tstTestServMgr': '',                           # some strange xpcom18a4 test, does not work
        'tstPDMAsyncCompletion': '',                    # parameters required
        'testcase/tstXptDump': '',                      # parameters required
        'tstXptDump': '',                               # parameters required
        'testcase/tstnsIFileEnumerator': '',            # some strange xpcom18a4 test, does not work
        'tstnsIFileEnumerator': '',                     # some strange xpcom18a4 test, does not work
        'testcase/tstSimpleTypeLib': '',                # parameters required
        'tstSimpleTypeLib': '',                         # parameters required
        'testcase/tstTestAtoms': '',                    # additional test file (words.txt) required
        'tstTestAtoms': '',                             # additional test file (words.txt) required
        'testcase/tstXptLink': '',                      # parameters required
        'tstXptLink': '',                               # parameters required
        'tstXPCOMCGlue': '',                            # user interaction required
        'testcase/tstXPCOMCGlue': '',                   # user interaction required
        'testcase/tstCAPIGlue': '',                     # user interaction required
        'testcase/tstTestCallTemplates': '',            # some strange xpcom18a4 test, segfaults
        'tstTestCallTemplates': '',                     # some strange xpcom18a4 test, segfaults
        'testcase/tstRTFilesystem': '',                 # parameters required
        'testcase/tstRTDvm': '',                        # parameters required
        'tstSSLCertDownloads': '',                      # Obsolete.
        # later
        'testcase/tstIntNetR0': '',                     # RTSPINLOCK_FLAGS_INTERRUPT_SAFE == RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE
        # slow stuff
        'testcase/tstAvl': '',                          # SLOW!
        'testcase/tstRTAvl': '',                        # SLOW! (new name)
        'testcase/tstVD': '',                           # 8GB fixed-sized vmdk
        # failed or hang
        'testcase/tstCryptoPkcs7Verify': '',            # hang
        'tstOVF': '',                                   # hang (only ancient version, now in new place)
        'testcase/tstRTLockValidator': '',              # Lock validation is not enabled for critical sections
        'testcase/tstGuestControlSvc': '',              # failed: line 288: testHost(&svcTable):
                                                        # expected VINF_SUCCESS, got VERR_NOT_FOUND
        'testcase/tstRTMemEf': '',                      # failed w/o error message
        'testcase/tstSupSem': '',                       # failed: SRE Timeout Accuracy (ms) : FAILED (1 errors)
        'testcase/tstCryptoPkcs7Sign': '',              # failed: 29330:
                                                        # error:02001002:lib(2):func(1):reason(2):NA:0:fopen('server.pem': '','r')
        'testcase/tstCompressionBenchmark': '',         # failed: error: RTZipBlockCompress failed
                                                        # for 'RTZipBlock/LZJB' (#4): VERR_NOT_SUPPORTED
        'tstPDMAsyncCompletionStress': '',              # VERR_INVALID_PARAMETER (cbSize = 0)
        'tstMicro': '',                                 # doesn't work on solaris, fix later if we care.
        'tstVMM-HwAccm': '',                            # failed: Only checked AMD-V on linux
        'tstVMM-HM': '',                                # failed: Only checked AMD-V on linux
        'tstVMMFork': '',                               # failed: xtracker 6171
        'tstTestFactory': '',                           # some strange xpcom18a4 test, does not work
        'testcase/tstRTSemXRoads': '',                  # sporadically failed: Traffic - 8 threads per direction, 10 sec :
                                                        # FAILED (8 errors)
        'tstVBoxAPILinux': '',                          # creates VirtualBox directories for root user because of sudo
                                                        # (should be in vbox)
        'testcase/tstVMStructDTrace': '',               # This is a D-script generator.
        'tstVMStructRC': '',                            # This is a C-code generator.
        'tstDeviceStructSizeRC': '',                    # This is a C-code generator.
        'testcase/tstTSC': '',                          # Doesn't test anything and might fail with HT or/and too many cores.
        'testcase/tstOpenUSBDev': '',                   # Not a useful testcase.
        'testcase/tstX86-1': '',                        # Really more guest side.
        'testcase/tstX86-FpuSaveRestore': '',           # Experiments, could be useful for the guest not the host.
        'tstAsmStructsRC': '',                          # Testcase run during build time (fails to find libstdc++.so.6 on some
                                                        # Solaris testboxes).
    };

    ## The permanent exclude list for ASAN builds because they trigger false-positives.
    # @note Stripped of extensions!
    kdTestCasesBlackListAsan = {
        'testcase/tstVMMR0CallHost-1': '',              # Triggers a stack overflow error on linux.amd64
    }

    # Suffix exclude list.
    kasSuffixBlackList = [
        '.r0',
        '.gc',
        '.debug',
        '.rel',
        '.sys',
        '.ko',
        '.o',
        '.obj',
        '.lib',
        '.a',
        '.so',
        '.dll',
        '.dylib',
        '.tmp',
        '.log',
        '.py',
        '.pyc',
        '.pyo',
        '.pdb',
        '.dSYM',
        '.sym',
        '.template',
        '.expected',
        '.expect',
    ];

    # White list, which contains tests considered to be safe to execute,
    # even on remote targets (guests).
    #
    # When --only-whitelist is specified, this is the only list being checked for.
    kdTestCasesWhiteList = {
        'testcase/tstFile': '',
        'testcase/tstFileLock': '',
        'testcase/tstClipboardMockHGCM': '',            # Requires X on Linux OSes. Execute on remote targets only (guests).
        'testcase/tstRTFsQueries': '',
        'testcase/tstRTLocalIpc': '',
        'testcase/tstRTPathQueryInfo': '',
        'testcase/tstRTPipe': '',
        'testcase/tstRTProcCreateEx': '',
        'testcase/tstRTProcCreatePrf': '',
        'testcase/tstRTProcIsRunningByName': '',
        'testcase/tstRTProcQueryUsername': '',
        'testcase/tstRTProcWait': '',
        'testcase/tstRTSystemQueryFirmware': '',
        'testcase/tstTime-2': '',
        'testcase/tstTime-3': '',
        'testcase/tstTime-4': '',
        'testcase/tstTimer': '',
        'testcase/tstThread-1': '',
        'testcase/tstUtf8': ''
    };

    # Test dependency list -- libraries.
    # Needed in order to execute testcases on remote targets which don't have a VBox installation present.
    kdTestCaseDepsLibs = [
        "VBoxRT"
    ];

    ## The exclude list.
    # @note Stripped extensions!
    kasHardened = [
        "testcase/tstIntNet-1",
        "testcase/tstR0ThreadPreemptionDriver", # VBox 4.3
        "testcase/tstRTR0ThreadPreemptionDriver",
        "testcase/tstRTR0MemUserKernelDriver",
        "testcase/tstRTR0SemMutexDriver",
        "testcase/tstRTR0TimerDriver",
        "testcase/tstRTR0ThreadDriver",
        'testcase/tstRTR0DbgKrnlInfoDriver',
        "tstInt",
        "tstPDMQueue",  # Comment in testcase says its driverless, but it needs driver access.
        "tstVMM",
        "tstVMMFork",
        "tstVMREQ",
        'testcase/tstCFGM',
        'testcase/tstContiguous',
        'testcase/tstGetPagingMode',
        'testcase/tstGIP-2',
        'testcase/tstInit',
        'testcase/tstLow',
        'testcase/tstMMHyperHeap',
        'testcase/tstPage',
        'testcase/tstPin',
        'testcase/tstRTTime',   'testcase/tstTime',   # GIP test case.
        'testcase/tstRTTime-2', 'testcase/tstTime-2', # GIP test case.
        'testcase/tstRTTime-4', 'testcase/tstTime-4', # GIP test case.
        'testcase/tstSSM',
        'testcase/tstSupSem-Zombie',
    ]

    ## Argument lists
    kdArguments = {
        'testcase/tstbntest':       [ '-out', os.devnull, ], # Very noisy.
    };


    ## Status code translations.
    ## @{
    kdExitCodeNames = {
        0:              'RTEXITCODE_SUCCESS',
        1:              'RTEXITCODE_FAILURE',
        2:              'RTEXITCODE_SYNTAX',
        3:              'RTEXITCODE_INIT',
        4:              'RTEXITCODE_SKIPPED',
    };
    kdExitCodeNamesWin = {
        -1073741515:    'STATUS_DLL_NOT_FOUND',
        -1073741512:    'STATUS_ORDINAL_NOT_FOUND',
        -1073741511:    'STATUS_ENTRYPOINT_NOT_FOUND',
        -1073741502:    'STATUS_DLL_INIT_FAILED',
        -1073741500:    'STATUS_UNHANDLED_EXCEPTION',
        -1073741499:    'STATUS_APP_INIT_FAILURE',
        -1073741819:    'STATUS_ACCESS_VIOLATION',
        -1073741571:    'STATUS_STACK_OVERFLOW',
    };
    ## @}

    def __init__(self):
        """
        Reinitialize child class instance.
        """
        vbox.TestDriver.__init__(self);

        # We need to set a default test VM set here -- otherwise the test
        # driver base class won't let us use the "--test-vms" switch.
        #
        # See the "--local" switch in self.parseOption().
        self.oTestVmSet = self.oTestVmManager.getSmokeVmSet('nat');

        # Selected NIC attachment.
        self.sNicAttachment = '';

        # Session handling stuff.
        # Only needed for remote tests executed by TxS.
        self.oSession    = None;
        self.oTxsSession = None;

        # The VirtualBox installation root directory.
        self.sVBoxInstallRoot = None;

        ## Testing mode being used:
        #   "local":  Execute unit tests locally (same host, default).
        #   "remote": Executes unit tests right on the remote from a given source.
        self.sMode      = 'local';

        self.cSkipped   = 0;
        self.cPassed    = 0;
        self.cFailed    = 0;

        ## The source directory where our unit tests live.
        #
        # For local mode this is our out/ or some staging directory and
        # also acts the source for copying over the testcases to a remote target.
        #
        # For remote remote this is the ${CDROM} directory where we ship the included
        # testcases on the Validation Kit ISO.
        self.sUnitTestsPathSrc = None;

        # The destination directory our unit tests live when being
        # copied over to a remote target (via TxS).
        self.sUnitTestsPathDst = None;

        # The executable suffix to use for the executing the actual testcases.
        # Will be re-set when executing the testcases on a remote (VM) once we know
        # what type of suffix to use then (based on guest OS).
        self.sExeSuff = base.exeSuff();

        self.aiVBoxVer  = (4, 3, 0, 0);

        # For testing testcase logic.
        self.fDryRun        = False;
        self.fOnlyWhiteList = False;

    @staticmethod
    def _sanitizePath(sPath):
        """
        Does a little bit of sanitizing a given path by removing quoting, if any.

        This is needed because handed-in paths via command line arguments can contain variables like "${CDROM}"
        which might need to get processed by TXS on the guest side first.

        Returns the sanitized path.
        """
        if sPath is None: # Keep uninitialized strings as-is.
            return None;
        return sPath.strip('\"').strip('\'');

    def _detectPaths(self):
        """
        Internal worker for actionVerify and actionExecute that detects paths.

        This sets sVBoxInstallRoot and sUnitTestsPathBase and returns True/False.
        """

        reporter.log2('Detecting paths (mode is "%s") ...' % ("remot" if self.isRemoteMode() else "local",));

        #
        # We need a VBox install (/ build) to test.
        #
        if not self._detectBuild():
            reporter.error('Unabled to detect the VBox build.');
            return False;

        #
        # Where are the files installed?
        # Solaris requires special handling because of it's multi arch subdirs.
        #
        if not self.sVBoxInstallRoot and self.isRemoteMode():
            self.sVBoxInstallRoot = '${CDROM}/${OS}/${ARCH}';

        elif not self.sVBoxInstallRoot:
            self.sVBoxInstallRoot = self.oBuild.sInstallPath;
            if not self.oBuild.isDevBuild() and utils.getHostOs() == 'solaris':
                sArchDir = utils.getHostArch();
                if sArchDir == 'x86': sArchDir = 'i386';
                self.sVBoxInstallRoot = os.path.join(self.sVBoxInstallRoot, sArchDir);

            ## @todo r=andy Make sure the install root really exists and is accessible.

            # Add the installation root to the PATH on windows so we can get DLLs from it.
            if utils.getHostOs() == 'win':
                sPathName = 'PATH';
                if not sPathName in os.environ:
                    sPathName = 'Path';
                sPath = os.environ.get(sPathName, '.');
                if sPath and sPath[-1] != ';':
                    sPath += ';';
                os.environ[sPathName] = sPath + self.sVBoxInstallRoot + ';';
        else:
            reporter.log2('VBox installation root already set to "%s"' % (self.sVBoxInstallRoot));

        reporter.log('VBox installation root path: %s' % (self.sVBoxInstallRoot,));

        self.sVBoxInstallRoot = self._sanitizePath(self.sVBoxInstallRoot);

        #
        # The unittests are generally not installed, so look for them.
        #
        if not self.sUnitTestsPathSrc and self.isRemoteMode():
            self.sUnitTestsPathSrc = '${CDROM}/testcase/${OS}/${ARCH}';

        elif not self.sUnitTestsPathSrc:
            sBinOrDist = 'dist' if utils.getHostOs() in [ 'darwin', ] else 'bin';
            asCandidates = [
                self.oBuild.sInstallPath,
                os.path.join(self.sScratchPath, utils.getHostOsDotArch(), self.oBuild.sType, sBinOrDist),
                os.path.join(self.sScratchPath, utils.getHostOsDotArch(), 'release', sBinOrDist),
                os.path.join(self.sScratchPath, utils.getHostOsDotArch(), 'debug',   sBinOrDist),
                os.path.join(self.sScratchPath, utils.getHostOsDotArch(), 'strict',  sBinOrDist),
                os.path.join(self.sScratchPath, utils.getHostOsDotArch(), 'dbgopt',  sBinOrDist),
                os.path.join(self.sScratchPath, utils.getHostOsDotArch(), 'profile', sBinOrDist),
                os.path.join(self.sScratchPath, sBinOrDist + '.' + utils.getHostArch()),
                os.path.join(self.sScratchPath, sBinOrDist, utils.getHostArch()),
                os.path.join(self.sScratchPath, sBinOrDist),
            ];
            if utils.getHostOs() == 'darwin':
                for i in range(1, len(asCandidates)):
                    asCandidates[i] = os.path.join(asCandidates[i], 'VirtualBox.app', 'Contents', 'MacOS');

            for sCandidat in asCandidates:
                # The path of tstVMStructSize acts as a beacon to know where all other testcases are.
                sFileBeacon = os.path.join(sCandidat, 'testcase', 'tstVMStructSize' + self.sExeSuff);
                reporter.log2('Searching for "%s" ...' % sFileBeacon);
                if os.path.exists(sFileBeacon):
                    self.sUnitTestsPathSrc = sCandidat;
                    break

            if self.sUnitTestsPathSrc:
                reporter.log('Unit test source dir path: ', self.sUnitTestsPathSrc)
            else:
                reporter.error('Unable to find unit test source dir. Candidates: %s' % (asCandidates,));
                if reporter.getVerbosity() >= 2:
                    reporter.log('Contents of "%s"' % self.sScratchPath);
                    for paths, dirs, files in os.walk(self.sScratchPath):
                        reporter.log('{} {} {}'.format(repr(paths), repr(dirs), repr(files)));
                return False

        else:
            reporter.log2('Unit test source dir already set to "%s"' % (self.sUnitTestsPathSrc))

        reporter.log('Unit test source dir path: %s' % (self.sUnitTestsPathSrc,));

        self.sUnitTestsPathSrc = self._sanitizePath(self.sUnitTestsPathSrc);

        return True;

    #
    # Overridden methods.
    #

    def showUsage(self):
        """
        Shows the testdriver usage.
        """
        fRc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('Unit Test #1 options:');
        reporter.log('  --dryrun');
        reporter.log('      Performs a dryrun (no tests being executed).');
        reporter.log('  --mode <local|remote>');
        reporter.log('      Specifies the test execution mode:');
        reporter.log('      local:  Locally on the same machine.');
        reporter.log('      remote: On remote (guest) directly (needs unit test source).');
        reporter.log('  --only-whitelist');
        reporter.log('      Only processes the white list.');
        reporter.log('  --quick');
        reporter.log('      Very selective testing.');
        reporter.log('  --unittest-source <dir>');
        reporter.log('      Sets the unit test source to <dir>.');
        reporter.log('      Also used for remote execution.');
        reporter.log('  --vbox-install-root <dir>');
        reporter.log('      Sets the VBox install root to <dir>.');
        reporter.log('      Also used for remote execution.');
        return fRc;

    def parseOption(self, asArgs, iArg):
        """
        Parses the testdriver arguments from the command line.
        """
        if asArgs[iArg] == '--dryrun':
            self.fDryRun = True;
        elif asArgs[iArg] == '--mode':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('Option "%s" needs a value' % (asArgs[iArg - 1]));
            if asArgs[iArg] in ('local', 'remote',):
                self.sMode = asArgs[iArg];
            else:
                raise base.InvalidOption('Argument "%s" invalid' % (asArgs[iArg]));
        elif asArgs[iArg] == '--unittest-source':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('Option "%s" needs a value' % (asArgs[iArg - 1]));
            self.sUnitTestsPathSrc = asArgs[iArg];
        elif asArgs[iArg] == '--only-whitelist':
            self.fOnlyWhiteList = True;
        elif asArgs[iArg] == '--quick':
            self.fOnlyWhiteList = True;
        elif asArgs[iArg] == '--vbox-install-root':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('Option "%s" needs a value' % (asArgs[iArg - 1]));
            self.sVBoxInstallRoot = asArgs[iArg];
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def actionVerify(self):
        if not self._detectPaths():
            return False;

        if self.oTestVmSet:
            return vbox.TestDriver.actionVerify(self);

        return True;

    def actionConfig(self):
        # Make sure vboxapi has been imported so we can use the constants.
        reporter.log2('actionConfig started\n')
        if not self.importVBoxApi():
            return False

        # Do the configuring.
        if self.isRemoteMode():
            if   self.sNicAttachment == 'nat':     eNic0AttachType = vboxcon.NetworkAttachmentType_NAT;
            elif self.sNicAttachment == 'bridged': eNic0AttachType = vboxcon.NetworkAttachmentType_Bridged;
            else:                                  eNic0AttachType = None;

            # Make sure to mount the Validation Kit .ISO so that TxS has the chance
            # to update itself.
            #
            # This is necessary as a lot of our test VMs nowadays have a very old TxS
            # installed which don't understand commands like uploading files to the guest.
            # Uploading files is needed for this test driver, however.
            #
            ## @todo Get rid of this as soon as we create test VMs in a descriptive (automated) manner.
            return self.oTestVmSet.actionConfig(self, eNic0AttachType = eNic0AttachType,
                                                sDvdImage = self.sVBoxValidationKitIso);
        reporter.log2('actionConfig finished\n')

        return True;

    def actionExecute(self):
        # Make sure vboxapi has been imported so we can execute the driver without going thru
        # a former configuring step.
        reporter.log2('actionExecute started\n')
        if not self.importVBoxApi():
            reporter.log2('failed to import VBox API while actionExecute\n')
            return False;
        if not self._detectPaths():
            return False;
        reporter.log2('Unit test source path is "%s"\n' % self.sUnitTestsPathSrc);

        if not self.sUnitTestsPathDst:
            self.sUnitTestsPathDst = self.sScratchPath;
        reporter.log2('Unit test destination path is "%s"\n' % self.sUnitTestsPathDst);

        if self.isRemoteMode(): # Run on a test VM (guest).
            if self.fpApiVer < 7.0: ## @todo Needs Validation Kit .ISO tweaking (including the unit tests) first.
                reporter.log('Remote unit tests for non-trunk builds skipped.');
                fRc = True;
            else:
                assert self.oTestVmSet is not None;
                fRc = self.oTestVmSet.actionExecute(self, self.testOneVmConfig);
        else: # Run locally (host).
            self._figureVersion();
            self._makeEnvironmentChanges();

            # If this is an ASAN build and we're on linux, make sure we've got
            # libasan.so.N in the  LD_LIBRARY_PATH or stuff w/o a RPATH entry
            # pointing to /opt/VirtualBox will fail (like tstAsmStructs).
            if self.getBuildType() == 'asan'  and  utils.getHostOs() in ('linux',):
                sLdLibraryPath = '';
                if 'LD_LIBRARY_PATH' in os.environ:
                    sLdLibraryPath = os.environ['LD_LIBRARY_PATH'] + ':';
                sLdLibraryPath += self.oBuild.sInstallPath;
                os.environ['LD_LIBRARY_PATH'] = sLdLibraryPath;

            fRc = self._testRunUnitTests(None);
        reporter.log2('actionExecute finished\n')

        return fRc;

    #
    # Misc.
    #
    def isRemoteMode(self):
        """ Predicate method for checking if in any remote mode. """
        return self.sMode.startswith('remote');

    #
    # Test execution helpers.
    #

    def _testRunUnitTests(self, oTestVm):
        """
        Main function to execute all unit tests.
        """

        # Determine executable suffix based on selected execution mode.
        if self.isRemoteMode(): # Run on a test VM (guest).
            if oTestVm.isWindows():
                self.sExeSuff = '.exe';
            else:
                self.sExeSuff = '';
        else:
            # For local tests this already is set in __init__
            pass;

        self._testRunUnitTestsSet(oTestVm, r'^tst*', 'testcase');
        self._testRunUnitTestsSet(oTestVm, r'^tst*', '.');

        fRc = self.cFailed == 0;

        reporter.log('');
        if self.fDryRun:
            reporter.log('*********************************************************');
            reporter.log('DRY RUN - DRY RUN - DRY RUN - DRY RUN - DRY RUN - DRY RUN');
            reporter.log('*********************************************************');
        reporter.log('*********************************************************');
        reporter.log('           Target: %s' % (oTestVm.sVmName if oTestVm else 'local',));
        reporter.log('             Mode: %s' % (self.sMode,));
        reporter.log('       Exe suffix: %s' % (self.sExeSuff,));
        reporter.log('Unit tests source: %s %s'
                     % (self.sUnitTestsPathSrc, '(on remote)' if self.isRemoteMode() else '',));
        reporter.log('VBox install root: %s %s'
                     % (self.sVBoxInstallRoot, '(on remote)' if self.isRemoteMode() else '',));
        reporter.log('*********************************************************');
        reporter.log('***  PASSED: %d' % (self.cPassed,));
        reporter.log('***  FAILED: %d' % (self.cFailed,));
        reporter.log('*** SKIPPED: %d' % (self.cSkipped,));
        reporter.log('***   TOTAL: %d' % (self.cPassed + self.cFailed + self.cSkipped,));

        return fRc;


    def testOneVmConfig(self, oVM, oTestVm):
        """
        Runs the specified VM thru test #1.
        """

        # Simple test.
        self.logVmInfo(oVM);

        if not self.fDryRun:
            # Try waiting for a bit longer (5 minutes) until the CD is available to avoid running into timeouts.
            self.oSession, self.oTxsSession = self.startVmAndConnectToTxsViaTcp(oTestVm.sVmName,
                                                                                fCdWait = not self.fDryRun,
                                                                                cMsCdWait = 5 * 60 * 1000);
            if self.oSession is None:
                return False;

            self.addTask(self.oTxsSession);

        # Determine the unit tests destination path.
        self.sUnitTestsPathDst = oTestVm.pathJoin(self.getGuestTempDir(oTestVm), 'testUnitTests');

        # Run the unit tests.
        self._testRunUnitTests(oTestVm);

        # Cleanup.
        if self.oSession is not None:
            self.removeTask(self.oTxsSession);
            self.terminateVmBySession(self.oSession);
        return True;

    #
    # Test execution helpers.
    #

    def _figureVersion(self):
        """ Tries to figure which VBox version this is, setting self.aiVBoxVer. """
        try:
            oVBox = self.oVBoxMgr.getVirtualBox();
            sVer = oVBox.version;
            sVer += 'r' + str(self.uRevision);

            sVer = sVer.strip();
            sVer = re.sub(r'_BETA.*r', '.', sVer);
            sVer = re.sub(r'_ALPHA.*r', '.', sVer);
            sVer = re.sub(r'_RC.*r', '.', sVer);
            sVer = re.sub('_SPB', '', sVer)
            sVer = sVer.replace('r', '.');

            self.aiVBoxVer = [int(sComp) for sComp in sVer.split('.')];

            reporter.log('VBox version: %s' % (self.aiVBoxVer,));
        except:
            reporter.logXcpt();
            return False;
        return True;

    def _compareVersion(self, aiVer):
        """
        Compares the give version string with the vbox version string,
        returning a result similar to C strcmp().  aiVer is on the right side.
        """
        cComponents = min(len(self.aiVBoxVer), len(aiVer));
        for i in range(cComponents):
            if self.aiVBoxVer[i] < aiVer[i]:
                return -1;
            if self.aiVBoxVer[i] > aiVer[i]:
                return 1;
        return len(self.aiVBoxVer) - len(aiVer);

    def _isExcluded(self, sTest, dExclList):
        """ Checks if the testcase is excluded or not. """
        if sTest in dExclList:
            sFullExpr = dExclList[sTest].replace(' ', '').strip();
            if sFullExpr == '':
                return True;

            # Consider each exclusion expression. These are generally ranges,
            # either open ended or closed: "<4.3.51r12345", ">=4.3.0 && <=4.3.4".
            asExprs = sFullExpr.split(';');
            for sExpr in asExprs:

                # Split it on the and operator and process each sub expression.
                fResult = True;
                for sSubExpr in sExpr.split('&&'):
                    # Split out the comparison operator and the version value.
                    if sSubExpr.startswith('<=') or sSubExpr.startswith('>='):
                        sOp = sSubExpr[:2];
                        sValue = sSubExpr[2:];
                    elif sSubExpr.startswith('<') or sSubExpr.startswith('>') or sSubExpr.startswith('='):
                        sOp = sSubExpr[:1];
                        sValue = sSubExpr[1:];
                    else:
                        sOp = sValue = '';

                    # Convert the version value, making sure we've got a valid one.
                    try:    aiValue = [int(sComp) for sComp in sValue.replace('r', '.').split('.')];
                    except: aiValue = ();
                    if not aiValue or len(aiValue) > 4:
                        reporter.error('Invalid exclusion expression for %s: "%s" [%s]' % (sTest, sSubExpr, dExclList[sTest]));
                        return True;

                    # Do the compare.
                    iCmp = self._compareVersion(aiValue);
                    if sOp == '>=' and iCmp < 0:
                        fResult = False;
                    elif sOp == '>' and iCmp <= 0:
                        fResult = False;
                    elif sOp == '<' and iCmp >= 0:
                        fResult = False;
                    elif sOp == '>=' and iCmp < 0:
                        fResult = False;
                    reporter.log2('iCmp=%s; %s %s %s -> %s' % (iCmp, self.aiVBoxVer, sOp, aiValue, fResult));

                # Did the expression match?
                if fResult:
                    return True;

        return False;

    def _sudoExecuteSync(self, asArgs):
        """
        Executes a sudo child process synchronously.
        Returns True if the process executed successfully and returned 0,
        otherwise False is returned.
        """
        reporter.log2('Executing [sudo]: %s' % (asArgs, ));
        if self.isRemoteMode():
            iRc = -1; ## @todo Not used remotely yet.
        else:
            try:
                iRc = utils.sudoProcessCall(asArgs, shell = False, close_fds = False);
            except:
                reporter.errorXcpt();
                return False;
            reporter.log('Exit code [sudo]: %s (%s)' % (iRc, asArgs));
        return iRc == 0;


    def _logExpandString(self, sString, cVerbosity = 2):
        """
        Expands a given string by asking TxS on the guest side and logs it.
        Uses log level 2 by default.

        No-op if no TxS involved.
        """
        if reporter.getVerbosity() < cVerbosity  or  self.oTxsSession is None:
            return;
        sStringExp = self.oTxsSession.syncExpandString(sString);
        if not sStringExp:
            return;
        reporter.log2('_logExpandString: "%s" -> "%s"' % (sString, sStringExp));

    def _wrapPathExists(self, sPath):
        """
        Creates the directory specified sPath (including parents).
        """
        reporter.log2('_wrapPathExists: %s' % (sPath,));
        if self.fDryRun:
            return True;
        fRc = False;
        if self.isRemoteMode():
            self._logExpandString(sPath);
            fRc = self.oTxsSession.syncIsDir(sPath, fIgnoreErrors = True);
            if not fRc:
                fRc = self.oTxsSession.syncIsFile(sPath, fIgnoreErrors = True);
        else:
            fRc = os.path.exists(sPath);
        return fRc;

    def _wrapMkDir(self, sPath):
        """
        Creates the directory specified sPath (including parents).
        """
        reporter.log2('_wrapMkDir: %s' % (sPath,));
        if self.fDryRun:
            return True;
        fRc = True;
        if self.isRemoteMode():
            fRc = self.oTxsSession.syncMkDirPath(sPath, fMode = 0o755);
        else:
            if utils.getHostOs() in [ 'win', 'os2' ]:
                os.makedirs(sPath, 0o755);
            else:
                fRc = self._sudoExecuteSync(['/bin/mkdir', '-p', '-m', '0755', sPath]);
        if not fRc:
            reporter.log('Failed to create dir "%s".' % (sPath,));
        return fRc;

    def _wrapCopyFile(self, sSrc, sDst, iMode):
        """
        Copies a file.
        """
        reporter.log2('_wrapCopyFile: %s -> %s (mode: %o)' % (sSrc, sDst, iMode,));
        if self.fDryRun:
            return True;
        fRc = True;
        if self.isRemoteMode():
            self._logExpandString(sSrc);
            self._logExpandString(sDst);
            if self.isRemoteMode():
                self.oTxsSession.syncCopyFile(sSrc, sDst, iMode);
            else:
                fRc = self.oTxsSession.syncUploadFile(sSrc, sDst);
                if fRc:
                    fRc = self.oTxsSession.syncChMod(sDst, iMode);
        else:
            if utils.getHostOs() in [ 'win', 'os2' ]:
                utils.copyFileSimple(sSrc, sDst);
                os.chmod(sDst, iMode);
            else:
                fRc = self._sudoExecuteSync(['/bin/cp', sSrc, sDst]);
                if fRc:
                    fRc = self._sudoExecuteSync(['/bin/chmod', '%o' % (iMode,), sDst]);
                    if fRc is not True:
                        raise Exception('Failed to chmod "%s".' % (sDst,));
        if not fRc:
            reporter.log('Failed to copy "%s" to "%s".' % (sSrc, sDst,));
        return fRc;

    def _wrapDeleteFile(self, sPath):
        """
        Deletes a file.
        """
        reporter.log2('_wrapDeleteFile: %s' % (sPath,));
        if self.fDryRun:
            return True;
        fRc = True;
        if self.isRemoteMode():
            if self.oTxsSession.syncIsFile(sPath):
                fRc = self.oTxsSession.syncRmFile(sPath, fIgnoreErrors = True);
        else:
            if os.path.exists(sPath):
                if utils.getHostOs() in [ 'win', 'os2' ]:
                    os.remove(sPath);
                else:
                    fRc = self._sudoExecuteSync(['/bin/rm', sPath]);
        if not fRc:
            reporter.log('Failed to remove "%s".' % (sPath,));
        return fRc;

    def _wrapRemoveDir(self, sPath):
        """
        Removes a directory.
        """
        reporter.log2('_wrapRemoveDir: %s' % (sPath,));
        if self.fDryRun:
            return True;
        fRc = True;
        if self.isRemoteMode():
            if self.oTxsSession.syncIsDir(sPath):
                fRc = self.oTxsSession.syncRmDir(sPath, fIgnoreErrors = True);
        else:
            if os.path.exists(sPath):
                if utils.getHostOs() in [ 'win', 'os2' ]:
                    os.rmdir(sPath);
                else:
                    fRc = self._sudoExecuteSync(['/bin/rmdir', sPath]);
        if not fRc:
            reporter.log('Failed to remove "%s".' % (sPath,));
        return fRc;

    def _executeTestCase(self, oTestVm, sName, sFilePathAbs, sTestCaseSubDir, oDevNull): # pylint: disable=too-many-locals,too-many-statements
        """
        Executes a test case.

        sFilePathAbs contains the absolute path (including OS-dependent executable suffix) of the testcase.

        Returns @c true if testcase was skipped, or @c if not.
        """

        fSkipped = False;

        #
        # If hardening is enabled, some test cases and their dependencies needs
        # to be copied to and execute from the source directory in order to
        # work.  They also have to be executed as root, i.e. via sudo.
        #
        fHardened       = sName in self.kasHardened and self.sUnitTestsPathSrc != self.sVBoxInstallRoot;
        asFilesToRemove = []; # Stuff to clean up.
        asDirsToRemove  = []; # Ditto.

        if fHardened or self.isRemoteMode():
            if self.isRemoteMode():
                sDstDir = os.path.join(self.sUnitTestsPathDst, sTestCaseSubDir);
            else:
                sDstDir = os.path.join(self.sVBoxInstallRoot, sTestCaseSubDir);
            if not self._wrapPathExists(sDstDir):
                self._wrapMkDir(sDstDir);
                asDirsToRemove.append(sDstDir);

            sSrc = sFilePathAbs;
            # If the testcase source does not exist for whatever reason, just mark it as skipped
            # instead of reporting an error.
            if not self._wrapPathExists(sSrc):
                self.cSkipped += 1;
                return True;

            sDst = os.path.join(sDstDir, os.path.basename(sFilePathAbs));
            fModeExe  = 0;
            fModeDeps = 0;
            if not oTestVm or (oTestVm and not oTestVm.isWindows()): ## @todo NT4 does not like the chmod. Investigate this!
                fModeExe  = 0o755;
                fModeDeps = 0o644;
            self._wrapCopyFile(sSrc, sDst, fModeExe);
            asFilesToRemove.append(sDst);

            # Copy required dependencies to destination.
            # Note! The testcases are statically linked, so there are no VBoxRT.dll/so/dylib
            #       to copy here.  This code can be currently be ignored.
            if self.isRemoteMode():
                for sLib in self.kdTestCaseDepsLibs:
                    for sSuff in [ '.dll', '.so', '.dylib' ]:
                        assert self.sVBoxInstallRoot is not None;
                        sSrc = os.path.join(self.sVBoxInstallRoot, sLib + sSuff);
                        if self._wrapPathExists(sSrc):
                            sDst = os.path.join(sDstDir, os.path.basename(sSrc));
                            self._wrapCopyFile(sSrc, sDst, fModeDeps);
                            asFilesToRemove.append(sDst);

            ## @todo r=bird: The next two are checks for _local_ files matching the remote path when in remote-mode.
            ##               It makes for very confusing reading and is a potential for trouble.

            # Copy any associated .dll/.so/.dylib.
            for sSuff in [ '.dll', '.so', '.dylib' ]:
                sSrc = os.path.splitext(sFilePathAbs)[0] + sSuff;
                if os.path.exists(sSrc):
                    sDst = os.path.join(sDstDir, os.path.basename(sSrc));
                    self._wrapCopyFile(sSrc, sDst, fModeDeps);
                    asFilesToRemove.append(sDst);

            # Copy any associated .r0, .rc and .gc modules.
            offDriver = sFilePathAbs.rfind('Driver')
            if offDriver > 0:
                for sSuff in [ '.r0', 'RC.rc', 'RC.gc' ]:
                    sSrc = sFilePathAbs[:offDriver] + sSuff;
                    if os.path.exists(sSrc):
                        sDst = os.path.join(sDstDir, os.path.basename(sSrc));
                        self._wrapCopyFile(sSrc, sDst, fModeDeps);
                        asFilesToRemove.append(sDst);

            sFilePathAbs = os.path.join(sDstDir, os.path.basename(sFilePathAbs));

        #
        # Set up arguments.
        #
        asArgs = [sFilePathAbs,]
        if sName in self.kdArguments:
            asArgs.extend(self.kdArguments[sName]);

        #
        # Set up the environment.
        #
        # - We set IPRT_TEST_OMIT_TOP_TEST to avoid the unnecessary top-test
        #   entry when running the inner tests, as it'll just add an unnecessary
        #   result nesting.
        #
        # - IPRT_TEST_FILE is set to a result.xml file when running locally.
        #   This is not necessary when executing via TxS as it sets IPRT_TEST_PIPE,
        #   which overrides IPRT_TEST_FILE, to collect the XML output.
        #
        dEnvChanges = {
            'IPRT_TEST_OMIT_TOP_TEST': '1',
        };

        sXmlFile = os.path.join(self.sUnitTestsPathDst, 'result.xml') if not self.isRemoteMode() else None;
        if sXmlFile:
            dEnvChanges['IPRT_TEST_FILE'] = sXmlFile;
            if self._wrapPathExists(sXmlFile):
                try:    os.unlink(sXmlFile);
                except: self._wrapDeleteFile(sXmlFile);

        #
        # Execute the test case.
        #
        # Windows is confusing output.  Trying a few things to get rid of this.
        # First, flush both stderr and stdout before running the child.  Second,
        # assign the child stderr to stdout.  If this doesn't help, we'll have
        # to capture the child output.
        #
        reporter.log('*** Executing %s%s...' % (asArgs, ' [hardened]' if fHardened else ''));
        try:    sys.stdout.flush();
        except: pass;
        try:    sys.stderr.flush();
        except: pass;

        iRc = 0;

        if not self.fDryRun:
            if self.isRemoteMode():
                asRemoteEnvChg = ['%s=%s' % (sKey, sValue) for sKey, sValue in utils.iteritems(dEnvChanges)];

                fRc = self.txsRunTest(self.oTxsSession, sName, cMsTimeout = 30 * 60 * 1000, sExecName = asArgs[0],
                                      asArgs = asArgs, asAddEnv = asRemoteEnvChg, fCheckSessionStatus = True);
                if fRc:
                    iRc = 0;
                else:
                    (_, sOpcode, abPayload) = self.oTxsSession.getLastReply();
                    if sOpcode.startswith('PROC NOK '): # Extract process rc.
                        iRc = abPayload[0]; # ASSUMES 8-bit rc for now.
                        if iRc == 0: # Might happen if the testcase misses some dependencies. Set it to -42 then.
                            iRc = -42;
                    else:
                        iRc = -1; ## @todo
            else:
                for sKey, sValue in utils.iteritems(dEnvChanges):
                    os.environ[sKey] = sValue;

                oChild = None;
                try:
                    if fHardened:
                        oChild = utils.sudoProcessPopen(asArgs, stdin = oDevNull, stdout = sys.stdout, stderr = sys.stdout);
                    else:
                        oChild = utils.processPopenSafe(asArgs, stdin = oDevNull, stdout = sys.stdout, stderr = sys.stdout);
                except:
                    if sName in [ 'tstAsmStructsRC',    # 32-bit, may fail to start on 64-bit linux. Just ignore.
                                ]:
                        reporter.logXcpt();
                        fSkipped = True;
                    else:
                        reporter.errorXcpt();
                    iRc    = 1023;
                    oChild = None;

                if oChild is not None:
                    self.pidFileAdd(oChild.pid, sName, fSudo = fHardened);
                    iRc = oChild.wait();
                    self.pidFileRemove(oChild.pid);
        #
        # Clean up
        #
        for sPath in asFilesToRemove:
            self._wrapDeleteFile(sPath);
        for sPath in asDirsToRemove:
            self._wrapRemoveDir(sPath);

        #
        # Report (sXmlFile is None when in remote mode).
        #
        if sXmlFile and os.path.exists(sXmlFile):
            reporter.addSubXmlFile(sXmlFile);
            if fHardened:
                self._wrapDeleteFile(sXmlFile);
            else:
                os.unlink(sXmlFile);

        if iRc == 0:
            reporter.log('*** %s: exit code %d' % (sFilePathAbs, iRc));
            self.cPassed += 1;

        elif iRc == 4: # RTEXITCODE_SKIPPED
            reporter.log('*** %s: exit code %d (RTEXITCODE_SKIPPED)' % (sFilePathAbs, iRc));
            fSkipped = True;
            self.cSkipped += 1;

        elif fSkipped:
            reporter.log('*** %s: exit code %d (Skipped)' % (sFilePathAbs, iRc));
            self.cSkipped += 1;

        else:
            sName = self.kdExitCodeNames.get(iRc, '');
            if iRc in self.kdExitCodeNamesWin and utils.getHostOs() == 'win':
                sName = self.kdExitCodeNamesWin[iRc];
            if sName != '':
                sName = ' (%s)' % (sName);

            if iRc != 1:
                reporter.testFailure('Exit status: %d%s' % (iRc, sName));
                reporter.log(  '!*! %s: exit code %d%s' % (sFilePathAbs, iRc, sName));
            else:
                reporter.error('!*! %s: exit code %d%s' % (sFilePathAbs, iRc, sName));
            self.cFailed += 1;

        return fSkipped;

    def _testRunUnitTestsSet(self, oTestVm, sTestCasePattern, sTestCaseSubDir):
        """
        Run subset of the unit tests set.
        """

        # Open /dev/null for use as stdin further down.
        try:
            oDevNull = open(os.path.devnull, 'w+');             # pylint: disable=consider-using-with,unspecified-encoding
        except:
            oDevNull = None;

        # Determin the host OS specific exclusion lists.
        dTestCasesBuggyForHostOs = self.kdTestCasesBuggyPerOs.get(utils.getHostOs(), []);
        dTestCasesBuggyForHostOs.update(self.kdTestCasesBuggyPerOs.get(utils.getHostOsDotArch(), []));

        ## @todo Add filtering for more specific OSes (like OL server, doesn't have X installed) by adding a separate
        #        black list + using utils.getHostOsVersion().

        #
        # Process the file list and run everything looking like a testcase.
        #
        if not self.fOnlyWhiteList:
            if not self.isRemoteMode():
                asFiles = sorted(os.listdir(os.path.join(self.sUnitTestsPathSrc, sTestCaseSubDir)));
            else: # 'remote'
                ## @todo Implement remote file enumeration / directory listing.
                reporter.error('Sorry, no remote file enumeration implemented yet!\nUse --only-whitelist instead.');
                return;
        else:
            # Transform our dict into a list, where the keys are the list elements.
            asFiles = list(self.kdTestCasesWhiteList.keys());
            # Make sure to only keep the list item's base name so that the iteration down below works
            # with our white list without any additional modification.
            asFiles = [os.path.basename(s) for s in asFiles];

        for sFilename in asFiles:
            # When executing in remote execution mode, make sure to append the executable suffix here, as
            # the (white / black) lists do not contain any OS-specific executable suffixes.
            if self.isRemoteMode():
                sFilename = sFilename + self.sExeSuff;
            # Separate base and suffix and morph the base into something we
            # can use for reporting and array lookups.
            sBaseName = os.path.basename(sFilename);
            sName, sSuffix = os.path.splitext(sBaseName);
            if sTestCaseSubDir != '.':
                sName = sTestCaseSubDir + '/' + sName;

            reporter.log2('sTestCasePattern=%s, sBaseName=%s, sName=%s, sSuffix=%s, sFileName=%s'
                          % (sTestCasePattern, sBaseName, sName, sSuffix, sFilename,));

            # Process white list first, if set.
            if  self.fOnlyWhiteList \
            and not self._isExcluded(sName, self.kdTestCasesWhiteList):
                # (No testStart/Done or accounting here!)
                reporter.log('%s: SKIPPED (not in white list)' % (sName,));
                continue;

            # Basic exclusion.
            if  not re.match(sTestCasePattern, sBaseName) \
            or  sSuffix in self.kasSuffixBlackList:
                reporter.log2('"%s" is not a test case.' % (sName,));
                continue;

            # When not only processing the white list, do some more checking first.
            if not self.fOnlyWhiteList:
                # Check if the testcase is black listed or buggy before executing it.
                if self._isExcluded(sName, self.kdTestCasesBlackList):
                    # (No testStart/Done or accounting here!)
                    reporter.log('%s: SKIPPED (blacklisted)' % (sName,));
                    continue;

                if self._isExcluded(sName, self.kdTestCasesBuggy):
                    reporter.testStart(sName);
                    reporter.log('%s: Skipping, buggy in general.' % (sName,));
                    reporter.testDone(fSkipped = True);
                    self.cSkipped += 1;
                    continue;

                # Some testcases don't work with ASAN.
                if self.getBuildType() == 'asan' \
                and self._isExcluded(sName, self.kdTestCasesBlackListAsan):
                    # (No testStart/Done or accounting here!)
                    reporter.log('%s: SKIPPED (blacklisted ASAN)' % (sName,));
                    continue;

                if self._isExcluded(sName, dTestCasesBuggyForHostOs):
                    reporter.testStart(sName);
                    reporter.log('%s: Skipping, buggy on %s.' % (sName, utils.getHostOs(),));
                    reporter.testDone(fSkipped = True);
                    self.cSkipped += 1;
                    continue;
            else:
                # Passed the white list check already above.
                pass;

            sFilePathAbs = os.path.normpath(os.path.join(self.sUnitTestsPathSrc, os.path.join(sTestCaseSubDir, sFilename)));
            reporter.log2('sFilePathAbs=%s\n' % (sFilePathAbs,));
            reporter.testStart(sName);
            try:
                fSkipped = self._executeTestCase(oTestVm, sName, sFilePathAbs, sTestCaseSubDir, oDevNull);
            except:
                reporter.errorXcpt('!*!');
                self.cFailed += 1;
                fSkipped = False;
            reporter.testDone(fSkipped);


if __name__ == '__main__':
    sys.exit(tdUnitTest1().main(sys.argv))
