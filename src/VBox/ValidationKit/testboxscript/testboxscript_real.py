#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: testboxscript_real.py 110227 2025-07-15 12:51:01Z ksenia.s.stepanova@oracle.com $

"""
TestBox Script - main().
"""

__copyright__ = \
"""
Copyright (C) 2012-2024 Oracle and/or its affiliates.

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
__version__ = "$Revision: 110227 $"


# Standard python imports.
import math
import os
from optparse import OptionParser       # pylint: disable=deprecated-module
import platform
import random
import shutil
import sys
import tempfile
import time
import uuid

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksTestScriptDir = os.path.dirname(os.path.abspath(__file__));
g_ksValidationKitDir = os.path.dirname(g_ksTestScriptDir);
sys.path.extend([g_ksTestScriptDir, g_ksValidationKitDir]);

# Validation Kit imports.
from common             import constants;
from common             import utils;
import testboxcommons;
from testboxcommons     import TestBoxException;
from testboxcommand     import TestBoxCommand;
from testboxconnection  import TestBoxConnection;
from testboxscript      import TBS_EXITCODE_SYNTAX, TBS_EXITCODE_FAILURE;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


class TestBoxScriptException(Exception):
    """ For raising exceptions during TestBoxScript.__init__. """
    pass;                               # pylint: disable=unnecessary-pass


class TestBoxScript(object):
    """
    Implementation of the test box script.
    Communicate with test manager and perform offered actions.
    """

    ## @name Class Constants.
    # @{

    # Scratch space round value (MB).
    kcMbScratchSpaceRounding = 64
    # Memory size round value (MB).
    kcMbMemoryRounding = 4
    # A NULL UUID in string form.
    ksNullUuid = '00000000-0000-0000-0000-000000000000';
    # The minimum dispatch loop delay.
    kcSecMinDelay = 12;
    # The maximum dispatch loop delay (inclusive).
    kcSecMaxDelay = 24;
    # The minimum sign-on delay.
    kcSecMinSignOnDelay = 30;
    # The maximum sign-on delay (inclusive).
    kcSecMaxSignOnDelay = 60;

    # Keys for config params
    VALUE = 'value'
    FN = 'fn'                           # pylint: disable=invalid-name

    ## @}


    def __init__(self, oOptions):
        """
        Initialize internals
        """
        self._oOptions        = oOptions;
        self._sTestBoxHelper  = None;

        # Signed-on state
        self._cSignOnAttempts = 0;
        self._fSignedOn       = False;
        self._fNeedReSignOn   = False;
        self._fFirstSignOn    = True;
        self._idTestBox       = None;
        self._sTestBoxName    = '';
        self._sTestBoxUuid    = self.ksNullUuid; # convenience, assigned below.

        # Command processor.
        self._oCommand = TestBoxCommand(self);

        #
        # Scratch dir setup.  Use /var/tmp instead of /tmp because we may need
        # many many GBs for some test scenarios and /tmp can be backed by swap
        # or be a fast+small disk of some kind, while /var/tmp is normally
        # larger, if slower.  /var/tmp is generally not cleaned up on reboot,
        # /tmp often is, this would break host panic / triple-fault detection.
        #
        if self._oOptions.sScratchRoot is None:
            if utils.getHostOs() in ('win', 'os2', 'haiku', 'dos'):
                # We need *lots* of space, so avoid /tmp as it may be a memory
                # file system backed by the swap file, or worse.
                self._oOptions.sScratchRoot = tempfile.gettempdir();
            else:
                self._oOptions.sScratchRoot = '/var/tmp';
            sSubDir = 'testbox';
            try:
                sSubDir = '%s-%u' % (sSubDir, os.getuid()); # pylint: disable=no-member
            except:
                pass;
            self._oOptions.sScratchRoot = os.path.join(self._oOptions.sScratchRoot, sSubDir);

        self._sScratchSpill   = os.path.join(self._oOptions.sScratchRoot, 'scratch');
        self._sScratchScripts = os.path.join(self._oOptions.sScratchRoot, 'scripts');
        self._sScratchState   = os.path.join(self._oOptions.sScratchRoot, 'state');   # persistant storage.

        for sDir in [self._oOptions.sScratchRoot, self._sScratchSpill, self._sScratchScripts, self._sScratchState]:
            if not os.path.isdir(sDir):
                os.makedirs(sDir, 0o700);

        # We count consecutive reinitScratch failures and will reboot the
        # testbox after a while in the hope that it will correct the issue.
        self._cReinitScratchErrors = 0;

        #
        # Mount builds and test resources if requested.
        #
        self.mountShares();

        #
        # Sign-on parameters: Packed into list of records of format:
        # { <Parameter ID>: { <Current value>, <Check function> } }
        #
        self._ddSignOnParams = \
        {
            constants.tbreq.ALL_PARAM_TESTBOX_UUID:        { self.VALUE: self._getHostSystemUuid(),    self.FN: None },
            constants.tbreq.SIGNON_PARAM_OS:               { self.VALUE: utils.getHostOs(),            self.FN: None },
            constants.tbreq.SIGNON_PARAM_OS_VERSION:       { self.VALUE: utils.getHostOsVersion(),     self.FN: None },
            constants.tbreq.SIGNON_PARAM_CPU_ARCH:         { self.VALUE: utils.getHostArch(),          self.FN: None },
            constants.tbreq.SIGNON_PARAM_CPU_VENDOR:       { self.VALUE: self._getHostCpuVendor(),     self.FN: None },
            constants.tbreq.SIGNON_PARAM_CPU_NAME:         { self.VALUE: self._getHostCpuName(),       self.FN: None },
            constants.tbreq.SIGNON_PARAM_CPU_REVISION:     { self.VALUE: self._getHostCpuRevision(),   self.FN: None },
            constants.tbreq.SIGNON_PARAM_HAS_HW_VIRT:      { self.VALUE: self._hasHostHwVirt(),        self.FN: None },
            constants.tbreq.SIGNON_PARAM_HAS_NESTED_PAGING:{ self.VALUE: self._hasHostNestedPaging(),  self.FN: None },
            constants.tbreq.SIGNON_PARAM_HAS_64_BIT_GUEST: { self.VALUE: self._can64BitGuest(),        self.FN: None },
            constants.tbreq.SIGNON_PARAM_HAS_IOMMU:        { self.VALUE: self._hasHostIoMmu(),         self.FN: None },
            constants.tbreq.SIGNON_PARAM_HAS_NATIVE_API:   { self.VALUE: self._hasHostNativeApi(),     self.FN: None },
            #constants.tbreq.SIGNON_PARAM_WITH_RAW_MODE:    { self.VALUE: self._withRawModeSupport(),   self.FN: None },
            constants.tbreq.SIGNON_PARAM_SCRIPT_REV:       { self.VALUE: self._getScriptRev(),         self.FN: None },
            constants.tbreq.SIGNON_PARAM_REPORT:           { self.VALUE: self._getHostReport(),        self.FN: None },
            constants.tbreq.SIGNON_PARAM_PYTHON_VERSION:   { self.VALUE: self._getPythonHexVersion(),  self.FN: None },
            constants.tbreq.SIGNON_PARAM_CPU_COUNT:        { self.VALUE: None,     self.FN: utils.getPresentCpuCount },
            constants.tbreq.SIGNON_PARAM_MEM_SIZE:         { self.VALUE: None,     self.FN: self._getHostMemSize },
            constants.tbreq.SIGNON_PARAM_SCRATCH_SIZE:     { self.VALUE: None,     self.FN: self._getFreeScratchSpace },
        }
        for sItem in self._ddSignOnParams:                      # pylint: disable=consider-using-dict-items
            if self._ddSignOnParams[sItem][self.FN] is not None:
                self._ddSignOnParams[sItem][self.VALUE] = self._ddSignOnParams[sItem][self.FN]()

        testboxcommons.log('Starting Test Box script (%s)' % (self._getScriptRev(),));
        testboxcommons.log('Test Manager URL: %s' % self._oOptions.sTestManagerUrl,)
        testboxcommons.log('Scratch root path: %s' % self._oOptions.sScratchRoot,)
        for sItem in self._ddSignOnParams:                      # pylint: disable=consider-using-dict-items
            testboxcommons.log('Sign-On value %18s: %s' % (sItem, self._ddSignOnParams[sItem][self.VALUE]));

        #
        # The System UUID is the primary identification of the machine, so
        # refuse to cooperate if it's NULL.
        #
        self._sTestBoxUuid = self.getSignOnParam(constants.tbreq.ALL_PARAM_TESTBOX_UUID);
        if self._sTestBoxUuid == self.ksNullUuid:
            raise TestBoxScriptException('Couldn\'t determine the System UUID, please use --system-uuid to specify it.');

        #
        # Export environment variables, clearing any we don't know yet.
        #
        for sEnvVar in self._oOptions.asEnvVars:
            iEqual = sEnvVar.find('=');
            if iEqual == -1:    # No '=', remove it.
                if sEnvVar in os.environ:
                    del os.environ[sEnvVar];
            elif iEqual > 0:    # Set it.
                os.environ[sEnvVar[:iEqual]] = sEnvVar[iEqual+1:];
            else:               # Starts with '=', bad user.
                raise TestBoxScriptException('Invalid -E argument: "%s"' % (sEnvVar,));

        os.environ['TESTBOX_PATH_BUILDS']       = self._oOptions.sBuildsPath;
        os.environ['TESTBOX_PATH_RESOURCES']    = self._oOptions.sTestRsrcPath;
        os.environ['TESTBOX_PATH_SCRATCH']      = self._sScratchSpill;
        os.environ['TESTBOX_PATH_SCRIPTS']      = self._sScratchScripts;
        os.environ['TESTBOX_PATH_UPLOAD']       = self._sScratchSpill; ## @todo drop the UPLOAD dir?
        os.environ['TESTBOX_HAS_HW_VIRT']       = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_HAS_HW_VIRT);
        os.environ['TESTBOX_HAS_NESTED_PAGING'] = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_HAS_NESTED_PAGING);
        os.environ['TESTBOX_HAS_IOMMU']         = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_HAS_IOMMU);
        os.environ['TESTBOX_HAS_NATIVE_API']    = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_HAS_NATIVE_API);
        os.environ['TESTBOX_SCRIPT_REV']        = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_SCRIPT_REV);
        os.environ['TESTBOX_CPU_COUNT']         = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_CPU_COUNT);
        os.environ['TESTBOX_MEM_SIZE']          = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_MEM_SIZE);
        os.environ['TESTBOX_SCRATCH_SIZE']      = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_SCRATCH_SIZE);
        #TODO: os.environ['TESTBOX_WITH_RAW_MODE']     = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_WITH_RAW_MODE);
        os.environ['TESTBOX_WITH_RAW_MODE']     = str(self._withRawModeSupport());
        os.environ['TESTBOX_MANAGER_URL']       = self._oOptions.sTestManagerUrl;
        os.environ['TESTBOX_UUID']              = self._sTestBoxUuid;
        os.environ['TESTBOX_REPORTER']          = 'remote';
        os.environ['TESTBOX_NAME']              = '';
        os.environ['TESTBOX_ID']                = '';
        os.environ['TESTBOX_TEST_SET_ID']       = '';
        os.environ['TESTBOX_TIMEOUT']           = '0';
        os.environ['TESTBOX_TIMEOUT_ABS']       = '0';

        if utils.getHostOs() == 'win':
            os.environ['COMSPEC']            = os.path.join(os.environ['SystemRoot'], 'System32', 'cmd.exe');
        # Currently omitting any kBuild tools.

    def mountShares(self):
        """
        Mounts the shares.
        Raises exception on failure.
        """
        self._mountShare(self._oOptions.sBuildsPath, self._oOptions.sBuildsServerType, self._oOptions.sBuildsServerName,
                         self._oOptions.sBuildsServerShare,
                         self._oOptions.sBuildsServerUser, self._oOptions.sBuildsServerPasswd,
                         self._oOptions.sBuildsServerMountOpt, 'builds');
        self._mountShare(self._oOptions.sTestRsrcPath, self._oOptions.sTestRsrcServerType, self._oOptions.sTestRsrcServerName,
                         self._oOptions.sTestRsrcServerShare,
                         self._oOptions.sTestRsrcServerUser, self._oOptions.sTestRsrcServerPasswd,
                         self._oOptions.sTestRsrcServerMountOpt, 'testrsrc');
        return True;

    def _mountShare(self, sMountPoint, sType, sServer, sShare, sUser, sPassword, sMountOpt, sWhat):
        """
        Mounts the specified share if needed.
        Raises exception on failure.
        """
        # Only mount if the type is specified.
        if sType is None:
            return True;

        # Test if already mounted.
        sTestFile = os.path.join(sMountPoint + os.path.sep, os.path.basename(sShare) + '-new.txt');
        if os.path.isfile(sTestFile):
            return True;

        #
        # Platform specific mount code.
        #
        sHostOs = utils.getHostOs()
        if sHostOs in ('darwin', 'freebsd'):
            if sMountOpt != '':
                sMountOpt = ',' + sMountOpt
            utils.sudoProcessCall(['/sbin/umount', sMountPoint]);
            utils.sudoProcessCall(['/bin/mkdir', '-p', sMountPoint]);
            utils.sudoProcessCall(['/usr/sbin/chown', str(os.getuid()), sMountPoint]); # pylint: disable=no-member
            if sType == 'cifs':
                # Note! no smb://server/share stuff here, 10.6.8 didn't like it.
                utils.processOutputChecked(['/sbin/mount_smbfs',
                                            '-o',
                                            'automounted,nostreams,soft,noowners,noatime,rdonly' + sMountOpt,
                                            '-f', '0555', '-d', '0555',
                                            '//%s:%s@%s/%s' % (sUser, sPassword, sServer, sShare),
                                            sMountPoint]);
            else:
                raise TestBoxScriptException('Unsupported server type %s.' % (sType,));

        elif sHostOs == 'linux':
            if sShare == 'builds':
                sMountOpt = 'vers=2.1'  # right now only linux unhappy with builds mount, adding vers to avoid it
            if sMountOpt != '':
                sMountOpt = ',' + sMountOpt
            utils.sudoProcessCall(['/bin/umount', sMountPoint]);
            utils.sudoProcessCall(['/bin/mkdir', '-p', sMountPoint]);
            if sType == 'cifs':
                utils.sudoProcessOutputChecked(['/bin/mount', '-t', 'cifs',
                                                '-o',
                                                'user=' + sUser
                                                + ',password=' + sPassword
                                                + ',sec=ntlmv2'
                                                + ',uid=' + str(os.getuid()) # pylint: disable=no-member
                                                + ',gid=' + str(os.getgid()) # pylint: disable=no-member
                                                + ',nounix,file_mode=0555,dir_mode=0555,soft,ro'
                                                + sMountOpt,
                                                '//%s/%s' % (sServer, sShare),
                                                sMountPoint]);
            elif sType == 'nfs':
                utils.sudoProcessOutputChecked(['/bin/mount', '-t', 'nfs',
                                                '-o', 'soft,ro' + sMountOpt,
                                                '%s:%s' % (sServer, sShare if sShare.find('/') >= 0 else ('/export/' + sShare)),
                                                sMountPoint]);

            else:
                raise TestBoxScriptException('Unsupported server type %s.' % (sType,));

        elif sHostOs == 'solaris':
            if sMountOpt != '':
                sMountOpt = ',' + sMountOpt
            utils.sudoProcessCall(['/sbin/umount', sMountPoint]);
            utils.sudoProcessCall(['/bin/mkdir', '-p', sMountPoint]);
            if sType == 'cifs':
                ## @todo This stuff doesn't work on wei01-x4600b.de.oracle.com running 11.1. FIXME!
                oPasswdFile = tempfile.TemporaryFile();     # pylint: disable=consider-using-with
                oPasswdFile.write(sPassword + '\n');
                oPasswdFile.flush();
                utils.sudoProcessOutputChecked(['/sbin/mount', '-F', 'smbfs',
                                                '-o',
                                                'user=' + sUser
                                                + ',uid=' + str(os.getuid()) # pylint: disable=no-member
                                                + ',gid=' + str(os.getgid()) # pylint: disable=no-member
                                                + ',fileperms=0555,dirperms=0555,noxattr,ro'
                                                + sMountOpt,
                                                '//%s/%s' % (sServer, sShare),
                                                sMountPoint],
                                               stdin = oPasswdFile);
                oPasswdFile.close();
            elif sType == 'nfs':
                utils.sudoProcessOutputChecked(['/sbin/mount', '-F', 'nfs',
                                                '-o', 'noxattr,ro' + sMountOpt,
                                                '%s:%s' % (sServer, sShare if sShare.find('/') >= 0 else ('/export/' + sShare)),
                                                sMountPoint]);

            else:
                raise TestBoxScriptException('Unsupported server type %s.' % (sType,));


        elif sHostOs == 'win':
            if sType != 'cifs':
                raise TestBoxScriptException('Only CIFS mounts are supported on Windows.');
            utils.processCall(['net', 'use', sMountPoint, '/d']);
            utils.processOutputChecked(['net', 'use', sMountPoint,
                                        '\\\\' + sServer + '\\' + sShare,
                                        sPassword,
                                        '/USER:' + sUser,]);
        else:
            raise TestBoxScriptException('Unsupported host %s' % (sHostOs,));

        #
        # Re-test.
        #
        if not os.path.isfile(sTestFile):
            raise TestBoxException('Failed to mount %s (%s[%s]) at %s: %s not found'
                                   % (sWhat, sServer, sShare, sMountPoint, sTestFile));

        return True;

    ## @name Signon property releated methods.
    # @{

    def _getHelperOutput(self, sCmd):
        """
        Invokes TestBoxHelper to obtain information hard to access from python.
        """
        if self._sTestBoxHelper is None:
            if not utils.isRunningFromCheckout():
                # See VBoxTestBoxScript.zip for layout.
                self._sTestBoxHelper = os.path.join(g_ksValidationKitDir, utils.getHostOs(), utils.getHostArch(), \
                                                    'TestBoxHelper');
            else: # Only for in-tree testing, so don't bother be too accurate right now.
                sType = os.environ.get('KBUILD_TYPE', 'debug');
                self._sTestBoxHelper = os.path.join(g_ksValidationKitDir, os.pardir, os.pardir, os.pardir, 'out', \
                                                    utils.getHostOsDotArch(), sType, 'testboxscript', \
                                                    utils.getHostOs(), utils.getHostArch(), \
                                                    'TestBoxHelper');
            if utils.getHostOs() in ['win', 'os2']:
                self._sTestBoxHelper += '.exe';

        return utils.processOutputChecked([self._sTestBoxHelper, sCmd]).strip();

    def _getHelperOutputTristate(self, sCmd, fDunnoValue):
        """
        Invokes TestBoxHelper to obtain information hard to access from python.
        """
        sValue = self._getHelperOutput(sCmd);
        sValue = sValue.lower();
        if sValue == 'true':
            return True;
        if sValue == 'false':
            return False;
        if sValue not in  ('dunno', 'none',):
            raise TestBoxException('Unexpected response "%s" to helper command "%s"' % (sValue, sCmd));
        return fDunnoValue;


    @staticmethod
    def _isUuidGood(sUuid):
        """
        Checks if the UUID looks good.

        There are systems with really bad UUIDs, for instance
        "03000200-0400-0500-0006-000700080009".
        """
        if sUuid == TestBoxScript.ksNullUuid:
            return False;
        sUuid = sUuid.lower();
        for sDigit in ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f']:
            if sUuid.count(sDigit) > 16:
                return False;
        return True;

    def _getHostSystemUuid(self):
        """
        Get the system UUID string from the System, return null-uuid if
        unable to get retrieve it.
        """
        if self._oOptions.sSystemUuid is not None:
            return self._oOptions.sSystemUuid;

        sUuid = self.ksNullUuid;

        #
        # Try get at the firmware UUID.
        #
        if utils.getHostOs() == 'linux':
            # NOTE: This requires to have kernel option enabled:
            #       Firmware Drivers -> Export DMI identification via sysfs to userspace
            if os.path.exists('/sys/devices/virtual/dmi/id/product_uuid'):
                try:
                    sVar = utils.sudoProcessOutputChecked(['cat', '/sys/devices/virtual/dmi/id/product_uuid']);
                    sUuid = str(uuid.UUID(sVar.strip()));
                except:
                    pass;
            ## @todo consider dmidecoder? What about EFI systems?

        elif utils.getHostOs() == 'win':
            # Windows: WMI
            try:
                import win32com.client;  # pylint: disable=import-error
                oWmi  = win32com.client.Dispatch('WbemScripting.SWbemLocator');
                oWebm = oWmi.ConnectServer('.', 'root\\cimv2');
                for oItem in oWebm.ExecQuery('SELECT * FROM Win32_ComputerSystemProduct'):
                    if oItem.UUID is not None:
                        sUuid = str(uuid.UUID(oItem.UUID));
            except:
                pass;

        elif utils.getHostOs() == 'darwin':
            try:
                sVar = utils.processOutputChecked(['/bin/sh', '-c',
                                                   '/usr/sbin/ioreg -k IOPlatformUUID' \
                                                   + '| /usr/bin/grep IOPlatformUUID' \
                                                   + '| /usr/bin/head -1']);
                sVar = sVar.strip()[-(len(self.ksNullUuid) + 1):-1];
                sUuid = str(uuid.UUID(sVar));
            except:
                pass;

        elif utils.getHostOs() == 'solaris':
            # Solaris: The smbios util.
            try:
                sVar = utils.processOutputChecked(['/bin/sh', '-c',
                                                   '/usr/sbin/smbios ' \
                                                   + '| /usr/xpg4/bin/sed -ne \'s/^.*UUID: *//p\'' \
                                                   + '| /usr/bin/head -1']);
                sUuid = str(uuid.UUID(sVar.strip()));
            except:
                pass;

        if self._isUuidGood(sUuid):
            return sUuid;

        #
        # Try add the MAC address.
        # uuid.getnode may provide it, or it may return a random number...
        #
        lMacAddr = uuid.getnode();
        sNode = '%012x' % (lMacAddr,)
        if lMacAddr == uuid.getnode() and lMacAddr != 0 and len(sNode) == 12:
            return sUuid[:-12] + sNode;

        return sUuid;

    def _getHostCpuVendor(self):
        """
        Get the CPUID vendor string on intel HW.
        """
        return self._getHelperOutput('cpuvendor');

    def _getHostCpuName(self):
        """
        Get the CPU name/description string.
        """
        return self._getHelperOutput('cpuname');

    def _getHostCpuRevision(self):
        """
        Get the CPU revision (family/model/stepping) value.
        """
        return self._getHelperOutput('cpurevision');

    def _hasHostHwVirt(self):
        """
        Check if the host supports AMD-V or VT-x
        """
        if self._oOptions.fHasHwVirt is None:
            self._oOptions.fHasHwVirt = self._getHelperOutput('cpuhwvirt');
        return self._oOptions.fHasHwVirt;

    def _hasHostNestedPaging(self):
        """
        Check if the host supports nested paging.
        """
        if not self._hasHostHwVirt():
            return False;
        if self._oOptions.fHasNestedPaging is None:
            self._oOptions.fHasNestedPaging = self._getHelperOutputTristate('nestedpaging', False);
        return self._oOptions.fHasNestedPaging;

    def _can64BitGuest(self):
        """
        Check if the we (VBox) can run 64-bit guests.
        """
        if not self._hasHostHwVirt():
            return False;
        if self._oOptions.fCan64BitGuest is None:
            self._oOptions.fCan64BitGuest = self._getHelperOutputTristate('longmode', True);
        return self._oOptions.fCan64BitGuest;

    def _hasHostIoMmu(self):
        """
        Check if the host has an I/O MMU of the VT-d kind.
        """
        if not self._hasHostHwVirt():
            return False;
        if self._oOptions.fHasIoMmu is None:
            ## @todo Any way to figure this one out on any host OS?
            self._oOptions.fHasIoMmu = False;
        return self._oOptions.fHasIoMmu;

    def _hasHostNativeApi(self):
        """
        Check if the host supports the native API/NEM mode.
        """
        if self._oOptions.fHasNativeApi is None:
            self._oOptions.fHasNativeApi = self._getHelperOutput('nativeapi');
        return self._oOptions.fHasNativeApi;

    def _withRawModeSupport(self):
        """
        Check if the testbox is configured with raw-mode support or not.
        """
        if self._oOptions.fWithRawMode is None:
            self._oOptions.fWithRawMode = True;
        return self._oOptions.fWithRawMode;

    def _getHostReport(self):
        """
        Generate a report about the host hardware and software.
        """
        return self._getHelperOutput('report');


    def _getHostMemSize(self):
        """
        Gets the amount of physical memory on the host (and accessible to the
        OS, i.e. don't report stuff over 4GB if Windows doesn't wanna use it).
        Unit: MiB.
        """
        cMbMemory = long(self._getHelperOutput('memsize').strip()) / (1024 * 1024);

        # Round it.
        cMbMemory = long(math.floor(cMbMemory / self.kcMbMemoryRounding)) * self.kcMbMemoryRounding;
        return cMbMemory;

    def _getFreeScratchSpace(self):
        """
        Get free space on the volume where scratch directory is located and
        return it in bytes rounded down to nearest 64MB
        (currently works on Linux only)
        Unit: MiB.
        """
        if platform.system() == 'Windows':
            import ctypes
            cTypeMbFreeSpace = ctypes.c_ulonglong(0)
            ctypes.windll.kernel32.GetDiskFreeSpaceExW(ctypes.c_wchar_p(self._oOptions.sScratchRoot), None, None,
                                                       ctypes.pointer(cTypeMbFreeSpace))
            cMbFreeSpace = cTypeMbFreeSpace.value
        else:
            stats = os.statvfs(self._oOptions.sScratchRoot); # pylint: disable=no-member
            cMbFreeSpace = stats.f_frsize * stats.f_bfree

        # Convert to MB
        cMbFreeSpace = long(cMbFreeSpace) /(1024 * 1024)

        # Round free space size
        cMbFreeSpace = long(math.floor(cMbFreeSpace / self.kcMbScratchSpaceRounding)) * self.kcMbScratchSpaceRounding;
        return cMbFreeSpace;

    def _getScriptRev(self):
        """
        The script (subversion) revision number.
        """
        sRev = '@VBOX_SVN_REV@';
        sRev = sRev.strip();            # just in case...
        try:
            _ = int(sRev);
        except:
            return __version__[11:-1].strip();
        return sRev;

    def _getPythonHexVersion(self):
        """
        The python hex version number.
        """
        uHexVersion = getattr(sys, 'hexversion', None);
        if uHexVersion is None:
            uHexVersion = (sys.version_info[0] << 24) | (sys.version_info[1] << 16) | (sys.version_info[2] << 8);
            if sys.version_info[3] == 'final':
                uHexVersion |= 0xf0;
        return uHexVersion;

    # @}

    def openTestManagerConnection(self):
        """
        Opens up a connection to the test manager.

        Raises exception on failure.
        """
        return TestBoxConnection(self._oOptions.sTestManagerUrl, self._idTestBox, self._sTestBoxUuid);

    def getSignOnParam(self, sName):
        """
        Returns a sign-on parameter value as string.
        Raises exception if the name is incorrect.
        """
        return str(self._ddSignOnParams[sName][self.VALUE]);

    def getPathState(self):
        """
        Get the path to the state dir in the scratch area.
        """
        return self._sScratchState;

    def getPathScripts(self):
        """
        Get the path to the scripts dir (TESTBOX_PATH_SCRIPTS) in the scratch area.
        """
        return self._sScratchScripts;

    def getPathSpill(self):
        """
        Get the path to the spill dir (TESTBOX_PATH_SCRATCH) in the scratch area.
        """
        return self._sScratchSpill;

    def getPathBuilds(self):
        """
        Get the path to the builds.
        """
        return self._oOptions.sBuildsPath;

    def getTestBoxId(self):
        """
        Get the TestBox ID for state saving purposes.
        """
        return self._idTestBox;

    def getTestBoxName(self):
        """
        Get the TestBox name for state saving purposes.
        """
        return self._sTestBoxName;

    def _reinitScratch(self, fnLog, fUseTheForce):
        """
        Wipes the scratch directories and re-initializes them.

        No exceptions raise, returns success indicator instead.
        """
        if fUseTheForce is None:
            fUseTheForce = self._fFirstSignOn;

        class ErrorCallback(object): # pylint: disable=too-few-public-methods
            """
            Callbacks + state for the cleanup.
            """
            def __init__(self):
                self.fRc = True;
            def onErrorCallback(self, sFnName, sPath, aXcptInfo):
                """ Logs error during shutil.rmtree operation. """
                fnLog('Error removing "%s": fn=%s %s' % (sPath, sFnName, aXcptInfo[1]));
                self.fRc = False;
        oRc = ErrorCallback();

        #
        # Cleanup.
        #
        for sName in os.listdir(self._oOptions.sScratchRoot):
            sFullName = os.path.join(self._oOptions.sScratchRoot, sName);
            try:
                if os.path.isdir(sFullName):
                    shutil.rmtree(sFullName, False, oRc.onErrorCallback); # pylint: disable=deprecated-argument
                else:
                    os.remove(sFullName);
                if os.path.exists(sFullName):
                    raise Exception('Still exists after deletion, weird.');
            except Exception as oXcpt:
                if    fUseTheForce is True \
                  and utils.getHostOs() not in ['win', 'os2'] \
                  and len(sFullName) >= 8 \
                  and sFullName[0] == '/' \
                  and sFullName[1] != '/' \
                  and sFullName.find('/../') < 0:
                    fnLog('Problems deleting "%s" (%s) using the force...' % (sFullName, oXcpt));
                    try:
                        if os.path.isdir(sFullName):
                            iRc = utils.sudoProcessCall(['/bin/rm', '-Rf', sFullName])
                        else:
                            iRc = utils.sudoProcessCall(['/bin/rm', '-f', sFullName])
                        if iRc != 0:
                            raise Exception('exit code %s' % iRc);
                        if os.path.exists(sFullName):
                            raise Exception('Still exists after forced deletion, weird^2.');
                    except:
                        fnLog('Error sudo deleting "%s": %s' % (sFullName, oXcpt));
                        oRc.fRc = False;
                else:
                    fnLog('Error deleting "%s": %s' % (sFullName, oXcpt));
                    oRc.fRc = False;

        # Display files left behind.
        def dirEnumCallback(sName, oStat):
            """ callback for dirEnumerateTree """
            fnLog(u'%s %s' % (utils.formatFileStat(oStat) if oStat is not None else '????????????', sName));
        utils.dirEnumerateTree(self._oOptions.sScratchRoot, dirEnumCallback);

        #
        # Re-create the directories.
        #
        for sDir in [self._oOptions.sScratchRoot, self._sScratchSpill, self._sScratchScripts, self._sScratchState]:
            if not os.path.isdir(sDir):
                try:
                    os.makedirs(sDir, 0o700);
                except Exception as oXcpt:
                    fnLog('Error creating "%s": %s' % (sDir, oXcpt));
                    oRc.fRc = False;

        if oRc.fRc is True:
            self._cReinitScratchErrors = 0;
        else:
            self._cReinitScratchErrors += 1;
        return oRc.fRc;

    def reinitScratch(self, fnLog = testboxcommons.log, fUseTheForce = None, cRetries = 0, cMsDelay = 5000):
        """
        Wipes the scratch directories and re-initializes them.

        Will retry according to the cRetries and cMsDelay parameters.  Windows
        forces us to apply this hack as it ships with services asynchronously
        scanning files after they execute, thus racing us cleaning up after a
        test.  On testboxwin3 we had frequent trouble with aelupsvc.dll keeping
        vts_rm.exe kind of open, somehow preventing us from removing the
        directory containing it, despite not issuing any errors deleting the
        file itself.  The service is called "Application Experience", which
        feels like a weird joke here.

        No exceptions raise, returns success indicator instead.
        """
        fRc = self._reinitScratch(fnLog, fUseTheForce)
        while fRc is False and cRetries > 0:
            time.sleep(cMsDelay / 1000.0);
            fnLog('reinitScratch: Retrying...');
            fRc = self._reinitScratch(fnLog, fUseTheForce)
            cRetries -= 1;
        return fRc;


    def _doSignOn(self):
        """
        Worker for _maybeSignOn that does the actual signing on.
        """
        assert not self._oCommand.isRunning();

        # Reset the siged-on state.
        testboxcommons.log('Signing-on...')
        self._fSignedOn = False
        self._idTestBox = None
        self._cSignOnAttempts += 1;

        # Assemble SIGN-ON request parameters and send the request.
        dParams = {};
        for sParam in self._ddSignOnParams:                     # pylint: disable=consider-using-dict-items
            dParams[sParam] = self._ddSignOnParams[sParam][self.VALUE];
        oResponse = TestBoxConnection.sendSignOn(self._oOptions.sTestManagerUrl, dParams);

        # Check response.
        try:
            sResult = oResponse.getStringChecked(constants.tbresp.ALL_PARAM_RESULT);
            if sResult != constants.tbresp.STATUS_ACK:
                raise TestBoxException('Result is %s' % (sResult,));
            oResponse.checkParameterCount(3);
            idTestBox    = oResponse.getIntChecked(constants.tbresp.SIGNON_PARAM_ID, 1, 0x7ffffffe);
            sTestBoxName = oResponse.getStringChecked(constants.tbresp.SIGNON_PARAM_NAME);
        except TestBoxException as err:
            testboxcommons.log('Failed to sign-on: %s' % (str(err),))
            testboxcommons.log('Server response: %s' % (oResponse.toString(),));
            return False;

        # Successfully signed on, update the state.
        self._fSignedOn       = True;
        self._fNeedReSignOn   = False;
        self._cSignOnAttempts = 0;
        self._idTestBox       = idTestBox;
        self._sTestBoxName    = sTestBoxName;

        # Update the environment.
        os.environ['TESTBOX_ID']            = str(self._idTestBox);
        os.environ['TESTBOX_NAME']          = sTestBoxName;
        os.environ['TESTBOX_CPU_COUNT']     = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_CPU_COUNT);
        os.environ['TESTBOX_MEM_SIZE']      = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_MEM_SIZE);
        os.environ['TESTBOX_SCRATCH_SIZE']  = self.getSignOnParam(constants.tbreq.SIGNON_PARAM_SCRATCH_SIZE);

        testboxcommons.log('Successfully signed-on with Test Box ID #%s and given the name "%s"' \
                           % (self._idTestBox, self._sTestBoxName));

        # Set up the scratch area.
        self.reinitScratch(fUseTheForce = self._fFirstSignOn, cRetries = 2);

        self._fFirstSignOn = False;
        return True;

    def _maybeSignOn(self):
        """
        Check if Test Box parameters were changed
        and do sign-in in case of positive result
        """

        # Skip sign-on check if background command is currently in
        # running state (avoid infinite signing on).
        if self._oCommand.isRunning():
            return None;

        # Refresh sign-on parameters, changes triggers sign-on.
        fNeedSignOn = not self._fSignedOn or self._fNeedReSignOn;
        for sItem in self._ddSignOnParams:                      # pylint: disable=consider-using-dict-items
            if self._ddSignOnParams[sItem][self.FN] is None:
                continue

            sOldValue = self._ddSignOnParams[sItem][self.VALUE]
            self._ddSignOnParams[sItem][self.VALUE] = self._ddSignOnParams[sItem][self.FN]()
            if sOldValue != self._ddSignOnParams[sItem][self.VALUE]:
                fNeedSignOn = True
                testboxcommons.log('Detected %s parameter change: %s -> %s'
                                   % (sItem, sOldValue, self._ddSignOnParams[sItem][self.VALUE],))

        if fNeedSignOn:
            self._doSignOn();
        return None;

    def dispatch(self):
        """
        Receive orders from Test Manager and execute them
        """

        (self._idTestBox, self._sTestBoxName, self._fSignedOn) = self._oCommand.resumeIncompleteCommand();
        self._fNeedReSignOn = self._fSignedOn;
        if self._fSignedOn:
            os.environ['TESTBOX_ID']   = str(self._idTestBox);
            os.environ['TESTBOX_NAME'] = self._sTestBoxName;

        while True:
            # Make sure we're signed on before trying to do anything.
            self._maybeSignOn();
            while not self._fSignedOn:
                iFactor = 1 if self._cSignOnAttempts < 100 else 4;
                time.sleep(random.randint(self.kcSecMinSignOnDelay * iFactor, self.kcSecMaxSignOnDelay * iFactor));
                self._maybeSignOn();

            # Retrieve and handle command from the TM.
            (oResponse, oConnection) = TestBoxConnection.requestCommandWithConnection(self._oOptions.sTestManagerUrl,
                                                                                      self._idTestBox,
                                                                                      self._sTestBoxUuid,
                                                                                      self._oCommand.isRunning());
            if oResponse is not None:
                self._oCommand.handleCommand(oResponse, oConnection);
            if oConnection is not None:
                if oConnection.isConnected():
                    self._oCommand.flushLogOnConnection(oConnection);
                oConnection.close();

            # Automatically reboot if scratch init fails.
            #if self._cReinitScratchErrors > 8 and self.reinitScratch(cRetries = 3) is False:
            #    testboxcommons.log('Scratch does not initialize cleanly after %d attempts, rebooting...'
            #                       % ( self._cReinitScratchErrors, ));
            #    self._oCommand.doReboot();

            # delay a wee bit before looping.
            ## @todo We shouldn't bother the server too frequently.  We should try combine the test reporting done elsewhere
            # with the command retrieval done here.  I believe tinderclient.pl is capable of doing that.
            iFactor = 1;
            if self._cReinitScratchErrors > 0:
                iFactor = 4;
            time.sleep(random.randint(self.kcSecMinDelay * iFactor, self.kcSecMaxDelay * iFactor));

        # Not reached.


    @staticmethod
    def main():
        """
        Main function a la C/C++. Returns exit code.
        """

        #
        # Parse arguments.
        #
        sDefShareType = 'nfs' if utils.getHostOs() == 'solaris' else 'cifs';
        if utils.getHostOs() in ('win', 'os2'):
            sDefTestRsrc  = 'T:';
            sDefBuilds    = 'U:';
        elif utils.getHostOs() == 'darwin':
            sDefTestRsrc  = '/Volumes/testrsrc';
            sDefBuilds    = '/Volumes/builds';
        else:
            sDefTestRsrc  = '/mnt/testrsrc';
            sDefBuilds    = '/mnt/builds';

        class MyOptionParser(OptionParser):
            """ We need to override the exit code on --help, error and so on. """
            def __init__(self, *args, **kwargs):
                OptionParser.__init__(self, *args, **kwargs);
            def exit(self, status = 0, msg = None):
                OptionParser.exit(self, TBS_EXITCODE_SYNTAX, msg);

        parser = MyOptionParser(version=__version__[11:-1].strip());
        for sMixed, sDefault, sDesc in [('Builds', sDefBuilds, 'builds'),  ('TestRsrc', sDefTestRsrc, 'test resources') ]:
            sLower = sMixed.lower();
            sPrefix = 's' + sMixed;
            parser.add_option('--' + sLower + '-path',
                              dest=sPrefix + 'Path',         metavar='<abs-path>', default=sDefault,
                              help='Where ' + sDesc + ' can be found');
            parser.add_option('--' + sLower + '-server-type',
                              dest=sPrefix + 'ServerType',   metavar='<nfs|cifs>', default=sDefShareType,
                              help='The type of server, cifs (default) or nfs. If empty, we won\'t try mount anything.');
            parser.add_option('--' + sLower + '-server-name',
                              dest=sPrefix + 'ServerName',   metavar='<server>',
                              default='10.165.98.144' if sLower == 'builds' else 'teststor.de.oracle.com',
                              help='The name of the server with the builds.');
            parser.add_option('--' + sLower + '-server-share',
                              dest=sPrefix + 'ServerShare',  metavar='<share>',    default=sLower,
                              help='The name of the builds share.');
            parser.add_option('--' + sLower + '-server-user',
                              dest=sPrefix + 'ServerUser',   metavar='<user>',     default='guestr',
                              help='The user name to use when accessing the ' + sDesc + ' share.');
            parser.add_option('--' + sLower + '-server-passwd', '--' + sLower + '-server-password',
                              dest=sPrefix + 'ServerPasswd', metavar='<password>', default='guestr',
                              help='The password to use when accessing the ' + sDesc + ' share.');
            parser.add_option('--' + sLower + '-server-mountopt',
                              dest=sPrefix + 'ServerMountOpt', metavar='<mountopt>', default='',
                              help='The mount options to use when accessing the ' + sDesc + ' share.');

        parser.add_option("--test-manager", metavar="<url>",
                          dest="sTestManagerUrl",
                          help="Test Manager URL",
                          default="http://tindertux.de.oracle.com/testmanager")
        parser.add_option("--scratch-root", metavar="<abs-path>",
                          dest="sScratchRoot",
                          help="Path to the scratch directory",
                          default=None)
        parser.add_option("--system-uuid", metavar="<uuid>",
                          dest="sSystemUuid",
                          help="The system UUID of the testbox, used for uniquely identifiying the machine",
                          default=None)
        parser.add_option("--hwvirt",
                          dest="fHasHwVirt", action="store_true", default=None,
                          help="Hardware virtualization available in the CPU");
        parser.add_option("--no-hwvirt",
                          dest="fHasHwVirt", action="store_false", default=None,
                          help="Hardware virtualization not available in the CPU");
        parser.add_option("--nested-paging",
                          dest="fHasNestedPaging", action="store_true", default=None,
                          help="Nested paging is available");
        parser.add_option("--no-nested-paging",
                          dest="fHasNestedPaging", action="store_false", default=None,
                          help="Nested paging is not available");
        parser.add_option("--64-bit-guest",
                          dest="fCan64BitGuest", action="store_true", default=None,
                          help="Host can execute 64-bit guests");
        parser.add_option("--no-64-bit-guest",
                          dest="fCan64BitGuest", action="store_false", default=None,
                          help="Host cannot execute 64-bit guests");
        parser.add_option("--native-api",
                          dest="fHasNativeApi", action="store_true", default=None,
                          help="Native API virtualization is available");
        parser.add_option("--no-native-api",
                          dest="fHasNativeApi", action="store_false", default=None,
                          help="Native API virtualization is not available");
        parser.add_option("--io-mmu",
                          dest="fHasIoMmu", action="store_true", default=None,
                          help="I/O MMU available");
        parser.add_option("--no-io-mmu",
                          dest="fHasIoMmu", action="store_false", default=None,
                          help="No I/O MMU available");
        parser.add_option("--raw-mode",
                          dest="fWithRawMode", action="store_true", default=None,
                          help="Use raw-mode on this host.");
        parser.add_option("--no-raw-mode",
                          dest="fWithRawMode", action="store_false", default=None,
                          help="Disables raw-mode tests on this host.");
        parser.add_option("--pidfile",
                          dest="sPidFile", default=None,
                          help="For the parent script, ignored.");
        parser.add_option("-E", "--putenv", metavar = "<variable>=<value>", action = "append",
                          dest = "asEnvVars", default = [],
                          help = "Sets an environment variable. Can be repeated.");
        def sbp_callback(option, opt_str, value, parser):
            _, _, _ = opt_str, value, option
            parser.values.sTestManagerUrl = 'http://10.162.100.8/testmanager/'
            parser.values.sBuildsServerName = 'vbox-st02.ru.oracle.com'
            parser.values.sTestRsrcServerName = 'vbox-st02.ru.oracle.com'
            parser.values.sTestRsrcServerShare = 'scratch/data/testrsrc'
        parser.add_option("--spb", "--load-sbp-defaults", action="callback", callback=sbp_callback,
                          help="Load defaults for the sbp setup.")

        (oOptions, args) = parser.parse_args()
        # Check command line
        if args != []:
            parser.print_help();
            return TBS_EXITCODE_SYNTAX;

        if oOptions.sSystemUuid is not None:
            uuid.UUID(oOptions.sSystemUuid);
        if   not oOptions.sTestManagerUrl.startswith('http://') \
         and not oOptions.sTestManagerUrl.startswith('https://'):
            print('Syntax error: Invalid test manager URL "%s"' % (oOptions.sTestManagerUrl,));
            return TBS_EXITCODE_SYNTAX;

        for sPrefix in ['sBuilds', 'sTestRsrc']:
            sType = getattr(oOptions, sPrefix + 'ServerType');
            if sType is None or not sType.strip():
                setattr(oOptions, sPrefix + 'ServerType', None);
            elif sType not in ['cifs', 'nfs']:
                print('Syntax error: Invalid server type "%s"' % (sType,));
                return TBS_EXITCODE_SYNTAX;


        #
        # Instantiate the testbox script and start dispatching work.
        #
        try:
            oTestBoxScript = TestBoxScript(oOptions);
        except TestBoxScriptException as oXcpt:
            print('Error: %s' % (oXcpt,));
            return TBS_EXITCODE_SYNTAX;
        oTestBoxScript.dispatch();

        # Not supposed to get here...
        return TBS_EXITCODE_FAILURE;



if __name__ == '__main__':
    sys.exit(TestBoxScript.main());

