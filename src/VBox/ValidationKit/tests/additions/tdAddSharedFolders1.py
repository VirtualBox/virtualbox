#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
VirtualBox Validation Kit - Shared Folders #1.
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
import shutil
import sys

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter;
from testdriver import base;
from common     import utils;


class SubTstDrvAddSharedFolders1(base.SubTestDriverBase):
    """
    Sub-test driver for executing shared folders tests.
    """

    def __init__(self, oTstDrv):
        base.SubTestDriverBase.__init__(self, oTstDrv, 'add-shared-folders', 'Shared Folders');

        # Note: 'basic' acts as a placeholder, to be able to run w/o 'fsperf' or 'all'.
        self.asTestsDef         = [ 'basic', 'fsperf', ];
        self.asTests            = self.asTestsDef;
        self.asExtraArgs        = [];
        self.asGstFsPerfPaths   = [
            '${CDROM}/vboxvalidationkit/${OS/ARCH}/FsPerf${EXESUFF}',
            '${CDROM}/${OS/ARCH}/FsPerf${EXESUFF}',
            '${TXSDIR}/${OS/ARCH}/FsPerf${EXESUFF}',
            '${TXSDIR}/FsPerf${EXESUFF}',
            'E:/vboxvalidationkit/${OS/ARCH}/FsPerf${EXESUFF}',
        ];
        self.sGuestSlash = '';

    def parseOption(self, asArgs, iArg):
        if asArgs[iArg] == '--add-shared-folders-tests': # 'add' as in 'additions', not the verb.
            iArg += 1;
            iNext = self.oTstDrv.requireMoreArgs(1, asArgs, iArg);
            if asArgs[iArg] == 'all':
                self.asTests = self.asTestsDef;
            else:
                self.asTests = asArgs[iArg].split(':');
                for s in self.asTests:
                    if s not in self.asTestsDef:
                        raise base.InvalidOption('The "--add-shared-folders-tests" value "%s" is not valid; valid values are: %s'
                                                 % (s, ' '.join(self.asTestsDef)));
            return iNext;
        if asArgs[iArg] == '--add-shared-folders-quick':
            self.asTests = [ 'basic' ];
            return iArg + 1;
        if asArgs[iArg] == '--add-shared-folders-extra-arg':
            iArg += 1;
            iNext = self.oTstDrv.requireMoreArgs(1, asArgs, iArg);
            self.asExtraArgs.append(asArgs[iArg]);
            return iNext;
        return iArg;

    def showUsage(self):
        base.SubTestDriverBase.showUsage(self);
        reporter.log('  --add-shared-folders-tests <t1[:t2[:]]>');
        reporter.log('      Default: all  (%s)' % (':'.join(self.asTestsDef)));
        reporter.log('  --add-shared-folders-extra-arg <fsperf-arg>');
        reporter.log('      Adds an extra FsPerf argument.  Can be repeated.');
        reporter.log('  --add-shared-folders-quick');
        reporter.log('      Skips lengthly tests (such as FsPerf).');

        return True;

    def mountShareEx(self, oSession, oTxsSession, sShareName, sHostPath, sGuestMountPoint, fMustSucceed):
        """
        Automount a shared folder in the guest, extended version.

        Returns success status, based on fMustSucceed.
        """
        reporter.testStart('Automounting "%s"' % (sShareName,));

        reporter.log2('Creating shared folder "%s" at "%s" ...' % (sShareName, sGuestMountPoint));
        try:
            oConsole = oSession.o.console;
            oConsole.createSharedFolder(sShareName, sHostPath, True, True, sGuestMountPoint);
        except:
            if fMustSucceed:
                reporter.errorXcpt('createSharedFolder(%s,%s,True,True,%s)' % (sShareName, sHostPath, sGuestMountPoint));
            else:
                reporter.log('createSharedFolder(%s,%s,True,True,%s) failed, good' % (sShareName, sHostPath, sGuestMountPoint));
            reporter.testDone();
            return False is fMustSucceed;

        # Check whether we can see the shared folder now.  Retry for 30 seconds.
        msStart = base.timestampMilli();
        while True:
            fRc = oTxsSession.syncIsDir(sGuestMountPoint + self.sGuestSlash + 'candle.dir');
            reporter.log2('candle.dir check -> %s' % (fRc,));
            if fRc is fMustSucceed:
                break;
            if base.timestampMilli() - msStart > 30000:
                reporter.error('Shared folder mounting timed out!');
                break;
            self.oTstDrv.sleep(1);

        reporter.testDone();

        return fRc == fMustSucceed;

    def mountShare(self, oSession, oTxsSession, sShareName, sHostPath, sGuestMountPoint):
        """
        Automount a shared folder in the guest.

        Returns success status.
        """
        return self.mountShareEx(oSession, oTxsSession, sShareName, sHostPath, sGuestMountPoint, fMustSucceed = True);

    def unmountShareEx(self, oSession, oTxsSession, sShareName, sGuestMountPoint, fMustSucceed):
        """
        Unmounts a shared folder in the guest.

        Returns success status, based on fMustSucceed.
        """
        reporter.log2('Autounmount');
        try:
            oConsole = oSession.o.console;
            oConsole.removeSharedFolder(sShareName);
        except:
            if fMustSucceed:
                reporter.errorXcpt('removeSharedFolder(%s)' % (sShareName,));
            else:
                reporter.log('removeSharedFolder(%s)' % (sShareName,));
            reporter.testDone();
            return False is fMustSucceed;

        # Check whether the shared folder is gone on the guest now.  Retry for 30 seconds.
        msStart = base.timestampMilli();
        while True:
            fRc = oTxsSession.syncIsDir(sGuestMountPoint + self.sGuestSlash + 'candle.dir');
            reporter.log2('candle.dir check -> %s' % (fRc,));
            if fRc is not fMustSucceed:
                break;
            if base.timestampMilli() - msStart > 30000:
                reporter.error('Shared folder unmounting timed out!');
                fRc = False;
                break;
            self.oTstDrv.sleep(1);

        reporter.testDone();

        return fRc is not fMustSucceed;

    def unmountShare(self, oSession, oTxsSession, sShareName, sGuestMountPoint):
        """
        Unmounts a shared folder in the guest, extended version.

        Returns success status, based on fMustSucceed.
        """
        return self.unmountShareEx(oSession, oTxsSession, sShareName, sGuestMountPoint, fMustSucceed = True);

    def testIt(self, oTestVm, oSession, oTxsSession): #pylint: disable=too-many-statements
        """
        Executes the test.

        Returns fRc, oTxsSession.  The latter may have changed.
        """
        reporter.log("Active tests: %s" % (self.asTests,));

        #
        # Skip the test if before 6.0 or if the VM is NT4 or older.
        #
        if self.oTstDrv.fpApiVer < 6.0:
            reporter.log('Requires 6.0 or later (for now)');
            return (None, oTxsSession);
        if oTestVm.isWindows() and oTestVm.sKind in ('WindowsNT3x', 'WindowsNT4', 'Windows2000',):
            reporter.log('No shared folders on %s' % (oTestVm.sKind,));
            return (None, oTxsSession);

        # Guess a free mount point inside the guest.
        if oTestVm.isWindows() or oTestVm.isOS2():
            self.sGuestSlash  = '\\';
        else:
            self.sGuestSlash  = '/';

        #
        # Create the host directory to share. Empty except for a 'candle.dir' subdir
        # that we use to check that it mounted correctly.
        #
        sShareName1     = 'shfl1';
        sShareHostPath1 = os.path.join(self.oTstDrv.sScratchPath, sShareName1);
        reporter.log2('Creating shared host folder "%s"...' % (sShareHostPath1,));
        if os.path.exists(sShareHostPath1):
            try:    shutil.rmtree(sShareHostPath1);
            except: return (reporter.errorXcpt('shutil.rmtree(%s)' % (sShareHostPath1,)), oTxsSession);
        try:    os.mkdir(sShareHostPath1);
        except: return (reporter.errorXcpt('os.mkdir(%s)' % (sShareHostPath1,)), oTxsSession);
        try:    os.mkdir(os.path.join(sShareHostPath1, 'candle.dir'));
        except: return (reporter.errorXcpt('os.mkdir(%s)' % (sShareHostPath1,)), oTxsSession);

        # Guess a free mount point inside the guest.
        if oTestVm.isWindows() or oTestVm.isOS2():
            sMountPoint1 = 'V:';
        else:
            sMountPoint1 = '/mnt/' + sShareName1;

        fRc = self.mountShare(oSession, oTxsSession, sShareName1, sShareHostPath1, sMountPoint1);
        if fRc is not True:
            return (False, oTxsSession); # skip the remainder if we cannot auto mount the folder.

        #
        # Run FsPerf inside the guest.
        #
        fSkip = 'fsperf' not in self.asTests;
        if fSkip is False:
            cMbFree = utils.getDiskUsage(sShareHostPath1);
            if cMbFree >= 16:
                reporter.log2('Free space: %u MBs' % (cMbFree,));
            else:
                reporter.log('Skipping FsPerf because only %u MB free on %s' % (cMbFree, sShareHostPath1,));
                fSkip = True;
        if fSkip is False:
            # Common arguments:
            asArgs = ['FsPerf', '-d', sMountPoint1 + self.sGuestSlash + 'fstestdir-1', '-s8'];

            # Skip part of mmap on older windows systems without CcCoherencyFlushAndPurgeCache (>= w7).
            reporter.log2('oTestVm.sGuestOsType=%s' % (oTestVm.sGuestOsType,));
            if   oTestVm.getNonCanonicalGuestOsType() \
              in [ 'WindowsNT3x', 'WindowsNT4', 'Windows2000', 'WindowsXP', 'WindowsXP_64', 'Windows2003',
                   'Windows2003_64', 'WindowsVista', 'WindowsVista_64', 'Windows2008', 'Windows2008_64']:
                asArgs.append('--no-mmap-coherency');

            # Configure I/O block sizes according to guest memory size:
            cbMbRam = 128;
            try:    cbMbRam = oSession.o.machine.memorySize;
            except: reporter.errorXcpt();
            reporter.log2('cbMbRam=%s' % (cbMbRam,));
            asArgs.append('--set-block-size=1');
            asArgs.append('--add-block-size=512');
            asArgs.append('--add-block-size=4096');
            asArgs.append('--add-block-size=16384');
            asArgs.append('--add-block-size=65536');
            asArgs.append('--add-block-size=1048576');       #   1 MiB
            if cbMbRam >= 512:
                asArgs.append('--add-block-size=33554432');  #  32 MiB
            if cbMbRam >= 768:
                asArgs.append('--add-block-size=134217728'); # 128 MiB

            # Putting lots (10000) of files in a single directory causes issues on OS X
            # (HFS+ presumably, though could be slow disks) and some linuxes (slow disks,
            # maybe ext2/3?).  So, generally reduce the file count to 4096 everywhere
            # since we're not here to test the host file systems, and 3072 on macs.
            if utils.getHostOs() in [ 'darwin', ]:
                asArgs.append('--many-files=3072');
            elif utils.getHostOs() in [ 'linux', ]:
                asArgs.append('--many-files=4096');

            # Add the extra arguments from the command line and kick it off:
            asArgs.extend(self.asExtraArgs);

            # Run FsPerf:
            reporter.log2('Starting guest FsPerf (%s)...' % (asArgs,));
            sFsPerfPath = self._locateGstFsPerf(oTxsSession);

            ## @todo For some odd reason the combined GA/VaKit .ISO (by IPRT/fs/isomakercmd)
            #        sometimes (?) contains FsPerf as non-executable (-r--r--r-- 1 root root) on Linux.
            #
            #        So work around this for now by copying the desired FsPerf binary to the temp directory,
            #        make it executable and execute it from there.
            fISOMakerCmdIsBuggy = oTestVm.isLinux();
            if fISOMakerCmdIsBuggy:
                sFsPerfPathTemp = oTestVm.pathJoin(self.oTstDrv.getGuestTempDir(oTestVm), 'FsPerf${EXESUFF}');
                if oTestVm.isWindows() \
                or oTestVm.isOS2():
                    sCopy           = self.oTstDrv.getGuestSystemShell();
                    sCopyArgs       = ( sCopy, "/C", "copy", "/Y",  sFsPerfPath, sFsPerfPathTemp );
                else:
                    # Really old guests (like OL 6) have their coreutils in /bin instead of symlinking /bin to /usr/bin.
                    if self.oTstDrv.txsIsFile(oSession, oTxsSession, "/bin/cp", fIgnoreErrors = True):
                        sCopy = "/bin/cp";
                    else:
                        sCopy = oTestVm.pathJoin(self.oTstDrv.getGuestSystemDir(oTestVm), 'cp');
                    sCopyArgs = ( sCopy, "-a", "-v", sFsPerfPath, sFsPerfPathTemp );
                fRc = self.oTstDrv.txsRunTest(oTxsSession, 'Copying FsPerf', 60 * 1000,
                                              sCopy, sCopyArgs, fCheckSessionStatus = True);
                fRc = fRc and oTxsSession.syncChMod(sFsPerfPathTemp, 0o755);
                if fRc:
                    sFsPerfPath = sFsPerfPathTemp;

            fRc = self.oTstDrv.txsRunTest(oTxsSession, 'Running FsPerf', 90 * 60 * 1000, sFsPerfPath, asArgs,
                                          fCheckSessionStatus = True);
            reporter.log2('FsPerf -> %s' % (fRc,));
            if fRc:
                # Do a bit of diagnosis to find out why this failed.
                if     not oTestVm.isWindows() \
                   and not oTestVm.isOS2():
                    # Really old guests (like OL 6) have their coreutils in /bin instead of symlinking /bin to /usr/bin.
                    if self.oTstDrv.txsIsFile(oSession, oTxsSession, "/bin/ls", fIgnoreErrors = True):
                        sCmdLs = "/bin/ls";
                    else:
                        sCmdLs = oTestVm.pathJoin(self.oTstDrv.getGuestSystemDir(oTestVm), 'ls');
                    oTxsSession.syncExec(sCmdLs, (sCmdLs, "-al", sFsPerfPath), fIgnoreErrors = True);
                    oTxsSession.syncExec(sCmdLs, (sCmdLs, "-al", "-R", "/opt"), fIgnoreErrors = True);
                    oTxsSession.syncExec(sCmdLs, (sCmdLs, "-al", "-R", "/media/cdrom"), fIgnoreErrors = True);

            sTestDir = os.path.join(sShareHostPath1, 'fstestdir-1');
            if os.path.exists(sTestDir):
                fRc = reporter.errorXcpt('test directory lingers: %s' % (sTestDir,));
                try:    shutil.rmtree(sTestDir);
                except: fRc = reporter.errorXcpt('shutil.rmtree(%s)' % (sTestDir,));
        else:
            reporter.testStart('FsPerf');
            reporter.testDone(fSkip or fRc is None);

        #
        # Check if auto-unmounting works.
        #
        if fRc is True:
            fRc = self.unmountShare(oSession, oTxsSession, sShareName1, sMountPoint1);

        ## @todo Add tests for multiple automount shares, random unmounting, reboot test.

        return (fRc, oTxsSession);

    def _locateGstFsPerf(self, oTxsSession):
        """
        Returns guest side path to FsPerf.
        """
        for sFsPerfPath in self.asGstFsPerfPaths:
            if oTxsSession.syncIsFile(sFsPerfPath):
                reporter.log('Using FsPerf at "%s"' % (sFsPerfPath,));
                return sFsPerfPath;
        reporter.log('Unable to find guest FsPerf in any of these places: %s' % ('\n    '.join(self.asGstFsPerfPaths),));
        return self.asGstFsPerfPaths[0];



if __name__ == '__main__':
    reporter.error('Cannot run standalone, use tdAddBasic1.py');
    sys.exit(1);
